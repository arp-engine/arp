#include "scriptvm/interpreter/vm.h"

#include "core/cpu_allocator.h"
#include "core/log.h"
#include "reflection/reflection.h"
#include "scriptvm/ir/typed_ir.h"
#include "scriptvm/runtime/semantic_runtime.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <optional>

namespace arp::scriptvm {

namespace {

// Empty captures vector (PMR-aware), reused for non-closure calls.
const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& emptyCaptures()
{
    static const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> empty{
        &arp::core::defaultCpuMemoryResource()};
    return empty;
}

// Empty args vector (PMR-aware).
const arp::core::pmr::Vector<Value>& emptyArgs()
{
    static const arp::core::pmr::Vector<Value> empty(
        (arp::core::pmr::Vector<Value>::allocator_type(&arp::core::defaultCpuMemoryResource())));
    return empty;
}

int depthBudget(std::size_t continuationFrames) noexcept
{
    constexpr auto maxIntBudget = static_cast<std::size_t>(std::numeric_limits<int>::max() - 1);
    return static_cast<int>(std::min(continuationFrames, maxIntBudget));
}

int depthOverflow(int remaining) noexcept
{
    if (remaining <= 0)
        return 1;
    if (remaining == std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return remaining + 1;
}

int depthAdd(int lhs, int rhs) noexcept
{
    if (rhs > std::numeric_limits<int>::max() - lhs)
        return std::numeric_limits<int>::max();
    return lhs + rhs;
}

int delegatedAwaitDepth(const Value& value, int remaining)
{
    if (remaining <= 0)
        return depthOverflow(remaining);
    if (value.is<ScriptGenerator>()) {
        const auto* generator = value.ptr<ScriptGenerator>();
        if (generator == nullptr || !generator->delegatedChildPending)
            return 0;
        if (generator->delegatedSourceSlot < 0
            || generator->delegatedSourceSlot >= static_cast<int>(generator->frame.locals.size()))
            return depthOverflow(remaining);
        return depthAdd(1,
            delegatedAwaitDepth(
                generator->frame.locals[static_cast<std::size_t>(generator->delegatedSourceSlot)],
                remaining - 1));
    }
    if (const auto* bridge = generatorBridgeV2();
        bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(value))
        return bridge->delegatedAwaitDepth != nullptr
            ? bridge->delegatedAwaitDepth(value, remaining)
            : depthOverflow(remaining);
    return 0;
}

int delegatedAwaitDepth(const ScriptGenerator& generator)
{
    if (!generator.delegatedChildPending)
        return 0;
    if (generator.delegatedSourceSlot < 0
        || generator.delegatedSourceSlot >= static_cast<int>(generator.frame.locals.size()))
        return depthOverflow(depthBudget(
            generator.ownerVm != nullptr ? generator.ownerVm->maxContinuationFrames() : 0u));
    const int budget =
        depthBudget(generator.ownerVm != nullptr ? generator.ownerVm->maxContinuationFrames() : 0u);
    return depthAdd(1,
        delegatedAwaitDepth(
            generator.frame.locals[static_cast<std::size_t>(generator.delegatedSourceSlot)],
            budget - 1));
}

bool programOwnsFunction(const Program& program, const Function* function)
{
    if (function == nullptr)
        return false;
    for (const auto& candidate : program.functions) {
        if (&candidate == function)
            return true;
    }
    return false;
}

bool interpreterBridgeIsGenerator(const Value& value) noexcept
{
    return isScriptGenerator(value);
}

bool interpreterBridgeQueryIdentity(const Value& value, GeneratorIdentityV3& identity) noexcept
{
    const auto* generator = asScriptGenerator(value);
    if (generator == nullptr || generator->ownerVm == nullptr || generator->moduleId.empty()
        || generator->bridgeArtifactIdentity.empty())
        return false;
    identity.moduleId = generator->moduleId.c_str();
    identity.moduleVersion = generator->moduleVersion;
    identity.artifactIdentity = generator->bridgeArtifactIdentity.c_str();
    return identity.complete();
}

int interpreterBridgeDelegatedAwaitDepth(const Value& value, int remaining) noexcept
{
    return delegatedAwaitDepth(value, remaining);
}

bool interpreterBridgeIsDone(const Value& value) noexcept
{
    const auto* generator = asScriptGenerator(value);
    return generator == nullptr || generator->done;
}

bool interpreterBridgeIsSuspended(const Value& value) noexcept
{
    const auto* generator = asScriptGenerator(value);
    return generator != nullptr
        && (generator->suspendedFrame != nullptr || generator->delegatedChildPending);
}

bool supportsPendingAwaitInjection(const Value& value, int remaining) noexcept
{
    if (remaining <= 0)
        return false;
    if (const auto* generator = asScriptGenerator(value); generator != nullptr) {
        if (generator->suspendedFrame != nullptr) {
            const auto* awaitable = asScriptAwaitable(generator->suspendedAwaitable);
            return awaitable != nullptr && awaitable->supportsInjection();
        }
        if (!generator->delegatedChildPending || generator->delegatedSourceSlot < 0
            || generator->delegatedSourceSlot >= static_cast<int>(generator->frame.locals.size()))
            return false;
        return supportsPendingAwaitInjection(
            generator->frame.locals[static_cast<std::size_t>(generator->delegatedSourceSlot)],
            remaining - 1);
    }
    const auto* capability = generatorBridgeV3Capability();
    return capability != nullptr && capability->isGenerator != nullptr
        && capability->isGenerator(value) && capability->supportsPendingAwaitInjection != nullptr
        && capability->supportsPendingAwaitInjection(value);
}

bool interpreterBridgeSupportsPendingAwaitInjection(const Value& value) noexcept
{
    const auto* generator = asScriptGenerator(value);
    if (generator == nullptr || generator->ownerVm == nullptr)
        return false;
    return supportsPendingAwaitInjection(
        value, depthBudget(generator->ownerVm->maxContinuationFrames()));
}

Value interpreterBridgeResume(Value& value, VmError* error)
{
    auto* generator = asScriptGenerator(value);
    VmError localError;
    VmError& target = error != nullptr ? *error : localError;
    if (generator == nullptr || generator->ownerVm == nullptr) {
        target.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        target.message = "Interpreter generator has no owning VM";
        return makeNil();
    }
    return generator->ownerVm->resumeGeneratorValue(*generator, target);
}

Value interpreterBridgeResumeSend(Value& value, const Value& sent, VmError* error)
{
    auto* generator = asScriptGenerator(value);
    VmError localError;
    VmError& target = error != nullptr ? *error : localError;
    if (generator == nullptr || generator->ownerVm == nullptr) {
        target.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        target.message = "Interpreter generator has no owning VM";
        return makeNil();
    }
    return generator->ownerVm->resumeGeneratorValueSend(*generator, sent, target);
}

Value interpreterBridgeResumeThrow(Value& value, const Value& thrown, VmError* error)
{
    auto* generator = asScriptGenerator(value);
    VmError localError;
    VmError& target = error != nullptr ? *error : localError;
    if (generator == nullptr || generator->ownerVm == nullptr) {
        target.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        target.message = "Interpreter generator has no owning VM";
        return makeNil();
    }
    return generator->ownerVm->resumeGeneratorValueThrow(*generator, thrown, target);
}

void interpreterBridgeClose(Value& value) noexcept
{
    auto* generator = asScriptGenerator(value);
    if (generator == nullptr)
        return;
    if (generator->ownerVm != nullptr) {
        generator->ownerVm->closeGenerator(*generator);
        return;
    }
    generator->done = true;
    generator->suspendedFrame.reset();
}

void installInterpreterGeneratorBridge() noexcept
{
    static const GeneratorBridgeV2 bridge{
        &interpreterBridgeIsGenerator,
        &interpreterBridgeDelegatedAwaitDepth,
        &interpreterBridgeIsDone,
        &interpreterBridgeIsSuspended,
        &interpreterBridgeResume,
        &interpreterBridgeResumeSend,
        &interpreterBridgeResumeThrow,
        &interpreterBridgeClose,
        nullptr,
    };
    installGeneratorBridgeV2(&bridge);
    static const GeneratorBridgeV3 bridgeV3{
        kGeneratorBridgeV3ContractVersion,
        sizeof(GeneratorBridgeV3),
        &interpreterBridgeIsGenerator,
        &interpreterBridgeDelegatedAwaitDepth,
        &interpreterBridgeIsDone,
        &interpreterBridgeIsSuspended,
        &interpreterBridgeResume,
        &interpreterBridgeResumeSend,
        &interpreterBridgeResumeThrow,
        &interpreterBridgeClose,
        nullptr,
        &interpreterBridgeQueryIdentity,
    };
    installGeneratorBridgeV3(&bridgeV3);
    static const GeneratorBridgeV3Capability capability{
        kGeneratorBridgeV3CapabilityContractVersion,
        sizeof(GeneratorBridgeV3Capability),
        &interpreterBridgeIsGenerator,
        &interpreterBridgeSupportsPendingAwaitInjection,
    };
    installGeneratorBridgeV3Capability(&capability);
}

ScriptDiagnosticCode interpreterHandlerFilter(const Function& function, int handlerPc, bool& valid)
{
    valid = true;
    for (const auto& instruction : function.code) {
        if (instruction.op != Opcode::PushHandler || instruction.a != handlerPc)
            continue;
        if (instruction.s.empty())
            return ScriptDiagnosticCode::None;
        ScriptDiagnosticCode filter = ScriptDiagnosticCode::None;
        if (!scriptDiagnosticCodeFromName(instruction.s, filter))
            valid = false;
        return filter;
    }
    valid = false;
    return ScriptDiagnosticCode::None;
}

// A yielded value is not reintroduced on a resume path that computes a new
// value and returns it without a StoreLocal consumer. Keeping the injected
// send value out of the operand stack preserves the frame's statically
// declared maxStack for source-level `yield; return expr` generators, while
// retaining send() for the explicit StoreLocal continuation shape.
bool resumeSendIgnoredByDirectReturn(const ScriptGenerator& generator)
{
    const auto* function = generator.frame.function;
    const auto ip = generator.frame.ip;
    if (function == nullptr || ip < 0 || ip >= static_cast<int>(function->code.size()))
        return false;
    for (int cursor = ip; cursor < static_cast<int>(function->code.size()); ++cursor) {
        switch (function->code[static_cast<std::size_t>(cursor)].op) {
        case Opcode::StoreLocal:
        case Opcode::StoreCapture:
        case Opcode::StoreGlobal:
        case Opcode::StoreIndirect:
            // Only the first store can consume the value injected at the
            // yield boundary. Later stores belong to the resumed program's
            // own computation and must not retain that value on the stack.
            return cursor != ip;
        case Opcode::Return:
        case Opcode::ReturnInt:
            return true;
        case Opcode::Yield:
        case Opcode::Jump:
        case Opcode::JumpIfFalse:
        case Opcode::JumpIfTrue:
            return false;
        default:
            break;
        }
    }
    return false;
}

// Delegation continuation is compiler-owned metadata. Interpreter execution
// never infers it from neighboring bytecode positions.
int delegationYieldPcForRange(const Function& function, int rangePc, int valueSlot)
{
    for (const auto& site : function.delegationSites) {
        if (site.rangePc == rangePc && site.valueSlot == valueSlot && site.yieldPc >= 0
            && site.yieldPc < static_cast<int>(function.code.size())) {
            return site.yieldPc;
        }
    }
    return -1;
}

core::pmr::String programArtifactIdentity(const Program& program)
{
    const auto& identity = program.artifactIdentity;
    if (!identity.complete())
        return core::pmr::String{&core::defaultCpuMemoryResource()};
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    result += identity.moduleId;
    result += "\n";
    result += std::to_string(identity.moduleVersion);
    result += "\n";
    result += identity.sourceHash;
    result += "\n";
    result += identity.dependencyGraphHash;
    result += "\n";
    result += identity.reflectionRegistryHash;
    result += "\n";
    result += identity.typedIrFingerprint;
    return result;
}

bool programMatchesFrame(const Program& program,
    const core::pmr::String& moduleId,
    std::uint32_t moduleVersion,
    const core::pmr::String& artifactIdentity)
{
    const auto& identity = program.artifactIdentity;
    if (identity.moduleId != moduleId || identity.moduleVersion != moduleVersion)
        return false;
    return artifactIdentity.empty() || programArtifactIdentity(program) == artifactIdentity;
}

template <typename Dictionary>
const Value* findFastDictionaryValue(const Dictionary& dictionary, const Value& key)
{
    if (key.is<int>()) {
        const int wanted = *key.ptr<int>();
        for (const auto& entry : dictionary) {
            if (entry.first.template is<int>() && *entry.first.template ptr<int>() == wanted)
                return &entry.second;
        }
    } else if (key.is<std::int64_t>()) {
        const std::int64_t wanted = *key.ptr<std::int64_t>();
        for (const auto& entry : dictionary) {
            if (entry.first.template is<std::int64_t>()
                && *entry.first.template ptr<std::int64_t>() == wanted)
                return &entry.second;
        }
    } else if (key.is<core::pmr::String>()) {
        const auto& wanted = *key.ptr<core::pmr::String>();
        for (const auto& entry : dictionary) {
            if (entry.first.template is<core::pmr::String>()
                && *entry.first.template ptr<core::pmr::String>() == wanted)
                return &entry.second;
        }
    }
    return nullptr;
}

template <typename Dictionary>
Value* findFastDictionaryValue(Dictionary& dictionary, const Value& key)
{
    if (key.is<int>()) {
        const int wanted = *key.ptr<int>();
        for (auto& entry : dictionary) {
            if (entry.first.template is<int>() && *entry.first.template ptr<int>() == wanted)
                return &entry.second;
        }
    } else if (key.is<std::int64_t>()) {
        const std::int64_t wanted = *key.ptr<std::int64_t>();
        for (auto& entry : dictionary) {
            if (entry.first.template is<std::int64_t>()
                && *entry.first.template ptr<std::int64_t>() == wanted)
                return &entry.second;
        }
    } else if (key.is<core::pmr::String>()) {
        const auto& wanted = *key.ptr<core::pmr::String>();
        for (auto& entry : dictionary) {
            if (entry.first.template is<core::pmr::String>()
                && *entry.first.template ptr<core::pmr::String>() == wanted)
                return &entry.second;
        }
    }
    return nullptr;
}

bool appendValidatedArgument(const Value& argument,
    RuntimeType expected,
    bool nullable,
    const core::pmr::String& className,
    arp::core::pmr::Vector<Value>& normalized,
    std::size_t index,
    VmError& err)
{
    const auto result =
        ScriptSemanticRuntime::normalizeArgument(argument, expected, nullable, className);
    if (result.ok()) {
        normalized.push_back(result.value);
        return true;
    }
    err.code = result.diagnostic.code;
    err.message = "argument ";
    err.message += std::to_string(index + 1u);
    err.message += " ";
    err.message += result.diagnostic.message;
    return false;
}

bool normalizePublicArguments(const Function& function,
    const arp::core::pmr::Vector<Value>& args,
    arp::core::pmr::Vector<Value>& normalized,
    VmError& err)
{
    const int fixedParams = function.isVariadic ? function.fixedParams : function.params;
    if ((!function.isVariadic && static_cast<int>(args.size()) != function.params)
        || (function.isVariadic && static_cast<int>(args.size()) < fixedParams)) {
        return true; // Let runFunctionInternal report the canonical arg-count error.
    }
    if (function.paramTypes.size() < static_cast<std::size_t>(fixedParams))
        return true; // Hand-authored bytecode without a parameter contract.

    normalized.reserve(args.size());
    for (int i = 0; i < fixedParams; ++i) {
        if (!appendValidatedArgument(args[static_cast<std::size_t>(i)],
                function.paramTypes[static_cast<std::size_t>(i)],
                i < static_cast<int>(function.nullableParams.size())
                    && function.nullableParams[static_cast<std::size_t>(i)] != 0,
                i < static_cast<int>(function.paramClassNames.size())
                    ? function.paramClassNames[static_cast<std::size_t>(i)]
                    : core::pmr::String{},
                normalized,
                static_cast<std::size_t>(i),
                err)) {
            return false;
        }
    }
    if (function.isVariadic) {
        for (std::size_t i = static_cast<std::size_t>(fixedParams); i < args.size(); ++i) {
            if (!appendValidatedArgument(args[i],
                    function.variadicElementType,
                    function.variadicElementNullable,
                    function.variadicElementClassName,
                    normalized,
                    i,
                    err)) {
                return false;
            }
        }
    }
    return true;
}

// Set when entering executeFrame so we know we've reached the base frame.
struct FrameResult {
    arp::core::pmr::Vector<Value> values = arp::core::pmr::Vector<Value>(
        arp::core::pmr::Vector<Value>::allocator_type(&arp::core::defaultCpuMemoryResource()));
    bool returned = false; // true if this frame executed a top-level Return
};

inline double bitsToDouble(std::int64_t bits)
{
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

inline std::int64_t doubleToBits(double d)
{
    std::int64_t bits;
    std::memcpy(&bits, &d, sizeof(bits));
    return bits;
}

// Check whether v is a ScriptPointer (registered type).
bool isScriptPointer(const Value& v)
{
    return v.is<ScriptPointer>();
}

const ScriptPointer& asScriptPointer(const Value& v)
{
    return *v.ptr<ScriptPointer>();
}

bool isScriptClosure(const Value& v)
{
    return v.is<ScriptClosure>();
}

const ScriptClosure& asScriptClosure(const Value& v)
{
    return *v.ptr<ScriptClosure>();
}

bool isHostCallable(const Value& v)
{
    return v.is<HostCallable>();
}

bool isScriptMethod(const Value& v)
{
    return v.is<ScriptMethod>();
}

bool isNumericScalar(const Value& v)
{
    return v.is<int>() || v.is<std::int64_t>() || v.is<float>() || v.is<double>();
}

bool isIntegerScalar(const Value& v)
{
    return v.is<int>() || v.is<std::int64_t>();
}

bool normalizeNumericOperands(Value& lhs, Value& rhs)
{
    if (!isNumericScalar(lhs) || !isNumericScalar(rhs)) {
        return true;
    }
    if (lhs.is<float>() || rhs.is<float>() || lhs.is<double>() || rhs.is<double>()) {
        double lhsValue = 0.0;
        double rhsValue = 0.0;
        if (!tryGetFloat(lhs, lhsValue) || !tryGetFloat(rhs, rhsValue)) {
            return false;
        }
        lhs = makeFloat(lhsValue);
        rhs = makeFloat(rhsValue);
    } else if (lhs.is<std::int64_t>() || rhs.is<std::int64_t>()) {
        std::int64_t lhsValue = 0;
        std::int64_t rhsValue = 0;
        if (!tryGetInt64(lhs, lhsValue) || !tryGetInt64(rhs, rhsValue)) {
            return false;
        }
        lhs = makeInt64(lhsValue);
        rhs = makeInt64(rhsValue);
    }
    return true;
}

} // namespace

Vm::Vm()
{
    installInterpreterGeneratorBridge();
    registerScriptObjectReflection();
    registerModuleReflection();
}

ScriptResult Vm::bindProgram(const ValidatedInterpreterProgram& program)
{
    if (!program.valid())
        return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
            "cannot bind an empty validated interpreter artifact");
    if (!frames_.empty() || suspended())
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            "cannot rebind a VM with active or suspended execution");
    try {
        // Retain each bound snapshot and its global graph together. Pointer
        // identity in initializer tracking cannot alias a released artifact,
        // and rebinding a different module cannot inherit another's globals.
        core::CpuMemoryQuotaScope quotaScope(memoryQuota_);
        bool registered = false;
        for (const auto& binding : programGlobalBindings_)
            registered = registered || binding.program == program.owner();
        if (!registered)
            setProgramGlobalStorage(program.owner(), core::makeShared<ScriptModuleGlobalStorage>());
        validatedProgram_ = program;
        programOwner_ = program.owner();
        return ScriptResult::success();
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticsScope(nullptr);
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            "validated interpreter binding allocation failed");
    }
}

void Vm::setProgramGlobalStorage(
    std::shared_ptr<const Program> program, std::shared_ptr<ScriptModuleGlobalStorage> storage)
{
    if (program == nullptr || storage == nullptr)
        return;
    for (auto& binding : programGlobalBindings_) {
        if (binding.program.get() == program.get()) {
            binding.program = std::move(program);
            binding.storage = std::move(storage);
            return;
        }
    }
    programGlobalBindings_.push_back(ProgramGlobalBinding{std::move(program), std::move(storage)});
}

arp::core::pmr::Vector<Value>& Vm::globalStorage(const Program& program)
{
    for (const auto& binding : programGlobalBindings_) {
        if (binding.program != nullptr && binding.storage != nullptr
            && binding.program.get() == &program)
            return binding.storage->values;
    }
    return sharedGlobals_ != nullptr ? *sharedGlobals_ : globals_;
}

void Vm::ensureGlobals(const Program& program)
{
    auto& globals = globalStorage(program);
    if (globals.empty() && program.globalCount > 0) {
        globals.resize(program.globalCount, Value::undefined());
    }
}

bool Vm::popInt(Frame& f, int& out, VmError& err)
{
    Value v = f.pop();
    if (!tryGetInt(v, out)) {
        err.code = ScriptDiagnosticCode::BadCast;
        err.message = "expected int operand";
        return false;
    }
    return true;
}

bool Vm::popInt64(Frame& f, std::int64_t& out, VmError& err)
{
    Value v = f.pop();
    if (!tryGetInt64(v, out)) {
        err.code = ScriptDiagnosticCode::BadCast;
        err.message = "expected int64 operand";
        return false;
    }
    return true;
}

bool Vm::popFloat(Frame& f, double& out, VmError& err)
{
    Value v = f.pop();
    if (!tryGetFloat(v, out)) {
        err.code = ScriptDiagnosticCode::BadCast;
        err.message = "expected float operand";
        return false;
    }
    return true;
}

bool Vm::popBool(Frame& f, bool& out, VmError& err)
{
    if (!f.stack.empty() && f.stack.back().is<bool>()) {
        out = *f.stack.back().ptr<bool>();
        f.stack.pop_back();
        return true;
    }
    auto slot = f.popSlot();
    if (slot.is<bool>()) {
        out = *slot.ptr<bool>();
        return true;
    }
    if (!tryGetBool(slot.boxed(), out)) {
        err.code = ScriptDiagnosticCode::BadCast;
        err.message = "expected bool operand";
        return false;
    }
    return true;
}

bool Vm::popString(Frame& f, core::pmr::String& out, VmError& err)
{
    Value v = f.pop();
    if (!tryGetString(v, out)) {
        err.code = ScriptDiagnosticCode::BadCast;
        err.message = "expected string operand";
        return false;
    }
    return true;
}

bool Vm::popPointer(Frame& f, std::shared_ptr<ScriptCell>& out, VmError& err)
{
    ScriptPointer pointer;
    if (!popScriptPointer(f, pointer, err))
        return false;
    if (pointer.kind != ScriptPointerKind::Cell || !pointer.cell) {
        err.code = ScriptDiagnosticCode::NilDeref;
        err.message = "expected cell pointer operand";
        return false;
    }
    out = std::move(pointer.cell);
    return true;
}

bool Vm::popScriptPointer(Frame& f, ScriptPointer& out, VmError& err)
{
    Value v = f.pop();
    if (!isScriptPointer(v)) {
        err.code = ScriptDiagnosticCode::NilDeref;
        err.message = "expected pointer operand";
        return false;
    }
    out = *v.ptr<ScriptPointer>();
    if (out.kind == ScriptPointerKind::Cell && !out.cell) {
        err.code = ScriptDiagnosticCode::NilDeref;
        err.message = "nil pointer dereference";
        return false;
    }
    return true;
}

void Vm::buildTraceback(const Program& program, VmError& err) const
{
    auto& res = arp::core::defaultCpuMemoryResource();
    core::pmr::String tb{&res};
    tb += "traceback (most recent call last):\n";
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        const Frame& fr = **it;
        if (!fr.function)
            continue;
        const auto& pos = (!fr.function->code.empty() && fr.lastIp >= 0
                              && fr.lastIp < static_cast<int>(fr.function->code.size()))
            ? fr.function->code[fr.lastIp].pos
            : SourcePos{};
        tb += "  at ";
        tb += fr.function->name;
        tb += "(";
        if (pos.valid()) {
            tb += pos.file;
            tb += ":";
            tb += std::to_string(pos.line);
        }
        tb += ")\n";
    }
    err.traceback = std::move(tb);
}

core::pmr::String Vm::logicalTraceback() const
{
    core::pmr::String traceback{&arp::core::defaultCpuMemoryResource()};
    traceback += "traceback (most recent call last):\n";
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        const Frame& frame = **it;
        if (frame.function == nullptr)
            continue;
        const auto& position = (!frame.function->code.empty() && frame.lastIp >= 0
                                   && frame.lastIp < static_cast<int>(frame.function->code.size()))
            ? frame.function->code[frame.lastIp].pos
            : SourcePos{};
        traceback += "  at ";
        traceback += frame.function->name;
        traceback += "(";
        if (position.valid()) {
            traceback += position.file;
            traceback += ":";
            traceback += std::to_string(position.line);
        }
        traceback += ")\n";
    }
    return traceback;
}

Vm::FunctionFrameLifetime::~FunctionFrameLifetime()
{
    if (frame_ != nullptr)
        vm_.discardFunctionFrame(*this);
}

