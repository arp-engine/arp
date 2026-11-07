#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/module_registry.h"
#include "tests/scriptvm_interpreter_test_fixture.h"

#include <cstdio>
#include <stdexcept>

using namespace arp::scriptvm;
namespace core = arp::core;

namespace arp::scriptvm::detail {
struct FrameReferenceContract {
    static void enable(Frame& frame) { frame.enablePrivateReferences(nullptr); }
};
} // namespace arp::scriptvm::detail

static void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static void slotBoundaries()
{
    Frame frame;
    detail::FrameReferenceContract::enable(frame);
    Value value{183};
    const auto identity = value.storageIdentity();
    frame.push(value);
    frame.push(frame.stack[0]);
    require(frame.stack[1].scalarPointer<int>() && *frame.stack[1].scalarPointer<int>() == 183,
        "private payload read");
    *value.ptr<int>() = 184;
    require(*frame.stack[1].scalarPointer<int>() == 184, "mutable scalar alias");
    auto* borrowed = &frame.stack[0].boxed();
    require(borrowed->storageIdentity() == identity, "public identity");
    Value rebound{901};
    frame.stack[0] = rebound;
    require(&frame.stack[0].boxed() == borrowed
            && borrowed->storageIdentity() == rebound.storageIdentity(),
        "published carrier address");
    require(frame.stack[1].boxed().storageIdentity() == identity, "independent rebinding");
    frame.stack.clear();
    frame.push(value);
    frame.push(frame.stack[0]);
    Frame copied(frame);
    Frame moved(std::move(frame));
    require(copied.stack[0].boxed().storageIdentity() == identity
            && moved.stack[1].boxed().storageIdentity() == identity,
        "public frame publication");
    ExecutionSlot slot(std::move(moved.stack[0]));
    require(slot.boxed().storageIdentity() == identity
            && moved.stack[0].boxed().storageIdentity() == nullptr,
        "exact moved-from carrier");
    frame.reset(nullptr);
    moved.reset(nullptr);
    copied.reset(nullptr);
    {
        Frame source;
        detail::FrameReferenceContract::enable(source);
        source.push(value);
        Frame assigned;
        detail::FrameReferenceContract::enable(assigned);
        assigned.push(rebound);
        assigned = source;
        source.reset(nullptr);
        require(assigned.stack[0].boxed().storageIdentity() == identity,
            "public frame copy assignment after private target");
        Frame movedAssigned;
        detail::FrameReferenceContract::enable(movedAssigned);
        movedAssigned.push(rebound);
        movedAssigned = std::move(assigned);
        require(movedAssigned.stack[0].boxed().storageIdentity() == identity,
            "public frame move assignment after private target");
    }
    std::optional<ExecutionSlot> escaped;
    {
        Frame owner;
        detail::FrameReferenceContract::enable(owner);
        owner.push(value);
        escaped.emplace(std::move(owner.stack[0]));
    }
    require(escaped->boxed().storageIdentity() == identity, "slot move outlives domain");

    detail::ExecutionReferenceDomain domain(nullptr);
    require(domain.prepare(2), "prepare");
    require(!domain.prepare(std::numeric_limits<std::size_t>::max()),
        "wrapper capacity overflow must decline before reserve");
    Value source{317};
    const auto original = source.storageIdentity();
    auto a = domain.adopt(std::move(source));
    require(source.storageIdentity() == nullptr && domain.inspect(a).storageIdentity() == original,
        "move admission");
    auto b = domain.clone(a);
    auto c = domain.clone(b); // Exercises actual metadata growth.
    auto out = domain.take(a);
    require(!out.hasUniqueStorage(), "alias-aware publication");
    domain.drop(b);
    domain.drop(c);
    require(out.hasUniqueStorage() && domain.empty(), "complete release");
    auto quota = core::makeShared<core::CpuMemoryQuota>(1024);
    detail::ExecutionReferenceDomain gated(&quota);
    require(!gated.prepare(64), "live VM quota growth rejection");
    {
        core::CpuMemoryQuotaScope scope(quota);
        Frame closed;
        detail::FrameReferenceContract::enable(closed);
        closed.push(value);
        require(
            closed.stack[0].boxed().storageIdentity() == identity, "ambient quota ordinary path");
    }
    std::puts("slot_identity_alias_borrow_frame_move_adopt_growth_quota=passed");
}

static void realVm()
{
    auto built = test::buildInterpreter("def twice(value: any) -> any:\n"
                                        "    var alias: any = value\n"
                                        "    var alias2: any = alias\n"
                                        "    return alias2\n"
                                        "def through(value: any) -> any:\n"
                                        "    return twice(value)\n",
        "domain-vm.arp",
        "domain-vm");
    require(built.ok(), "frontend");
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(), "validated owner");
    for (bool bound : {false, true}) {
        Vm vm;
        vm.setPrivateReferenceDomainsEnabled(true);
        if (bound)
            require(vm.bindProgram(artifact).ok(), "bind");
        auto& program = bound ? artifact.program() : built.program;
        core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
        args.emplace_back(183);
#ifdef ARP_VM_REFERENCE_DIAGNOSTICS
        detail::referenceDomainCounters = {};
#endif
        VmError error;
        auto value = vm.runFunctionByName(program, "through", args, &error);
        require(error.code == ScriptDiagnosticCode::None
                && value.storageIdentity() == args[0].storageIdentity(),
            "real nested VM carrier");
#ifdef ARP_VM_REFERENCE_DIAGNOSTICS
        const auto counts = detail::referenceDomainCounters;
        if (bound)
            require(counts.adoptions > 0 && counts.clones > 0 && counts.exports > 0,
                "bound VM must actually use private ownership");
        else
            require(counts.imports + counts.adoptions + counts.clones == 0,
                "unowned raw program must keep ordinary path");
        std::printf("real_vm_private_path bound=%d adopt=%zu clone=%zu export=%zu\n",
            bound,
            counts.adoptions,
            counts.clones,
            counts.exports);
#endif
    }
    std::puts("real_raw_and_bound_vm_nested_import_return=passed");
}

