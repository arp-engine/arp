#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/interpreter/bytecode.h"
#include "scriptvm/interpreter/vm.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string_view>

extern "C" int arp_register_script_aot_package_v5(arp::scriptvm::AotRegistrationHostV5* host);

namespace {

using namespace arp::scriptvm;

const AotPackageDescriptorV5* registeredPackage = nullptr;

bool capturePackage(const AotPackageDescriptorV5* package, void*)
{
    registeredPackage = package;
    return true;
}

struct ChildState {
    Value child;
    arp::core::CpuMemoryQuotaLease memoryLease;
};

bool describeInterpreterChild(
    void* userData, const Value& value, ScriptValueMemoryDescriptor& descriptor) noexcept
{
    auto* state = static_cast<ChildState*>(userData);
    auto* generator = asScriptGenerator(value);
    if (state == nullptr || generator == nullptr || generator != asScriptGenerator(state->child)
        || !state->memoryLease.charged()) {
        return false;
    }
    descriptor.allocationIdentity = generator;
    descriptor.retainedBytes = state->memoryLease.bytes();
    descriptor.externallyLeased = true;
    return true;
}

bool forwardInterpreterChild(void* userData,
    const char*,
    const char* targetId,
    const Value*,
    const Value*,
    std::size_t,
    Value* result,
    VmError* error)
{
    auto* state = static_cast<ChildState*>(userData);
    if (state == nullptr || result == nullptr || targetId == nullptr
        || std::string_view(targetId) != "arp-fn:math_dep/delegated_import_child") {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotModuleUnavailable;
            error->message = "cross-backend child binding is unavailable";
        }
        return false;
    }
    *result = state->child;
    return true;
}

std::shared_ptr<Program> makePendingChildProgram()
{
    auto program = arp::core::makeShared<Program>();
    Function child("pending_child");
    child.params = 1;
    child.locals = 1;
    child.maxStack = 1;
    child.isGenerator = true;
    child.code.emplace_back(Opcode::LoadLocal, 0);
    child.code.emplace_back(Opcode::Await);
    child.code.emplace_back(Opcode::Yield);
    child.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    child.code.emplace_back(Opcode::Return, 1);
    program->functions.push_back(std::move(child));
    program->funcIndex.emplace("pending_child", 0);
    Function maker("make_pending_child");
    maker.params = 1;
    maker.locals = 1;
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::LoadLocal, 0);
    maker.code.emplace_back(Opcode::Call, 0, 1);
    maker.code.emplace_back(Opcode::Return, 1);
    program->functions.push_back(std::move(maker));
    program->funcIndex.emplace("make_pending_child", 1);
    return program;
}

Value makeInterpreterChild(
    const AotModuleDescriptorV5& dependency, bool injectable, bool& ready, std::size_t& polls)
{
    auto program = makePendingChildProgram();
    ScriptAwaitable awaitable;
    awaitable.poll = [&ready, &polls](Value& result, arp::core::pmr::String&) {
        ++polls;
        if (!ready)
            return ScriptAwaitStatus::Pending;
        result = makeInt(70);
        return ScriptAwaitStatus::Ready;
    };
    if (injectable) {
        awaitable.inject = [](ScriptAwaitInjectionKind kind,
                               const Value& value,
                               Value& result,
                               arp::core::pmr::String&) {
            if (kind != ScriptAwaitInjectionKind::Send && kind != ScriptAwaitInjectionKind::Throw)
                return ScriptAwaitInjectionStatus::Rejected;
            int injected = 0;
            if (!tryGetInt(value, injected))
                return ScriptAwaitInjectionStatus::Failed;
            result = makeInt(injected + 1);
            return ScriptAwaitInjectionStatus::Ready;
        };
    }
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{std::move(awaitable)});

    auto vm = arp::core::makeShared<Vm>();
    vm->setOwner(vm);
    vm->setProgramOwner(program);
    vm->setModuleIdentity(dependency.moduleId, dependency.moduleVersion);
    vm->setBridgeArtifactIdentity(dependency.artifactIdentity);
    VmError error;
    const Value child = vm->runFunction(*program, 1u, args, &error);
    if (error.code != ScriptDiagnosticCode::None || !isScriptGenerator(child)) {
        std::fprintf(stderr,
            "cross-backend child creation failed: code=%d message=%s generator=%d\n",
            static_cast<int>(error.code),
            error.message.c_str(),
            isScriptGenerator(child) ? 1 : 0);
        return makeNil();
    }
    return child;
}