void Vm::discardFunctionFrame(FunctionFrameLifetime& activation) noexcept
{
    Frame* frame = activation.frame_;
    if (activation.published_) {
        assert(!frames_.empty() && frames_.back() == frame);
        frames_.pop_back();
    }
    // Exceptional unwinding must not allocate to publish a pool pointer.
    // The original owning vector can release this actual frame in place.
    // The caller's original logical scope outlives this physical guard.
    frame->reset(nullptr);
    const auto owner =
        std::find_if(frameStorage_.begin(), frameStorage_.end(), [frame](const auto& candidate) {
            return candidate.get() == frame;
        });
    assert(owner != frameStorage_.end());
    frameStorage_.erase(owner);
    activation.frame_ = nullptr;
    activation.published_ = false;
}

template <bool ProvenStaticCall, bool SlotReturn>
Vm::InternalReturn<SlotReturn> Vm::runFunctionInternalImpl(const Program& program,
    int funcIndex,
    const ExecutionArgumentsView& args,
    const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
    VmError* err,
    bool* produced,
    ExecutionSlot* slotResult)
{
    static_assert(!SlotReturn || ProvenStaticCall);
    const auto nilResult = []() -> InternalReturn<SlotReturn> {
        if constexpr (SlotReturn)
            return {};
        else
            return makeNil();
    };
    const auto publish = [](Value value) -> InternalReturn<SlotReturn> {
        if constexpr (SlotReturn)
            return {};
        else
            return value;
    };
    if (produced != nullptr)
        *produced = false;
    // Frame storage is bounded by the compiled function graph for ordinary
    // calls; reserve it before recursive dispatch so PMR frame moves cannot
    // invalidate active execution state.
    if constexpr (!ProvenStaticCall) {
        if (frames_.capacity() < program.functions.size() + 1u)
            frames_.reserve(program.functions.size() + 1u);
    }
    if (frames_.empty() && executionSession_ == nullptr)
        remainingInstructions_ = instructionBudget_ >= 0 ? instructionBudget_ : -1;
    const auto metadataInvalid = [](bool invalid) {
        if constexpr (ProvenStaticCall) {
            assert(!invalid);
            return false;
        } else
            return invalid;
    };
    if (metadataInvalid(funcIndex < 0 || funcIndex >= static_cast<int>(program.functions.size()))) {
        if (err) {
            err->code = ScriptDiagnosticCode::InvalidOpcode;
            err->message = "invalid function index";
        }
        return nilResult();
    }
    if (executionSession_ == nullptr && recursionLimit_ >= 0
        && frames_.size() >= static_cast<std::size_t>(recursionLimit_)) {
        if (err) {
            err->code = ScriptDiagnosticCode::RecursionLimitExceeded;
            err->message = "script recursion limit exceeded";
        }
        return nilResult();
    }
    const Function& fn = program.functions[funcIndex];
    ScriptExecutionFrameScope executionFrame(executionSession_.get());
    if (!executionFrame.active()) {
        if (err != nullptr) {
            err->code = ScriptDiagnosticCode::RecursionLimitExceeded;
            err->message = "script recursion limit exceeded";
        }
        return nilResult();
    }
    if (metadataInvalid(fn.locals < 0 || fn.maxStack < 0)
        || static_cast<std::size_t>(fn.locals) > maxFrameValues_
        || static_cast<std::size_t>(fn.maxStack) > maxFrameValues_) {
        if (err) {
            err->code = ScriptDiagnosticCode::ResourceLimitExceeded;
            err->message = "script frame metadata exceeds the execution limit";
        }
        return nilResult();
    }
    if (args.size() > maxCallArguments_) {
        if (err) {
            err->code = ScriptDiagnosticCode::ResourceLimitExceeded;
            err->message = "script call exceeds the argument limit";
        }
        return nilResult();
    }
    const int fixedParams = ProvenStaticCall ? fn.params
        : fn.isVariadic                      ? fn.fixedParams
                                             : fn.params;
    if (metadataInvalid((!fn.isVariadic && static_cast<int>(args.size()) != fn.params)
            || (fn.isVariadic && static_cast<int>(args.size()) < fixedParams))) {
        if (err) {
            err->code = ScriptDiagnosticCode::WrongArgCount;
            err->message = "function '";
            err->message += fn.name;
            err->message += "' expected ";
            err->message += std::to_string(fixedParams);
            err->message += fn.isVariadic ? " or more" : "";
            err->message += " argument(s), got ";
            err->message += std::to_string(args.size());
        }
        return nilResult();
    }
    int prefixResumePc = 0, prefixLastPc = 0, prefixCost = 0;
    if constexpr (SlotReturn) {
        // All mutable admission and the original logical execution scope are
        // already active. No callback or allocation precedes this transaction.
        const auto* plan = validatedProgram_.scalarExpressionsOwned(&fn);
        auto& budget =
            executionSession_ ? executionSession_->remainingInstructions : remainingInstructions_;
        if (memoryQuota_ == nullptr && plan && plan->guardedReturn.valid()
            && (budget < 0 || budget >= plan->guardedReturn.cost())) {
            const auto& guard = plan->guardedReturn;
            bool returns = false;
            if (guard.evaluate(args, returns)) {
                if (returns) {
                    auto result = guard.result(args);
                    args.finishConsumption();
                    *slotResult = std::move(result);
                    if (budget > 0)
                        budget -= guard.cost();
                    if (produced != nullptr)
                        *produced = true;
                    return {};
                }
                prefixResumePc = guard.resumePc();
                prefixLastPc = guard.lastPc();
                prefixCost = guard.prefixCost();
            }
        }
    }
    // Reuse a pooled frame if available, otherwise allocate it once and keep
    // ownership in frameStorage_. Pointer reuse avoids moving the frame's PMR
    // vectors on every recursive call.
    FunctionFrameLifetime lifetime(*this);
    Frame* frame = nullptr;
    if (!framePool_.empty()) {
        frame = framePool_.back();
        framePool_.pop_back();
        lifetime.frame_ = frame;
        frame->reset(&fn);
    } else {
        auto owner = arp::core::makeUnique<Frame>();
        frame = owner.get();
        frame->reset(&fn);
        frameStorage_.push_back(std::move(owner));
        lifetime.frame_ = frame;
    }
    for (int i = 0; i < fixedParams; ++i)
        frame->locals[i] = args[i];
    if constexpr (!ProvenStaticCall) {
        if (fn.isVariadic) {
            auto& res = arp::core::defaultCpuMemoryResource();
            arp::reflection::hlist variadicArgs((arp::reflection::hlist::allocator_type(&res)));
            variadicArgs.reserve(args.size() - static_cast<std::size_t>(fixedParams));
            for (std::size_t i = static_cast<std::size_t>(fixedParams); i < args.size(); ++i)
                variadicArgs.push_back(args[i]);
            frame->locals[fixedParams] = Value{std::move(variadicArgs)};
        }
    }
    // Copy captures.
    if constexpr (ProvenStaticCall) {
        assert(fn.captureCount == 0 && !fn.isVariadic && !fn.isGenerator && captures.empty());
    } else
        frame->captures = captures;
    args.finishConsumption();

    // Keep the active frame in its local scope. The traceback stack stores a
    // pointer, so recursive calls cannot invalidate the frame by reallocating
    // the owning frame pool/vector.
    frames_.push_back(frame);
    lifetime.published_ = true;
    if constexpr (SlotReturn) {
        if (prefixCost != 0) {
            auto& budget = executionSession_ ? executionSession_->remainingInstructions
                                             : remainingInstructions_;
            if (budget > 0)
                budget -= prefixCost;
            frame->ip = prefixResumePc;
            frame->lastIp = prefixLastPc;
        }
    }
    bool frameProduced = false;
    const auto execute = [&]() -> InternalReturn<SlotReturn> {
        if constexpr (ProvenStaticCall) {
            assert(validatedProgram_.owner().get() == &program
                && validatedProgram_.hasVerifiedStackBoundOwned(&fn));
            if (validatedProgram_.hasScalarDispatchOwned(&fn)) {
                // The private call publishes only to the real ExecutionSlot.
                // Do not acquire a second undefined Value carrier per call.
                std::optional<Value> result;
                if constexpr (!SlotReturn)
                    result.emplace(Value::undefined());
                const auto status = executeScalarDispatch(
                    program, *frame, *err, result ? &*result : nullptr, &frameProduced, slotResult);
                if (status == ScalarDispatchStatus::Returned) {
                    if constexpr (SlotReturn)
                        return {};
                    else
                        return std::move(*result);
                }
                if (status == ScalarDispatchStatus::Failed)
                    return publish(executeFrameImpl<true, false>(
                        program, *frame, *err, &frameProduced, true, slotResult));
            }
            const auto* plan = validatedProgram_.scalarExpressionsOwned(&fn);
            if (plan && plan->hasMethodLoop)
                return publish(executeFrameImpl<true, true, true>(
                    program, *frame, *err, &frameProduced, false, slotResult));
            return publish(executeFrameImpl<true, true>(
                program, *frame, *err, &frameProduced, false, slotResult));
        } else
            return publish(
                executeFrameRef(program, *frame, *err, &frameProduced, false, slotResult));
    };
    auto result = execute();
    assert(lifetime.published_ && !frames_.empty() && frames_.back() == frame);
    frames_.pop_back();
    lifetime.published_ = false;
    frame->reset(nullptr);
    framePool_.push_back(frame);
    lifetime.frame_ = nullptr;
    if (produced != nullptr)
        *produced = frameProduced;
    return result;
}

bool Vm::createGenerator(const Program& program,
    int funcIndex,
    const arp::core::pmr::Vector<Value>& args,
    const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
    Value& out,
    VmError& err)
{
    if (funcIndex < 0 || funcIndex >= static_cast<int>(program.functions.size())) {
        err.code = ScriptDiagnosticCode::InvalidOpcode;
        err.message = "invalid generator function index";
        return false;
    }
    const Function& fn = program.functions[funcIndex];
    const int fixedParams = fn.isVariadic ? fn.fixedParams : fn.params;
    if ((!fn.isVariadic && static_cast<int>(args.size()) != fn.params)
        || (fn.isVariadic && static_cast<int>(args.size()) < fixedParams)) {
        err.code = ScriptDiagnosticCode::WrongArgCount;
        err.message = "function '";
        err.message += fn.name;
        err.message += "' expected ";
        err.message += std::to_string(fixedParams);
        err.message += fn.isVariadic ? " or more" : "";
        err.message += " argument(s), got ";
        err.message += std::to_string(args.size());
        return false;
    }
    if (fixedParams < 0 || fixedParams > fn.locals) {
        err.code = ScriptDiagnosticCode::InvalidOpcode;
        err.message = "generator parameter slots are invalid";
        return false;
    }
    if (fn.locals < 0 || fn.maxStack < 0 || static_cast<std::size_t>(fn.locals) > maxFrameValues_
        || static_cast<std::size_t>(fn.maxStack) > maxFrameValues_
        || args.size() > maxCallArguments_) {
        err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
        err.message = "script generator frame or argument limit exceeded";
        return false;
    }

    auto& res = arp::core::defaultCpuMemoryResource();
    ScriptGenerator generator{&fn};
    generator.retentionLease = makeScriptModuleRetentionLease(retentionBudget_);
    for (int i = 0; i < fixedParams; ++i)
        generator.frame.locals[i] = args[i];
    if (fn.isVariadic) {
        arp::reflection::hlist variadicArgs((arp::reflection::hlist::allocator_type(&res)));
        variadicArgs.reserve(args.size() - static_cast<std::size_t>(fixedParams));
        for (std::size_t i = static_cast<std::size_t>(fixedParams); i < args.size(); ++i)
            variadicArgs.push_back(args[i]);
        generator.frame.locals[fixedParams] = Value{std::move(variadicArgs)};
    }
    generator.frame.captures = captures;
    generator.program = &program;
    generator.moduleId = moduleId_;
    generator.moduleVersion = moduleVersion_;
    generator.bridgeArtifactIdentity = bridgeArtifactIdentity_;
    if (programOwner_ != nullptr && programOwner_.get() == &program)
        generator.programOwner = programOwner_;
    generator.ownerVm = this;
    generator.ownerVmOwner = owner_.lock();
    generator.funcIndex = funcIndex;
    out = Value{std::move(generator)};
    return true;
}

Value Vm::resumeGenerator(ScriptGenerator& generator, VmError& err, bool injectError)
{
    if (generator.done)
        return makeNil();
    if (generator.program == nullptr) {
        err.code = ScriptDiagnosticCode::InvalidOpcode;
        err.message = "generator has no owning program";
        return makeNil();
    }

    const bool boundedDelegatedChild = activeGenerator_ != nullptr && activeGenerator_ != &generator
        && activeGenerator_->delegatedChildPending && activeGenerator_->delegatedSourceSlot >= 0
        && activeGenerator_->delegatedSourceSlot
            < static_cast<int>(activeGenerator_->frame.locals.size())
        && activeGenerator_->frame
               .locals[static_cast<std::size_t>(activeGenerator_->delegatedSourceSlot)]
               .is<ScriptGenerator>()
        && activeGenerator_->frame
                .locals[static_cast<std::size_t>(activeGenerator_->delegatedSourceSlot)]
                .ptr<ScriptGenerator>()
            == &generator
        && delegatedAwaitDepth(*activeGenerator_) <= depthBudget(maxContinuationFrames_);
    if (activeGenerator_ != nullptr && activeGenerator_ != &generator && !boundedDelegatedChild
        && (generator.suspendedFrame != nullptr || !generator.suspendedCallers.empty())) {
        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        err.message = "nested generator pending continuation is not representable";
        return makeNil();
    }

    generator.started = true;
    generator.resumingSuspendedCallers =
        generator.suspendedFrame != nullptr && !generator.suspendedCallers.empty();
    if (!generator.suspendedCallerPrograms.empty()
        && generator.suspendedCallerPrograms.size() != generator.suspendedCallers.size()) {
        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        err.message = "generator caller program provenance is incomplete";
        return makeNil();
    }

    ScriptGenerator* previousGenerator = activeGenerator_;
    const Program* previousActiveProgram = activeProgram_;
    const auto previousActiveProgramOwner = activeProgramOwner_;
    activeGenerator_ = &generator;
    auto restoreGenerator = [&]() {
        activeGenerator_ = previousGenerator;
        activeProgram_ = previousActiveProgram;
        activeProgramOwner_ = previousActiveProgramOwner;
    };

    std::optional<Frame> resumedFrame;
    Frame* frame = &generator.frame;
    activeProgram_ = generator.program;
    activeProgramOwner_ = generator.programOwner;
    if (generator.suspendedFrame != nullptr) {
        activeProgram_ = generator.suspendedFrameProgram != nullptr
            ? generator.suspendedFrameProgram
            : generator.program;
        activeProgramOwner_ = generator.suspendedFrameProgramOwner != nullptr
            ? generator.suspendedFrameProgramOwner
            : generator.programOwner;
        if (!isScriptAwaitable(generator.suspendedAwaitable)) {
            generator.suspendedFrame.reset();
            generator.suspendedAwaitable = makeNil();
            generator.suspendedAwaitInjection = makeNil();
            generator.suspendedAwaitInjectionReady = false;
            restoreGenerator();
            err.code = ScriptDiagnosticCode::HostError;
            err.message = "generator continuation has no awaitable";
            return makeNil();
        }
        Value awaitedResult;
        if (generator.suspendedAwaitInjectionReady) {
            // The injection callback already resolved the await boundary.
            // Consume the callback result directly; polling here would
            // execute the host operation twice and violate no-replay.
            awaitedResult = std::move(generator.suspendedAwaitInjection);
            generator.suspendedAwaitInjection = makeNil();
            generator.suspendedAwaitInjectionReady = false;
        } else {
            arp::core::pmr::String awaitDiagnostics{&arp::core::defaultCpuMemoryResource()};
            const auto status = asScriptAwaitable(generator.suspendedAwaitable)
                                    ->pollResult(awaitedResult, awaitDiagnostics);
            if (status == ScriptAwaitStatus::Pending) {
                restoreGenerator();
                err.code = ScriptDiagnosticCode::Suspended;
                err.message = "generator await remains pending";
                return makeNil();
            }
            if (status != ScriptAwaitStatus::Failed)
                goto generator_await_ready;
            err.code = ScriptDiagnosticCode::HostError;
            err.message = awaitDiagnostics.empty()
                ? arp::core::pmr::String(
                      "generator await failed", &arp::core::defaultCpuMemoryResource())
                : std::move(awaitDiagnostics);

            // A failed await is a script exception at the suspended
            // operation boundary. Preserve the generator-owned frame/caller
            // chain, inject into the nearest retained handler, and resume
            // through the normal interpreter unwind path. This is the same
            // no-replay rule used by Vm::resume for ordinary continuations.
            auto injectFailure = [&err](Frame& target) {
                if (target.handlers.empty())
                    return false;
                const auto handler = target.handlers.back();
                target.handlers.pop_back();
                target.stack.resize(handler.stackDepth);
                target.stack.push_back(makeString(err.message));
                err.traceback.clear();
                err.code = ScriptDiagnosticCode::None;
                err.message.clear();
                target.ip = handler.handlerPc;
                return true;
            };

            Frame failedFrame = std::move(*generator.suspendedFrame);
            generator.suspendedFrame.reset();
            generator.suspendedAwaitable = makeNil();
            generator.suspendedAwaitInjection = makeNil();
            generator.suspendedAwaitInjectionReady = false;
            auto callers = std::move(generator.suspendedCallers);
            auto callerPushResults = std::move(generator.suspendedCallerPushResults);
            auto callerPrograms = std::move(generator.suspendedCallerPrograms);
            auto callerProgramOwners = std::move(generator.suspendedCallerProgramOwners);
            generator.suspendedCallers.clear();
            generator.suspendedCallerPushResults.clear();
            generator.suspendedCallerPrograms.clear();
            generator.suspendedCallerProgramOwners.clear();

            bool handled = injectFailure(failedFrame);
            std::size_t handlerIndex = callers.size();
            if (handled) {
                resumedFrame.emplace(std::move(failedFrame));
            } else {
                for (std::size_t index = 0; index < callers.size(); ++index) {
                    if (!injectFailure(callers[index]))
                        continue;
                    handled = true;
                    handlerIndex = index;
                    resumedFrame.emplace(std::move(callers[index]));
                    activeProgram_ =
                        index < callerPrograms.size() && callerPrograms[index] != nullptr
                        ? callerPrograms[index]
                        : generator.program;
                    activeProgramOwner_ =
                        index < callerProgramOwners.size() && callerProgramOwners[index] != nullptr
                        ? callerProgramOwners[index]
                        : generator.programOwner;
                    break;
                }
            }
            if (!handled) {
                generator.resumingSuspendedCallers = false;
                restoreGenerator();
                return makeNil();
            }

            // Callers outside the handling frame remain part of the
            // canonical continuation and will be resumed by the existing
            // caller loop after the handler completes.
            if (handlerIndex == callers.size()) {
                // The failed frame handled the exception; every caller is
                // still waiting for this frame's eventual result.
                generator.suspendedCallers = std::move(callers);
                generator.suspendedCallerPushResults = std::move(callerPushResults);
                generator.suspendedCallerPrograms = std::move(callerPrograms);
                generator.suspendedCallerProgramOwners = std::move(callerProgramOwners);
            } else {
                const std::size_t firstOuter = handlerIndex + 1u;
                if (firstOuter < callers.size()) {
                    generator.suspendedCallers.assign(
                        callers.begin() + static_cast<std::ptrdiff_t>(firstOuter), callers.end());
                    if (firstOuter < callerPushResults.size())
                        generator.suspendedCallerPushResults.assign(
                            callerPushResults.begin() + static_cast<std::ptrdiff_t>(firstOuter),
                            callerPushResults.end());
                    if (firstOuter < callerPrograms.size())
                        generator.suspendedCallerPrograms.assign(
                            callerPrograms.begin() + static_cast<std::ptrdiff_t>(firstOuter),
                            callerPrograms.end());
                    if (firstOuter < callerProgramOwners.size())
                        generator.suspendedCallerProgramOwners.assign(
                            callerProgramOwners.begin() + static_cast<std::ptrdiff_t>(firstOuter),
                            callerProgramOwners.end());
                }
            }
            frame = &*resumedFrame;
        }
    generator_await_ready:
        if (!resumedFrame || frame != &*resumedFrame) {
            resumedFrame.emplace(std::move(*generator.suspendedFrame));
            generator.suspendedFrame.reset();
            generator.suspendedAwaitable = makeNil();
            generator.suspendedAwaitInjection = makeNil();
            generator.suspendedAwaitInjectionReady = false;
            resumedFrame->push(std::move(awaitedResult));
            frame = &*resumedFrame;
        }
    }

    bool produced = false;
    Value result = executeFrameRef(*activeProgram_, *frame, err, &produced, injectError);
    if (err.code == ScriptDiagnosticCode::Suspended) {
        if (generator.suspendedFrame == nullptr && produced) {
            // A yield from the generator root is a value-producing
            // suspension, not an await continuation.
            if (resumedFrame && frame == &*resumedFrame)
                generator.frame = std::move(*resumedFrame);
            generator.resumingSuspendedCallers = false;
            restoreGenerator();
            err.code = ScriptDiagnosticCode::None;
            err.message.clear();
            return std::move(result);
        }
        generator.resumingSuspendedCallers = false;
        restoreGenerator();
        return produced ? std::move(result) : makeNil();
    }

    while (err.code == ScriptDiagnosticCode::None && !generator.suspendedCallers.empty()) {
        const Program* callerProgram = generator.suspendedCallerPrograms.empty()
            ? generator.program
            : generator.suspendedCallerPrograms.front();
        const auto callerProgramOwner = generator.suspendedCallerProgramOwners.empty()
            ? generator.programOwner
            : generator.suspendedCallerProgramOwners.front();
        if (callerProgram == nullptr) {
            generator.resumingSuspendedCallers = false;
            restoreGenerator();
            err.code = ScriptDiagnosticCode::AotMissingEntry;
            err.message = "generator caller program is unavailable";
            return makeNil();
        }
        Frame caller = std::move(generator.suspendedCallers.front());
        generator.suspendedCallers.erase(generator.suspendedCallers.begin());
        if (!generator.suspendedCallerPrograms.empty())
            generator.suspendedCallerPrograms.erase(generator.suspendedCallerPrograms.begin());
        if (!generator.suspendedCallerProgramOwners.empty())
            generator.suspendedCallerProgramOwners.erase(
                generator.suspendedCallerProgramOwners.begin());
        const bool pushResult = generator.suspendedCallerPushResults.front() != 0u;
        generator.suspendedCallerPushResults.erase(generator.suspendedCallerPushResults.begin());
        if (pushResult && produced)
            caller.push(std::move(result));
        produced = false;
        activeProgram_ = callerProgram;
        activeProgramOwner_ = callerProgramOwner;
        result = executeFrameRef(*callerProgram, caller, err, &produced);
        if (err.code == ScriptDiagnosticCode::Suspended) {
            if (generator.suspendedFrame == nullptr && produced) {
                // The outermost frame is the generator itself and may yield
                // after an awaited ordinary callee has completed.
                generator.frame = std::move(caller);
                generator.suspendedFrameProgram = callerProgram;
                generator.suspendedFrameProgramOwner = callerProgramOwner;
                generator.suspendedCallers.clear();
                generator.suspendedCallerPushResults.clear();
                generator.suspendedCallerPrograms.clear();
                generator.suspendedCallerProgramOwners.clear();
                generator.resumingSuspendedCallers = false;
                restoreGenerator();
                err.code = ScriptDiagnosticCode::None;
                err.message.clear();
                return std::move(result);
            }
            generator.resumingSuspendedCallers = false;
            restoreGenerator();
            return produced ? std::move(result) : makeNil();
        }
    }

    generator.resumingSuspendedCallers = false;
    restoreGenerator();
    if (err.code != ScriptDiagnosticCode::None)
        return makeNil();

    generator.done = true;
    return produced ? std::move(result) : makeNil();
}

Value Vm::resumeGeneratorValue(ScriptGenerator& generator, VmError& err)
{
    return resumeGenerator(generator, err);
}

bool validateLoadedProgram(const Program& program, VmError& error)
{
    // Version zero is reserved for VM white-box and malformed-bytecode tests.
    if (program.bytecodeVersion == 0u)
        return true;
    const auto validation = validateInterpreterBytecode(program);
    if (validation.ok())
        return true;
    error.code = validation.diagnostic().code;
    error.message = validation.diagnostic().message;
    return false;
}

bool Vm::validateForExecution(const Program& program, VmError& error) const
{
    if (validatedProgram_.valid() && validatedProgram_.owner().get() == &program)
        return true;
    return validateLoadedProgram(program, error);
}