struct ReentrantOwner {
    Vm* vm;
    const Program* program;
    const core::pmr::Vector<Value>* args;
    unsigned* releases;
    bool* valid;

    ReentrantOwner(
        Vm* v, const Program* p, const core::pmr::Vector<Value>* a, unsigned* r, bool* ok)
        : vm(v), program(p), args(a), releases(r), valid(ok)
    {}

    ~ReentrantOwner()
    {
        ++*releases;
        VmError error;
        auto result = vm->runFunctionByName(*program, "echo", *args, &error);
        *valid = *valid && error.code == ScriptDiagnosticCode::None
            && result.storageIdentity() == (*args)[0].storageIdentity();
    }
};

static void callbackAndMoveAdmission()
{
    using Traits = detail::ReferenceValueTraits;
    detail::PrivateDomain<Value, Traits, 2, 1> domain;
    Value first{901}, second{902};
    const auto firstIdentity = first.storageIdentity();
    const auto secondIdentity = second.storageIdentity();
    require(domain.adoptEmpty(0, std::move(first)), "first move admission");
    require(!domain.adoptEmpty(1, std::move(second)) && second.storageIdentity() == secondIdentity,
        "capacity refusal preserves source");
    auto out = domain.takeOut(0);
    require(out.storageIdentity() == firstIdentity, "sole root transfer");
    require(domain.adoptEmpty(1, std::move(second)), "retry after retirement");
    domain.drop(1);
    Value empty(std::move(second));
    require(domain.adoptEmpty(0, std::move(empty)), "null control admission");
    require(domain.identity(0) == nullptr, "null identity index");
    domain.debugValidateIndex();
    domain.drop(0);

    auto built = test::buildInterpreter("def echo(value: any) -> any:\n    return value\n"
                                        "def discard(make: () -> any) -> int:\n"
                                        "    var value: any = make()\n"
                                        "    value = make()\n"
                                        "    return 7\n",
        "domain-reentry.arp",
        "domain-reentry");
    require(built.ok(), "callback frontend");
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(), "callback snapshot");
    for (bool cross : {false, true}) {
        Vm vm, other;
        vm.setPrivateReferenceDomainsEnabled(true);
        other.setPrivateReferenceDomainsEnabled(true);
        require(vm.bindProgram(artifact).ok() && other.bindProgram(artifact).ok(), "callback bind");
        core::pmr::Vector<Value> echoArgs{&core::defaultCpuMemoryResource()};
        echoArgs.emplace_back(731);
        unsigned releases = 0;
        bool valid = true;
        core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
        args.emplace_back(HostCallable{[&](const core::pmr::Vector<Value>&) {
            return Value{core::makeShared<ReentrantOwner>(
                cross ? &other : &vm, &artifact.program(), &echoArgs, &releases, &valid)};
        }});
        VmError error;
        auto result = vm.runFunctionByName(artifact.program(), "discard", args, &error);
        require(error.code == ScriptDiagnosticCode::None && *result.ptr<int>() == 7,
            "callback outer result");
        require(valid && releases == 2, "last release same/cross VM reentry");
    }
    std::puts("adopt_capacity_failure_null_identity_same_cross_vm_last_release=passed");
}

static void inactiveVmMoveAndThreadHandoff()
{
    auto built = test::buildInterpreter(
        "def echo(value: any) -> any:\n    return value\n", "domain-handoff.arp", "domain-handoff");
    require(built.ok(), "handoff frontend");
    ValidatedInterpreterProgram artifact;
    require(ValidatedInterpreterProgram::create(built.program, artifact).ok(), "handoff snapshot");
    std::optional<Vm> moved;
    core::pmr::Vector<Value> args{&core::defaultCpuMemoryResource()};
    args.emplace_back(481);
    const auto call = [&](Vm& vm) {
        VmError error;
        auto out = vm.runFunctionByName(artifact.program(), "echo", args, &error);
        require(error.code == ScriptDiagnosticCode::None
                && out.storageIdentity() == args[0].storageIdentity(),
            "moved VM identity");
    };
    {
        Vm vm;
        vm.setPrivateReferenceDomainsEnabled(true);
        require(vm.bindProgram(artifact).ok(), "handoff bind");
        call(vm);
        moved.emplace(std::move(vm));
    }
    call(*moved); // The original quota member no longer exists.
    bool succeeded = false;
    std::thread worker([vm = std::move(*moved), &call, &succeeded]() mutable {
        call(vm); // Empty metadata from the other thread must stay unadmitted.
        succeeded = true;
    });
    worker.join();
    require(succeeded, "inactive thread handoff");
    std::puts("inactive_vm_move_destroy_source_thread_handoff=passed");
}

int main()
{
    try {
        Vm defaultVm;
        require(!defaultVm.privateReferenceDomainsEnabled(), "production default remains opt-in");
#ifdef _WIN64
        static_assert(sizeof(ExecutionSlot) == 48);
#endif
        std::printf("layout slot=%zu frame=%zu\n", sizeof(ExecutionSlot), sizeof(Frame));
        slotBoundaries();
        realVm();
        callbackAndMoveAdmission();
        inactiveVmMoveAndThreadHandoff();
        std::puts("vm_private_reference_contract=passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED %s\n", e.what());
        return 1;
    }
}