bool runCase(const AotPackageDescriptorV5& package,
    bool injectable,
    DirectNativeBoundCallV5 callback,
    int injectionValue,
    int expectedYield,
    std::size_t expectedPolls)
{
    const auto fail = [](const char* stage, const VmError* error = nullptr) {
        std::fprintf(stderr, "cross-backend failure at %s", stage);
        if (error != nullptr)
            std::fprintf(stderr,
                ": code=%d message=%s",
                static_cast<int>(error->code),
                error->message.c_str());
        std::fprintf(stderr, "\n");
        return false;
    };
    ScriptAotPackageRegistry registry;
    if (!registry.registerPackage(package).ok())
        return fail("register package");
    ScriptAotModuleInstanceV5 instance;
    if (!createAotOnlyModuleInstanceV5(registry, package.packageIdentity, "basic", instance).ok())
        return fail("create module");

    const AotModuleDescriptorV5* dependency = nullptr;
    for (std::size_t index = 0; index < package.modules[0].dependencyCount; ++index) {
        const auto& candidate = package.modules[0].dependencies[index];
        if (candidate.moduleId != nullptr && std::string_view(candidate.moduleId) == "math_dep") {
            dependency = registry.findModule(package.packageIdentity, candidate.moduleId);
            break;
        }
    }
    if (dependency == nullptr)
        return fail("find dependency");

    bool ready = false;
    std::size_t polls = 0u;
    ChildState childState;
    constexpr std::size_t kInterpreterChildFixtureBytes = 4096u;
    if (!childState.memoryLease.acquire(
            instance.context->memoryQuota, kInterpreterChildFixtureBytes)) {
        return fail("reserve interpreter child memory");
    }
    childState.child = makeInterpreterChild(*dependency, injectable, ready, polls);
    if (!isScriptGenerator(childState.child))
        return fail("create interpreter child");
    // Replace only the basic module's bound-call entry in this explicit
    // differential fixture. Production package runtime keeps its own
    // package-owned resolver and never links this target.
    instance.context->boundCall = callback;
    instance.context->boundCallUserData = &childState;
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    dispatcher.setValueMemoryDescriptor(&describeInterpreterChild, &childState);
    const Value args[] = {makeInt(7)};
    VmError error;
    const Value parentValue =
        dispatcher.invoke("arp-fn:basic/delegated_import_parent", args, 1u, &error);
    auto* parent = asDirectNativeGeneratorV5(parentValue);
    if (parent == nullptr || error.code != ScriptDiagnosticCode::None)
        return fail("create native parent", &error);

    const Value pending = resumeDirectNativeGeneratorV5(*parent, &error);
    if (!isNil(pending) || error.code != ScriptDiagnosticCode::Suspended || polls != expectedPolls
        || parent->state->resume.awaitInjectionSupported != injectable) {
        std::fprintf(stderr,
            "pending details: nil=%d error=%d polls=%zu supported=%d suspend=%d done=%d\n",
            isNil(pending) ? 1 : 0,
            static_cast<int>(error.code),
            polls,
            parent->state->resume.awaitInjectionSupported ? 1 : 0,
            static_cast<int>(parent->state->resume.suspendKind),
            parent->state->done ? 1 : 0);
        return fail("pending await", &error);
    }

    const Value injected = injectable
        ? (injectionValue == 41
                  ? sendDirectNativeGeneratorV5(*parent, makeInt(injectionValue), &error)
                  : throwDirectNativeGeneratorV5(*parent, makeInt(injectionValue), &error))
        : sendDirectNativeGeneratorV5(*parent, makeInt(injectionValue), &error);
    if (!injectable) {
        if (!isNil(injected) || error.code != ScriptDiagnosticCode::AotSwitchUnsupported
            || parent->state == nullptr || parent->state->done || polls != expectedPolls)
            return fail("reject injection", &error);
        closeDirectNativeGeneratorV5(*parent);
        auto* child = asScriptGenerator(childState.child);
        return parent->state->done && parent->state->resume.frame == nullptr && child != nullptr
            && child->done;
    }

    int yielded = 0;
    if (!tryGetInt(injected, yielded) || yielded != expectedYield
        || error.code != ScriptDiagnosticCode::None || polls != expectedPolls
        || parent->state->done)
        return fail("consume injection", &error);
    const Value completed = resumeDirectNativeGeneratorV5(*parent, &error);
    int result = 0;
    return tryGetInt(completed, result) && result == 99 && error.code == ScriptDiagnosticCode::None
        && parent->state->done && polls == expectedPolls;
}

} // namespace

int main()
{
    using namespace arp::scriptvm;
    AotRegistrationHostV5 host{kAotContractVersion, &capturePackage, nullptr};
    if (arp_register_script_aot_package_v5(&host) != 1 || registeredPackage == nullptr
        || !validateAotPackageV5(*registeredPackage, currentAotPlatform()).ok())
        return 1;

    // The callback is intentionally explicit: this target is a differential
    // migration fixture and is never linked by AOT-only production targets.
    if (!runCase(*registeredPackage, true, &forwardInterpreterChild, 41, 42, 1u)) {
        std::fprintf(stderr, "cross-backend send case failed\n");
        return 2;
    }
    if (!runCase(*registeredPackage, true, &forwardInterpreterChild, 4, 5, 1u)) {
        std::fprintf(stderr, "cross-backend throw case failed\n");
        return 3;
    }
    if (!runCase(*registeredPackage, false, &forwardInterpreterChild, 41, 0, 1u)) {
        std::fprintf(stderr, "cross-backend rejection case failed\n");
        return 4;
    }
    return 0;
}