bool Vm::injectPendingGeneratorAwait(
    ScriptGenerator& generator, ScriptAwaitInjectionKind kind, const Value& value, VmError& err)
{
    if (generator.suspendedFrame == nullptr)
        return false;
    const auto* awaitable = asScriptAwaitable(generator.suspendedAwaitable);
    if (awaitable == nullptr || !awaitable->supportsInjection()) {
        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        err.message = kind == ScriptAwaitInjectionKind::Send
            ? "generator send() cannot target a pending await"
            : "generator throw() cannot target a pending await";
        return true;
    }
    Value injected;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    ScriptAwaitInjectionStatus status = ScriptAwaitInjectionStatus::Rejected;
    try {
        status = awaitable->injectResult(kind, value, injected, diagnostics);
    } catch (const std::bad_alloc&) {
        err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
        err.message = "generator await injection exceeded the memory budget";
        return true;
    }
    switch (status) {
    case ScriptAwaitInjectionStatus::Rejected:
        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        err.message = diagnostics.empty()
            ? (kind == ScriptAwaitInjectionKind::Send ? "generator pending await rejected send()"
                                                      : "generator pending await rejected throw()")
            : std::move(diagnostics);
        return true;
    case ScriptAwaitInjectionStatus::Pending:
        err.code = ScriptDiagnosticCode::Suspended;
        err.message = diagnostics.empty() ? "generator await injection remains pending"
                                          : std::move(diagnostics);
        return true;
    case ScriptAwaitInjectionStatus::Failed:
        err.code = ScriptDiagnosticCode::HostError;
        err.message =
            diagnostics.empty() ? "generator await injection failed" : std::move(diagnostics);
        return true;
    case ScriptAwaitInjectionStatus::Ready:
        generator.suspendedAwaitInjection = std::move(injected);
        generator.suspendedAwaitInjectionReady = true;
        err.code = ScriptDiagnosticCode::None;
        err.message.clear();
        err.traceback.clear();
        return true;
    }
    err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
    err.message = "generator await injection returned an unknown status";
    return true;
}

Value Vm::resumeGeneratorValueSend(ScriptGenerator& generator, const Value& value, VmError& err)
{
    if (generator.done) {
        err.code = ScriptDiagnosticCode::None;
        err.message.clear();
        return makeNil();
    }
    if (!generator.started) {
        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        err.message = "generator send() requires a prior yield";
        return makeNil();
    }
    if (injectPendingGeneratorAwait(generator, ScriptAwaitInjectionKind::Send, value, err)) {
        return err.code == ScriptDiagnosticCode::None ? resumeGenerator(generator, err) : makeNil();
    }
    if (generator.frame.function != nullptr && generator.frame.function->hasYieldFrom) {
        const int yieldPc = generator.delegatedChildPending ? generator.delegatedPendingYieldPc
                                                            : generator.delegatedYieldPc;
        if (!generator.delegatedActive || generator.delegatedSourceSlot < 0
            || generator.delegatedSourceSlot >= static_cast<int>(generator.frame.locals.size())
            || yieldPc < 0 || yieldPc >= static_cast<int>(generator.frame.function->code.size())) {
            err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
            err.message = "generator yield-from send state is unavailable";
            return makeNil();
        }
        Value& childValue =
            generator.frame.locals[static_cast<std::size_t>(generator.delegatedSourceSlot)];
        Value childResult;
        if (isScriptGenerator(childValue)) {
            auto* child = asScriptGenerator(childValue);
            if (child == nullptr || child->done || child->ownerVm == nullptr) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "delegated child cannot receive send()";
                return makeNil();
            }
            childResult = child->ownerVm->resumeGeneratorValueSend(*child, value, err);
        } else if (const auto* bridge = generatorBridgeV2(); bridge != nullptr
            && bridge->isGenerator != nullptr && bridge->isGenerator(childValue)) {
            if (bridge->isDone != nullptr && bridge->isDone(childValue)) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "delegated child cannot receive send()";
                return makeNil();
            }
            if (bridge->resumeSend == nullptr) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "generator bridge provider is unavailable";
                return makeNil();
            }
            childResult = bridge->resumeSend(childValue, value, &err);
        } else {
            if (!isNil(value)) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "yield-from collection accepts only nil send()";
                return makeNil();
            }
            // A collection has no child generator to resume.  A nil send()
            // is the protocol's request to continue the ordinary iterator.
            err.code = ScriptDiagnosticCode::None;
            err.message.clear();
            err.traceback.clear();
            return resumeGenerator(generator, err);
        }
        if (err.code != ScriptDiagnosticCode::None)
            return makeNil();
        const bool childDone = isScriptGenerator(childValue)
            ? asScriptGenerator(childValue)->done
            : (generatorBridgeV2()->isDone != nullptr && generatorBridgeV2()->isDone(childValue));
        if (childDone) {
            generator.delegatedActive = false;
            generator.delegatedChildPending = false;
            generator.delegatedPendingRangePc = -1;
            generator.delegatedPendingYieldPc = -1;
            return resumeGenerator(generator, err);
        }
        generator.frame.push(std::move(childResult));
        generator.frame.ip = yieldPc;
        generator.delegatedYieldPc = yieldPc;
        generator.delegatedChildPending = false;
        generator.delegatedPendingRangePc = -1;
        generator.delegatedPendingYieldPc = -1;
        return resumeGenerator(generator, err);
    }
    if (!resumeSendIgnoredByDirectReturn(generator)) {
        if (generator.frame.stack.size() >= maxFrameValues_) {
            err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
            err.message = "generator send() exceeds the frame value limit";
            return makeNil();
        }
        generator.frame.push(value);
    }
    return resumeGenerator(generator, err);
}

Value Vm::resumeGeneratorValueThrow(ScriptGenerator& generator, const Value& value, VmError& err)
{
    if (generator.done) {
        err.code = ScriptDiagnosticCode::None;
        err.message.clear();
        return makeNil();
    }
    if (!generator.started) {
        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        err.message = "generator throw() requires a prior yield";
        return makeNil();
    }
    if (injectPendingGeneratorAwait(generator, ScriptAwaitInjectionKind::Throw, value, err)) {
        return err.code == ScriptDiagnosticCode::None ? resumeGenerator(generator, err) : makeNil();
    }
    if (generator.frame.function != nullptr && generator.frame.function->hasYieldFrom
        && generator.delegatedActive) {
        if (generator.delegatedSourceSlot < 0
            || generator.delegatedSourceSlot >= static_cast<int>(generator.frame.locals.size())
            || (generator.delegatedChildPending ? generator.delegatedPendingYieldPc
                                                : generator.delegatedYieldPc)
                < 0
            || (generator.delegatedChildPending ? generator.delegatedPendingYieldPc
                                                : generator.delegatedYieldPc)
                >= static_cast<int>(generator.frame.function->code.size())) {
            err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
            err.message = "generator yield-from throw state is unavailable";
            return makeNil();
        }
        Value& childValue =
            generator.frame.locals[static_cast<std::size_t>(generator.delegatedSourceSlot)];
        Value childResult;
        if (isScriptGenerator(childValue)) {
            auto* child = asScriptGenerator(childValue);
            if (child == nullptr || child->done || child->ownerVm == nullptr) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "delegated child cannot receive throw()";
                return makeNil();
            }
            childResult = child->ownerVm->resumeGeneratorValueThrow(*child, value, err);
        } else if (const auto* bridge = generatorBridgeV2(); bridge != nullptr
            && bridge->isGenerator != nullptr && bridge->isGenerator(childValue)) {
            if (bridge->isDone != nullptr && bridge->isDone(childValue)) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "delegated child cannot receive throw()";
                return makeNil();
            }
            if (bridge->resumeThrow == nullptr) {
                err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                err.message = "generator bridge provider is unavailable";
                return makeNil();
            }
            childResult = bridge->resumeThrow(childValue, value, &err);
        } else {
            // Collections do not consume throw() themselves.  Inject the
            // exception at the parent RangeNext boundary so its own handler
            // can observe it without replaying the preceding yield.
            generator.delegatedActive = false;
            generator.delegatedChildPending = false;
            generator.delegatedPendingRangePc = -1;
            generator.delegatedPendingYieldPc = -1;
            err.code = ScriptDiagnosticCode::UserRaise;
            if (!tryGetString(value, err.message))
                err.message = "script raised a non-string value";
            return resumeGenerator(generator, err, true);
        }
        if (err.code == ScriptDiagnosticCode::UserRaise) {
            const auto message = err.message;
            generator.delegatedActive = false;
            generator.delegatedChildPending = false;
            generator.delegatedPendingRangePc = -1;
            generator.delegatedPendingYieldPc = -1;
            err.code = ScriptDiagnosticCode::UserRaise;
            err.message = message;
            return resumeGenerator(generator, err, true);
        }
        if (err.code != ScriptDiagnosticCode::None)
            return makeNil();
        const bool childDone = isScriptGenerator(childValue)
            ? asScriptGenerator(childValue)->done
            : (generatorBridgeV2()->isDone != nullptr && generatorBridgeV2()->isDone(childValue));
        if (childDone) {
            generator.delegatedActive = false;
            generator.delegatedChildPending = false;
            generator.delegatedPendingRangePc = -1;
            generator.delegatedPendingYieldPc = -1;
            return resumeGenerator(generator, err);
        }
        generator.frame.push(std::move(childResult));
        const int yieldPc = generator.delegatedChildPending ? generator.delegatedPendingYieldPc
                                                            : generator.delegatedYieldPc;
        generator.frame.ip = yieldPc;
        generator.delegatedYieldPc = yieldPc;
        generator.delegatedChildPending = false;
        generator.delegatedPendingRangePc = -1;
        generator.delegatedPendingYieldPc = -1;
        return resumeGenerator(generator, err);
    }
    err.code = ScriptDiagnosticCode::UserRaise;
    if (!tryGetString(value, err.message))
        err.message = "script raised a non-string value";
    return resumeGenerator(generator, err, true);
}

void Vm::closeGenerator(ScriptGenerator& generator) noexcept
{
    if (generator.delegatedActive && generator.delegatedSourceSlot >= 0
        && generator.delegatedSourceSlot < static_cast<int>(generator.frame.locals.size())) {
        Value& childValue =
            generator.frame.locals[static_cast<std::size_t>(generator.delegatedSourceSlot)];
        if (isScriptGenerator(childValue)) {
            if (auto* child = asScriptGenerator(childValue);
                child != nullptr && child->ownerVm != nullptr)
                child->ownerVm->closeGenerator(*child);
        } else if (const auto* bridge = generatorBridgeV2(); bridge != nullptr
            && bridge->isGenerator != nullptr && bridge->isGenerator(childValue)) {
            if (bridge->close != nullptr)
                bridge->close(childValue);
        }
    }
    generator.suspendedAwaitInjection = makeNil();
    generator.suspendedAwaitInjectionReady = false;
    generator.suspendedAwaitable = makeNil();
    generator.suspendedFrame.reset();
    generator.delegatedPendingRangePc = -1;
    generator.delegatedPendingYieldPc = -1;
    generator.delegatedChildPending = false;
    generator.done = true;
}

ScriptResult Vm::snapshotGeneratorFrame(const ScriptGenerator& generator,
    const core::pmr::String& moduleId,
    std::uint32_t moduleVersion,
    SuspendedScriptFrame& out) const
{
    // Only a started, non-terminal generator has a resumable continuation.
    // The frame object remains allocated after close()/completion, so checking
    // the frame pointer alone would allow an initial or terminal state to be
    // adopted and replayed as if it were suspended.
    if (!generator.started || generator.done)
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("generator is not suspended at a resumable boundary",
                &core::defaultCpuMemoryResource()));
    if (generator.suspendedAwaitInjectionReady)
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("generator await injection must be consumed before adoption",
                &core::defaultCpuMemoryResource()));
    if (generator.program == nullptr || generator.funcIndex < 0
        || generator.funcIndex >= static_cast<int>(generator.program->functions.size()))
        return ScriptResult::failure(ScriptDiagnosticCode::HostError,
            core::pmr::String(
                "generator has no valid interpreter program", &core::defaultCpuMemoryResource()));
    if (moduleId.empty() || moduleVersion == 0u
        || (!generator.moduleId.empty() && generator.moduleId != moduleId)
        || (generator.moduleVersion != 0u && generator.moduleVersion != moduleVersion))
        return ScriptResult::failure(ScriptDiagnosticCode::HybridModuleVersionMismatch,
            core::pmr::String(
                "generator module identity is invalid", &core::defaultCpuMemoryResource()));
    if (generator.program->artifactIdentity.complete()
        && (generator.program->artifactIdentity.moduleId != moduleId
            || generator.program->artifactIdentity.moduleVersion != moduleVersion))
        return ScriptResult::failure(ScriptDiagnosticCode::HybridModuleVersionMismatch,
            core::pmr::String(
                "generator program artifact identity does not match the requested module",
                &core::defaultCpuMemoryResource()));

    const Frame& frame =
        generator.suspendedFrame != nullptr ? *generator.suspendedFrame : generator.frame;
    if (frame.function == nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::HostError,
            core::pmr::String(
                "generator has no suspended frame", &core::defaultCpuMemoryResource()));
    if (!generator.suspendedCallerPrograms.empty()
        && generator.suspendedCallerPrograms.size() != generator.suspendedCallers.size())
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("generator caller program provenance is incomplete",
                &core::defaultCpuMemoryResource()));
    if (!generator.suspendedCallerProgramOwners.empty()
        && generator.suspendedCallerProgramOwners.size() != generator.suspendedCallers.size())
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("generator caller program ownership is incomplete",
                &core::defaultCpuMemoryResource()));
    if (generator.suspendedCallers.size() > maxContinuationFrames_)
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            core::pmr::String("generator caller chain exceeds the continuation frame limit",
                &core::defaultCpuMemoryResource()));
    const Program* activeProgram = generator.suspendedFrameProgram != nullptr
        ? generator.suspendedFrameProgram
        : generator.program;
    if (activeProgram == nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
            core::pmr::String("generator active Interpreter program is unavailable",
                &core::defaultCpuMemoryResource()));
    if (!programOwnsFunction(*activeProgram, frame.function))
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("generator active frame function provenance is invalid",
                &core::defaultCpuMemoryResource()));
    if (generator.delegatedChildPending) {
        if (delegatedAwaitDepth(generator) > depthBudget(maxContinuationFrames_))
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String(
                    "interpreter delegated child-await depth exceeds the bounded contract",
                    &core::defaultCpuMemoryResource()));
        if (generator.suspendedFrame != nullptr || generator.delegatedPendingRangePc < 0
            || generator.delegatedPendingRangePc != frame.ip || generator.delegatedSourceSlot < 0
            || generator.delegatedSourceSlot >= static_cast<int>(frame.locals.size())
            || generator.delegatedPendingYieldPc < 0
            || generator.delegatedPendingYieldPc >= static_cast<int>(frame.function->code.size())
            || frame.function->code[static_cast<std::size_t>(generator.delegatedPendingYieldPc)].op
                != Opcode::Yield)
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String("interpreter delegated child-await retry state is invalid",
                    &core::defaultCpuMemoryResource()));
        const Value& childValue =
            frame.locals[static_cast<std::size_t>(generator.delegatedSourceSlot)];
        bool childPending = false;
        if (childValue.is<ScriptGenerator>()) {
            const auto* child = childValue.ptr<ScriptGenerator>();
            childPending = child != nullptr && !child->done && child->ownerVm != nullptr
                && (child->suspendedFrame != nullptr || child->delegatedChildPending);
        } else if (const auto* bridge = generatorBridgeV2(); bridge != nullptr
            && bridge->isGenerator != nullptr && bridge->isGenerator(childValue)) {
            const bool done = bridge->isDone != nullptr && bridge->isDone(childValue);
            const bool suspended =
                bridge->isSuspended != nullptr && bridge->isSuspended(childValue);
            const bool delegated = bridge->delegatedAwaitDepth != nullptr
                && bridge->delegatedAwaitDepth(childValue, 1) > 0;
            childPending = !done && (suspended || delegated);
        }
        if (!childPending)
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String("interpreter delegated child-await ownership is invalid",
                    &core::defaultCpuMemoryResource()));
    }
    const auto& activeIdentity = activeProgram->artifactIdentity;
    const core::pmr::String activeModuleId =
        activeIdentity.complete() ? activeIdentity.moduleId : moduleId;
    const auto activeModuleVersion =
        activeIdentity.complete() ? activeIdentity.moduleVersion : moduleVersion;
    out = SuspendedScriptFrame{};
    out.moduleId = activeModuleId;
    out.moduleVersion = activeModuleVersion;
    out.artifactIdentity = programArtifactIdentity(*activeProgram);
    out.interpreterProgram = generator.suspendedFrameProgramOwner != nullptr
        ? generator.suspendedFrameProgramOwner
        : generator.programOwner;
    const auto& generatorFunction =
        generator.program->functions[static_cast<std::size_t>(generator.funcIndex)];
    const core::pmr::String generatorModuleId = generator.program->artifactIdentity.complete()
        ? generator.program->artifactIdentity.moduleId
        : moduleId;
    out.generatorFunctionId = makeTypedIrFunctionId(generatorModuleId, generatorFunction.name);
    out.functionId = makeTypedIrFunctionId(activeModuleId, frame.function->name);
    out.bytecodePc = frame.ip;
    out.safepointId = out.functionId;
    out.safepointId += ":pc:";
    out.safepointId += std::to_string(frame.ip);
    const int sourcePc =
        frame.lastIp >= 0 && frame.lastIp < static_cast<int>(frame.function->code.size())
        ? frame.lastIp
        : frame.ip;
    if (sourcePc >= 0 && sourcePc < static_cast<int>(frame.function->code.size()))
        out.source = frame.function->code[static_cast<std::size_t>(sourcePc)].pos;
    out.locals = frame.locals.materialize();
    out.operandStack = frame.stack.materialize();
    out.delegatedSourceSlot = generator.delegatedSourceSlot;
    out.delegatedRangePc = generator.delegatedPendingRangePc;
    out.delegatedYieldPc = generator.delegatedYieldPc;
    out.delegatedActive = generator.delegatedActive;
    out.delegatedChildAwaiting = generator.delegatedChildPending;
    out.delegatedPendingYieldPc = generator.delegatedPendingYieldPc;
    out.awaiting = generator.suspendedFrame != nullptr;
    out.suspendedValue = out.awaiting ? generator.suspendedAwaitable : generator.yieldedValue;
    for (const auto& capture : frame.captures) {
        out.captures.push_back(capture ? capture->value : makeNil());
        out.captureCells.push_back(capture);
    }
    for (const auto& handler : frame.handlers) {
        out.handlerStack.push_back(handler.handlerPc);
        out.handlerStackDepths.push_back(handler.stackDepth);
    }
    for (std::size_t callerIndex = 0; callerIndex < generator.suspendedCallers.size();
        ++callerIndex) {
        const auto& callerFrame = generator.suspendedCallers[callerIndex];
        if (callerFrame.function == nullptr)
            return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
                core::pmr::String(
                    "generator caller has no function", &core::defaultCpuMemoryResource()));
        SuspendedScriptCallerFrame caller;
        const Program* callerProgram = generator.suspendedCallerPrograms.empty()
            ? generator.program
            : generator.suspendedCallerPrograms[callerIndex];
        if (callerProgram == nullptr)
            return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
                core::pmr::String("generator caller Interpreter program is unavailable",
                    &core::defaultCpuMemoryResource()));
        const auto& callerIdentity = callerProgram->artifactIdentity;
        const auto callerModuleId = callerIdentity.complete() ? callerIdentity.moduleId : moduleId;
        caller.moduleId = callerModuleId;
        caller.moduleVersion =
            callerIdentity.complete() ? callerIdentity.moduleVersion : moduleVersion;
        caller.artifactIdentity = programArtifactIdentity(*callerProgram);
        caller.interpreterProgram = !generator.suspendedCallerProgramOwners.empty()
            ? generator.suspendedCallerProgramOwners[callerIndex]
            : generator.programOwner;
        caller.functionId = makeTypedIrFunctionId(callerModuleId, callerFrame.function->name);
        caller.bytecodePc = callerFrame.ip;
        if (callerFrame.lastIp >= 0
            && callerFrame.lastIp < static_cast<int>(callerFrame.function->code.size()))
            caller.source =
                callerFrame.function->code[static_cast<std::size_t>(callerFrame.lastIp)].pos;
        else if (callerFrame.ip >= 0
            && callerFrame.ip < static_cast<int>(callerFrame.function->code.size()))
            caller.source =
                callerFrame.function->code[static_cast<std::size_t>(callerFrame.ip)].pos;
        caller.locals = callerFrame.locals.materialize();
        caller.operandStack = callerFrame.stack.materialize();
        for (const auto& capture : callerFrame.captures) {
            caller.captures.push_back(capture ? capture->value : makeNil());
            caller.captureCells.push_back(capture);
        }
        for (const auto& handler : callerFrame.handlers) {
            caller.handlerStack.push_back(handler.handlerPc);
            caller.handlerStackDepths.push_back(handler.stackDepth);
        }
        out.callers.push_back(std::move(caller));
    }
    return validateSuspendedScriptFrame(out);
}

ScriptResult Vm::adoptGeneratorFrame(
    ScriptGenerator& generator, const Program& program, const SuspendedScriptFrame& frame)
{
    const auto valid = validateSuspendedScriptFrame(frame);
    if (!valid.ok())
        return valid;
    if (frame.callers.size() > maxContinuationFrames_)
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            core::pmr::String(
                "canonical generator caller chain exceeds the continuation frame limit",
                &core::defaultCpuMemoryResource()));
    const bool foreignActiveFrame = (!moduleId_.empty() && frame.moduleId != moduleId_)
        || (moduleVersion_ != 0u && frame.moduleVersion != moduleVersion_);
    const Program* activeProgram = &program;
    std::shared_ptr<const Program> activeProgramOwner = programOwner_;
    if (frame.interpreterProgram != nullptr) {
        const bool legacyRootProgram = frame.interpreterProgram.get() == &program
            && !frame.interpreterProgram->artifactIdentity.complete();
        if (!legacyRootProgram
            && !programMatchesFrame(*frame.interpreterProgram,
                frame.moduleId,
                frame.moduleVersion,
                frame.artifactIdentity))
            return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
                core::pmr::String("canonical active Interpreter program identity does not match",
                    &core::defaultCpuMemoryResource()));
        activeProgram = frame.interpreterProgram.get();
        activeProgramOwner = frame.interpreterProgram;
    }
    if (foreignActiveFrame && (frame.interpreterProgram == nullptr || activeProgram == &program))
        return ScriptResult::failure(ScriptDiagnosticCode::HybridModuleVersionMismatch,
            core::pmr::String("foreign canonical generator has no receiving Interpreter program",
                &core::defaultCpuMemoryResource()));
    if (activeProgram->artifactIdentity.complete()
        && !programMatchesFrame(
            *activeProgram, frame.moduleId, frame.moduleVersion, frame.artifactIdentity))
        return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
            core::pmr::String("canonical generator program identity does not match",
                &core::defaultCpuMemoryResource()));

    const core::pmr::String& rootId =
        frame.generatorFunctionId.empty() ? frame.functionId : frame.generatorFunctionId;
    const auto& rootModuleId = moduleId_.empty() ? frame.moduleId : moduleId_;
    const Function* generatorFunction = nullptr;
    int generatorIndex = -1;
    for (std::size_t index = 0; index < program.functions.size(); ++index) {
        if (makeTypedIrFunctionId(rootModuleId, program.functions[index].name) == rootId) {
            generatorFunction = &program.functions[index];
            generatorIndex = static_cast<int>(index);
            break;
        }
    }
    if (generatorFunction == nullptr && foreignActiveFrame) {
        for (std::size_t index = 0; index < activeProgram->functions.size(); ++index) {
            if (makeTypedIrFunctionId(frame.moduleId, activeProgram->functions[index].name)
                == rootId) {
                generatorFunction = &activeProgram->functions[index];
                generatorIndex = static_cast<int>(index);
                break;
            }
        }
    }
    if (generatorFunction == nullptr || !generatorFunction->isGenerator)
        return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
            core::pmr::String("canonical frame has no interpreter generator owner",
                &core::defaultCpuMemoryResource()));
    if (frame.delegatedActive && frame.delegatedSourceSlot >= generatorFunction->locals)
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("canonical delegated source slot is outside the generator",
                &core::defaultCpuMemoryResource()));
    if (frame.delegatedChildAwaiting) {
        if (frame.delegatedSourceSlot < 0
            || frame.delegatedSourceSlot >= static_cast<int>(frame.locals.size())
            || delegatedAwaitDepth(
                   frame.locals[static_cast<std::size_t>(frame.delegatedSourceSlot)],
                   depthBudget(maxContinuationFrames_))
                >= depthBudget(maxContinuationFrames_))
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String(
                    "canonical delegated child-await depth exceeds the bounded contract",
                    &core::defaultCpuMemoryResource()));
    }
    const Function* function = nullptr;
    for (const auto& candidate : activeProgram->functions) {
        if (makeTypedIrFunctionId(frame.moduleId, candidate.name) == frame.functionId) {
            function = &candidate;
            break;
        }
    }
    if (function == nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
            core::pmr::String("canonical generator active frame is unavailable",
                &core::defaultCpuMemoryResource()));
    if (frame.locals.size() != static_cast<std::size_t>(std::max(0, function->locals))
        || frame.operandStack.size() > static_cast<std::size_t>(std::max(0, function->maxStack))
        || frame.handlerStack.size() != frame.handlerStackDepths.size())
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("canonical generator active-frame slots do not match interpreter",
                &core::defaultCpuMemoryResource()));
    if (!frame.awaiting && function != generatorFunction)
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("non-await generator continuation is not at its root frame",
                &core::defaultCpuMemoryResource()));
    if (frame.delegatedChildAwaiting
        && (frame.delegatedPendingYieldPc < 0
            || frame.delegatedPendingYieldPc >= static_cast<int>(function->code.size())
            || function->code[static_cast<std::size_t>(frame.delegatedPendingYieldPc)].op
                != Opcode::Yield))
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("canonical delegated child-await yield continuation is invalid",
                &core::defaultCpuMemoryResource()));

    int resumePc = frame.bytecodePc;
    if (frame.awaiting) {
        resumePc = -1;
        for (std::size_t index = 0; index < function->code.size(); ++index) {
            const auto& instruction = function->code[index];
            if (instruction.op == Opcode::Await && instruction.pos == frame.source) {
                if (resumePc >= 0)
                    return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                        core::pmr::String("canonical generator maps to multiple await points",
                            &core::defaultCpuMemoryResource()));
                resumePc = static_cast<int>(index + 1u);
            }
        }
        if (resumePc < 0)
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String("canonical generator has no interpreter await point",
                    &core::defaultCpuMemoryResource()));
    }
    if (resumePc < 0 || resumePc > static_cast<int>(function->code.size()))
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String(
                "canonical generator resume PC is invalid", &core::defaultCpuMemoryResource()));
    if (!frame.awaiting) {
        const bool delegatedRetry = frame.delegatedChildAwaiting
            && resumePc < static_cast<int>(function->code.size())
            && function->code[static_cast<std::size_t>(resumePc)].op == Opcode::RangeNext;
        const bool yieldResume = !frame.delegatedChildAwaiting && resumePc > 0
            && function->code[static_cast<std::size_t>(resumePc - 1u)].op == Opcode::Yield;
        if (!delegatedRetry && !yieldResume)
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String(
                    "canonical generator resume PC is not a declared operation boundary",
                    &core::defaultCpuMemoryResource()));
    }

    Frame imported;
    imported.reset(function);
    imported.ip = resumePc;
    imported.lastIp = resumePc > 0 ? resumePc - 1 : 0;
    imported.locals = frame.locals;
    imported.stack = frame.operandStack;
    if (!frame.captureCells.empty()) {
        imported.captures = frame.captureCells;
    } else {
        for (const auto& capture : frame.captures)
            imported.captures.push_back(core::makeShared<ScriptCell>(capture));
    }
    for (std::size_t index = 0; index < frame.handlerStack.size(); ++index) {
        if (frame.handlerStack[index] < 0
            || frame.handlerStack[index] >= static_cast<int>(function->code.size())
            || frame.handlerStackDepths[index] > imported.stack.size())
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String("canonical generator handler metadata is invalid",
                    &core::defaultCpuMemoryResource()));
        imported.handlers.push_back(
            Frame::HandlerInfo{frame.handlerStack[index], frame.handlerStackDepths[index]});
    }

    core::pmr::Vector<Frame> importedCallers{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<const Program*> importedCallerPrograms{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<const Program>> importedCallerOwners{
        &core::defaultCpuMemoryResource()};
    for (const auto& caller : frame.callers) {
        const Program* callerProgram = &program;
        std::shared_ptr<const Program> callerProgramOwner = programOwner_;
        const auto& callerModuleId = caller.moduleId.empty() ? frame.moduleId : caller.moduleId;
        const auto callerVersion =
            caller.moduleVersion != 0u ? caller.moduleVersion : frame.moduleVersion;
        if (caller.interpreterProgram != nullptr) {
            const bool legacyRootProgram = caller.interpreterProgram.get() == &program
                && !caller.interpreterProgram->artifactIdentity.complete();
            if (!legacyRootProgram
                && !programMatchesFrame(*caller.interpreterProgram,
                    callerModuleId,
                    callerVersion,
                    caller.artifactIdentity))
                return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
                    core::pmr::String(
                        "canonical caller Interpreter program identity does not match",
                        &core::defaultCpuMemoryResource()));
            callerProgram = caller.interpreterProgram.get();
            callerProgramOwner = caller.interpreterProgram;
        } else if (callerProgram->artifactIdentity.complete()
            && !programMatchesFrame(
                *callerProgram, callerModuleId, callerVersion, caller.artifactIdentity)) {
            return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
                core::pmr::String("canonical caller Interpreter program identity does not match",
                    &core::defaultCpuMemoryResource()));
        }
        const Function* callerFunction = nullptr;
        for (const auto& candidate : callerProgram->functions) {
            if (makeTypedIrFunctionId(callerModuleId, candidate.name) == caller.functionId) {
                callerFunction = &candidate;
                break;
            }
        }
        if (callerFunction == nullptr)
            return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
                core::pmr::String("canonical generator caller is unavailable in interpreter",
                    &core::defaultCpuMemoryResource()));
        Frame importedCaller;
        importedCaller.reset(callerFunction);
        importedCaller.ip = caller.bytecodePc;
        importedCaller.lastIp = caller.bytecodePc > 0 ? caller.bytecodePc - 1 : 0;
        importedCaller.locals = caller.locals;
        importedCaller.stack = caller.operandStack;
        if (!caller.captureCells.empty()) {
            importedCaller.captures = caller.captureCells;
        } else {
            for (const auto& capture : caller.captures)
                importedCaller.captures.push_back(core::makeShared<ScriptCell>(capture));
        }
        if (caller.handlerStack.size() != caller.handlerStackDepths.size())
            return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                core::pmr::String("canonical generator caller handler metadata is invalid",
                    &core::defaultCpuMemoryResource()));
        for (std::size_t index = 0; index < caller.handlerStack.size(); ++index) {
            if (caller.handlerStack[index] < 0
                || caller.handlerStack[index] >= static_cast<int>(callerFunction->code.size())
                || caller.handlerStackDepths[index] > importedCaller.stack.size())
                return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
                    core::pmr::String("canonical generator caller handler metadata is invalid",
                        &core::defaultCpuMemoryResource()));
            importedCaller.handlers.push_back(
                Frame::HandlerInfo{caller.handlerStack[index], caller.handlerStackDepths[index]});
        }
        importedCallers.push_back(std::move(importedCaller));
        importedCallerPrograms.push_back(callerProgram);
        importedCallerOwners.push_back(std::move(callerProgramOwner));
    }

    generator.program = &program;
    generator.suspendedFrameProgram = activeProgram;
    generator.suspendedFrameProgramOwner = std::move(activeProgramOwner);
    generator.moduleId = rootModuleId;
    generator.moduleVersion = foreignActiveFrame
        ? frame.moduleVersion
        : (moduleVersion_ != 0u ? moduleVersion_ : frame.moduleVersion);
    generator.programOwner = programOwner_;
    generator.ownerVm = this;
    generator.ownerVmOwner = owner_.lock();
    generator.funcIndex = generatorIndex;
    generator.started = true;
    generator.done = false;
    generator.yieldedValue = frame.awaiting ? makeNil() : frame.suspendedValue;
    generator.suspendedCallers = std::move(importedCallers);
    generator.suspendedCallerPrograms = std::move(importedCallerPrograms);
    generator.suspendedCallerProgramOwners = std::move(importedCallerOwners);
    generator.suspendedCallerPushResults.clear();
    generator.suspendedCallerPushResults.resize(generator.suspendedCallers.size(), 1u);
    generator.resumingSuspendedCallers = false;
    generator.delegatedSourceSlot = frame.delegatedSourceSlot;
    generator.delegatedYieldPc = frame.delegatedYieldPc;
    generator.delegatedActive = frame.delegatedActive;
    generator.delegatedPendingRangePc = frame.delegatedChildAwaiting ? frame.bytecodePc : -1;
    generator.delegatedPendingYieldPc =
        frame.delegatedChildAwaiting ? frame.delegatedPendingYieldPc : -1;
    generator.delegatedChildPending = frame.delegatedChildAwaiting;
    // Adoption replaces the complete continuation. Never retain an
    // injection result or capability from the previous backend/frame.
    generator.suspendedAwaitInjection = makeNil();
    generator.suspendedAwaitInjectionReady = false;
    if (frame.awaiting) {
        generator.frame.reset(generatorFunction);
        generator.suspendedFrame = arp::core::makeShared<Frame>(std::move(imported));
        generator.suspendedAwaitable = frame.suspendedValue;
    } else {
        generator.suspendedFrame.reset();
        generator.suspendedAwaitable = makeNil();
        generator.frame = std::move(imported);
    }
    return ScriptResult::success();
}

Value Vm::executeFrame(const Program& program, Frame&& frame, VmError& err, bool* produced)
{
    return executeFrameRef(program, frame, err, produced);
}

Value Vm::executeFrameRef(const Program& program,
    Frame& frame,
    VmError& err,
    bool* produced,
    bool injectError,
    ExecutionSlot* slotResult)
{
    const bool owned = validatedProgram_.valid() && validatedProgram_.owner().get() == &program;
    // Adopted/suspended frames retain their dynamic stack checks. Only a fresh
    // frame at the exact analyzed entry can consume the CFG stack proof.
    if (owned && validatedProgram_.ownsFunction(frame.function)
        && frame.locals.size() == static_cast<std::size_t>(frame.function->locals)) {
        if (validatedProgram_.hasVerifiedStackBoundOwned(frame.function) && !injectError
            && frame.ip == 0 && frame.stack.empty() && frame.handlers.empty()) {
            if (callBridge_ == nullptr
                && validatedProgram_.hasScalarDispatchOwned(frame.function)) {
                Value result = Value::undefined();
                const auto status =
                    executeScalarDispatch(program, frame, err, &result, produced, slotResult);
                if (status == ScalarDispatchStatus::Returned)
                    return result;
                if (status == ScalarDispatchStatus::Failed)
                    return executeFrameImpl<true, false>(
                        program, frame, err, produced, true, slotResult);
            }
            const auto* plan = validatedProgram_.scalarExpressionsOwned(frame.function);
            if (plan && plan->hasMethodLoop)
                return executeFrameImpl<true, true, true>(
                    program, frame, err, produced, injectError, slotResult);
            return executeFrameImpl<true, true>(
                program, frame, err, produced, injectError, slotResult);
        }
        const auto* plan = validatedProgram_.independentScalarExpressionsOwned(frame.function);
        if (plan && plan->hasClosureLoop)
            return executeFrameImpl<true, false, true>(
                program, frame, err, produced, injectError, slotResult);
        return executeFrameImpl<true, false>(
            program, frame, err, produced, injectError, slotResult);
    }
    return executeFrameImpl<false, false>(program, frame, err, produced, injectError, slotResult);
}

template <bool StaticValidated, bool StackProven, bool CallLoops>
Value Vm::executeFrameImpl(const Program& program,
    Frame& frame,
    VmError& err,
    bool* produced,
    bool injectError,
    ExecutionSlot* slotResult)
{
    const auto* handlerFilters =
        StaticValidated ? validatedProgram_.handlerFiltersOwned(frame.function) : nullptr;
    const auto handlerFilter = [&](int handlerPc, bool& valid, int* unobservedLocal = nullptr) {
        if constexpr (StaticValidated) {
            assert(handlerFilters != nullptr);
            return handlerFilters->lookup(handlerPc, valid, unobservedLocal);
        } else {
            if (unobservedLocal)
                *unobservedLocal = -1;
            return interpreterHandlerFilter(*frame.function, handlerPc, valid);
        }
    };
    const auto staticInvalid = [](bool invalid) {
        if constexpr (StaticValidated) {
            assert(!invalid);
            return false;
        } else
            return invalid;
    };
    const auto stackInvalid = [](bool invalid) {
        if constexpr (StackProven) {
            assert(!invalid);
            return false;
        } else
            return invalid;
    };
    const auto typedInteger = [&frame, &err, &stackInvalid](auto operation) {
        if (stackInvalid(frame.stack.size() < 2u)) {
            err.code = ScriptDiagnosticCode::StackUnderflow;
            err.message = "stack underflow on typed arithmetic";
            return false;
        }
        const int* rhs = frame.stack.back().scalarPointer<int>();
        const int* lhs = frame.stack[frame.stack.size() - 2u].scalarPointer<int>();
        if (!lhs || !rhs) {
            frame.stack.resize(frame.stack.size() - 2u);
            err.code = ScriptDiagnosticCode::BadCast;
            err.message = "typed arithmetic operand is not int";
            return false;
        }
        const auto scalar =
            ScriptSemanticRuntime::intBinary<decltype(operation)::value>(*lhs, *rhs);
        if (!scalar.ok()) {
            frame.stack.resize(frame.stack.size() - 2u);
            err.code = scalar.code;
            err.message = scalar.message;
            return false;
        }
        frame.replaceScalarPair<StackProven>(scalar.value);
        return true;
    };
    const auto comparison = [&frame, &err, &stackInvalid](auto operation) {
        constexpr auto op = decltype(operation)::value;
        if (stackInvalid(frame.stack.size() < 2u)) {
            err.code = ScriptDiagnosticCode::StackUnderflow;
            err.message = "stack underflow on comparison";
            return false;
        }
        ScriptScalar left, right;
        bool value = false;
        if (frame.peekScalarPair<StackProven>(left, right)
            && ScriptSemanticRuntime::scalarCompare<op>(left, right, value)) {
            frame.replaceScalarPair<StackProven>(ScriptScalar(value));
            return true;
        }
        auto rhs = frame.popSlot<StackProven>(), lhs = frame.popSlot<StackProven>();
        std::string_view leftText, rightText;
        if (lhs.stringView(leftText) && rhs.stringView(rightText)) {
            frame.push(
                ScriptScalar(ScriptSemanticRuntime::comparePayload<op>(leftText, rightText)));
            return true;
        }
        const auto semantic = ScriptSemanticRuntime::compare(op, lhs.boxed(), rhs.boxed());
        if (!semantic.ok()) {
            err.code = semantic.diagnostic.code;
            err.message = semantic.diagnostic.message;
            return false;
        }
        frame.push(semantic.value);
        return true;
    };
    const auto arithmetic = [&frame, &err](auto operation) {
        constexpr auto op = decltype(operation)::value;
        ScriptScalar left, right;
        ScalarOperationResult scalar;
        if (frame.peekScalarPair<StackProven>(left, right)
            && ScriptSemanticRuntime::scalarBinary<op>(left, right, scalar)) {
            if (!scalar.ok()) {
                frame.stack.resize(frame.stack.size() - 2u);
                err.code = scalar.code;
                err.message = scalar.message;
                return false;
            }
            frame.replaceScalarPair<StackProven>(scalar.value);
            return true;
        }
        auto rhs = frame.popSlot<StackProven>(), lhs = frame.popSlot<StackProven>();
        if constexpr (op == SemanticBinaryOperation::Add) {
            std::string_view leftText, rightText;
            if (lhs.stringView(leftText) && rhs.stringView(rightText)) {
                auto semantic = ScriptSemanticRuntime::concatenateStrings(leftText, rightText);
                frame.push(std::move(semantic.value));
                return true;
            }
        }
        const auto semantic = ScriptSemanticRuntime::binary(op, lhs.boxed(), rhs.boxed());
        if (!semantic.ok()) {
            err.code = semantic.diagnostic.code;
            err.message = semantic.diagnostic.message;
            return false;
        }
        frame.push(semantic.value);
        return true;
    };
    auto& res = arp::core::defaultCpuMemoryResource();
    Value result = Value::undefined();
    if (produced != nullptr)
        *produced = false;
    if (frames_.empty() && executionSession_ == nullptr)
        remainingInstructions_ = instructionBudget_ >= 0 ? instructionBudget_ : -1;

    const auto& code = frame.function->code;
    const int codeSize = static_cast<int>(code.size());
    const ScalarExpressionPlan* integerPlan = nullptr;
    if constexpr (StaticValidated && StackProven)
        integerPlan = validatedProgram_.scalarExpressionsOwned(frame.function);
    else if constexpr (StaticValidated)
        integerPlan = validatedProgram_.independentScalarExpressionsOwned(frame.function);
    auto preserveSuspendedCaller = [this](Frame&& caller) {
        const Program* callerProgram =
            activeProgram_ != nullptr ? activeProgram_ : suspendedProgram_;
        const auto callerProgramOwner = activeProgramOwner_;
        // A generator owns a separate continuation while an ordinary callee
        // below it is awaiting. Keep that chain on the generator instead of
        // exposing it as the VM's top-level await continuation.
        if (activeGenerator_ != nullptr && activeGenerator_->suspendedFrame != nullptr) {
            if (activeGenerator_->resumingSuspendedCallers
                && !activeGenerator_->suspendedCallers.empty()) {
                activeGenerator_->suspendedCallers.insert(
                    activeGenerator_->suspendedCallers.begin(), std::move(caller));
                activeGenerator_->suspendedCallerPushResults.insert(
                    activeGenerator_->suspendedCallerPushResults.begin(), 1u);
            } else {
                activeGenerator_->suspendedCallers.push_back(std::move(caller));
                activeGenerator_->suspendedCallerPushResults.push_back(1u);
            }
            return;
        }
        suspendedCallers_.push_back(std::move(caller));
        suspendedCallerPushResults_.push_back(1u);
        suspendedCallerPrograms_.push_back(callerProgram);
        suspendedCallerProgramOwners_.push_back(callerProgramOwner);
    };

dispatch:
    while (frame.ip < codeSize) {
        if (injectError) {
            injectError = false;
            goto error_exit;
        }
        auto& remainingInstructions = executionSession_ != nullptr
            ? executionSession_->remainingInstructions
            : remainingInstructions_;
        if (remainingInstructions == 0) {
            err.code = ScriptDiagnosticCode::ExecutionBudgetExceeded;
            err.message = "script instruction budget exceeded";
            goto error_exit;
        }
        if constexpr (StaticValidated && !StackProven) {
            if (integerPlan != nullptr && memoryQuota_ == nullptr) {
                const auto& entry = integerPlan->entries[static_cast<std::size_t>(frame.ip)];
                if constexpr (CallLoops) {
                    if (entry.terminal == ScalarExpressionPlan::Terminal::ClosureLoop) {
                        if (executeScalarClosureLoop(
                                program, frame, *integerPlan, entry, remainingInstructions))
                            continue;
                        goto scalar_call_replay;
                    }
                }
                const auto limit = std::min(maxFrameValues_,
                    static_cast<std::size_t>(std::max(0, frame.function->maxStack)));
                if (entry.count != 0 && frame.handlers.size() <= maxFrameValues_
                    && frame.stack.size() <= limit
                    && integerPlan->scratchSize <= limit - frame.stack.size()
                    && integerPlan->executeIndependent(entry,
                        frame.locals,
                        frame.stack,
                        remainingInstructions,
                        frame.ip,
                        frame.lastIp))
                    continue;
            }
        }
        if constexpr (StaticValidated && StackProven) {
            // Each plan consumes only the operands produced by its own exact
            // instruction span. Existing stack prefixes retain their carriers.
            if (integerPlan != nullptr) {
                const int entryPc = frame.ip;
                const auto& entry = integerPlan->entries[static_cast<std::size_t>(frame.ip)];
                if constexpr (CallLoops) {
                    if (entry.terminal == ScalarExpressionPlan::Terminal::MethodLoop) {
                        if (static_cast<std::size_t>(frame.function->maxStack) <= maxFrameValues_
                            && executeScalarMethodLoop(
                                program, frame, *integerPlan, entry, remainingInstructions))
                            continue;
                        goto scalar_call_replay;
                    }
                }
                if (entry.terminal >= ScalarExpressionPlan::Terminal::IndexedStore
                    && memoryQuota_ == nullptr
                    && static_cast<std::size_t>(frame.function->maxStack) <= maxFrameValues_
                    && (remainingInstructions < 0 || remainingInstructions >= entry.cost)) {
                    if (entry.terminal == ScalarExpressionPlan::Terminal::ConstructionOperands
                        || entry.terminal == ScalarExpressionPlan::Terminal::ConstructionLoop) {
                        if (frame.integerScratch.size() < integerPlan->scratchSize)
                            frame.integerScratch.resize(integerPlan->scratchSize);
                        const bool progressed =
                            entry.terminal == ScalarExpressionPlan::Terminal::ConstructionLoop
                            ? integerPlan->repeatConstruction(entry,
                                  frame.locals,
                                  frame.stack,
                                  frame.integerScratch.data(),
                                  frame.dictionaryScratch,
                                  maxContainerValues_,
                                  remainingInstructions,
                                  frame.ip,
                                  frame.lastIp)
                            : integerPlan->executeConstructionOperands(entry,
                                  frame.locals,
                                  frame.stack,
                                  frame.integerScratch.data(),
                                  remainingInstructions,
                                  frame.ip,
                                  frame.lastIp);
                        if (progressed)
                            continue;
                    } else if (entry.terminal == ScalarExpressionPlan::Terminal::IndexedLoop) {
                        if (integerPlan->repeatIndexed(
                                entry, frame.locals, remainingInstructions, frame.ip, frame.lastIp))
                            continue;
                    } else {
                        ScriptSemanticRuntime::IntegerIndexUpdate update;
                        int value;
                        if (integerPlan->prepareIndexedUpdate(entry, frame.locals, update, value)) {
                            if (remainingInstructions > 0)
                                remainingInstructions -= entry.cost;
                            frame.lastIp = entry.lastPc;
                            frame.ip = entry.nextPc;
                            update.commit(value);
                            continue;
                        }
                    }
                }
                if (entry.count != 0 || entry.appendLocal >= 0) {
                    if (entry.conditionalBody >= 0
                        && integerPlan->repeatConditional(
                            entry, frame.locals, remainingInstructions, frame.ip, frame.lastIp))
                        continue;
                    if (entry.boundIntEvaluator
                        && integerPlan->repeatInteger(
                            entry, frame.locals, remainingInstructions, frame.ip, frame.lastIp))
                        continue;
                    if (entry.boundString
                        && integerPlan->repeatString(entry,
                            frame.locals,
                            code[entry.appendLiteralPc].s,
                            remainingInstructions,
                            frame.ip,
                            frame.lastIp))
                        continue;
                    do {
                        // A self edge reuses the same immutable handler. Each
                        // iteration still evaluates, guards and charges its full
                        // original instruction span. Short budgets/failed guards
                        // leave the current PC for ordinary instruction replay.
                        if (remainingInstructions == 0)
                            goto dispatch;
                        const int cost = entry.cost;
                        // A short budget must stop at the original intermediate PC,
                        // with its original stack effects; replay that path instead.
                        if (remainingInstructions < 0 || remainingInstructions >= cost) {
                            int value = 0;
                            ScriptScalar expressionValue;
                            const auto evaluatePayload = [&] {
                                if (entry.appendLocal >= 0) {
                                    frame.lastIp = entry.appendLiteralPc + 1;
                                    auto& destination =
                                        frame.locals[static_cast<std::size_t>(entry.appendLocal)];
                                    if (entry.stringSource >= 0) {
                                        std::string_view prefix;
                                        if (!frame
                                                .locals[static_cast<std::size_t>(
                                                    entry.stringSource)]
                                                .stringView(prefix))
                                            return false;
                                        return ScriptSemanticRuntime::assignUniqueString(
                                            destination, prefix, code[entry.appendLiteralPc].s);
                                    }
                                    return ScriptSemanticRuntime::appendUniqueString(
                                        destination, code[entry.appendLiteralPc].s);
                                }
                                if (entry.floating) {
                                    if (!entry.floatEvaluator
                                        && frame.floatScratch.size() < integerPlan->scratchSize)
                                        frame.floatScratch.resize(integerPlan->scratchSize);
                                    double floatingValue;
                                    if (!integerPlan->evaluateFloatSlots(entry,
                                            frame.locals,
                                            frame.floatScratch.data(),
                                            floatingValue))
                                        return false;
                                    expressionValue = ScriptScalar(floatingValue);
                                    value = floatingValue != 0.0;
                                } else {
                                    if (!entry.evaluator
                                        && frame.integerScratch.size() < integerPlan->scratchSize)
                                        frame.integerScratch.resize(integerPlan->scratchSize);
                                    if (!integerPlan->evaluateSlots(entry,
                                            frame.locals,
                                            frame.integerScratch.data(),
                                            value))
                                        return false;
                                    expressionValue = ScriptScalar(value);
                                }
                                return true;
                            };
                            const auto evaluate = [&] {
                                if (entry.leafFrameValues == 0)
                                    return evaluatePayload();
                                if (callBridge_ != nullptr || maxCallArguments_ < 1
                                    || static_cast<std::size_t>(entry.leafFrameValues)
                                        > maxFrameValues_
                                    || (executionSession_ == nullptr && recursionLimit_ >= 0
                                        && frames_.size()
                                            >= static_cast<std::size_t>(recursionLimit_)))
                                    return false;
                                ScriptExecutionFrameScope leafFrame(executionSession_.get());
                                return leafFrame.active() && evaluatePayload();
                            };
                            int stepValue = 0;
                            bool repeatLoop = true;
                            const auto tail = [&] {
                                return integerPlan->prepareTail(
                                    entry, frame.locals, expressionValue, stepValue, repeatLoop);
                            };
                            // Validate the pure tail first: a mutating string commit
                            // must never precede a guard that could require replay.
                            const bool success = entry.appendLocal >= 0 ? tail() && evaluate()
                                                                        : evaluate() && tail();
                            if (success) {
                                if (remainingInstructions > 0)
                                    remainingInstructions -= cost;
                                frame.lastIp = entry.lastPc;
                                frame.ip = entry.loopCondition >= 0 && !repeatLoop
                                    ? integerPlan
                                          ->entries[static_cast<std::size_t>(entry.loopCondition)]
                                          .destination
                                    : entry.nextPc;
                                switch (entry.terminal) {
                                case ScalarExpressionPlan::Terminal::Push:
                                    frame.push(expressionValue);
                                    break;
                                case ScalarExpressionPlan::Terminal::IndexedStore:
                                case ScalarExpressionPlan::Terminal::ConstructionOperands:
                                case ScalarExpressionPlan::Terminal::ConstructionLoop:
                                    assert(false
                                        && "indexed stores use their own exact-span executor");
                                    break;
                                case ScalarExpressionPlan::Terminal::AppendString:
                                    if (entry.stepLocal >= 0)
                                        frame.locals[static_cast<std::size_t>(entry.stepLocal)] =
                                            ScriptScalar(stepValue);
                                    break;
                                case ScalarExpressionPlan::Terminal::Store:
                                case ScalarExpressionPlan::Terminal::IndexedLoop:
                                    frame.locals[static_cast<std::size_t>(entry.destination)] =
                                        expressionValue;
                                    if (entry.stepLocal >= 0)
                                        frame.locals[static_cast<std::size_t>(entry.stepLocal)] =
                                            ScriptScalar(stepValue);
                                    break;
                                case ScalarExpressionPlan::Terminal::BranchFalse:
                                    if (value == 0)
                                        frame.ip = entry.destination;
                                    break;
                                case ScalarExpressionPlan::Terminal::BranchTrue:
                                    if (value != 0)
                                        frame.ip = entry.destination;
                                    break;
                                case ScalarExpressionPlan::Terminal::Return:
                                    if (slotResult != nullptr && !frame.replaceReturnValue) {
                                        *slotResult = expressionValue;
                                    } else {
                                        result = expressionValue.materialize();
                                        if (frame.replaceReturnValue)
                                            result = frame.returnValue.boxed();
                                        if (slotResult != nullptr)
                                            *slotResult = ExecutionSlot(result);
                                    }
                                    if (produced != nullptr)
                                        *produced = true;
                                    goto normal_exit;
                                default:
                                    assert(false && "invalid scalar terminal");
                                    break;
                                }
                                continue;
                            }
                        }
                        break;
                    } while (frame.ip == entryPc);
                    if (frame.ip != entryPc)
                        continue;
                }
            }
        }
    scalar_call_replay:
        if (remainingInstructions > 0)
            --remainingInstructions;
        frame.lastIp = frame.ip;
        const Instruction& inst = code[frame.ip];
        ++frame.ip;

        switch (inst.op) {
        // ---------- literals / load ----------
        case Opcode::LoadNil:
            frame.push(ScriptScalar());
            break;
        case Opcode::LoadBool:
            frame.push(ScriptScalar(inst.a != 0));
            break;
        case Opcode::LoadInt:
            frame.push(ScriptScalar(static_cast<int>(inst.i64)));
            break;
        case Opcode::LoadInt64:
            frame.push(ScriptScalar(inst.i64));
            break;
        case Opcode::LoadFloat:
            frame.push(ScriptScalar(bitsToDouble(inst.i64)));
            break;
        case Opcode::LoadString:
            if constexpr (StaticValidated)
                frame.push(ExecutionSlot::borrowLiteral(inst.s));
            else
                frame.push(makeString(inst.s));
            break;
        case Opcode::LoadLocal:
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            frame.push(frame.locals[inst.a]);
            break;
        case Opcode::LoadCapture:
            if (inst.a < 0 || inst.a >= static_cast<int>(frame.captures.size())
                || !frame.captures[inst.a]) {
                err.code = ScriptDiagnosticCode::NilDeref;
                err.message = "invalid capture slot";
                goto error_exit;
            }
            frame.push(frame.captures[inst.a]->value);
            break;
        case Opcode::LoadGlobal:
            ensureGlobals(program);
            if (inst.a < 0 || inst.a >= static_cast<int>(globalStorage(program).size())) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "global slot out of range";
                goto error_exit;
            }
            if (isScriptPointer(globalStorage(program)[inst.a])) {
                const auto semantic = ScriptSemanticRuntime::dereference(
                    asScriptPointer(globalStorage(program)[inst.a]));
                if (!semantic.ok()) {
                    err.code = semantic.diagnostic.code;
                    err.message = semantic.diagnostic.message;
                    goto error_exit;
                }
                frame.push(semantic.value);
            } else {
                frame.push(globalStorage(program)[inst.a]);
            }
            break;

        // ---------- store ----------
        case Opcode::StoreLocal:
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            frame.locals[inst.a] = frame.popSlot<StackProven>();
            break;
        case Opcode::StoreCapture:
            if (inst.a < 0 || inst.a >= static_cast<int>(frame.captures.size())
                || !frame.captures[inst.a]) {
                err.code = ScriptDiagnosticCode::NilDeref;
                err.message = "invalid capture slot";
                goto error_exit;
            }
            frame.captures[inst.a]->value = frame.pop<StackProven>();
            break;
        case Opcode::StoreGlobal:
            ensureGlobals(program);
            if (inst.a < 0 || inst.a >= static_cast<int>(globalStorage(program).size())) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "global slot out of range";
                goto error_exit;
            }
            {
                Value value = frame.pop<StackProven>();
                if (isScriptPointer(globalStorage(program)[inst.a])) {
                    auto semantic = ScriptSemanticRuntime::storePointer(
                        *globalStorage(program)[inst.a].ptr<ScriptPointer>(), std::move(value));
                    if (!semantic.ok()) {
                        err.code = semantic.diagnostic.code;
                        err.message = semantic.diagnostic.message;
                        goto error_exit;
                    }
                } else {
                    globalStorage(program)[inst.a] = std::move(value);
                }
            }
            break;
        case Opcode::Pop:
            if (stackInvalid(frame.stack.empty())) {
                err.code = ScriptDiagnosticCode::StackUnderflow;
                err.message = "stack underflow on pop";
                goto error_exit;
            }
            frame.popSlot<StackProven>();
            break;

        // ---------- return ----------
        case Opcode::ReturnInt:
        case Opcode::Return: {
            int n = inst.op == Opcode::ReturnInt ? 1 : inst.a;
            if (n < 0)
                n = 0;
            if (n == 1) {
                if (stackInvalid(frame.stack.empty())) {
                    err.code = ScriptDiagnosticCode::StackUnderflow;
                    err.message = "stack underflow on return";
                    goto error_exit;
                }
                if (slotResult != nullptr && !frame.replaceReturnValue) {
                    *slotResult = frame.popSlot<StackProven>();
                } else {
                    result = frame.pop<StackProven>();
                    if (frame.replaceReturnValue)
                        result = frame.returnValue.boxed();
                    if (slotResult != nullptr)
                        *slotResult = ExecutionSlot(result);
                }
                if (produced != nullptr)
                    *produced = true;
                goto normal_exit;
            }
            // Preserve multi-return bytecode as one list Value.
            arp::reflection::hlist ret((arp::reflection::hlist::allocator_type(&res)));
            ret.resize(static_cast<std::size_t>(n));
            for (int i = n - 1; i >= 0; --i) {
                if (stackInvalid(frame.stack.empty())) {
                    err.code = ScriptDiagnosticCode::StackUnderflow;
                    err.message = "stack underflow on return";
                    goto error_exit;
                }
                ret[static_cast<std::size_t>(i)] = frame.pop<StackProven>();
            }
            result = Value{std::move(ret)};
            if (frame.replaceReturnValue)
                result = frame.returnValue.boxed();
            if (slotResult != nullptr)
                *slotResult = ExecutionSlot(result);
            if (produced != nullptr)
                *produced = true;
            goto normal_exit;
        }

        // The operation is fixed by the bytecode; payload type remains checked
        // because imported/captured Values can cross the dynamic ABI.
        case Opcode::IntAdd:
            if (!typedInteger(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Add>{}))
                goto error_exit;
            break;
        case Opcode::IntSub:
            if (!typedInteger(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Sub>{}))
                goto error_exit;
            break;
        case Opcode::IntMul:
            if (!typedInteger(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Mul>{}))
                goto error_exit;
            break;
        case Opcode::IntDiv:
            if (!typedInteger(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Div>{}))
                goto error_exit;
            break;
        case Opcode::IntMod:
            if (!typedInteger(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Mod>{}))
                goto error_exit;
            break;

        case Opcode::IntCmpEq:
        case Opcode::CmpEq:
            if (!comparison(std::integral_constant<SemanticCompareOperation,
                    SemanticCompareOperation::Equal>{}))
                goto error_exit;
            break;
        case Opcode::IntCmpNe:
        case Opcode::CmpNe:
            if (!comparison(std::integral_constant<SemanticCompareOperation,
                    SemanticCompareOperation::NotEqual>{}))
                goto error_exit;
            break;
        case Opcode::IntCmpLt:
        case Opcode::CmpLt:
            if (!comparison(std::integral_constant<SemanticCompareOperation,
                    SemanticCompareOperation::Less>{}))
                goto error_exit;
            break;
        case Opcode::IntCmpLe:
        case Opcode::CmpLe:
            if (!comparison(std::integral_constant<SemanticCompareOperation,
                    SemanticCompareOperation::LessEqual>{}))
                goto error_exit;
            break;
        case Opcode::IntCmpGt:
        case Opcode::CmpGt:
            if (!comparison(std::integral_constant<SemanticCompareOperation,
                    SemanticCompareOperation::Greater>{}))
                goto error_exit;
            break;
        case Opcode::IntCmpGe:
        case Opcode::CmpGe:
            if (!comparison(std::integral_constant<SemanticCompareOperation,
                    SemanticCompareOperation::GreaterEqual>{}))
                goto error_exit;
            break;

        case Opcode::Add:
            if (!arithmetic(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Add>{}))
                goto error_exit;
            break;
        case Opcode::Sub:
            if (!arithmetic(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Sub>{}))
                goto error_exit;
            break;
        case Opcode::Mul:
            if (!arithmetic(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Mul>{}))
                goto error_exit;
            break;
        case Opcode::Div:
            if (!arithmetic(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Div>{}))
                goto error_exit;
            break;
        case Opcode::Mod:
            if (!arithmetic(std::integral_constant<SemanticBinaryOperation,
                    SemanticBinaryOperation::Mod>{}))
                goto error_exit;
            break;

        case Opcode::Pow:
        case Opcode::FloorDiv:
        case Opcode::BitAnd:
        case Opcode::BitOr:
        case Opcode::BitXor:
        case Opcode::BitClear:
        case Opcode::ShiftLeft:
        case Opcode::ShiftRight: {
            SemanticBinaryOperation operation = SemanticBinaryOperation::Add;
            switch (inst.op) {
            case Opcode::Pow:
                operation = SemanticBinaryOperation::Pow;
                break;
            case Opcode::FloorDiv:
                operation = SemanticBinaryOperation::FloorDiv;
                break;
            case Opcode::BitAnd:
                operation = SemanticBinaryOperation::BitAnd;
                break;
            case Opcode::BitOr:
                operation = SemanticBinaryOperation::BitOr;
                break;
            case Opcode::BitXor:
                operation = SemanticBinaryOperation::BitXor;
                break;
            case Opcode::BitClear:
                operation = SemanticBinaryOperation::BitClear;
                break;
            case Opcode::ShiftLeft:
                operation = SemanticBinaryOperation::ShiftLeft;
                break;
            case Opcode::ShiftRight:
                operation = SemanticBinaryOperation::ShiftRight;
                break;
            default:
                break;
            }
            ScriptScalar left, right;
            ScalarOperationResult scalar;
            if (frame.peekScalarPair<StackProven>(left, right)
                && ScriptSemanticRuntime::scalarBinary(operation, left, right, scalar)) {
                if (!scalar.ok()) {
                    frame.stack.resize(frame.stack.size() - 2u);
                    err.code = scalar.code;
                    err.message = scalar.message;
                    goto error_exit;
                }
                frame.replaceScalarPair<StackProven>(scalar.value);
                break;
            }
            auto rhs = frame.popSlot<StackProven>(), lhs = frame.popSlot<StackProven>();
            if (operation >= SemanticBinaryOperation::BitAnd
                && operation <= SemanticBinaryOperation::ShiftRight
                && (!isIntegerScalar(lhs.boxed()) || !isIntegerScalar(rhs.boxed()))) {
                err.code = ScriptDiagnosticCode::BadCast;
                err.message = "bitwise operand not integer";
                goto error_exit;
            }
            const auto semantic =
                ScriptSemanticRuntime::binary(operation, lhs.boxed(), rhs.boxed());
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::Neg: {
            Value v = frame.pop<StackProven>();
            const auto semantic = ScriptSemanticRuntime::negate(std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::Not: {
            Value v = frame.pop<StackProven>();
            const auto semantic = ScriptSemanticRuntime::logicalNot(std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }

        // ---------- bitwise ----------
        // ---------- jumps ----------
        case Opcode::Jump:
            frame.ip = inst.a;
            break;
        case Opcode::JumpIfFalse: {
            bool b = false;
            if (!popBool(frame, b, err))
                goto error_exit;
            if (!b)
                frame.ip = inst.a;
            break;
        }
        case Opcode::JumpIfTrue: {
            bool b = false;
            if (!popBool(frame, b, err))
                goto error_exit;
            if (b)
                frame.ip = inst.a;
            break;
        }

        // ---------- calls ----------
        case Opcode::Call: {
            // A = func index, B = arg count.
            int argC = inst.b;
            if (argC < 0 || static_cast<std::size_t>(argC) > maxCallArguments_) {
                err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                err.message = "script call exceeds the argument limit";
                goto error_exit;
            }
            if (callBridge_ == nullptr && inst.a >= 0
                && inst.a < static_cast<int>(program.functions.size())
                && !program.functions[inst.a].isGenerator) {
                if constexpr (StaticValidated && StackProven) {
                    const auto& callee = program.functions[inst.a];
                    const auto* leaf = validatedProgram_.scalarExpressionsOwned(&callee);
                    if (leaf != nullptr && leaf->argumentLeaf
                        && executeScalarArgumentLeaf(callee,
                            *leaf,
                            frame,
                            static_cast<std::size_t>(argC),
                            false,
                            remainingInstructions))
                        break;
                }
                ExecutionSlot returnedSlot;
                bool resultProduced = false;
                Value callResult = Value::undefined();
                if constexpr (StackProven) {
                    assert(static_cast<std::size_t>(argC) <= frame.stack.size());
                    const ExecutionArgumentsView arguments(
                        frame.stack, static_cast<std::size_t>(argC));
                    callResult = runFunctionInternal(program,
                        inst.a,
                        arguments,
                        emptyCaptures(),
                        &err,
                        &resultProduced,
                        &returnedSlot,
                        true);
                } else {
                    if (!collectSlotArguments(frame, argC, err, "stack underflow on call args"))
                        goto error_exit;
                    callResult = runFunctionInternal(program,
                        inst.a,
                        ExecutionArgumentsView(callSlots_),
                        emptyCaptures(),
                        &err,
                        &resultProduced,
                        &returnedSlot);
                }
                if (err.code == ScriptDiagnosticCode::Suspended) {
                    result = std::move(callResult);
                    if (produced != nullptr)
                        *produced = resultProduced;
                    preserveSuspendedCaller(std::move(frame));
                    goto normal_exit;
                }
                if (err.code != ScriptDiagnosticCode::None)
                    goto error_exit;
                if (resultProduced)
                    frame.push(std::move(returnedSlot));
                break;
            }
            // Reuse the per-VM callArgs_ buffer to avoid per-call allocation.
            callArgs_.clear();
            callArgs_.reserve(static_cast<std::size_t>(argC));
            // args are on stack in order; pop them in reverse then reverse.
            for (int i = 0; i < argC; ++i) {
                if (stackInvalid(frame.stack.empty())) {
                    err.code = ScriptDiagnosticCode::StackUnderflow;
                    err.message = "stack underflow on call args";
                    goto error_exit;
                }
                callArgs_.push_back(frame.pop<StackProven>());
            }
            std::reverse(callArgs_.begin(), callArgs_.end());
            // Generator calls materialize a local resumable value. They must
            // not cross the dispatcher bridge merely because this VM is
            // dispatcher-owned; doing so turns normal generator creation into
            // a nested-await diagnostic.
            if (inst.a >= 0 && inst.a < static_cast<int>(program.functions.size())
                && program.functions[inst.a].isGenerator) {
                Value generator;
                if (!createGenerator(program, inst.a, callArgs_, emptyCaptures(), generator, err))
                    goto error_exit;
                frame.push(std::move(generator));
                break;
            }
            if (callBridge_ != nullptr) {
                if (inst.a < 0 || inst.a >= static_cast<int>(program.functions.size())) {
                    err.code = ScriptDiagnosticCode::InvalidOpcode;
                    err.message = "script call target is invalid";
                    goto error_exit;
                }
                const core::pmr::String target(
                    program.functions[inst.a].name, &core::defaultCpuMemoryResource());
                Value callResult = (*callBridge_)(callBridgeUserData_, target, callArgs_, &err);
                if (err.code == ScriptDiagnosticCode::Suspended) {
                    // A dispatcher may have adopted a callee continuation into
                    // this VM. Preserve the active caller at the call boundary
                    // so resume() can push the callee result and continue it.
                    result = std::move(callResult);
                    if (produced != nullptr)
                        *produced = false;
                    preserveSuspendedCaller(std::move(frame));
                    goto normal_exit;
                }
                if (err.code != ScriptDiagnosticCode::None)
                    goto error_exit;
                frame.push(std::move(callResult));
                break;
            }
            bool resultProduced = false;
            Value callResult = runFunctionInternal(
                program, inst.a, callArgs_, emptyCaptures(), &err, &resultProduced);
            if (err.code == ScriptDiagnosticCode::Suspended) {
                // Propagate the yielded value up through the caller chain.
                result = std::move(callResult);
                if (produced != nullptr)
                    *produced = resultProduced;
                preserveSuspendedCaller(std::move(frame));
                goto normal_exit;
            }
            if (err.code != ScriptDiagnosticCode::None)
                goto error_exit;
            if (resultProduced)
                frame.push(std::move(callResult));
            break;
        }
        case Opcode::CallValue: {
            // B = arg count; callee on top.
            int argC = inst.b;
            if (argC < 0 || static_cast<std::size_t>(argC) > maxCallArguments_) {
                err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                err.message = "indirect call exceeds the argument limit";
                goto error_exit;
            }
            ExecutionSlot calleeSlot = frame.popSlot<StackProven>();
            const ScriptMethod* method;
            const ScriptClosure* closure;
            calleeSlot.scriptCallable(method, closure);
            const bool localClosure = closure != nullptr && closure->aotModule == nullptr
                && (closure->vmOwner == nullptr || closure->vmOwner.get() == this);
            const int localIndex = method != nullptr ? method->funcIndex
                : localClosure                       ? closure->funcIndex
                                                     : -1;
            if (localIndex >= 0 && localIndex < static_cast<int>(program.functions.size())
                && !program.functions[localIndex].isGenerator) {
                const auto& captures = localClosure ? closure->captures : emptyCaptures();
                if constexpr (StackProven) {
                    assert(method != nullptr && !localClosure);
                    const auto& callee = program.functions[localIndex];
                    const auto* leaf = validatedProgram_.scalarExpressionsOwned(&callee);
                    if (leaf != nullptr && leaf->argumentLeaf
                        && executeScalarArgumentLeaf(callee,
                            *leaf,
                            frame,
                            static_cast<std::size_t>(argC),
                            true,
                            remainingInstructions))
                        break;
                }
                ExecutionSlot returnedSlot;
                bool resultProduced = false;
                Value callResult = Value::undefined();
                if constexpr (StackProven) {
                    // The CallValue proof includes its exact MakeMethodValue
                    // predecessor and every actual override's arity/result CFG.
                    // Consume the original suffix with a borrowed receiver;
                    // callee frame/session/recursion/quota admission is shared.
                    assert(method != nullptr && !localClosure);
                    const ExecutionArgumentsView arguments(
                        frame.stack, static_cast<std::size_t>(argC), method->receiver);
                    callResult = runFunctionInternal(program,
                        localIndex,
                        arguments,
                        emptyCaptures(),
                        &err,
                        &resultProduced,
                        &returnedSlot,
                        true);
                } else {
                    if (!collectSlotArguments(
                            frame, argC, err, "stack underflow on callvalue args"))
                        goto error_exit;
                    if (method != nullptr)
                        callSlots_.insert(callSlots_.begin(), ExecutionSlot(method->receiver));
                    if (localClosure) {
                        const auto& callee = program.functions[localIndex];
                        const auto* plan = validatedProgram_.scalarExpressions(&callee);
                        if (plan != nullptr && plan->captureLeaf.valid()
                            && executeScalarCaptureLeaf(callee,
                                plan->captureLeaf,
                                *closure,
                                frame,
                                static_cast<std::size_t>(argC),
                                remainingInstructions))
                            break;
                    }
                    callResult = runFunctionInternal(program,
                        localIndex,
                        ExecutionArgumentsView(callSlots_),
                        captures,
                        &err,
                        &resultProduced,
                        &returnedSlot);
                }
                if (err.code == ScriptDiagnosticCode::Suspended) {
                    result = std::move(callResult);
                    if (produced != nullptr)
                        *produced = resultProduced;
                    preserveSuspendedCaller(std::move(frame));
                    goto normal_exit;
                }
                if (err.code != ScriptDiagnosticCode::None)
                    goto error_exit;
                if (resultProduced)
                    frame.push(std::move(returnedSlot));
                break;
            }
            // Foreign closures, generators and host calls retain the Value ABI.
            Value callee = calleeSlot.takeValue();
            arp::core::pmr::Vector<Value> args(
                (arp::core::pmr::Vector<Value>::allocator_type(&res)));
            args.reserve(argC);
            for (int i = 0; i < argC; ++i) {
                if (stackInvalid(frame.stack.empty())) {
                    err.code = ScriptDiagnosticCode::StackUnderflow;
                    err.message = "stack underflow on callvalue args";
                    goto error_exit;
                }
                args.push_back(frame.pop<StackProven>());
            }
            std::reverse(args.begin(), args.end());
            if (isScriptClosure(callee)) {
                const auto& cl = asScriptClosure(callee);
                const bool foreignAot = cl.aotModule != nullptr;
                const bool foreignVm = cl.vmOwner != nullptr && cl.vmOwner.get() != this;
                if (foreignVm && cl.programOwner != nullptr && programOwner_ != nullptr
                    && cl.programOwner != programOwner_) {
                    err.code = ScriptDiagnosticCode::HybridModuleVersionMismatch;
                    err.message = "closure belongs to a replaced interpreter module";
                    goto error_exit;
                }
                if ((foreignAot || foreignVm) && cl.bridge != nullptr && !cl.functionId.empty()) {
                    Value bridged = (*cl.bridge)(cl.bridgeUserData, cl.functionId, args, &err);
                    if (err.code != ScriptDiagnosticCode::None)
                        goto error_exit;
                    frame.push(std::move(bridged));
                    break;
                }
                if (foreignAot || foreignVm) {
                    err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                    err.message = "closure backend owner is unavailable";
                    goto error_exit;
                }
                if (cl.funcIndex >= 0 && cl.funcIndex < static_cast<int>(program.functions.size())
                    && program.functions[cl.funcIndex].isGenerator) {
                    Value generator;
                    if (!createGenerator(program, cl.funcIndex, args, cl.captures, generator, err))
                        goto error_exit;
                    frame.push(std::move(generator));
                    break;
                }
                bool resultProduced = false;
                Value callResult = runFunctionInternal(
                    program, cl.funcIndex, args, cl.captures, &err, &resultProduced);
                if (err.code == ScriptDiagnosticCode::Suspended) {
                    // Propagate the yielded value up through the caller chain.
                    result = std::move(callResult);
                    if (produced != nullptr)
                        *produced = resultProduced;
                    preserveSuspendedCaller(std::move(frame));
                    goto normal_exit;
                }
                if (err.code != ScriptDiagnosticCode::None)
                    goto error_exit;
                if (resultProduced)
                    frame.push(std::move(callResult));
            } else if (isHostCallable(callee)) {
                // Host function exported from a module.
                const auto& hc = *callee.ptr<HostCallable>();
                if (!hc.contract.acceptsArity(args.size())) {
                    err.code = ScriptDiagnosticCode::WrongArgCount;
                    err.message = "host callable argument count does not match its contract";
                    goto error_exit;
                }
                try {
                    frame.push(hc.fn(args));
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const ScriptHostException& exception) {
                    err.code = ScriptDiagnosticCode::UserRaise;
                    err.message = exception.what();
                    goto error_exit;
                } catch (const std::exception&) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                } catch (...) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                }
            } else if (isReflectedClassBinding(callee)) {
                const auto* classInfo = asReflectedClassBinding(callee);
                if (classInfo == nullptr) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "reflected class binding is invalid";
                    goto error_exit;
                }
                Value newFunction = classInfo->get_field("__new__");
                if (!newFunction.is_function()) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "bound reflected class has no __new__ constructor: ";
                    err.message += classInfo->name();
                    goto error_exit;
                }
                std::optional<Value> nativeInstance;
                try {
                    nativeInstance.emplace(newFunction.cast<arp::reflection::function&>().call_v(
                        Value::undefined(), args));
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const std::exception&) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                } catch (...) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                }
                if (!nativeInstance.has_value() || nativeInstance->is_undefined()) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "bound reflected class constructor failed: ";
                    err.message += classInfo->name();
                    goto error_exit;
                }
                frame.push(std::move(*nativeInstance));
            } else if (callee.is<arp::reflection::function>()) {
                auto& function = callee.cast<arp::reflection::function&>();
                Value reflected = Value::undefined();
                try {
                    if (function.is_static()) {
                        reflected = function.call_v(Value::undefined(), args);
                    } else {
                        if (args.empty()) {
                            err.code = ScriptDiagnosticCode::WrongArgCount;
                            err.message = "reflected member function expects an instance";
                            goto error_exit;
                        }
                        Value receiver = args.front();
                        arp::core::pmr::Vector<Value> memberArgs(
                            (arp::core::pmr::Vector<Value>::allocator_type(&res)));
                        memberArgs.reserve(args.size() - 1u);
                        for (std::size_t index = 1; index < args.size(); ++index) {
                            memberArgs.push_back(args[index]);
                        }
                        reflected = function.call_v(receiver, memberArgs);
                    }
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const std::exception&) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                } catch (...) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                }
                frame.push(std::move(reflected));
            } else if (callee.is<ReflectedMethod>()) {
                const auto& method = callee.cast<ReflectedMethod&>();
                try {
                    if (method.function.is_static()) {
                        if (method.function.parameter_count() == args.size() + 1u) {
                            args.insert(args.begin(), method.receiver);
                        }
                    }
                    auto reflected = method.function.call_v(method.receiver, args);
                    frame.push(std::move(reflected));
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const std::exception&) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                } catch (...) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                }
            } else if (callee.is<arp::reflection::property>()) {
                if (args.size() < 1u || args.size() > 2u) {
                    err.code = ScriptDiagnosticCode::WrongArgCount;
                    err.message = "reflected property expects instance and optional value";
                    goto error_exit;
                }
                auto& property = callee.cast<arp::reflection::property&>();
                try {
                    Value reflected = args.size() == 1u ? property.get_value(args.front())
                                                        : property.set_value(args.front(), args[1]);
                    frame.push(std::move(reflected));
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const std::exception&) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                } catch (...) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                }
            } else if (isScriptMethod(callee)) {
                const auto& method = *callee.ptr<ScriptMethod>();
                args.insert(args.begin(), method.receiver);
                if (method.funcIndex >= 0
                    && method.funcIndex < static_cast<int>(program.functions.size())
                    && program.functions[method.funcIndex].isGenerator) {
                    Value generator;
                    if (!createGenerator(
                            program, method.funcIndex, args, emptyCaptures(), generator, err))
                        goto error_exit;
                    frame.push(std::move(generator));
                    break;
                }
                bool resultProduced = false;
                Value callResult = runFunctionInternal(
                    program, method.funcIndex, args, emptyCaptures(), &err, &resultProduced);
                if (err.code == ScriptDiagnosticCode::Suspended) {
                    // Propagate the yielded value up through the caller chain.
                    result = std::move(callResult);
                    if (produced != nullptr)
                        *produced = resultProduced;
                    preserveSuspendedCaller(std::move(frame));
                    goto normal_exit;
                }
                if (err.code != ScriptDiagnosticCode::None)
                    goto error_exit;
                if (resultProduced)
                    frame.push(std::move(callResult));
            } else {
                err.code = ScriptDiagnosticCode::NotCallable;
                err.message = "value is not callable";
                goto error_exit;
            }
            break;
        }
        case Opcode::CallHost: {
            // Versioned bytecode resolves host calls exclusively by stable
            // binding ID. Numeric indices remain available only to version-0
            // VM white-box and malformed-bytecode fixtures.
            if (program.bytecodeVersion != 0u && inst.s.empty()) {
                err.code = ScriptDiagnosticCode::TypedIrInvalid;
                err.message = "versioned host call has no stable binding ID";
                goto error_exit;
            }
            const int hostIdx = !inst.s.empty()
                ? (hostRegistry_ == nullptr ? -1 : hostRegistry_->lookupBindingIndex(inst.s))
                : inst.a;
            int argC = inst.b;
            if (argC < 0 || static_cast<std::size_t>(argC) > maxCallArguments_) {
                err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                err.message = "host call exceeds the argument limit";
                goto error_exit;
            }
            if (!hostRegistry_) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = "no host registry set";
                goto error_exit;
            }
            const HostFn* fn = hostRegistry_->lookup(hostIdx);
            if (!fn) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = "host function not found";
                goto error_exit;
            }
            const auto* hostContract = hostRegistry_->lookupContract(hostIdx);
            if (hostContract != nullptr
                && !hostContract->acceptsArity(static_cast<std::size_t>(argC))) {
                err.code = ScriptDiagnosticCode::WrongArgCount;
                err.message = "host function argument count does not match its contract";
                goto error_exit;
            }
            arp::core::pmr::Vector<Value> hostArgs(
                (arp::core::pmr::Vector<Value>::allocator_type(&res)));
            hostArgs.reserve(argC);
            for (int i = 0; i < argC; ++i) {
                if (stackInvalid(frame.stack.empty())) {
                    err.code = ScriptDiagnosticCode::StackUnderflow;
                    err.message = "stack underflow on host call args";
                    goto error_exit;
                }
                hostArgs.push_back(frame.pop<StackProven>());
            }
            std::reverse(hostArgs.begin(), hostArgs.end());
            try {
                frame.push((*fn)(hostArgs));
            } catch (const std::bad_alloc&) {
                throw;
            } catch (const std::exception&) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = "host function threw";
                goto error_exit;
            } catch (...) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = "host function threw";
                goto error_exit;
            }
            break;
        }

        // ---------- function values ----------
        case Opcode::MakeFuncValue: {
            ScriptClosure closure{inst.a, {}};
            closure.retentionLease = makeScriptModuleRetentionLease(retentionBudget_);
            if (inst.a >= 0 && inst.a < static_cast<int>(program.functions.size())) {
                closure.functionId = program.functions[inst.a].name;
                closure.programOwner = programOwner_;
                closure.vmOwner = owner_.lock();
                closure.bridge = callBridge_;
                closure.bridgeUserData = callBridgeUserData_;
            }
            frame.push(Value{std::move(closure)});
            break;
        }
        case Opcode::MakeClosure: {
            int capC = inst.b;
            if (capC < 0 || static_cast<std::size_t>(capC) > maxFrameValues_) {
                err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                err.message = "closure capture count exceeds the frame limit";
                goto error_exit;
            }
            arp::core::pmr::Vector<std::shared_ptr<ScriptCell>> caps{&res};
            caps.reserve(capC);
            for (int i = 0; i < capC; ++i) {
                std::shared_ptr<ScriptCell> cell;
                if (!popPointer(frame, cell, err))
                    goto error_exit;
                caps.push_back(std::move(cell));
            }
            std::reverse(caps.begin(), caps.end());
            ScriptClosure closure{inst.a, std::move(caps)};
            closure.retentionLease = makeScriptModuleRetentionLease(retentionBudget_);
            if (inst.a >= 0 && inst.a < static_cast<int>(program.functions.size())) {
                closure.functionId = program.functions[inst.a].name;
                closure.programOwner = programOwner_;
                closure.vmOwner = owner_.lock();
                closure.bridge = callBridge_;
                closure.bridgeUserData = callBridgeUserData_;
            }
            frame.push(Value{std::move(closure)});
            break;
        }
        case Opcode::MakeMethodValue: {
            Value receiver = frame.pop<StackProven>();
            const int methodIndex = resolveScriptMethod(program, inst, receiver);
            frame.push(ExecutionSlot(ScriptMethod{methodIndex, std::move(receiver)}));
            break;
        }

        // ---------- container construction ----------
        case Opcode::MakeList: {
            if constexpr (StaticValidated && StackProven) {
                const int local = integerPlan
                    ? integerPlan->entries[static_cast<std::size_t>(frame.lastIp)].listDestination
                    : -1;
                if (local >= 0 && remainingInstructions != 0
                    && ScriptSemanticRuntime::reuseUniqueList(
                        frame.locals[static_cast<std::size_t>(local)],
                        frame.stack,
                        inst.b,
                        maxContainerValues_)) {
                    if (remainingInstructions > 0)
                        --remainingInstructions;
                    frame.lastIp = frame.ip++;
                    break;
                }
            }
            const auto semantic =
                ScriptSemanticRuntime::makeList(frame.stack, inst.b, maxContainerValues_);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::MakeDict: {
            if constexpr (StaticValidated && StackProven) {
                const int local = integerPlan
                    ? integerPlan->entries[static_cast<std::size_t>(frame.lastIp)]
                          .dictionaryDestination
                    : -1;
                if (local >= 0 && remainingInstructions != 0
                    && ScriptSemanticRuntime::reuseUniqueDictionary(
                        frame.locals[static_cast<std::size_t>(local)],
                        frame.stack,
                        inst.b,
                        maxContainerValues_,
                        frame.dictionaryScratch)) {
                    if (remainingInstructions > 0)
                        --remainingInstructions;
                    frame.lastIp = frame.ip++;
                    break;
                }
            }
            const auto semantic =
                ScriptSemanticRuntime::makeDict(frame.stack, inst.b, maxContainerValues_);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::MakeInstance: {
            // S = class name, B = explicit constructor argument count.
            if (inst.b < 0 || static_cast<std::size_t>(inst.b) > maxCallArguments_) {
                err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                err.message = "constructor exceeds the argument limit";
                goto error_exit;
            }
            // Script classes use the VM instance path. A reflected C++ class
            // must be created through its registered __new__/__init__ path so
            // the returned Value contains the real native object.
            auto schemaIt = program.classSchemas.find(inst.s);
            arp::reflection::class_info* cls = arp::reflection::db::instance().find_class(inst.s);
            if (schemaIt == program.classSchemas.end() && cls == nullptr) {
                err.code = ScriptDiagnosticCode::UndeclaredName;
                err.message = "unknown class '";
                err.message += inst.s;
                err.message += "'";
                goto error_exit;
            }

            arp::core::pmr::Vector<Value> initArgs(
                (arp::core::pmr::Vector<Value>::allocator_type(&res)));
            initArgs.reserve(inst.b);
            for (int i = 0; i < inst.b; ++i) {
                if (stackInvalid(frame.stack.empty())) {
                    err.code = ScriptDiagnosticCode::StackUnderflow;
                    err.message = "stack underflow on class constructor args";
                    goto error_exit;
                }
                initArgs.push_back(frame.pop<StackProven>());
            }
            std::reverse(initArgs.begin(), initArgs.end());

            if (schemaIt == program.classSchemas.end()) {
                Value newFunction = cls->get_field("__new__");
                if (!newFunction.is_function()) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "reflected class has no __new__ constructor: ";
                    err.message += inst.s;
                    goto error_exit;
                }
                std::optional<Value> nativeInstance;
                try {
                    nativeInstance.emplace(newFunction.cast<arp::reflection::function&>().call_v(
                        Value::undefined(), initArgs));
                } catch (const std::bad_alloc&) {
                    throw;
                } catch (const std::exception&) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                } catch (...) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "host callable threw";
                    goto error_exit;
                }
                if (!nativeInstance.has_value() || nativeInstance->is_undefined()) {
                    err.code = ScriptDiagnosticCode::HostError;
                    err.message = "reflected class constructor failed: ";
                    err.message += inst.s;
                    goto error_exit;
                }
                frame.push(std::move(*nativeInstance));
                break;
            }

            const auto& fieldNames = schemaIt->second;
            const auto scriptInstance =
                ScriptSemanticRuntime::makeScriptInstance(inst.s, fieldNames);
            if (!scriptInstance.ok()) {
                err.code = scriptInstance.diagnostic.code;
                err.message = scriptInstance.diagnostic.message;
                goto error_exit;
            }
            Value instanceValue = scriptInstance.value;
            auto constructorIt = program.constructorIndex.find(inst.s);
            if (constructorIt != program.constructorIndex.end()) {
                initArgs.insert(initArgs.begin(), instanceValue);
                bool constructorProduced = false;
                runFunctionInternal(program,
                    constructorIt->second,
                    initArgs,
                    emptyCaptures(),
                    &err,
                    &constructorProduced);
                if (err.code == ScriptDiagnosticCode::Suspended) {
                    if (suspendedFrame_.function == nullptr) {
                        err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                        err.message = "script constructor suspension has no active frame";
                        goto error_exit;
                    }
                    suspendedFrame_.replaceReturnValue = true;
                    suspendedFrame_.returnValue = std::move(instanceValue);
                    result = makeNil();
                    if (produced != nullptr)
                        *produced = false;
                    preserveSuspendedCaller(std::move(frame));
                    goto normal_exit;
                }
                if (err.code != ScriptDiagnosticCode::None)
                    goto error_exit;
            }
            frame.push(std::move(instanceValue));
            break;
        }
        case Opcode::MakeRange: {
            // Pushes a simple range as a two-element list [start, end).
            // Actual iteration uses RangeInit/RangeNext with local slots.
            // I64 high 32 = start, low 32 = end (exclusive).
            int start = static_cast<int>((inst.i64 >> 32) & 0xFFFFFFFF);
            int end = static_cast<int>(inst.i64 & 0xFFFFFFFF);
            const auto semantic = ScriptSemanticRuntime::makeRange(start, end, maxContainerValues_);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::MakeRangeFromStack: {
            const auto semantic = frame.stack.applyValueOperation(
                inst.b == 1 || inst.b == 2 ? static_cast<std::size_t>(inst.b) : 0u,
                [&](auto& operands) {
                    return ScriptSemanticRuntime::makeRangeFromStack(
                        operands, inst.b, maxContainerValues_);
                });
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }

        // ---------- container access ----------
        case Opcode::LoadIndex: {
            auto idx = frame.popSlot<StackProven>();
            Value base = frame.pop<StackProven>();
            auto semantic = ScriptSemanticRuntime::loadIndexSlots(base, idx);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(std::move(semantic.value));
            break;
        }
        case Opcode::StoreIndex: {
            Value val = frame.pop<StackProven>();
            Value idx = frame.pop<StackProven>();
            Value base = frame.pop<StackProven>();
            if (base.is<arp::reflection::hdict>()) {
                auto* dict = base.ptr<arp::reflection::hdict>();
                if (dict->find(idx) == dict->end() && dict->size() >= maxContainerValues_) {
                    err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
                    err.message = "dict store exceeds the container limit";
                    goto error_exit;
                }
            }
            const auto semantic =
                ScriptSemanticRuntime::storeIndex(std::move(base), std::move(idx), std::move(val));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            break;
        }
        case Opcode::LoadField: {
            // A = field index (unused for instance-backed; kept for compat),
            // S = field name. Route through reflection instance __getitem__.
            Value base = frame.pop<StackProven>();
            const auto& fname = inst.s;
            auto semantic = ScriptSemanticRuntime::findField(base, fname);
            Value result = Value::undefined();
            if (semantic.ok() && !semantic.value.is_undefined()) {
                result = std::move(semantic.value);
            } else if (const auto* actual = checkedValuePayload<arp::reflection::instance>(base);
                actual != nullptr && actual->class_() != nullptr) {
                // Script methods are held in Program metadata rather than the
                // reflection class. Keep this lookup in the Interpreter while
                // field storage itself remains shared with AOT.
                const auto methods = program.methodSets.find(actual->class_()->name());
                if (methods != program.methodSets.end()) {
                    const auto method = methods->second.find(fname);
                    if (method != methods->second.end()) {
                        frame.push(ExecutionSlot(ScriptMethod{method->second, std::move(base)}));
                        break;
                    }
                }
            }
            if (result.is_undefined()) {
                err.code =
                    semantic.ok() ? ScriptDiagnosticCode::NoMember : semantic.diagnostic.code;
                if (semantic.ok()) {
                    err.message = "field lookup failed: ";
                    err.message += fname;
                } else
                    err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            if (result.is<arp::reflection::function>() && base.class_() != nullptr
                && !base.is<arp::reflection::instance>() && !isReflectedTypeBinding(base)
                && !base.is<arp::reflection::enumeration>() && !isModuleObject(base))
                result = Value{ReflectedMethod{result.cast<arp::reflection::function&>(), base}};
            frame.push(std::move(result));
            break;
        }
        case Opcode::StoreField: {
            // A = field index (unused), S = field name.
            Value val = frame.pop<StackProven>();
            Value base = frame.pop<StackProven>();
            const auto& fname = inst.s;
            const auto semantic =
                ScriptSemanticRuntime::storeField(std::move(base), fname, std::move(val));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            break;
        }
        case Opcode::AddrLocal: {
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            Value& slot = frame.locals[inst.a];
            if (isScriptPointer(slot)) {
                // Already captured: reuse the existing cell so all closures
                // capturing this slot share one upvalue.
                frame.push(slot);
            } else {
                // Promote: move the value into a shared cell, replace the
                // slot with a pointer to it. Future reads of the slot must
                // deref; the compiler emits Deref transparently for captured
                // locals, or the slot is only accessed via the cell.
                auto cell = arp::core::makeShared<ScriptCell>(slot);
                slot = Value{ScriptPointer{cell}};
                frame.push(slot);
            }
            break;
        }
        case Opcode::AddrCapture: {
            if (inst.a < 0 || inst.a >= static_cast<int>(frame.captures.size())
                || !frame.captures[inst.a]) {
                err.code = ScriptDiagnosticCode::NilDeref;
                err.message = "invalid capture slot";
                goto error_exit;
            }
            frame.push(Value{ScriptPointer{frame.captures[inst.a]}});
            break;
        }
        case Opcode::AddrGlobal:
            ensureGlobals(program);
            if (inst.a < 0 || inst.a >= static_cast<int>(globalStorage(program).size())) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "global slot out of range";
                goto error_exit;
            }
            {
                Value& slot = globalStorage(program)[inst.a];
                if (!isScriptPointer(slot)) {
                    auto cell = arp::core::makeShared<ScriptCell>(slot);
                    slot = Value{ScriptPointer{cell}};
                }
                frame.push(slot);
            }
            break;
        case Opcode::AddrIndex: {
            if (stackInvalid(frame.stack.size() < 2u)) {
                err.code = ScriptDiagnosticCode::StackUnderflow;
                err.message = "stack underflow on AddrIndex";
                goto error_exit;
            }
            Value index = frame.pop<StackProven>();
            Value base = frame.pop<StackProven>();
            const auto valid = ScriptSemanticRuntime::loadIndex(base, index);
            if (!valid.ok()) {
                err.code = valid.diagnostic.code;
                err.message = valid.diagnostic.message;
                goto error_exit;
            }
            frame.push(Value{ScriptPointer{std::move(base), std::move(index)}});
            break;
        }
        case Opcode::AddrField: {
            if (stackInvalid(frame.stack.empty())) {
                err.code = ScriptDiagnosticCode::StackUnderflow;
                err.message = "stack underflow on AddrField";
                goto error_exit;
            }
            Value base = frame.pop<StackProven>();
            const auto valid = ScriptSemanticRuntime::loadField(base, inst.s);
            if (!valid.ok() && !base.is<arp::reflection::instance>()) {
                err.code = valid.diagnostic.code;
                err.message = valid.diagnostic.message;
                goto error_exit;
            }
            frame.push(Value{ScriptPointer{std::move(base), inst.s}});
            break;
        }
        case Opcode::Deref: {
            ScriptPointer pointer;
            if (!popScriptPointer(frame, pointer, err))
                goto error_exit;
            const auto semantic = ScriptSemanticRuntime::dereference(pointer);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::StoreIndirect: {
            if (stackInvalid(frame.stack.size() < 2u)) {
                err.code = ScriptDiagnosticCode::StackUnderflow;
                err.message = "stack underflow on StoreIndirect";
                goto error_exit;
            }
            // The compiler leaves the address below the value: [pointer,
            // value]. Pop in the same order as the AOT Value frame machine.
            Value val = frame.pop<StackProven>();
            Value pointerValue = frame.pop<StackProven>();
            if (!pointerValue.is<ScriptPointer>()) {
                err.code = ScriptDiagnosticCode::NilDeref;
                err.message = "StoreIndirect requires a script pointer";
                goto error_exit;
            }
            ScriptPointer pointer;
            const auto* pointerSource = pointerValue.ptr<ScriptPointer>();
            pointer = *pointerSource;
            const auto semantic = ScriptSemanticRuntime::storePointer(pointer, std::move(val));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            break;
        }

        // ---------- builtins ----------
        case Opcode::BuiltinLen: {
            auto value = frame.popSlot<StackProven>();
            const auto semantic = ScriptSemanticRuntime::lengthScalar(value);
            if (!semantic.ok()) {
                err.code = semantic.code;
                err.message = semantic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::BuiltinAppend: {
            // pop value, pop list, append, push list back.
            Value val = frame.pop<StackProven>();
            Value lst = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::append(std::move(lst), std::move(val), maxContainerValues_);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::BuiltinDelete: {
            Value key = frame.pop<StackProven>();
            Value cont = frame.pop<StackProven>();
            const auto semantic = ScriptSemanticRuntime::erase(std::move(cont), std::move(key));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            break;
        }
        case Opcode::BuiltinClear: {
            Value cont = frame.pop<StackProven>();
            const auto semantic = ScriptSemanticRuntime::clear(std::move(cont));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            break;
        }
        case Opcode::RangeInit: {
            // A = source slot, B = index slot.
            // Stores the iterable into source slot, sets index = -1.
            if (staticInvalid(inst.b < 0 || inst.b >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "range index slot out of range";
                goto error_exit;
            }
            Value src = frame.pop<StackProven>();
            if (inst.a >= 0 && inst.a < static_cast<int>(frame.locals.size()))
                frame.locals[inst.a] = std::move(src);
            frame.locals[inst.b] = ScriptScalar(-1);
            break;
        }
        case Opcode::RangeNext: {
        range_iteration:
            // A = source slot, B = index slot, I64 = packed(keySlot, valueSlot).
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size())
                    || inst.b < 0 || inst.b >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "range slot out of range";
                goto error_exit;
            }
            const Value& src = frame.locals[inst.a];
            int idx = 0;
            if (const auto* index = frame.locals[inst.b].scalarPointer<int>())
                idx = *index;
            else if (!tryGetInt(frame.locals[inst.b].boxed(), idx)) {
                err.code = ScriptDiagnosticCode::BadCast;
                err.message = "range index corrupted";
                goto error_exit;
            }
            if (idx == std::numeric_limits<int>::max()) {
                err.code = ScriptDiagnosticCode::IntegerOverflow;
                err.message = "range index overflow";
                goto error_exit;
            }
            ++idx;
            // Keep the boxed source reference valid for overlapping raw slots.
            if (inst.a == inst.b)
                frame.locals[inst.b] = makeInt(idx);
            else
                frame.locals[inst.b] = ScriptScalar(idx);
            int keySlot = 0, valSlot = 0;
            unpackRangeTargets(inst.i64, keySlot, valSlot);
            bool hasMore = false;
            if (activeGenerator_ != nullptr && frame.function != nullptr
                && frame.function->hasYieldFrom) {
                activeGenerator_->delegatedSourceSlot = inst.a;
                activeGenerator_->delegatedActive = true;
            }
            if (isScriptGenerator(src)) {
                auto* generator = asScriptGenerator(src);
                VmError generatorError;
                Value yielded = resumeGenerator(*generator, generatorError);
                if (generatorError.code != ScriptDiagnosticCode::None) {
                    const bool childAwaitPending =
                        generatorError.code == ScriptDiagnosticCode::Suspended
                        && (generator->suspendedFrame != nullptr
                            || generator->delegatedChildPending);
                    if (childAwaitPending && activeGenerator_ != nullptr
                        && frame.function != nullptr && frame.function->hasYieldFrom) {
                        // Retry this exact RangeNext after the child await is
                        // polled again. Roll back the index increment so a
                        // backend migration cannot duplicate or skip a value.
                        const int pendingYieldPc =
                            delegationYieldPcForRange(*frame.function, frame.lastIp, valSlot);
                        if (pendingYieldPc < 0) {
                            err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                            err.message = "delegated child-await yield continuation is invalid";
                            goto error_exit;
                        }
                        frame.locals[inst.b] = ScriptScalar(idx - 1);
                        frame.ip = frame.lastIp;
                        activeGenerator_->delegatedPendingRangePc = frame.lastIp;
                        activeGenerator_->delegatedPendingYieldPc = pendingYieldPc;
                        activeGenerator_->delegatedChildPending = true;
                        if (delegatedAwaitDepth(*activeGenerator_)
                            > depthBudget(maxContinuationFrames_)) {
                            activeGenerator_->delegatedChildPending = false;
                            err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                            err.message =
                                "delegated child-await depth exceeds the bounded contract";
                            goto error_exit;
                        }
                    }
                    err = std::move(generatorError);
                    goto error_exit;
                }
                if (activeGenerator_ != nullptr)
                    activeGenerator_->delegatedChildPending = false;
                if (activeGenerator_ != nullptr)
                    activeGenerator_->delegatedPendingYieldPc = -1;
                if (!generator->done) {
                    if (keySlot >= 0 && keySlot < static_cast<int>(frame.locals.size()))
                        frame.locals[keySlot] = ScriptScalar(idx);
                    if (valSlot >= 0 && valSlot < static_cast<int>(frame.locals.size()))
                        frame.locals[valSlot] = std::move(yielded);
                    hasMore = true;
                }
            } else if (const auto* bridge = generatorBridgeV2();
                bridge != nullptr && bridge->isGenerator != nullptr && bridge->isGenerator(src)) {
                VmError generatorError;
                if (bridge->resume == nullptr) {
                    err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                    err.message = "generator bridge provider is unavailable";
                    goto error_exit;
                }
                Value generatorValue = src;
                Value yielded = bridge->resume(generatorValue, &generatorError);
                if (generatorError.code != ScriptDiagnosticCode::None) {
                    const bool childAwaitPending =
                        generatorError.code == ScriptDiagnosticCode::Suspended
                        && ((bridge->isSuspended != nullptr && bridge->isSuspended(generatorValue))
                            || (bridge->delegatedAwaitDepth != nullptr
                                && bridge->delegatedAwaitDepth(generatorValue, 1) > 0));
                    if (childAwaitPending && activeGenerator_ != nullptr
                        && frame.function != nullptr && frame.function->hasYieldFrom) {
                        const int pendingYieldPc =
                            delegationYieldPcForRange(*frame.function, frame.lastIp, valSlot);
                        if (pendingYieldPc < 0) {
                            err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                            err.message = "delegated child-await yield continuation is invalid";
                            goto error_exit;
                        }
                        frame.locals[inst.b] = ScriptScalar(idx - 1);
                        frame.ip = frame.lastIp;
                        activeGenerator_->delegatedPendingRangePc = frame.lastIp;
                        activeGenerator_->delegatedPendingYieldPc = pendingYieldPc;
                        activeGenerator_->delegatedChildPending = true;
                        if (delegatedAwaitDepth(*activeGenerator_)
                            > depthBudget(maxContinuationFrames_)) {
                            activeGenerator_->delegatedChildPending = false;
                            err.code = ScriptDiagnosticCode::AotSwitchUnsupported;
                            err.message =
                                "delegated child-await depth exceeds the bounded contract";
                            goto error_exit;
                        }
                    }
                    err = std::move(generatorError);
                    goto error_exit;
                }
                if (activeGenerator_ != nullptr)
                    activeGenerator_->delegatedChildPending = false;
                if (activeGenerator_ != nullptr)
                    activeGenerator_->delegatedPendingYieldPc = -1;
                const bool generatorDone =
                    bridge->isDone != nullptr && bridge->isDone(generatorValue);
                if (!generatorDone) {
                    if (keySlot >= 0 && keySlot < static_cast<int>(frame.locals.size()))
                        frame.locals[keySlot] = ScriptScalar(idx);
                    if (valSlot >= 0 && valSlot < static_cast<int>(frame.locals.size()))
                        frame.locals[valSlot] = std::move(yielded);
                    hasMore = true;
                }
            } else {
                auto next = ScriptSemanticRuntime::rangeNextCollectionStep(src, idx);
                if (!next.ok()) {
                    err.code = next.code;
                    err.message = "range over non-iterable";
                    goto error_exit;
                }
                if (next.hasMore) {
                    if (keySlot >= 0 && keySlot < static_cast<int>(frame.locals.size()))
                        frame.locals[keySlot] = std::move(next.key);
                    if (valSlot >= 0 && valSlot < static_cast<int>(frame.locals.size()))
                        frame.locals[valSlot] = std::move(next.value);
                    hasMore = true;
                }
            }
            if (!hasMore && activeGenerator_ != nullptr && frame.function != nullptr
                && frame.function->hasYieldFrom)
                activeGenerator_->delegatedActive = false;
            if constexpr (StaticValidated && StackProven) {
                const int rangePc = frame.lastIp;
                const int bodyPc = integerPlan == nullptr
                    ? -1
                    : integerPlan->entries[static_cast<std::size_t>(rangePc)].rangeBody;
                if (bodyPc >= 0 && remainingInstructions != 0) {
                    // Consume the original JumpIfFalse only after RangeNext
                    // committed its canonical index/key/value effects. A short
                    // budget or failed pure expression resumes at its own PC.
                    if (remainingInstructions > 0)
                        --remainingInstructions;
                    frame.lastIp = rangePc + 1;
                    frame.ip = hasMore ? bodyPc : code[rangePc + 1].a;
                    if (hasMore) {
                        const auto& body = integerPlan->entries[static_cast<std::size_t>(bodyPc)];
                        if (remainingInstructions < 0 || remainingInstructions >= body.cost) {
                            if (!body.evaluator
                                && frame.integerScratch.size() < integerPlan->scratchSize)
                                frame.integerScratch.resize(integerPlan->scratchSize);
                            int value;
                            if (integerPlan->evaluateSlots(
                                    body, frame.locals, frame.integerScratch.data(), value)) {
                                frame.locals[static_cast<std::size_t>(body.destination)] =
                                    ScriptScalar(value);
                                if (remainingInstructions > 0)
                                    remainingInstructions -= body.cost;
                                frame.lastIp = body.lastPc;
                                frame.ip = body.nextPc;
                                if (remainingInstructions != 0) {
                                    // Re-enter this same immutable RangeNext,
                                    // including every provider/type/error check.
                                    if (remainingInstructions > 0)
                                        --remainingInstructions;
                                    frame.lastIp = frame.ip++;
                                    goto range_iteration;
                                }
                            }
                        }
                    }
                    continue;
                }
            }
            frame.push(ScriptScalar(hasMore));
            break;
        }

        // ---------- conversions ----------
        case Opcode::ConvertInt: {
            Value v = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::Int, std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::ConvertInt64: {
            Value v = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::Int64, std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::ConvertFloat: {
            Value v = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::Float, std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::ConvertString: {
            Value v = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::String, std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::ConvertBool: {
            Value v = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::Bool, std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::ConvertBytes: {
            Value v = frame.pop<StackProven>();
            const auto semantic =
                ScriptSemanticRuntime::convert(SemanticConversionOperation::Bytes, std::move(v));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }

        // ---------- fast-path (Phase 7) ----------
        case Opcode::LocalAddFloatConst:
        case Opcode::LocalSubFloatConst: {
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            double constant = 0.0;
            std::int64_t bits = inst.i64;
            std::memcpy(&constant, &bits, sizeof(constant));
            const bool add = inst.op == Opcode::LocalAddFloatConst;
            auto& local = frame.locals[inst.a];
            if (const auto* current = local.scalarPointer<double>()) {
                local = ScriptScalar(add ? *current + constant : *current - constant);
                break;
            }
            const auto semantic = ScriptSemanticRuntime::binary(
                add ? SemanticBinaryOperation::Add : SemanticBinaryOperation::Sub,
                local,
                makeFloat(constant));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            local = semantic.value;
            break;
        }
        case Opcode::LocalAddIntConst:
        case Opcode::LocalSubIntConst:
        case Opcode::LocalAddInt64Const:
        case Opcode::LocalSubInt64Const: {
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            const bool int64Path =
                inst.op == Opcode::LocalAddInt64Const || inst.op == Opcode::LocalSubInt64Const;
            const bool add =
                inst.op == Opcode::LocalAddIntConst || inst.op == Opcode::LocalAddInt64Const;
            if (staticInvalid(!int64Path
                    && (inst.i64 < std::numeric_limits<int>::min()
                        || inst.i64 > std::numeric_limits<int>::max()))) {
                err.code = ScriptDiagnosticCode::IntegerOverflow;
                err.message = "integer fast-path constant is out of range";
                goto error_exit;
            }
            auto& local = frame.locals[inst.a];
            if (int64Path) {
                if (const auto* p = local.scalarPointer<std::int64_t>()) {
                    std::int64_t value = 0;
                    const bool valid = add
                        ? ScriptSemanticRuntime::checkedAddInt64(*p, inst.i64, value)
                        : ScriptSemanticRuntime::checkedSubtractInt64(*p, inst.i64, value);
                    if (valid) {
                        local = ScriptScalar(value);
                        break;
                    }
                }
            } else if (const auto* p = local.scalarPointer<int>()) {
                const std::int64_t value = add ? static_cast<std::int64_t>(*p) + inst.i64
                                               : static_cast<std::int64_t>(*p) - inst.i64;
                if (value >= std::numeric_limits<int>::min()
                    && value <= std::numeric_limits<int>::max()) {
                    local = ScriptScalar(static_cast<int>(value));
                    break;
                }
            }
            const auto semantic = ScriptSemanticRuntime::binary(
                add ? SemanticBinaryOperation::Add : SemanticBinaryOperation::Sub,
                local,
                int64Path ? makeInt64(inst.i64) : makeInt(static_cast<int>(inst.i64)));
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            local = semantic.value;
            break;
        }
        case Opcode::LocalAddLocal: {
            if (staticInvalid(inst.a < 0 || inst.a >= static_cast<int>(frame.locals.size())
                    || inst.b < 0 || inst.b >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            // Fast path is guarded by a checked wide intermediate.
            {
                const int* pa = frame.locals[inst.a].scalarPointer<int>();
                const int* pb = frame.locals[inst.b].scalarPointer<int>();
                if (pa && pb) {
                    const std::int64_t value =
                        static_cast<std::int64_t>(*pa) + static_cast<std::int64_t>(*pb);
                    if (value >= std::numeric_limits<int>::min()
                        && value <= std::numeric_limits<int>::max()) {
                        frame.locals[inst.a] = ScriptScalar(static_cast<int>(value));
                        break;
                    }
                }
            }
            const auto semantic = ScriptSemanticRuntime::binary(
                SemanticBinaryOperation::Add, frame.locals[inst.a], frame.locals[inst.b]);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.locals[inst.a] = semantic.value;
            break;
        }
        case Opcode::JumpIfLocalIntLtConstFalse: {
            // A = target, B = slot, I64 = const. Jump if NOT (locals[B] < const).
            if (staticInvalid(inst.b < 0 || inst.b >= static_cast<int>(frame.locals.size()))) {
                err.code = ScriptDiagnosticCode::IndexError;
                err.message = "local slot out of range";
                goto error_exit;
            }
            // Fast path: local is int, const is int.
            {
                const int* p = frame.locals[inst.b].scalarPointer<int>();
                if (p) {
                    if (!(*p < static_cast<int>(inst.i64)))
                        frame.ip = inst.a;
                    break;
                }
            }
            Value lhs = frame.locals[inst.b];
            Value rhs = makeInt(static_cast<int>(inst.i64));
            if (!normalizeNumericOperands(lhs, rhs)) {
                err.code = ScriptDiagnosticCode::BadCast;
                err.message = "JumpIfLocalIntLtConstFalse numeric conversion failed";
                goto error_exit;
            }
            if (!(lhs < rhs))
                frame.ip = inst.a;
            break;
        }

        // ---------- yield ----------
        case Opcode::Yield: {
            // Pop the yielded value and return it as a result.
            // Sets a special error code so the caller knows the function
            // suspended rather than returned.
            const IntegerValueCacheBorrow* cache = nullptr;
            if (quotaBinding_ && !frame.stack.empty()) {
                const auto* scalar = frame.stack.back().inlineScalar();
                if (const auto* integer = scalar ? scalar->ptr<int>() : nullptr;
                    integer && intValueUsesCache(*integer))
                    cache = &quotaBinding_->integerCache();
            }
            Value yielded = frame.pop<StackProven>(cache);
            result = std::move(yielded);
            if (activeGenerator_ != nullptr)
                activeGenerator_->yieldedValue = result;
            if (activeGenerator_ != nullptr && frame.function != nullptr
                && frame.function->hasYieldFrom) {
                for (const auto& site : frame.function->delegationSites) {
                    if (site.yieldPc == frame.lastIp) {
                        activeGenerator_->delegatedYieldPc = site.yieldPc;
                        break;
                    }
                }
            }
            if (produced != nullptr)
                *produced = true;
            err.code = ScriptDiagnosticCode::Suspended;
            err.message = "yielded";
            goto normal_exit;
        }

        case Opcode::Await: {
            Value awaitable = frame.pop<StackProven>();
            Value awaitedResult;
            core::pmr::String awaitDiagnostics{&res};
            if (!isScriptAwaitable(awaitable)) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = "await expects an awaitable value";
                goto error_exit;
            }
            const auto status =
                asScriptAwaitable(awaitable)->pollResult(awaitedResult, awaitDiagnostics);
            if (status == ScriptAwaitStatus::Pending) {
                if (activeGenerator_ != nullptr) {
                    // Both the generator frame and an ordinary callee below
                    // it are represented by the generator-owned chain. The
                    // active frame is identified by the canonical frame; the
                    // generator object remains the root owner.
                    activeGenerator_->suspendedFrame =
                        arp::core::makeShared<Frame>(std::move(frame));
                    activeGenerator_->suspendedAwaitable = std::move(awaitable);
                    activeGenerator_->suspendedAwaitInjection = makeNil();
                    activeGenerator_->suspendedAwaitInjectionReady = false;
                    activeGenerator_->suspendedFrameProgram = activeProgram_;
                    activeGenerator_->suspendedFrameProgramOwner = activeProgramOwner_;
                } else {
                    suspendedFrame_ = std::move(frame);
                    suspendedAwaitable_ = std::move(awaitable);
                    suspendedAwaiting_ = true;
                    suspendedProgram_ = &program;
                    suspendedFrameProgram_ = activeProgram_ != nullptr ? activeProgram_ : &program;
                    suspendedFrameProgramOwner_ = activeProgramOwner_;
                }
                err.code = ScriptDiagnosticCode::Suspended;
                err.message = "await suspended";
                goto normal_exit;
            }
            if (status == ScriptAwaitStatus::Failed) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = awaitDiagnostics.empty() ? core::pmr::String("await failed", &res)
                                                       : std::move(awaitDiagnostics);
                goto error_exit;
            }
            frame.push(std::move(awaitedResult));
            break;
        }

        case Opcode::PushHandler: {
            bool validFilter = false;
            (void)handlerFilter(inst.a, validFilter);
            if (!validFilter) {
                err.code = ScriptDiagnosticCode::InvalidOpcode;
                err.message = "invalid typed except diagnostic filter";
                goto error_exit;
            }
            frame.handlers.push_back(Frame::HandlerInfo{inst.a, frame.stack.size()});
            break;
        }
        case Opcode::PopHandler:
            if (frame.handlers.empty()) {
                err.code = ScriptDiagnosticCode::InvalidOpcode;
                err.message = "handler stack underflow";
                goto error_exit;
            }
            frame.handlers.pop_back();
            break;
        case Opcode::Throw: {
            if constexpr (StaticValidated) {
                const auto* literal =
                    frame.stack.empty() ? nullptr : frame.stack.back().immutableLiteral();
                if (literal != nullptr && core::currentCpuMemoryQuota() == nullptr) {
                    // The privately owned Program retains these immutable
                    // bytes. Copy into the diagnostic before unwinding; the
                    // catch still creates an independent owned String Value.
                    // Quota executions retain original materialization order.
                    frame.popSlot<StackProven>();
                    err.code = ScriptDiagnosticCode::UserRaise;
                    try {
                        err.message = *literal;
                    } catch (const std::exception&) {
                        err.message = "script raised a non-string value";
                    }
                    goto error_exit;
                }
            }
            Value raised = frame.pop<StackProven>();
            err.code = ScriptDiagnosticCode::UserRaise;
            if (!tryGetString(raised, err.message))
                err.message = "script raised a non-string value";
            goto error_exit;
        }

        case Opcode::LoadModule: {
            // S = module name. Load the module and push a ModuleObject.
            if (!moduleRegistry_) {
                err.code = ScriptDiagnosticCode::HostError;
                err.message = "no module registry set";
                goto error_exit;
            }
            const auto semantic = ScriptSemanticRuntime::loadModule(*moduleRegistry_, inst.s);
            if (!semantic.ok()) {
                err.code = semantic.diagnostic.code;
                err.message = semantic.diagnostic.message;
                goto error_exit;
            }
            frame.push(semantic.value);
            break;
        }
        case Opcode::LoadModuleExport: {
            // S = export name. Pop ModuleObject, push the exported Value.
            Value modVal = frame.pop<StackProven>();
            const auto result = ScriptSemanticRuntime::loadModuleExport(modVal, inst.s);
            if (!result.ok()) {
                err.code = result.diagnostic.code;
                err.message = result.diagnostic.message;
                goto error_exit;
            }
            frame.push(result.value);
            break;
        }

        default:
            err.code = ScriptDiagnosticCode::InvalidOpcode;
            err.message = "unhandled opcode: ";
            err.message += formatOpcode(inst.op);
            goto error_exit;
        }
        if (frame.stack.size() > maxFrameValues_
            || stackInvalid(frame.handlers.size() > maxFrameValues_
                || (frame.function != nullptr
                    && frame.stack.size()
                        > static_cast<std::size_t>(std::max(0, frame.function->maxStack))))) {
            err.code = ScriptDiagnosticCode::ResourceLimitExceeded;
            err.message = "script frame values exceed the execution limit";
            goto error_exit;
        }
        continue;
    }

normal_exit:
    return result;

error_exit:
    while (!frame.handlers.empty() && err.code != ScriptDiagnosticCode::None) {
        const auto handler = frame.handlers.back();
        frame.handlers.pop_back();
        bool validFilter = false;
        int unobservedLocal = -1;
        const auto filter = handlerFilter(handler.handlerPc, validFilter, &unobservedLocal);
        if (!validFilter) {
            err.code = ScriptDiagnosticCode::InvalidOpcode;
            err.message = "invalid typed except diagnostic filter";
            break;
        }
        if (filter != ScriptDiagnosticCode::None && filter != err.code)
            continue;
        while (frame.stack.size() > handler.stackDepth)
            frame.popSlot<StackProven>();
        bool stored = false;
        if constexpr (StaticValidated) {
            auto& remaining = executionSession_ != nullptr
                ? executionSession_->remainingInstructions
                : remainingInstructions_;
            // Only fuse a fully executable original handler StoreLocal. A short
            // budget, quota or value limit retains the original materialization
            // and dispatch failure PC. The payload remains a real owned Value.
            if (unobservedLocal >= 0 && memoryQuota_ == nullptr
                && core::currentCpuMemoryQuota() == nullptr && remaining != 0
                && frame.stack.size() == handler.stackDepth && frame.stack.size() < maxFrameValues_
                && frame.stack.size()
                    < static_cast<std::size_t>(std::max(0, frame.function->maxStack))
                && frame.stack.capacity() > frame.stack.size()
                && frame.handlers.size() <= maxFrameValues_
                && ScriptSemanticRuntime::assignUniqueString(
                    frame.locals[unobservedLocal], err.message, {})) {
                if (remaining > 0)
                    --remaining;
                frame.lastIp = handler.handlerPc;
                stored = true;
            }
        }
        if (!stored)
            frame.push(makeString(err.message));
        // A handled error is no longer the active failure. Do not leak its
        // traceback into a later exception raised by the handler.
        err.traceback.clear();
        err.code = ScriptDiagnosticCode::None;
        err.message.clear();
        frame.ip = handler.handlerPc + (stored ? 1 : 0);
        goto dispatch;
    }
    // A nested call may already have captured the complete stack before
    // returning its error to this frame. Preserve that traceback; rebuilding
    // it here would leave only the outer caller visible.
    if (err.traceback.empty())
        buildTraceback(program, err);
    return result;
}

bool Vm::collectSlotArguments(Frame& frame, int count, VmError& error, const char* underflowMessage)
{
    callSlots_.clear();
    callSlots_.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        if (frame.stack.empty()) {
            error.code = ScriptDiagnosticCode::StackUnderflow;
            error.message = underflowMessage;
            return false;
        }
        callSlots_.push_back(frame.popSlot());
    }
    std::reverse(callSlots_.begin(), callSlots_.end());
    return true;
}

template <bool SlotReturn>
Vm::InternalReturn<SlotReturn> Vm::runFunctionInternalBoundary(const Program& program,
    int funcIndex,
    const ExecutionArgumentsView& args,
    const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
    VmError* err,
    bool* produced,
    ExecutionSlot* slotResult,
    bool provenStaticCall)
{
    const auto execute = [&]() -> InternalReturn<SlotReturn> {
        if constexpr (SlotReturn) {
            assert(provenStaticCall && slotResult != nullptr);
            return runFunctionInternalImpl<true, true>(
                program, funcIndex, args, captures, err, produced, slotResult);
        } else {
            if (provenStaticCall)
                return runFunctionInternalImpl<true>(
                    program, funcIndex, args, captures, err, produced, slotResult);
            return runFunctionInternalImpl<false>(
                program, funcIndex, args, captures, err, produced, slotResult);
        }
    };
    try {
        if (provenStaticCall && quotaBinding_ && quotaBinding_->matches(memoryQuota_)) {
            // This call is a synchronous private bytecode edge, after the
            // parent's public/bridge boundary established the same binding.
            // It cannot observe another scope object: only its quota identity
            // is public. Debug verifies the actual TLS; Release needs no TLS
            // lookup or repeated install/restore on this admitted edge.
            assert(quotaBinding_->matches(core::currentCpuMemoryQuota()));
            return execute();
        }
        ExecutionQuotaBinding quotaScope(*this);
        return execute();
    } catch (const std::bad_alloc&) {
        core::CpuMemoryQuotaScope diagnosticsScope(nullptr);
        if (err != nullptr) {
            err->code = ScriptDiagnosticCode::ResourceLimitExceeded;
            err->message = "script memory quota exceeded";
        }
        if (produced != nullptr)
            *produced = false;
        if constexpr (SlotReturn)
            return {};
        else
            return makeNil();
    }
}

Value Vm::runFunctionInternal(const Program& program,
    int funcIndex,
    const ExecutionArgumentsView& args,
    const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
    VmError* err,
    bool* produced,
    ExecutionSlot* slotResult,
    bool provenStaticCall)
{
    return runFunctionInternalBoundary<false>(
        program, funcIndex, args, captures, err, produced, slotResult, provenStaticCall);
}

void Vm::runStaticFunctionSlots(const Program& program,
    int funcIndex,
    const ExecutionArgumentsView& args,
    const arp::core::pmr::Vector<std::shared_ptr<ScriptCell>>& captures,
    VmError* err,
    bool* produced,
    ExecutionSlot* slotResult)
{
    runFunctionInternalBoundary<true>(
        program, funcIndex, args, captures, err, produced, slotResult, true);
}

bool Vm::ensureProgramInitialized(const Program& program, VmError& error)
{
    for (const auto* initialized : initializedPrograms_) {
        if (initialized == &program)
            return true;
    }

    ensureGlobals(program);
    if (program.init >= 0) {
        runFunctionInternal(program, program.init, emptyArgs(), emptyCaptures(), &error);
        if (error.code != ScriptDiagnosticCode::None
            && error.code != ScriptDiagnosticCode::Suspended)
            return false;
        if (error.code == ScriptDiagnosticCode::Suspended)
            error.code = ScriptDiagnosticCode::None;
    }
    try {
        initializedPrograms_.push_back(&program);
    } catch (const std::bad_alloc&) {
        error.code = ScriptDiagnosticCode::ResourceLimitExceeded;
        error.message = "script initializer state allocation failed";
        return false;
    }
    return true;
}

Value Vm::run(const Program& program, VmError* err)
{
    VmError localErr;
    VmError* e = err ? err : &localErr;
    if (!validateForExecution(program, *e))
        return makeNil();

    if (!ensureProgramInitialized(program, *e))
        return makeNil();
    if (program.entry < 0) {
        e->code = ScriptDiagnosticCode::InvalidOpcode;
        e->message = "program has no entry function";
        return makeNil();
    }
    bool produced = false;
    Value result =
        runFunctionInternal(program, program.entry, emptyArgs(), emptyCaptures(), e, &produced);
    // Suspended is not an error: the yielded value is in result.
    if (e->code != ScriptDiagnosticCode::None && e->code != ScriptDiagnosticCode::Suspended)
        return makeNil();
    if (!produced)
        return makeNil();
    return result;
}

Value Vm::runFunction(
    const Program& program, int funcIndex, const arp::core::pmr::Vector<Value>& args, VmError* err)
{
    VmError localErr;
    VmError* e = err ? err : &localErr;
    if (!validateForExecution(program, *e))
        return makeNil();
    if (!ensureProgramInitialized(program, *e))
        return makeNil();
    arp::core::pmr::Vector<Value> normalizedArgs(
        (arp::core::pmr::Vector<Value>::allocator_type(&arp::core::defaultCpuMemoryResource())));
    const arp::core::pmr::Vector<Value>* callArgs = &args;
    if (funcIndex >= 0 && funcIndex < static_cast<int>(program.functions.size())) {
        const Function& function = program.functions[funcIndex];
        const int fixedParams = function.isVariadic ? function.fixedParams : function.params;
        const bool hasParameterContract =
            function.paramTypes.size() >= static_cast<std::size_t>(fixedParams)
            && ((!function.isVariadic && static_cast<int>(args.size()) == function.params)
                || (function.isVariadic && static_cast<int>(args.size()) >= fixedParams));
        if (hasParameterContract) {
            if (!normalizePublicArguments(function, args, normalizedArgs, *e))
                return makeNil();
            callArgs = &normalizedArgs;
        }
    }
    bool produced = false;
    Value result =
        runFunctionInternal(program, funcIndex, *callArgs, emptyCaptures(), e, &produced);
    // Suspended is not an error: the yielded value is in result.
    if (e->code != ScriptDiagnosticCode::None && e->code != ScriptDiagnosticCode::Suspended)
        return makeNil();
    if (!produced)
        return makeNil();
    return result;
}

Value Vm::runFunctionByName(const Program& program,
    core::pmr::String name,
    const arp::core::pmr::Vector<Value>& args,
    VmError* err)
{
    auto it = program.funcIndex.find(name);
    if (it == program.funcIndex.end()) {
        if (err) {
            err->code = ScriptDiagnosticCode::UndeclaredName;
            err->message = "unknown function: ";
            err->message += name;
        }
        return makeNil();
    }
    return runFunction(program, it->second, args, err);
}

Value Vm::runMethodByName(const Program& program,
    const Value& receiver,
    core::pmr::String className,
    core::pmr::String methodName,
    const arp::core::pmr::Vector<Value>& args,
    VmError* err)
{
    VmError localErr;
    VmError* e = err != nullptr ? err : &localErr;
    e->code = ScriptDiagnosticCode::None;
    e->message.clear();
    e->traceback.clear();
    const auto* actual = checkedValuePayload<arp::reflection::instance>(receiver);
    if (actual == nullptr || !actual->alive() || actual->class_() == nullptr) {
        e->code = ScriptDiagnosticCode::BadCast;
        e->message = "script method receiver is not an instance";
        return makeNil();
    }
    const auto actualClass = actual->class_()->name();
    if (!className.empty() && actualClass != className)
        className = actualClass;
    const auto methods = program.methodSets.find(className);
    if (methods == program.methodSets.end()) {
        e->code = ScriptDiagnosticCode::UndeclaredName;
        e->message = "script method class is unavailable: ";
        e->message += className;
        return makeNil();
    }
    const auto method = methods->second.find(methodName);
    if (method == methods->second.end()) {
        e->code = ScriptDiagnosticCode::UndeclaredName;
        e->message = "unknown script method: ";
        e->message += methodName;
        return makeNil();
    }
    arp::core::pmr::Vector<Value> callArgs{
        arp::core::pmr::Vector<Value>::allocator_type(&arp::core::defaultCpuMemoryResource())};
    callArgs.reserve(args.size() + 1u);
    callArgs.push_back(receiver);
    callArgs.insert(callArgs.end(), args.begin(), args.end());
    return runFunction(program, method->second, callArgs, e);
}

Value Vm::resume(const Program& program, VmError* err)
{
    VmError localErr;
    VmError* e = err ? err : &localErr;
    if (!validateForExecution(program, *e))
        return makeNil();
    if (suspendedFrame_.function == nullptr || suspendedProgram_ != &program) {
        e->code = ScriptDiagnosticCode::HostError;
        e->message = "no suspended coroutine belongs to this program";
        return makeNil();
    }

    Value awaitedResult;
    if (suspendedAwaiting_) {
        core::pmr::String awaitDiagnostics{&core::defaultCpuMemoryResource()};
        const auto status =
            asScriptAwaitable(suspendedAwaitable_)->pollResult(awaitedResult, awaitDiagnostics);
        if (status == ScriptAwaitStatus::Pending) {
            e->code = ScriptDiagnosticCode::Suspended;
            e->message = "await remains pending";
            return makeNil();
        }
        if (status == ScriptAwaitStatus::Failed) {
            e->code = ScriptDiagnosticCode::HostError;
            e->message = awaitDiagnostics.empty()
                ? core::pmr::String("await failed", &core::defaultCpuMemoryResource())
                : std::move(awaitDiagnostics);
            // A failed await is an exception at the suspended operation
            // boundary. Drop the failed child frame and inject the error into
            // the nearest retained handler, including a caller frame. This
            // keeps Interpreter resume equivalent to AOT unwind and avoids
            // replaying the awaitable after cross-backend adoption.
            auto injectFailure = [e](Frame& target) {
                if (target.handlers.empty())
                    return false;
                const auto handler = target.handlers.back();
                target.handlers.pop_back();
                target.stack.resize(handler.stackDepth);
                target.stack.push_back(makeString(e->message));
                e->traceback.clear();
                e->code = ScriptDiagnosticCode::None;
                e->message.clear();
                target.ip = handler.handlerPc;
                return true;
            };

            Frame failedFrame = std::move(suspendedFrame_);
            suspendedFrame_ = Frame{};
            suspendedAwaitable_ = Value::undefined();
            suspendedAwaiting_ = false;
            suspendedProgram_ = nullptr;
            auto callers = std::move(suspendedCallers_);
            auto callerPushResults = std::move(suspendedCallerPushResults_);
            auto callerPrograms = std::move(suspendedCallerPrograms_);
            auto callerProgramOwners = std::move(suspendedCallerProgramOwners_);
            suspendedCallers_.clear();
            suspendedCallerPushResults_.clear();
            suspendedCallerPrograms_.clear();
            suspendedCallerProgramOwners_.clear();

            Frame handledFrame;
            const Program* handledProgram =
                suspendedFrameProgram_ != nullptr ? suspendedFrameProgram_ : &program;
            auto handledProgramOwner = suspendedFrameProgramOwner_;
            bool handled = injectFailure(failedFrame);
            if (handled) {
                handledFrame = std::move(failedFrame);
            } else {
                std::size_t handlerIndex = callers.size();
                for (std::size_t index = 0; index < callers.size(); ++index) {
                    if (injectFailure(callers[index])) {
                        handlerIndex = index;
                        handled = true;
                        handledFrame = std::move(callers[index]);
                        if (index < callerPrograms.size() && callerPrograms[index] != nullptr)
                            handledProgram = callerPrograms[index];
                        if (index < callerProgramOwners.size())
                            handledProgramOwner = callerProgramOwners[index];
                        break;
                    }
                }
                if (handled) {
                    if (handlerIndex + 1u < callers.size()) {
                        suspendedCallers_.assign(
                            callers.begin() + static_cast<std::ptrdiff_t>(handlerIndex + 1u),
                            callers.end());
                        if (handlerIndex + 1u < callerPushResults.size()) {
                            suspendedCallerPushResults_.assign(callerPushResults.begin()
                                    + static_cast<std::ptrdiff_t>(handlerIndex + 1u),
                                callerPushResults.end());
                        }
                        if (handlerIndex + 1u < callerPrograms.size()) {
                            suspendedCallerPrograms_.assign(callerPrograms.begin()
                                    + static_cast<std::ptrdiff_t>(handlerIndex + 1u),
                                callerPrograms.end());
                        }
                        if (handlerIndex + 1u < callerProgramOwners.size()) {
                            suspendedCallerProgramOwners_.assign(callerProgramOwners.begin()
                                    + static_cast<std::ptrdiff_t>(handlerIndex + 1u),
                                callerProgramOwners.end());
                        }
                    }
                }
            }
            if (!handled) {
                suspendedCallers_.clear();
                suspendedCallerPushResults_.clear();
                suspendedCallerPrograms_.clear();
                suspendedCallerProgramOwners_.clear();
                if (e->traceback.empty())
                    buildTraceback(program, *e);
                return makeNil();
            }
            suspendedFrame_ = std::move(handledFrame);
            suspendedFrameProgram_ = handledProgram;
            suspendedFrameProgramOwner_ = std::move(handledProgramOwner);
            suspendedProgram_ = &program;
        }
    }

    Frame frame = std::move(suspendedFrame_);
    suspendedFrame_ = Frame{};
    suspendedAwaitable_ = Value::undefined();
    const bool pushAwaitedResult = suspendedAwaiting_;
    const Program* activeProgram =
        suspendedFrameProgram_ != nullptr ? suspendedFrameProgram_ : &program;
    const auto activeProgramOwner = suspendedFrameProgramOwner_;
    suspendedAwaiting_ = false;
    suspendedProgram_ = nullptr;
    suspendedFrameProgram_ = nullptr;
    suspendedFrameProgramOwner_.reset();
    e->code = ScriptDiagnosticCode::None;
    e->message.clear();
    e->traceback.clear();

    if (pushAwaitedResult)
        frame.push(std::move(awaitedResult));
    bool produced = false;
    activeProgram_ = activeProgram;
    activeProgramOwner_ = activeProgramOwner;
    Value result = executeFrame(*activeProgram, std::move(frame), *e, &produced);
    while (e->code == ScriptDiagnosticCode::None && !suspendedCallers_.empty()) {
        // Callers are recorded from the immediate frame outward. Resume them
        // in that same order so each callee result reaches its direct caller
        // before the outer frame continues.
        Frame caller = std::move(suspendedCallers_.front());
        suspendedCallers_.erase(suspendedCallers_.begin());
        const Program* callerProgram =
            suspendedCallerPrograms_.empty() || suspendedCallerPrograms_.front() == nullptr
            ? &program
            : suspendedCallerPrograms_.front();
        if (!suspendedCallerPrograms_.empty())
            suspendedCallerPrograms_.erase(suspendedCallerPrograms_.begin());
        std::shared_ptr<const Program> callerProgramOwner;
        if (!suspendedCallerProgramOwners_.empty()) {
            callerProgramOwner = std::move(suspendedCallerProgramOwners_.front());
            suspendedCallerProgramOwners_.erase(suspendedCallerProgramOwners_.begin());
        }
        const bool pushResults = suspendedCallerPushResults_.front() != 0u;
        suspendedCallerPushResults_.erase(suspendedCallerPushResults_.begin());
        if (pushResults && produced)
            caller.push(std::move(result));
        produced = false;
        activeProgram_ = callerProgram;
        activeProgramOwner_ = callerProgramOwner;
        result = executeFrame(*callerProgram, std::move(caller), *e, &produced);
    }
    if (e->code != ScriptDiagnosticCode::None && e->code != ScriptDiagnosticCode::Suspended) {
        suspendedCallers_.clear();
        suspendedCallerPushResults_.clear();
        suspendedCallerPrograms_.clear();
        suspendedCallerProgramOwners_.clear();
        activeProgram_ = nullptr;
        activeProgramOwner_.reset();
        return makeNil();
    }
    activeProgram_ = nullptr;
    activeProgramOwner_.reset();
    return produced ? std::move(result) : makeNil();
}

ScriptResult Vm::snapshotSuspendedFrame(
    const core::pmr::String& moduleId, std::uint32_t moduleVersion, SuspendedScriptFrame& out) const
{
    if (suspendedFrame_.function == nullptr || suspendedProgram_ == nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::HostError,
            core::pmr::String(
                "VM has no suspended await frame", &core::defaultCpuMemoryResource()));
    if (moduleId.empty() || moduleVersion == 0u)
        return ScriptResult::failure(ScriptDiagnosticCode::HybridModuleVersionMismatch,
            core::pmr::String(
                "suspended frame module identity is invalid", &core::defaultCpuMemoryResource()));
    if ((!suspendedCallerPrograms_.empty()
            && suspendedCallerPrograms_.size() != suspendedCallers_.size())
        || (!suspendedCallerProgramOwners_.empty()
            && suspendedCallerProgramOwners_.size() != suspendedCallers_.size()))
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String("suspended caller program provenance is incomplete",
                &core::defaultCpuMemoryResource()));
    if (suspendedCallers_.size() > maxContinuationFrames_)
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            core::pmr::String("suspended caller chain exceeds the continuation frame limit",
                &core::defaultCpuMemoryResource()));

    const Function* function = suspendedFrame_.function;
    const Program* activeProgram =
        suspendedFrameProgram_ != nullptr ? suspendedFrameProgram_ : suspendedProgram_;
    if (activeProgram == nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
            core::pmr::String("suspended active Interpreter program is unavailable",
                &core::defaultCpuMemoryResource()));
    const auto& activeIdentity = activeProgram->artifactIdentity;
    const bool activeHasIdentity = activeIdentity.complete();
    const core::pmr::String activeModuleId = activeHasIdentity ? activeIdentity.moduleId : moduleId;
    const std::uint32_t activeModuleVersion =
        activeHasIdentity ? activeIdentity.moduleVersion : moduleVersion;
    out = SuspendedScriptFrame{};
    out.moduleId = activeModuleId;
    out.moduleVersion = activeModuleVersion;
    out.artifactIdentity = activeHasIdentity ? programArtifactIdentity(*activeProgram)
                                             : core::pmr::String{&core::defaultCpuMemoryResource()};
    out.interpreterProgram = suspendedFrameProgramOwner_ != nullptr
        ? suspendedFrameProgramOwner_
        : (activeProgram == suspendedProgram_ ? programOwner_ : std::shared_ptr<const Program>{});
    out.functionId = makeTypedIrFunctionId(activeModuleId, function->name);
    out.safepointId = out.functionId;
    out.safepointId += ":pc:";
    out.safepointId += std::to_string(suspendedFrame_.ip);
    out.bytecodePc = suspendedFrame_.ip;
    if (suspendedFrame_.lastIp >= 0
        && suspendedFrame_.lastIp < static_cast<int>(function->code.size()))
        out.source = function->code[static_cast<std::size_t>(suspendedFrame_.lastIp)].pos;
    else if (suspendedFrame_.ip >= 0
        && suspendedFrame_.ip < static_cast<int>(function->code.size()))
        out.source = function->code[static_cast<std::size_t>(suspendedFrame_.ip)].pos;
    out.locals = suspendedFrame_.locals.materialize();
    out.operandStack = suspendedFrame_.stack.materialize();
    out.suspendedValue = suspendedAwaiting_ ? suspendedAwaitable_ : makeNil();
    out.awaiting = suspendedAwaiting_;
    out.replaceReturnValue = suspendedFrame_.replaceReturnValue;
    out.returnValue = suspendedFrame_.returnValue.boxed();
    out.captures.reserve(suspendedFrame_.captures.size());
    for (const auto& capture : suspendedFrame_.captures) {
        out.captures.push_back(capture ? capture->value : makeNil());
        out.captureCells.push_back(capture);
    }
    out.handlerStack.reserve(suspendedFrame_.handlers.size());
    out.handlerStackDepths.reserve(suspendedFrame_.handlers.size());
    for (const auto& handler : suspendedFrame_.handlers) {
        out.handlerStack.push_back(handler.handlerPc);
        out.handlerStackDepths.push_back(handler.stackDepth);
    }
    out.callers.reserve(suspendedCallers_.size());
    for (const auto& callerFrame : suspendedCallers_) {
        if (callerFrame.function == nullptr)
            return ScriptResult::failure(ScriptDiagnosticCode::TypedIrInvalid,
                core::pmr::String(
                    "suspended caller has no function", &core::defaultCpuMemoryResource()));
        SuspendedScriptCallerFrame caller;
        const std::size_t callerIndex = out.callers.size();
        const Program* callerProgram = callerIndex < suspendedCallerPrograms_.size()
                && suspendedCallerPrograms_[callerIndex] != nullptr
            ? suspendedCallerPrograms_[callerIndex]
            : suspendedProgram_;
        if (callerProgram == nullptr)
            return ScriptResult::failure(ScriptDiagnosticCode::AotMissingEntry,
                core::pmr::String("suspended caller Interpreter program is unavailable",
                    &core::defaultCpuMemoryResource()));
        const auto& callerIdentity = callerProgram->artifactIdentity;
        const bool callerHasIdentity = callerIdentity.complete();
        caller.moduleId = callerHasIdentity ? callerIdentity.moduleId : moduleId;
        caller.moduleVersion = callerHasIdentity ? callerIdentity.moduleVersion : moduleVersion;
        caller.artifactIdentity = callerHasIdentity
            ? programArtifactIdentity(*callerProgram)
            : core::pmr::String{&core::defaultCpuMemoryResource()};
        caller.interpreterProgram = callerIndex < suspendedCallerProgramOwners_.size()
                && suspendedCallerProgramOwners_[callerIndex] != nullptr
            ? suspendedCallerProgramOwners_[callerIndex]
            : (callerProgram == suspendedProgram_ ? programOwner_
                                                  : std::shared_ptr<const Program>{});
        caller.functionId = makeTypedIrFunctionId(caller.moduleId, callerFrame.function->name);
        caller.bytecodePc = callerFrame.ip;
        if (callerFrame.lastIp >= 0
            && callerFrame.lastIp < static_cast<int>(callerFrame.function->code.size()))
            caller.source =
                callerFrame.function->code[static_cast<std::size_t>(callerFrame.lastIp)].pos;
        caller.locals = callerFrame.locals.materialize();
        caller.operandStack = callerFrame.stack.materialize();
        for (const auto& capture : callerFrame.captures) {
            caller.captures.push_back(capture ? capture->value : makeNil());
            caller.captureCells.push_back(capture);
        }
        for (const auto& handler : callerFrame.handlers) {
            caller.handlerStack.push_back(handler.handlerPc);
            caller.handlerStackDepths.push_back(handler.stackDepth);
        }
        out.callers.push_back(std::move(caller));
    }
    return validateSuspendedScriptFrame(out);
}

ScriptResult Vm::adoptSuspendedFrame(const Program& program, const SuspendedScriptFrame& frame)
{
    const auto valid = validateSuspendedScriptFrame(frame);
    if (!valid.ok())
        return valid;
    if (frame.callers.size() > maxContinuationFrames_)
        return ScriptResult::failure(ScriptDiagnosticCode::ResourceLimitExceeded,
            core::pmr::String("canonical caller chain exceeds the continuation frame limit",
                &core::defaultCpuMemoryResource()));
    const bool foreignActiveFrame = (!moduleId_.empty() && frame.moduleId != moduleId_)
        || (moduleVersion_ != 0u && frame.moduleVersion != moduleVersion_);
    if (foreignActiveFrame && frame.interpreterProgram == nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::HybridModuleVersionMismatch,
            core::pmr::String(
                "foreign canonical suspended frame has no receiving Interpreter program",
                &core::defaultCpuMemoryResource()));
    if (!foreignActiveFrame && program.artifactIdentity.complete()
        && !programMatchesFrame(
            program, frame.moduleId, frame.moduleVersion, frame.artifactIdentity))
        return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
            core::pmr::String("canonical suspended frame program identity does not match",
                &core::defaultCpuMemoryResource()));
    if (suspendedFrame_.function != nullptr || suspendedProgram_ != nullptr)
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String(
                "VM already owns a suspended continuation", &core::defaultCpuMemoryResource()));
    if (frame.awaiting && !isScriptAwaitable(frame.suspendedValue))
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String(
                "canonical await frame has no awaitable", &core::defaultCpuMemoryResource()));

    auto selectProgram = [this, &program](const core::pmr::String& moduleId,
                             std::uint32_t moduleVersion,
                             const core::pmr::String& artifactIdentity,
                             const std::shared_ptr<const Program>& hint,
                             const char* mismatchMessage,
                             const Program*& selected,
                             std::shared_ptr<const Program>& selectedOwner) -> ScriptResult {
        selected = &program;
        selectedOwner = programOwner_;
        if (hint == nullptr) {
            if (program.artifactIdentity.complete()
                && !programMatchesFrame(program, moduleId, moduleVersion, artifactIdentity))
                return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
                    core::pmr::String(mismatchMessage, &core::defaultCpuMemoryResource()));
            return ScriptResult::success();
        }
        const bool legacyRootProgram = hint.get() == &program && !hint->artifactIdentity.complete();
        if (!legacyRootProgram
            && !programMatchesFrame(*hint, moduleId, moduleVersion, artifactIdentity))
            return ScriptResult::failure(ScriptDiagnosticCode::AotFingerprintMismatch,
                core::pmr::String(mismatchMessage, &core::defaultCpuMemoryResource()));
        selected = hint.get();
        selectedOwner = hint;
        return ScriptResult::success();
    };
    auto sameSource = [](const SourcePos& lhs, const SourcePos& rhs) {
        return lhs.file == rhs.file && lhs.line == rhs.line && lhs.column == rhs.column;
    };
    auto failure = [](const char* message) {
        return ScriptResult::failure(ScriptDiagnosticCode::AotSwitchUnsupported,
            core::pmr::String(message, &core::defaultCpuMemoryResource()));
    };

    const Program* activeProgram = nullptr;
    std::shared_ptr<const Program> activeProgramOwner;
    const auto activeSelection = selectProgram(frame.moduleId,
        frame.moduleVersion,
        frame.artifactIdentity,
        frame.interpreterProgram,
        "canonical active Interpreter program identity does not match",
        activeProgram,
        activeProgramOwner);
    if (!activeSelection.ok())
        return activeSelection;
    const Function* function = nullptr;
    for (const auto& candidate : activeProgram->functions) {
        if (makeTypedIrFunctionId(frame.moduleId, candidate.name) == frame.functionId) {
            function = &candidate;
            break;
        }
    }
    if (function == nullptr)
        return failure("canonical frame function is unavailable in the interpreter");
    if (frame.locals.size() != static_cast<std::size_t>(std::max(0, function->locals))
        || frame.operandStack.size() > static_cast<std::size_t>(std::max(0, function->maxStack)))
        return failure("canonical frame slot counts do not match the interpreter");

    int resumePc = frame.bytecodePc;
    if (frame.awaiting) {
        const bool directPc = resumePc > 0 && resumePc <= static_cast<int>(function->code.size())
            && function->code[static_cast<std::size_t>(resumePc - 1)].op == Opcode::Await;
        if (!directPc) {
            resumePc = -1;
            for (std::size_t index = 0; index < function->code.size(); ++index) {
                const auto& instruction = function->code[index];
                if (instruction.op == Opcode::Await && sameSource(instruction.pos, frame.source)) {
                    if (resumePc >= 0)
                        return failure("canonical frame maps to multiple interpreter await points");
                    resumePc = static_cast<int>(index + 1u);
                }
            }
            if (resumePc < 0)
                return failure("canonical frame has no interpreter await safepoint");
        }
    }

    auto materialize = [](const Function* target,
                           int pc,
                           const core::pmr::Vector<Value>& locals,
                           const core::pmr::Vector<Value>& captures,
                           const core::pmr::Vector<std::shared_ptr<ScriptCell>>& captureCells,
                           const core::pmr::Vector<Value>& stack,
                           const core::pmr::Vector<int>& handlers,
                           const core::pmr::Vector<std::size_t>& depths,
                           bool replaceReturnValue,
                           const Value& returnValue,
                           Frame& out) -> bool {
        if (target == nullptr
            || locals.size() != static_cast<std::size_t>(std::max(0, target->locals)) || pc < 0
            || pc > static_cast<int>(target->code.size())
            || stack.size() > static_cast<std::size_t>(std::max(0, target->maxStack))
            || handlers.size() != depths.size())
            return false;
        out.reset(target);
        out.ip = pc;
        out.lastIp = pc > 0 ? pc - 1 : 0;
        out.locals = locals;
        out.stack = stack;
        out.replaceReturnValue = replaceReturnValue;
        out.returnValue = returnValue;
        if (!captureCells.empty()) {
            out.captures = captureCells;
        } else {
            for (const auto& capture : captures)
                out.captures.push_back(core::makeShared<ScriptCell>(capture));
        }
        for (std::size_t index = 0; index < handlers.size(); ++index) {
            if (handlers[index] < 0 || handlers[index] >= static_cast<int>(target->code.size())
                || depths[index] > stack.size())
                return false;
            out.handlers.push_back(Frame::HandlerInfo{handlers[index], depths[index]});
        }
        return true;
    };

    Frame imported;
    if (!materialize(function,
            resumePc,
            frame.locals,
            frame.captures,
            frame.captureCells,
            frame.operandStack,
            frame.handlerStack,
            frame.handlerStackDepths,
            frame.replaceReturnValue,
            frame.returnValue,
            imported))
        return failure("canonical frame metadata cannot be adopted by the interpreter");

    core::pmr::Vector<Frame> importedCallers{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<const Program*> importedCallerPrograms{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<const Program>> importedCallerOwners{
        &core::defaultCpuMemoryResource()};
    for (const auto& caller : frame.callers) {
        const Program* callerProgram = nullptr;
        std::shared_ptr<const Program> callerProgramOwner;
        const auto callerSelection = selectProgram(caller.moduleId,
            caller.moduleVersion,
            caller.artifactIdentity,
            caller.interpreterProgram,
            "canonical caller Interpreter program identity does not match",
            callerProgram,
            callerProgramOwner);
        if (!callerSelection.ok())
            return callerSelection;
        const Function* callerFunction = nullptr;
        for (const auto& candidate : callerProgram->functions) {
            if (makeTypedIrFunctionId(caller.moduleId, candidate.name) == caller.functionId) {
                callerFunction = &candidate;
                break;
            }
        }
        if (callerFunction == nullptr)
            return failure("canonical caller function is unavailable in the interpreter");
        Frame importedCaller;
        if (!materialize(callerFunction,
                caller.bytecodePc,
                caller.locals,
                caller.captures,
                caller.captureCells,
                caller.operandStack,
                caller.handlerStack,
                caller.handlerStackDepths,
                false,
                makeNil(),
                importedCaller))
            return failure("canonical caller metadata cannot be adopted by the interpreter");
        importedCallers.push_back(std::move(importedCaller));
        importedCallerPrograms.push_back(callerProgram);
        importedCallerOwners.push_back(std::move(callerProgramOwner));
    }

    suspendedFrame_ = std::move(imported);
    suspendedAwaitable_ = frame.suspendedValue;
    suspendedAwaiting_ = frame.awaiting;
    suspendedProgram_ = &program;
    suspendedFrameProgram_ = activeProgram;
    suspendedFrameProgramOwner_ = std::move(activeProgramOwner);
    suspendedCallers_ = std::move(importedCallers);
    suspendedCallerPrograms_ = std::move(importedCallerPrograms);
    suspendedCallerProgramOwners_ = std::move(importedCallerOwners);
    suspendedCallerPushResults_.clear();
    suspendedCallerPushResults_.resize(suspendedCallers_.size(), 1u);
    return ScriptResult::success();
}

void Vm::discardSuspendedFrame()
{
    suspendedFrame_ = Frame{};
    suspendedAwaitable_ = Value::undefined();
    suspendedAwaiting_ = false;
    suspendedProgram_ = nullptr;
    suspendedFrameProgram_ = nullptr;
    suspendedFrameProgramOwner_.reset();
    suspendedCallers_.clear();
    suspendedCallerPushResults_.clear();
    suspendedCallerPrograms_.clear();
    suspendedCallerProgramOwners_.clear();
}

} // namespace arp::scriptvm
