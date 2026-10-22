#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/interpreter/suspended_frame.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/runtime/semantic_runtime.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

namespace {

using namespace arp::scriptvm;

class CrossBackendChildFrame final : public DirectNativeFrameV5 {
public:
    CrossBackendChildFrame() : DirectNativeFrameV5("cross-backend-child", "cross-backend-child#1")
    {}

    int phase = 0;
};

Value crossBackendChildEntry(DirectNativeExecutionContext&,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5* resume,
    VmError* error)
{
    if (resume == nullptr)
        return makeNil();
    if (resume->frame == nullptr) {
        auto frame = arp::core::makeUnique<CrossBackendChildFrame>();
        frame->phase = 1;
        resume->frame = std::move(frame);
        resume->suspendKind = DirectNativeSuspendKindV5::Yield;
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::Suspended;
            error->message = "cross-backend child yielded";
        }
        return makeInt(42);
    }
    auto* frame = static_cast<CrossBackendChildFrame*>(resume->frame.get());
    if (frame != nullptr && frame->phase == 1) {
        if (resume->injectionKind == DirectNativeInjectionKindV5::Throw) {
            resume->injectionKind = DirectNativeInjectionKindV5::None;
            resume->injectionValue = makeNil();
            resume->suspendKind = DirectNativeSuspendKindV5::None;
            resume->frame.reset();
            if (error != nullptr) {
                error->code = ScriptDiagnosticCode::UserRaise;
                error->message = "cross-backend child error";
            }
            return makeNil();
        }
        frame->phase = 2;
        resume->suspendKind = DirectNativeSuspendKindV5::Yield;
        Value injected = resume->injectionValue;
        resume->injectionKind = DirectNativeInjectionKindV5::None;
        resume->injectionValue = makeNil();
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::Suspended;
            error->message = "cross-backend child yielded again";
        }
        return isNil(injected) ? makeInt(99) : injected;
    }
    resume->suspendKind = DirectNativeSuspendKindV5::None;
    resume->frame.reset();
    return makeInt(99);
}

void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

void testComparisonContract()
{
    auto equal =
        ScriptSemanticRuntime::compare(SemanticCompareOperation::Equal, makeNil(), makeNil());
    auto different =
        ScriptSemanticRuntime::compare(SemanticCompareOperation::NotEqual, makeNil(), makeInt(1));
    bool value = false;
    require(equal.ok() && tryGetBool(equal.value, value) && value,
        "nil equality is shared and deterministic");
    require(different.ok() && tryGetBool(different.value, value) && value,
        "nil inequality is shared and deterministic");

    auto integerLessEqual =
        ScriptSemanticRuntime::compare(SemanticCompareOperation::LessEqual, makeInt(5), makeInt(0));
    require(integerLessEqual.ok() && tryGetBool(integerLessEqual.value, value) && !value,
        "integer ordering compares scalar payloads");
    auto integerGreater = ScriptSemanticRuntime::compare(
        SemanticCompareOperation::Greater, makeInt64(5), makeInt64(0));
    require(integerGreater.ok() && tryGetBool(integerGreater.value, value) && value,
        "int64 ordering compares scalar payloads");
    auto floatingLess = ScriptSemanticRuntime::compare(
        SemanticCompareOperation::Less, makeFloat(0.25), makeFloat(0.5));
    require(floatingLess.ok() && tryGetBool(floatingLess.value, value) && value,
        "floating ordering compares scalar payloads");

    auto stringEqual = ScriptSemanticRuntime::compare(SemanticCompareOperation::Equal,
        makeString(std::string_view("same")),
        makeString(std::string_view("same")));
    require(stringEqual.ok() && tryGetBool(stringEqual.value, value) && value,
        "string equality compares scalar payloads");
}

void testConversionAndLengthContract()
{
    auto integer = ScriptSemanticRuntime::convert(SemanticConversionOperation::Int64, makeInt(7));
    std::int64_t integerValue = 0;
    require(integer.ok() && tryGetInt64(integer.value, integerValue) && integerValue == 7,
        "integer conversion is shared");

    auto bytes = ScriptSemanticRuntime::convert(SemanticConversionOperation::Bytes,
        makeString(arp::core::pmr::String("abc", &arp::core::defaultCpuMemoryResource())));
    auto length = ScriptSemanticRuntime::length(bytes.value);
    int lengthValue = 0;
    require(bytes.ok() && length.ok() && tryGetInt(length.value, lengthValue) && lengthValue == 3,
        "bytes conversion and length are shared");
}

void testExtendedNumericArithmeticContract()
{
    auto concatenated = ScriptSemanticRuntime::binary(SemanticBinaryOperation::Add,
        makeString(std::string_view("left")),
        makeString(std::string_view("right")));
    arp::core::pmr::String concatenatedValue{&arp::core::defaultCpuMemoryResource()};
    require(concatenated.ok() && tryGetString(concatenated.value, concatenatedValue)
            && concatenatedValue == "leftright",
        "string addition is shared across Interpreter and AOT");

    auto power =
        ScriptSemanticRuntime::binary(SemanticBinaryOperation::Pow, makeInt(2), makeInt(3));
    int integer = 0;
    require(
        power.ok() && tryGetInt(power.value, integer) && integer == 8, "integer power is shared");

    auto negativeFloor =
        ScriptSemanticRuntime::binary(SemanticBinaryOperation::FloorDiv, makeInt(-7), makeInt(2));
    require(negativeFloor.ok() && tryGetInt(negativeFloor.value, integer) && integer == -4,
        "integer floor division rounds toward negative infinity");

    auto floatingFloor = ScriptSemanticRuntime::binary(
        SemanticBinaryOperation::FloorDiv, makeFloat(7.5), makeFloat(2.0));
    double floating = 0.0;
    require(floatingFloor.ok() && tryGetFloat(floatingFloor.value, floating) && floating == 3.0,
        "floating floor division is shared");

    auto negativePower =
        ScriptSemanticRuntime::binary(SemanticBinaryOperation::Pow, makeInt(2), makeInt(-2));
    require(negativePower.ok() && tryGetFloat(negativePower.value, floating) && floating == 0.25,
        "negative integer powers produce floating-point results");

    auto divisionByZero =
        ScriptSemanticRuntime::binary(SemanticBinaryOperation::FloorDiv, makeInt(1), makeInt(0));
    require(divisionByZero.diagnostic.code == ScriptDiagnosticCode::DivisionByZero,
        "floor division by zero has a deterministic diagnostic");

    auto overflow = ScriptSemanticRuntime::binary(SemanticBinaryOperation::Pow,
        makeInt64(std::numeric_limits<std::int64_t>::max()),
        makeInt(2));
    require(overflow.diagnostic.code == ScriptDiagnosticCode::IntegerOverflow,
        "integer power overflow has a deterministic diagnostic");
}

void testVmBudgetContract()
{
    Program program;
    Function function("budget");
    function.locals = 0;
    function.maxStack = 1;
    function.code.emplace_back(Opcode::LoadInt, 0, 0, 1);
    function.code.emplace_back(Opcode::Return, 1);
    program.functions.push_back(std::move(function));

    Vm vm;
    vm.setInstructionBudget(1);
    VmError error;
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    vm.runFunction(program, 0, args, &error);
    require(error.code == ScriptDiagnosticCode::ExecutionBudgetExceeded,
        "interpreter instruction budget is enforced");

    Program recursiveProgram;
    Function recursive("recursive");
    recursive.maxStack = 1;
    recursive.code.emplace_back(Opcode::Call, 0, 0);
    recursive.code.emplace_back(Opcode::Return, 1);
    recursiveProgram.functions.push_back(std::move(recursive));
    Vm recursiveVm;
    recursiveVm.setRecursionLimit(2);
    error = VmError{};
    recursiveVm.runFunction(recursiveProgram, 0, args, &error);
    require(error.code == ScriptDiagnosticCode::RecursionLimitExceeded,
        "interpreter recursion limit is enforced");
}

void testSuspendedFrameContract()
{
    SuspendedScriptFrame frame;
    frame.moduleId = "gameplay";
    frame.moduleVersion = 3;
    frame.functionId = "arp-fn:gameplay/tick";
    frame.safepointId = "sp:12";
    frame.bytecodePc = 12;
    frame.source.file = "tick.arp";
    frame.source.line = 4;
    require(validateSuspendedScriptFrame(frame).ok(), "complete suspended frame is accepted");
    frame.awaiting = true;
    require(validateSuspendedScriptFrame(frame).failed(),
        "awaiting suspended frame requires an awaitable");
    frame.awaiting = false;
    frame.moduleVersion = 0;
    require(validateSuspendedScriptFrame(frame).failed(),
        "suspended frame without module version is rejected");
    frame.moduleVersion = 3;
    frame.handlerStack.push_back(4);
    require(validateSuspendedScriptFrame(frame).failed(),
        "suspended frame rejects incomplete handler metadata");

    SuspendedScriptFrame delegatedFrame;
    delegatedFrame.moduleId = "gameplay";
    delegatedFrame.moduleVersion = 3;
    delegatedFrame.functionId = "arp-fn:gameplay/delegated";
    delegatedFrame.safepointId = "sp:7";
    delegatedFrame.bytecodePc = 7;
    delegatedFrame.source.file = "delegated.arp";
    delegatedFrame.source.line = 2;
    delegatedFrame.delegatedActive = true;
    delegatedFrame.delegatedChildAwaiting = true;
    delegatedFrame.delegatedSourceSlot = 0;
    delegatedFrame.locals.push_back(makeNil());
    require(validateSuspendedScriptFrame(delegatedFrame).failed(),
        "delegated child-await frame requires an exact RangeNext PC");
    delegatedFrame.delegatedRangePc = 7;
    delegatedFrame.delegatedPendingYieldPc = 10;
    require(validateSuspendedScriptFrame(delegatedFrame).ok(),
        "delegated child-await frame accepts complete continuation metadata");
}

void testVmSuspendedFrameSnapshot()
{
    Program program;
    program.artifactIdentity.moduleId = "gameplay";
    program.artifactIdentity.moduleVersion = 4;
    program.artifactIdentity.semanticAbiVersion = 1;
    program.artifactIdentity.typedIrVersion = 1;
    program.artifactIdentity.sourceHash = "source-v1";
    program.artifactIdentity.dependencyGraphHash = "deps-v1";
    program.artifactIdentity.reflectionRegistryHash = "reflection-v1";
    program.artifactIdentity.typedIrFingerprint = "typed-ir-v1";
    Function function("await_value");
    function.params = 1;
    function.locals = 1;
    function.maxStack = 1;
    function.code.emplace_back(Opcode::LoadLocal, 0);
    function.code.back().pos = SourcePos{
        arp::core::pmr::String("await.arp", &arp::core::defaultCpuMemoryResource()), 3, 5};
    function.code.emplace_back(Opcode::Await);
    function.code.back().pos = SourcePos{
        arp::core::pmr::String("await.arp", &arp::core::defaultCpuMemoryResource()), 3, 11};
    function.code.emplace_back(Opcode::Return, 1);
    function.code.back().pos = SourcePos{
        arp::core::pmr::String("await.arp", &arp::core::defaultCpuMemoryResource()), 3, 18};
    program.functions.push_back(std::move(function));

    ScriptAwaitable awaitable;
    awaitable.poll = [](Value&, arp::core::pmr::String&) {
        return ScriptAwaitStatus::Pending;
    };
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    args.push_back(Value{awaitable});
    Vm vm;
    VmError error;
    vm.runFunction(program, 0, args, &error);
    require(error.code == ScriptDiagnosticCode::Suspended && vm.suspended(),
        "VM produces a pending await frame");

    SuspendedScriptFrame snapshot;
    require(vm.snapshotSuspendedFrame(
                  arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()),
                  4,
                  snapshot)
                .ok(),
        "VM exports a validated backend-neutral suspended frame");
    require(snapshot.awaiting && snapshot.bytecodePc == 2
            && snapshot.functionId == "arp-fn:gameplay/await_value" && snapshot.source.line == 3
            && snapshot.suspendedValue.is<ScriptAwaitable>(),
        "suspended snapshot preserves resume PC, identity, source and awaitable");

    Vm wrongModuleVm;
    wrongModuleVm.setModuleIdentity(
        arp::core::pmr::String("other", &arp::core::defaultCpuMemoryResource()), 4u);
    const auto wrongModule = wrongModuleVm.adoptSuspendedFrame(program, snapshot);
    require(wrongModule.failed()
            && wrongModule.diagnostic().code == ScriptDiagnosticCode::HybridModuleVersionMismatch,
        "suspended-frame adoption rejects a different receiving module identity");

    program.artifactIdentity.sourceHash = "source-v2";
    Vm wrongArtifactVm;
    wrongArtifactVm.setModuleIdentity(
        arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()), 4u);
    const auto wrongArtifact = wrongArtifactVm.adoptSuspendedFrame(program, snapshot);
    require(wrongArtifact.failed()
            && wrongArtifact.diagnostic().code == ScriptDiagnosticCode::AotFingerprintMismatch,
        "suspended-frame adoption rejects a different complete artifact identity");

    Program nestedProgram;
    Function inner("inner");
    inner.params = 1;
    inner.locals = 1;
    inner.maxStack = 1;
    inner.code.emplace_back(Opcode::LoadLocal, 0);
    inner.code.back().pos = SourcePos{
        arp::core::pmr::String("nested.arp", &arp::core::defaultCpuMemoryResource()), 4, 3};
    inner.code.emplace_back(Opcode::Await);
    inner.code.back().pos = SourcePos{
        arp::core::pmr::String("nested.arp", &arp::core::defaultCpuMemoryResource()), 4, 9};
    inner.code.emplace_back(Opcode::Return, 1);
    inner.code.back().pos = SourcePos{
        arp::core::pmr::String("nested.arp", &arp::core::defaultCpuMemoryResource()), 4, 16};
    nestedProgram.functions.push_back(std::move(inner));
    Function outer("outer");
    outer.params = 1;
    outer.locals = 1;
    outer.maxStack = 1;
    outer.code.emplace_back(Opcode::LoadLocal, 0);
    outer.code.back().pos = SourcePos{
        arp::core::pmr::String("nested.arp", &arp::core::defaultCpuMemoryResource()), 8, 3};
    outer.code.emplace_back(Opcode::Call, 0, 1);
    outer.code.back().pos = SourcePos{
        arp::core::pmr::String("nested.arp", &arp::core::defaultCpuMemoryResource()), 8, 9};
    outer.code.emplace_back(Opcode::Return, 1);
    outer.code.back().pos = SourcePos{
        arp::core::pmr::String("nested.arp", &arp::core::defaultCpuMemoryResource()), 8, 16};
    nestedProgram.functions.push_back(std::move(outer));
    ScriptAwaitable nestedAwaitable;
    nestedAwaitable.poll = [](Value&, arp::core::pmr::String&) {
        return ScriptAwaitStatus::Pending;
    };
    arp::core::pmr::Vector<Value> nestedArgs{&arp::core::defaultCpuMemoryResource()};
    nestedArgs.push_back(Value{nestedAwaitable});
    Vm nestedVm;
    nestedVm.runFunction(nestedProgram, 1, nestedArgs, &error);
    SuspendedScriptFrame nestedSnapshot;
    require(
        nestedVm.snapshotSuspendedFrame(
                    arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()),
                    1,
                    nestedSnapshot)
                .ok()
            && nestedSnapshot.callers.size() == 1
            && nestedSnapshot.callers.front().functionId == "arp-fn:gameplay/outer",
        "nested await snapshot preserves caller frame state");
}

void testGeneratorCanonicalAdoptionIdentityAndResume()
{
    Program program;
    program.artifactIdentity.moduleId = "gameplay";
    program.artifactIdentity.moduleVersion = 1;
    program.artifactIdentity.semanticAbiVersion = kSemanticAbiVersion;
    program.artifactIdentity.typedIrVersion = kTypedIrVersion;
    program.artifactIdentity.sourceHash = "source-generator-v1";
    program.artifactIdentity.dependencyGraphHash = "deps-generator-v1";
    program.artifactIdentity.reflectionRegistryHash = "reflection-generator-v1";
    program.artifactIdentity.typedIrFingerprint = "typed-ir-generator-v1";
    Function generator("generator");
    generator.maxStack = 1;
    generator.isGenerator = true;
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    generator.code.emplace_back(Opcode::Yield);
    generator.code.emplace_back(Opcode::LoadInt, 0, 0, 9);
    generator.code.emplace_back(Opcode::Return, 1);
    for (std::size_t index = 0; index < generator.code.size(); ++index) {
        generator.code[index].pos = SourcePos{
            arp::core::pmr::String("adoption.arp", &arp::core::defaultCpuMemoryResource()),
            static_cast<int>(index + 1u),
            1};
    }
    program.functions.push_back(std::move(generator));
    program.funcIndex["generator"] = 0;

    Function maker("make_generator");
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::Call, 0, 0);
    maker.code.emplace_back(Opcode::Return, 1);
    program.functions.push_back(std::move(maker));
    program.funcIndex["make_generator"] = 1;

    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    Vm sourceVm;
    sourceVm.setModuleIdentity(
        arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()), 1u);
    VmError error;
    Value sourceValue = sourceVm.runFunction(program, 1, args, &error);
    require(error.code == ScriptDiagnosticCode::None && sourceValue.is<ScriptGenerator>(),
        "source VM creates a generator for canonical adoption");
    auto* sourceGenerator = sourceValue.ptr<ScriptGenerator>();
    SuspendedScriptFrame initialSnapshot;
    require(
        sourceVm.snapshotGeneratorFrame(*sourceGenerator,
                    arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()),
                    1u,
                    initialSnapshot)
                .failed()
            && initialSnapshot.functionId.empty(),
        "unstarted generator cannot be exported as a continuation");
    int yielded = 0;
    require(tryGetInt(sourceVm.resumeGeneratorValue(*sourceGenerator, error), yielded)
            && yielded == 7 && error.code == ScriptDiagnosticCode::None,
        "source generator reaches a declared yield safepoint");

    Program foreignProgram;
    Function foreignFunction("foreign_generator");
    foreignFunction.isGenerator = true;
    foreignProgram.functions.push_back(std::move(foreignFunction));
    sourceGenerator->suspendedFrameProgram = &foreignProgram;
    SuspendedScriptFrame foreignFrameSnapshot;
    require(
        sourceVm.snapshotGeneratorFrame(*sourceGenerator,
                    arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()),
                    1u,
                    foreignFrameSnapshot)
                .diagnostic()
                .code
            == ScriptDiagnosticCode::AotSwitchUnsupported,
        "generator snapshot rejects a frame function owned by a foreign program");
    sourceGenerator->suspendedFrameProgram = nullptr;

    SuspendedScriptFrame snapshot;
    require(sourceVm
                .snapshotGeneratorFrame(*sourceGenerator,
                    arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()),
                    1u,
                    snapshot)
                .ok(),
        "generator exports a canonical continuation");
    Vm receivingVm;
    receivingVm.setModuleIdentity(
        arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()), 1u);
    auto arbitraryPcSnapshot = snapshot;
    arbitraryPcSnapshot.bytecodePc = 1;
    ScriptGenerator arbitraryPcGenerator;
    require(receivingVm.adoptGeneratorFrame(arbitraryPcGenerator, program, arbitraryPcSnapshot)
                .diagnostic()
                .code
            == ScriptDiagnosticCode::AotSwitchUnsupported,
        "generator adoption rejects an arbitrary non-yield resume PC");
    SuspendedScriptFrame wrongIdentitySnapshot;
    require(
        sourceVm.snapshotGeneratorFrame(*sourceGenerator,
                    arp::core::pmr::String("other", &arp::core::defaultCpuMemoryResource()),
                    1u,
                    wrongIdentitySnapshot)
                .diagnostic()
                .code
            == ScriptDiagnosticCode::HybridModuleVersionMismatch,
        "generator snapshot rejects a caller identity that conflicts with the artifact");

    ScriptGenerator adopted;
    require(receivingVm.adoptGeneratorFrame(adopted, program, snapshot).ok(),
        "receiving interpreter adopts the canonical generator frame");
    int returned = 0;
    require(tryGetInt(receivingVm.resumeGeneratorValue(adopted, error), returned) && returned == 9
            && error.code == ScriptDiagnosticCode::None && adopted.done,
        "adopted generator resumes after the safepoint without replay");
    SuspendedScriptFrame terminalSnapshot;
    require(receivingVm
                .snapshotGeneratorFrame(adopted,
                    arp::core::pmr::String("gameplay", &arp::core::defaultCpuMemoryResource()),
                    1u,
                    terminalSnapshot)
                .diagnostic()
                .code
            == ScriptDiagnosticCode::AotSwitchUnsupported,
        "completed generator cannot be exported as a continuation");

    Vm mismatchedVm;
    mismatchedVm.setModuleIdentity(
        arp::core::pmr::String("other", &arp::core::defaultCpuMemoryResource()), 1u);
    ScriptGenerator rejected;
    const auto mismatch = mismatchedVm.adoptGeneratorFrame(rejected, program, snapshot);
    require(mismatch.failed()
            && mismatch.diagnostic().code == ScriptDiagnosticCode::HybridModuleVersionMismatch,
        "generator adoption rejects a receiving VM with a different module identity");
}

void testInterpreterConsumesDirectNativeGeneratorBridge()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 16u, 1u << 20u});
    VmError error;
    Value childValue =
        makeDirectNativeGeneratorV5(*context, &crossBackendChildEntry, nullptr, 0u, &error);
    require(error.code == ScriptDiagnosticCode::None && isDirectNativeGeneratorV5(childValue),
        "DirectNative bridge fixture creates a child generator");

    Program program;
    program.globalCount = 1;
    Function parent("parent_cross_backend");
    parent.locals = 3;
    parent.maxStack = 1;
    parent.isGenerator = true;
    parent.hasYieldFrom = true;
    parent.code.emplace_back(Opcode::LoadGlobal, 0);
    parent.code.emplace_back(Opcode::RangeInit, 0, 1);
    parent.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 2));
    parent.code.emplace_back(Opcode::JumpIfFalse, 7);
    parent.code.emplace_back(Opcode::LoadLocal, 2);
    parent.code.emplace_back(Opcode::Yield);
    parent.code.emplace_back(Opcode::Jump, 2);
    parent.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    parent.code.emplace_back(Opcode::Return, 1);
    parent.delegationSites.push_back(DelegationSiteMetadata{0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2});
    program.functions.push_back(std::move(parent));
    Function maker("make_parent_cross_backend");
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::Call, 0, 0);
    maker.code.emplace_back(Opcode::Return, 1);
    program.functions.push_back(std::move(maker));

    arp::core::pmr::Vector<Value> globals{&arp::core::defaultCpuMemoryResource()};
    globals.push_back(std::move(childValue));
    Vm vm;
    vm.setSharedGlobals(&globals);
    Value parentValue = vm.runFunction(program, 1, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && parentValue.is<ScriptGenerator>(),
        "Interpreter creates a parent for a DirectNative child");
    auto* parentGenerator = parentValue.ptr<ScriptGenerator>();
    int yielded = 0;
    require(tryGetInt(vm.resumeGeneratorValue(*parentGenerator, error), yielded) && yielded == 42
            && error.code == ScriptDiagnosticCode::None,
        "Interpreter bridge forwards the first DirectNative yield");
    require(tryGetInt(vm.resumeGeneratorValueSend(*parentGenerator, makeInt(55), error), yielded)
            && yielded == 55 && error.code == ScriptDiagnosticCode::None,
        "Interpreter bridge forwards send() without replay");
    const Value completed = vm.resumeGeneratorValue(*parentGenerator, error);
    require(
        tryGetInt(completed, yielded) && yielded == 7 && error.code == ScriptDiagnosticCode::None,
        "Interpreter bridge returns the parent completion value");
    require(
        parentGenerator->done, "Interpreter bridge marks the parent done after child exhaustion");

    auto closeContext = arp::core::makeShared<DirectNativeExecutionContext>();
    closeContext->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    closeContext->lifetimeOwner = closeContext;
    closeContext->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 16u, 1u << 20u});
    Value closeChild =
        makeDirectNativeGeneratorV5(*closeContext, &crossBackendChildEntry, nullptr, 0u, &error);
    arp::core::pmr::Vector<Value> closeGlobals{&arp::core::defaultCpuMemoryResource()};
    closeGlobals.push_back(std::move(closeChild));
    Vm closeVm;
    closeVm.setSharedGlobals(&closeGlobals);
    Value closeParentValue = closeVm.runFunction(program, 1, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && closeParentValue.is<ScriptGenerator>(),
        "Interpreter creates a closeable parent for a DirectNative child");
    auto* closeParent = closeParentValue.ptr<ScriptGenerator>();
    require(tryGetInt(closeVm.resumeGeneratorValue(*closeParent, error), yielded) && yielded == 42
            && error.code == ScriptDiagnosticCode::None,
        "close fixture reaches the DirectNative child yield");
    closeVm.closeGenerator(*closeParent);
    auto* closedChild = asDirectNativeGeneratorV5(closeGlobals[0]);
    require(closedChild != nullptr && closedChild->state != nullptr && closedChild->state->done,
        "Interpreter close() forwards to the DirectNative child");

    auto throwContext = arp::core::makeShared<DirectNativeExecutionContext>();
    throwContext->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    throwContext->lifetimeOwner = throwContext;
    throwContext->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 16u, 1u << 20u});
    Value throwChild =
        makeDirectNativeGeneratorV5(*throwContext, &crossBackendChildEntry, nullptr, 0u, &error);
    arp::core::pmr::Vector<Value> throwGlobals{&arp::core::defaultCpuMemoryResource()};
    throwGlobals.push_back(std::move(throwChild));
    Program throwProgram;
    throwProgram.globalCount = 1;
    Function throwParent("parent_cross_backend_throw");
    throwParent.locals = 3;
    throwParent.maxStack = 1;
    throwParent.isGenerator = true;
    throwParent.hasYieldFrom = true;
    throwParent.code.emplace_back(Opcode::PushHandler, 11);
    throwParent.code.emplace_back(Opcode::LoadGlobal, 0);
    throwParent.code.emplace_back(Opcode::RangeInit, 0, 1);
    throwParent.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, 2));
    throwParent.code.emplace_back(Opcode::JumpIfFalse, 8);
    throwParent.code.emplace_back(Opcode::LoadLocal, 2);
    throwParent.code.emplace_back(Opcode::Yield);
    throwParent.code.emplace_back(Opcode::Jump, 3);
    throwParent.code.emplace_back(Opcode::PopHandler);
    throwParent.code.emplace_back(Opcode::LoadInt, 0, 0, 7);
    throwParent.code.emplace_back(Opcode::Return, 1);
    throwParent.code.emplace_back(Opcode::StoreLocal, 0);
    throwParent.code.emplace_back(Opcode::LoadInt, 0, 0, 8);
    throwParent.code.emplace_back(Opcode::Return, 1);
    throwParent.delegationSites.push_back(DelegationSiteMetadata{1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2});
    throwProgram.functions.push_back(std::move(throwParent));
    Function throwMaker("make_parent_cross_backend_throw");
    throwMaker.maxStack = 1;
    throwMaker.code.emplace_back(Opcode::Call, 0, 0);
    throwMaker.code.emplace_back(Opcode::Return, 1);
    throwProgram.functions.push_back(std::move(throwMaker));
    Vm throwVm;
    throwVm.setSharedGlobals(&throwGlobals);
    Value throwParentValue = throwVm.runFunction(throwProgram, 1, {}, &error);
    require(error.code == ScriptDiagnosticCode::None && throwParentValue.is<ScriptGenerator>(),
        "Interpreter creates a handler-bearing parent for a DirectNative child");
    auto* throwParentGenerator = throwParentValue.ptr<ScriptGenerator>();
    require(tryGetInt(throwVm.resumeGeneratorValue(*throwParentGenerator, error), yielded)
            && yielded == 42 && error.code == ScriptDiagnosticCode::None,
        "throw fixture reaches the DirectNative child yield");
    require(tryGetInt(throwVm.resumeGeneratorValueThrow(*throwParentGenerator,
                          makeString(std::string_view("cross-backend child error")),
                          error),
                yielded)
            && yielded == 8 && error.code == ScriptDiagnosticCode::None
            && throwParentGenerator->done,
        "Interpreter parent handler catches DirectNative child throw without replay");
}

} // namespace

static void testBorrowedStringSlots()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::core::pmr::String source("immutable program literal", &resource);
    auto literal = ExecutionSlot::borrowLiteral(source);
    std::string_view view;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(literal.stringView(view) && view == source,
        "literal view reads immutable bytes without materialization");
    const auto length = ScriptSemanticRuntime::lengthScalar(literal);
    auto moved = std::move(literal);
    require(length.ok() && length.value.integer() == static_cast<int>(source.size())
            && moved.stringView(view) && view == source
            && arp::core::defaultCpuAllocator().stats().allocationCount == allocations,
        "literal length and move do not allocate");
    auto alias = moved;
    alias.ptr<arp::core::pmr::String>()->append(" changed");
    require(moved.stringView(view) && view == "immutable program literal changed"
            && source == "immutable program literal",
        "copy establishes shared carrier and mutable pointer cannot change Program bytes");
    ExecutionSlotBuffer slots{&resource};
    slots.emplace_back(ExecutionSlot::borrowLiteral(source));
    const auto exported = slots.materialize();
    source.clear();
    require(*exported[0].ptr<arp::core::pmr::String>() == "immutable program literal",
        "snapshot materializes literal before source lifetime ends");
    Value host = makeString(std::string_view("abc"));
    ExecutionSlot imported(host);
    host.ptr<arp::core::pmr::String>()->append("def");
    require(imported.stringView(view) && view == "abcdef",
        "read-only string view observes host carrier mutations");
    const std::string_view texts[]{"", "alpha", "z", u8"中文", std::string_view("a\0b", 3)};
    for (auto lhs : texts)
        for (auto rhs : texts) {
            const bool expected[]{
                lhs == rhs, lhs != rhs, lhs<rhs, lhs <= rhs, lhs> rhs, lhs >= rhs};
            for (int op = 0; op != 6; ++op)
                require(ScriptSemanticRuntime::compareStrings(
                            static_cast<SemanticCompareOperation>(op), lhs, rhs)
                        == expected[op],
                    "string comparison preserves byte ordering for empty/UTF8/embedded-zero strings");
        }
}

template <class T> static void testScalarPointerAlias(T initial, T changed)
{
    const T immutable = initial;
    Value cvCarrier{immutable};
    ExecutionSlot cvSlot(cvCarrier);
    require(exactValuePayload<T>(cvCarrier) == nullptr
            && cvSlot.scalarPointer<T>() == cvCarrier.ptr<T>()
            && *cvSlot.scalarPointer<T>() == initial,
        "boxed scalar: const selector miss retains original pointer and payload semantics");
    require(cvSlot.ptr<T>() == cvCarrier.ptr<T>(),
        "boxed general pointer: const selector miss retains original address");
    Value carrier{initial};
    ExecutionSlot owned(carrier);
    require(owned.scalarPointer<T>() == carrier.ptr<T>(),
        "boxed scalar read retains exact carrier address");
    require(
        owned.ptr<T>() == carrier.ptr<T>(), "boxed general pointer: exact actual storage address");
    *carrier.ptr<T>() = changed;
    require(
        *owned.scalarPointer<T>() == changed, "boxed scalar read observes later host mutations");
    T host = initial;
    ExecutionSlot native(Value::native_reference(&host));
    require(native.scalarPointer<T>() == nullptr,
        "native reference retains its pointer type at scalar boundary");
    host = changed;
    require(native.ptr<T>() == &host,
        "boxed general pointer: native-control fallback retains host address");
    require(*native.boxed().ptr<T>() == changed && native.scalarPointer<T>() == nullptr,
        "native payload mutations remain visible without promoting the pointer carrier to a scalar");
    if constexpr (!std::is_same_v<T, bool>)
        require(owned.scalarPointer<bool>() == nullptr && native.scalarPointer<bool>() == nullptr,
            "imported scalar mismatched payload is rejected");
    else
        require(owned.scalarPointer<int>() == nullptr && native.scalarPointer<int>() == nullptr,
            "imported bool is not an int payload");
}

static void testActualScalarReads()
{
    const int cv = -17;
    const Value values[]{Value{-17},
        Value{std::int64_t(1) << 40},
        Value{-0.0},
        Value{1.25f},
        Value{true},
        Value{cv},
        Value{std::numeric_limits<double>::quiet_NaN()},
        makeNil(),
        makeString(std::string_view("wrong"))};
    // Warm only the original general metadata path before measuring reads.
    for (const auto& value : values) {
        (void)value.is<int>();
        (void)value.is<std::int64_t>();
        (void)value.is<double>();
        (void)value.is<float>();
        (void)value.is<bool>();
        (void)isNil(value);
        if (value.is<int>())
            (void)value.ptr<int>();
        if (value.is<std::int64_t>())
            (void)value.ptr<std::int64_t>();
        if (value.is<double>())
            (void)value.ptr<double>();
        if (value.is<float>())
            (void)value.ptr<float>();
        if (value.is<bool>())
            (void)value.ptr<bool>();
    }
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (const auto& value : values) {
        const auto identity = value.storageIdentity();
        ScriptScalar actual(123);
        const bool ok = ScriptScalar::read(value, actual);
        require(
            ok == !value.is<arp::core::pmr::String>(), "actual scalar read: type qualification");
        if (value.is<int>())
            require(actual.kind() == ScriptScalar::Kind::Int && actual.integer() == -17,
                "actual scalar read: int and const selector fallback");
        else if (value.is<std::int64_t>())
            require(actual.kind() == ScriptScalar::Kind::Int64
                    && actual.integer() == (std::int64_t(1) << 40),
                "actual scalar read: int64 width");
        else if (value.is<double>()) {
            const double original = *value.ptr<double>();
            require(actual.kind() == ScriptScalar::Kind::Float
                    && (std::isnan(original) ? std::isnan(actual.floating())
                                             : actual.floating() == original
                                && std::signbit(actual.floating()) == std::signbit(original)),
                "actual scalar read: IEEE NaN and negative zero");
        } else if (value.is<float>())
            require(actual.kind() == ScriptScalar::Kind::Float && actual.floating() == 1.25,
                "actual scalar read: float promotes to original double representation");
        else if (value.is<bool>())
            require(actual.kind() == ScriptScalar::Kind::Bool && actual.boolean(),
                "actual scalar read: bool remains distinct");
        else if (isNil(value))
            require(actual.kind() == ScriptScalar::Kind::Nil, "actual scalar read: nil");
        else
            require(actual.kind() == ScriptScalar::Kind::Int && actual.integer() == 123,
                "actual scalar read: rejected value does not alter output");
        require(value.storageIdentity() == identity, "actual scalar read: input carrier unchanged");
    }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == allocations,
        "actual scalar read: warm reads allocate nothing");
    int host = 11;
    Value native = Value::native_reference(&host);
    ScriptScalar rejected(123);
    require(!ScriptScalar::read(native, rejected) && rejected.integer() == 123,
        "actual scalar read: native pointer is not silently promoted");
    Value carrier{100000};
    ScriptScalar first, second;
    require(ScriptScalar::read(carrier, first), "actual scalar read: initial shared payload");
    *carrier.ptr<int>() = 200000;
    require(ScriptScalar::read(carrier, second) && first.integer() == 100000
            && second.integer() == 200000,
        "actual scalar read: read actual mutable payload each time");
}

static void testOwnedRuntimeAccessors()
{
    using arp::core::pmr::String;
    Value integer{100000}, wide{std::int64_t(1) << 40}, real{-0.0}, small{1.25f}, boolean{true};
    const int cvInteger = 7;
    Value cv{cvInteger};
    int n = 0;
    std::int64_t w = 0;
    double d = 0.0;
    bool b = false;
    require(tryGetInt(integer, n) && n == 100000 && tryGetInt(cv, n) && n == 7,
        "runtime accessor: actual/cv integer values and original selector miss");
    require(tryGetInt64(wide, w) && w == (std::int64_t(1) << 40), "runtime accessor: int64 width");
    require(tryGetFloat(real, d) && d == 0.0 && std::signbit(d),
        "runtime accessor: IEEE negative zero");
    require(tryGetFloat(small, d) && d == 1.25 && tryGetBool(boolean, b) && b,
        "runtime accessor: float promotion and bool exact access");
    Value nan{std::numeric_limits<double>::quiet_NaN()};
    require(tryGetFloat(nan, d) && std::isnan(d), "runtime accessor: IEEE NaN");
    Value converted{1.75};
    require(tryGetInt(converted, n) && n == converted.cast<int>(),
        "runtime accessor: numeric cast fallback retained");
    Value wrong = makeString(std::string_view("unconvertible"));
    n = 123;
    require(!tryGetInt(wrong, n) && n == 123, "runtime accessor: failed cast preserves output");
    int host = 17;
    Value native = Value::native_reference(&host);
    require(checkedOwnedValuePayload<int>(native) == nullptr && !tryGetInt(native, n) && n == 123,
        "owned payload: native primitive remains rejected, not silently dereferenced");
    Value text = makeString(std::string_view("owned"));
    Value shared = text;
    require(checkedOwnedValuePayload<String>(shared) == text.ptr<String>(),
        "owned payload: storage proof retains shared identity and does not claim uniqueness");
    const String cvText("const-selector", &arp::core::defaultCpuMemoryResource());
    Value cvCarrier{cvText};
    require(exactValuePayload<String>(cvCarrier) == nullptr
            && checkedOwnedValuePayload<String>(cvCarrier) == cvCarrier.ptr<String>(),
        "owned payload: cv selector miss preserves original address");
    Value bytes = makeBytes(std::string_view("abc"));
    Value generator{ScriptGenerator{}};
    Value awaitable{ScriptAwaitable{}};
    require(asScriptBytes(bytes) == bytes.ptr<ScriptBytes>() && isScriptGenerator(generator)
            && asScriptGenerator(generator) == generator.ptr<ScriptGenerator>()
            && isScriptAwaitable(awaitable)
            && asScriptAwaitable(awaitable) == awaitable.ptr<ScriptAwaitable>(),
        "runtime accessor: actual bytes/generator/awaitable addresses");
    require(!asScriptBytes(wrong) && !asScriptGenerator(wrong) && !asScriptAwaitable(wrong),
        "runtime accessor: wrong carriers rejected");
    // Warm the general cv/mismatch/native paths before checking allocation.
    (void)checkedOwnedValuePayload<int>(wrong);
    (void)checkedOwnedValuePayload<String>(native);
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    *integer.ptr<int>() = 200000;
    require(tryGetInt(integer, n) && n == 200000
            && checkedOwnedValuePayload<String>(shared) == text.ptr<String>()
            && checkedOwnedValuePayload<String>(cvCarrier) == cvCarrier.ptr<String>()
            && !checkedOwnedValuePayload<String>(native) && !checkedOwnedValuePayload<int>(wrong)
            && asScriptBytes(bytes) == bytes.ptr<ScriptBytes>()
            && asScriptGenerator(generator) == generator.ptr<ScriptGenerator>()
            && asScriptAwaitable(awaitable) == awaitable.ptr<ScriptAwaitable>(),
        "runtime accessor: actual host payload changes and stable object identity");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "runtime accessor: warm owned/cv/native/payload reads allocate nothing");
}

static void testCheckedValueCarriers()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hlist list{&resource};
    list.push_back(Value{100000});
    Value carrier{std::move(list)};
    const arp::reflection::hlist cvList{*carrier.ptr<arp::reflection::hlist>()};
    Value cvCarrier{cvList};
    require(exactValuePayload<arp::reflection::hlist>(cvCarrier) == nullptr
            && checkedValuePayload<arp::reflection::hlist>(cvCarrier)
                == cvCarrier.ptr<arp::reflection::hlist>(),
        "checked payload: cv collection selector miss keeps original address");
    require(checkedValuePayload<arp::reflection::hlist>(carrier)
            == carrier.ptr<arp::reflection::hlist>(),
        "checked payload: actual owned collection address");
    Value native = Value::native_reference(carrier.ptr<arp::reflection::hlist>());
    require(checkedValuePayload<arp::reflection::hlist>(native) == nullptr
            && !native.is<arp::reflection::hlist>(),
        "checked payload: native pointer carrier is not a collection");
    Value text = makeString(std::string_view("first"));
    ExecutionSlot stringSlot(text);
    Value closure{ScriptClosure{3, {}}};
    ExecutionSlot closureSlot(closure);
    Value method{ScriptMethod{4, carrier}};
    ExecutionSlot methodSlot(method);
    require(closureSlot.closure() == closure.ptr<ScriptClosure>()
            && methodSlot.method() == method.ptr<ScriptMethod>(),
        "checked callable: actual address retained");
    require(closureSlot.method() == nullptr && methodSlot.closure() == nullptr,
        "checked callable: mismatched payload rejected");
    auto literal = ExecutionSlot::borrowLiteral(*text.ptr<arp::core::pmr::String>());
    auto inlineMethod = ExecutionSlot(ScriptMethod{5, carrier});
    // Warm only the original general pointer/type paths used by misses.
    (void)text.is<ScriptClosure>();
    (void)text.is<ScriptMethod>();
    (void)text.ptr<arp::core::pmr::String>();
    (void)closure.ptr<ScriptClosure>();
    (void)method.ptr<ScriptMethod>();
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    *text.ptr<arp::core::pmr::String>() = "second";
    closure.ptr<ScriptClosure>()->funcIndex = 7;
    method.ptr<ScriptMethod>()->funcIndex = 8;
    std::string_view view;
    require(stringSlot.stringView(view) && view == "second" && closureSlot.closure()->funcIndex == 7
            && methodSlot.method()->funcIndex == 8,
        "checked payload: observes current actual string/callable payload");
    require(literal.closure() == nullptr && literal.method() == nullptr
            && inlineMethod.closure() == nullptr,
        "checked callable: wrong literal/inline-method never materializes");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == allocations,
        "checked payload: warmed reads and nonboxed rejection allocate nothing");
    require(ScriptSemanticRuntime::rangeNextCollectionSlots(cvCarrier, 0)
                .value.boxed()
                .storageIdentity()
            == cvCarrier.ptr<arp::reflection::hlist>()->front().storageIdentity(),
        "checked collection: cv fallback preserves actual element carrier");
    require(ScriptSemanticRuntime::rangeNextCollectionSlots(native, 0).diagnostic.code
            == ScriptDiagnosticCode::BadCast,
        "checked collection: native rejection keeps original error");
    require(
        ScriptSemanticRuntime::rangeNextCollectionStep(cvCarrier, 0).value.boxed().storageIdentity()
                == cvCarrier.ptr<arp::reflection::hlist>()->front().storageIdentity()
            && ScriptSemanticRuntime::rangeNextCollectionStep(native, 0).code
                == ScriptDiagnosticCode::BadCast,
        "collection step: cv carrier identity and native failure match the original slot ABI");
}

static void testInlineMethodSlots()
{
    Value receiver{100000};
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    ExecutionSlot method(ScriptMethod{3, receiver});
    auto moved = std::move(method);
    require(moved.method() != nullptr && moved.method()->funcIndex == 3
            && arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "ephemeral bound method does not allocate a wrapper");
    auto alias = moved;
    alias.ptr<ScriptMethod>()->funcIndex = 5;
    require(moved.method()->funcIndex == 5,
        "copied bound methods materialize a shared mutable carrier");
    *receiver.ptr<int>() = 200000;
    require(
        *moved.method()->receiver.ptr<int>() == 200000, "inline method preserves receiver alias");
}

static void testIntegerValueRangeBuilder()
{
    const int cacheEnd = static_cast<int>(std::numeric_limits<std::uint16_t>::max()) + 1;

    struct Bounds {
        int begin, end;
    };

    for (const auto bounds : {Bounds{0, 0},
             Bounds{3, -3},
             Bounds{-3, 4},
             Bounds{cacheEnd - 3, cacheEnd + 3},
             Bounds{std::numeric_limits<int>::min(), std::numeric_limits<int>::min() + 3},
             Bounds{std::numeric_limits<int>::max() - 3, std::numeric_limits<int>::max()}}) {
        const auto count = static_cast<std::int64_t>(bounds.end) - bounds.begin;
        auto range = ScriptSemanticRuntime::makeRange(bounds.begin, bounds.end, 8);
        if (!range.ok())
            std::fprintf(stderr,
                "range fixture [%d,%d): %s\n",
                bounds.begin,
                bounds.end,
                range.diagnostic.message.c_str());
        require(range.ok(),
            "integer range builder: empty/reversed/negative/cache boundary/int extremes");
        const auto* list = range.value.ptr<arp::reflection::hlist>();
        require(list && list->size() == static_cast<std::size_t>(std::max<std::int64_t>(count, 0)),
            "integer range builder: actual public list and exact size");
        for (std::size_t i = 0; i < list->size(); ++i) {
            const int value = static_cast<int>(static_cast<std::int64_t>(bounds.begin) + i);
            const auto expected = makeInt(value);
            require(*(*list)[i].ptr<int>() == *expected.ptr<int>(),
                "integer range builder: original ascending payload construction");
            if (intValueUsesCache(value))
                require((*list)[i].storageIdentity() == expected.storageIdentity(),
                    "integer range builder: real cached carrier identity");
        }
    }
    require(ScriptSemanticRuntime::makeRange(0, 7, 6).diagnostic.code
            == ScriptDiagnosticCode::ResourceLimitExceeded,
        "integer range builder: count limit is checked before materialization");
    Value cacheAlias = makeInt(31);
    const int prior = *cacheAlias.ptr<int>();
    *cacheAlias.ptr<int>() = -71;
    auto changed = ScriptSemanticRuntime::makeRange(30, 33, 3);
    require(changed.ok()
            && changed.value.ptr<arp::reflection::hlist>()->at(1).storageIdentity()
                == cacheAlias.storageIdentity()
            && *changed.value.ptr<arp::reflection::hlist>()->at(1).ptr<int>() == -71,
        "integer range builder: altered live cached payload is never regenerated or assumed from index");
    *cacheAlias.ptr<int>() = prior;
    for (int i = 0; i < 4; ++i)
        (void)makeInt(i);
    auto destination = arp::reflection::makeEmptyHList();
    destination.reserve(5);
    Value prefix{100000};
    destination.push_back(prefix);
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    IntegerValueBuilder::appendReservedRange(destination, 0, 4);
    require(destination.size() == 5
            && destination.front().storageIdentity() == prefix.storageIdentity()
            && arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "integer range builder: reusable reserved append preserves prefix and warms without allocations");

    // Choose an actually cold cache entry, rather than assuming test order.
    int cold = cacheEnd - 1;
    while (cold >= 0 && findCachedIntValue(cold) != nullptr)
        --cold;
    require(cold >= 0, "integer range builder: cold cache fixture is available");
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u);
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        auto range = ScriptSemanticRuntime::makeRange(cold, cold + 1, 1);
        const auto* list = range.value.ptr<arp::reflection::hlist>();
        const auto cached = arp::core::inspectCpuAllocationCharge(findCachedIntValue(cold));
        const auto elements = arp::core::inspectCpuAllocationCharge(list->data());
        require(range.ok() && cached.found && cached.quota == nullptr && elements.found
                && elements.quota == quota.get() && quota->liveBytes() > 0,
            "integer range builder: cold thread cache is uncharged, actual result list is charged");
    }
    require(quota->liveBytes() == 0,
        "integer range builder: destroyed result releases script quota but thread cache survives");
    quota->setLimitBytes(1);
    bool rejected = false;
    try {
        arp::core::CpuMemoryQuotaScope scope(quota);
        (void)ScriptSemanticRuntime::makeRange(0, 4, 4);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    require(rejected && quota->liveBytes() == 0,
        "integer range builder: reserve failure keeps original quota boundary and releases charges");
}

static void testIntegerValueCacheBorrow()
{
    const auto cache = IntegerValueCacheBorrow::current();
    Value alias = makeInt(37);
    const int prior = *alias.ptr<int>();
    ExecutionSlot source(ScriptScalar(37));
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    *alias.ptr<int>() = -87;
    Value current = cache.makeCached(37);
    require(current.storageIdentity() == alias.storageIdentity() && *current.ptr<int>() == -87
            && source.boxed(cache).storageIdentity() == alias.storageIdentity()
            && arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "integer cache borrow: reads real live carrier and boxes at original boundary without allocation");
    *alias.ptr<int>() = prior;
    int cold = std::numeric_limits<std::uint16_t>::max();
    while (cold >= 0 && findCachedIntValue(cold))
        --cold;
    require(cold >= 0, "integer cache borrow: actual cold entry available");
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1);
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        Value materialized = cache.makeCached(cold);
        const auto charge = arp::core::inspectCpuAllocationCharge(findCachedIntValue(cold));
        require(charge.found && charge.quota == nullptr && quota->liveBytes() == 0
                && materialized.storageIdentity() == makeInt(cold).storageIdentity()
                && cache.makeCached(cold).storageIdentity() == materialized.storageIdentity(),
            "integer cache borrow: cold miss retains uncharged makeInt ownership and sees newly published entry");
    }
    const int cv = 900000;
    int native = 800000;
    ExecutionSlot imported[]{ExecutionSlot(Value{cv}),
        ExecutionSlot(Value::native_reference(&native)),
        ExecutionSlot(ScriptScalar(-3)),
        ExecutionSlot(ScriptScalar(std::int64_t(4)))};
    for (auto& slot : imported) {
        const Value& original = slot.boxed();
        require(slot.boxed(cache).storageIdentity() == original.storageIdentity(),
            "integer cache borrow: native/cv/noncache/wide boxing preserves original carrier");
    }
    // Each thread binds its own view; a parent view is never passed across the
    // boundary. Keep the parent carrier alive to prove simultaneous isolation.
    bool isolated = false;
    std::thread worker([&] {
        const auto local = IntegerValueCacheBorrow::current();
        Value value = local.makeCached(37);
        isolated = value.storageIdentity() != alias.storageIdentity() && *value.ptr<int>() == 37;
        *value.ptr<int>() = -99;
        require(*local.makeCached(37).ptr<int>() == -99,
            "integer cache borrow: worker view reads its own actual modified payload");
    });
    worker.join();
    require(isolated && *cache.makeCached(37).ptr<int>() == prior,
        "integer cache borrow: thread ownership and live parent payload remain independent");
}

static void testOwnedCollectionGeneratorExclusion()
{
    Vm vm;
    DirectNativeExecutionContext context;
    auto list = arp::reflection::makeEmptyHList();
    Value aliased{100000};
    list.push_back(aliased);
    arp::reflection::hdict dictionary{&arp::core::defaultCpuMemoryResource()};
    dictionary.emplace(makeInt(0), aliased);
    ScriptBytes bytes;
    bytes.data.push_back(3);
    const auto cvList = list;
    const ScriptGenerator cvGenerator;
    ScriptGenerator nativeGenerator;
    DirectNativeGeneratorV5 nativeDirect;
    Value values[]{Value{list},
        Value{dictionary},
        Value{bytes},
        Value{cvList},
        Value::native_reference(&list),
        Value{ScriptGenerator{}},
        Value{cvGenerator},
        Value::native_reference(&nativeGenerator),
        Value{DirectNativeGeneratorV5{}},
        Value::native_reference(&nativeDirect),
        Value{29},
        Value::undefined()};
    for (auto& value : values) {
        const auto identity = value.storageIdentity();
        require(isScriptGenerator(value) == value.is<ScriptGenerator>()
                && isDirectNativeGeneratorV5(value) == value.is<DirectNativeGeneratorV5>()
                && value.storageIdentity() == identity,
            "generator exclusion: owned/cv/native/other predicates preserve results and carrier identity");
    }
    require(hasExactOwnedCollectionPayload(values[0]) && hasExactOwnedCollectionPayload(values[1])
            && hasExactOwnedCollectionPayload(values[2])
            && !hasExactOwnedCollectionPayload(values[3])
            && !hasExactOwnedCollectionPayload(values[4])
            && !hasExactOwnedCollectionPayload(values[5]),
        "generator exclusion: only actual exact owned collection storage is proved");
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    *aliased.ptr<int>() = 100001;
    for (const auto& value : values) {
        require(isScriptGenerator(value) == value.is<ScriptGenerator>()
                && isDirectNativeGeneratorV5(value) == value.is<DirectNativeGeneratorV5>(),
            "generator exclusion: repeated reads retain actual payload predicates");
    }
    require(values[0].ptr<arp::reflection::hlist>()->front().storageIdentity()
                == aliased.storageIdentity()
            && *values[0].ptr<arp::reflection::hlist>()->front().ptr<int>() == 100001
            && arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "generator exclusion: live shared payload and warmed allocation-free reads");
}

static void testScriptCallableAccess()
{
    // VM construction registers both callable types before execution.
    Vm vm;
    auto& resource = arp::core::defaultCpuMemoryResource();
    Value receiver{100000};
    ScriptMethod hostMethod{7, receiver};
    ScriptClosure hostClosure{11, {}};
    hostClosure.captures.push_back(arp::core::makeShared<ScriptCell>(Value{19}));
    int moduleToken = 1;
    hostClosure.aotModule = &moduleToken;
    const ScriptClosure cvClosure(hostClosure);
    const ScriptMethod cvMethod(hostMethod);
    arp::core::pmr::String text("callee literal", &resource);
    ExecutionSlot slots[]{
        ExecutionSlot(ScriptScalar(23)),
        ExecutionSlot::borrowLiteral(text),
        ExecutionSlot(ScriptMethod{7, receiver}),
        ExecutionSlot(Value{hostMethod}),
        ExecutionSlot(Value{hostClosure}),
        ExecutionSlot(Value{cvClosure}),
        ExecutionSlot(Value{cvMethod}),
        ExecutionSlot(Value::native_reference(&hostClosure)),
        ExecutionSlot(Value::native_reference(&hostMethod)),
        ExecutionSlot(Value{29}),
        ExecutionSlot(Value::undefined()),
    };
    // Warm legacy cv/native fallback metadata before observing allocations.
    for (const auto& slot : slots) {
        (void)slot.method();
        (void)slot.closure();
    }
    Value replacement{31};
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (auto& slot : slots) {
        const auto* expectedMethod = slot.method();
        const auto* expectedClosure = slot.closure();
        const ScriptMethod* method = &hostMethod;
        const ScriptClosure* closure = &hostClosure;
        slot.scriptCallable(method, closure);
        require(method == expectedMethod && closure == expectedClosure,
            "callable access: actual owned/native/cv/inline/literal predicates preserve original pointers");
    }
    require(slots[1].immutableLiteral() == &text && slots[0].inlineScalar(),
        "callable access: a rejected slot is not materialized");
    const ScriptMethod* method;
    const ScriptClosure* closure;
    slots[4].scriptCallable(method, closure);
    require(!method && closure && closure->aotModule == &moduleToken
            && closure->captures[0] == hostClosure.captures[0],
        "callable access: foreign owner flags and actual capture identity remain authoritative");
    slots[4].boxed().ptr<ScriptClosure>()->funcIndex = 17;
    hostClosure.captures[0]->value = std::move(replacement);
    slots[4].scriptCallable(method, closure);
    require(closure && closure->funcIndex == 17 && *closure->captures[0]->value.ptr<int>() == 31,
        "callable access: later callee and shared-cell payload changes are read live");
    slots[2].scriptCallable(method, closure);
    require(method && !closure && method->receiver.storageIdentity() == receiver.storageIdentity(),
        "callable access: receiver carrier lifetime and identity remain intact");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "callable access: all reads are allocation-free and preserve carriers");
}

static void testSlotLifetimeTransitions()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    const arp::core::pmr::String text("stable literal", &resource);
    Value receiver{100000};
    auto makeSlot = [&](int kind) {
        if (kind == 0)
            return ExecutionSlot(ScriptScalar(100001));
        if (kind == 1)
            return ExecutionSlot(receiver);
        if (kind == 2)
            return ExecutionSlot::borrowLiteral(text);
        return ExecutionSlot(ScriptMethod{7, receiver});
    };
    for (int oldKind = 0; oldKind != 4; ++oldKind) {
        for (int newKind = 0; newKind != 4; ++newKind) {
            auto source = makeSlot(newKind);
            auto destination = makeSlot(oldKind);
            destination = std::move(source);
            auto alias = destination;
            if (newKind == 0) {
                *alias.ptr<int>() = 100003;
                require(*destination.ptr<int>() == 100001,
                    "inline scalar copies remain independent values");
                (void)destination.boxed();
                alias = destination;
            }
            require(destination.boxed().storageIdentity() == alias.boxed().storageIdentity(),
                "slot transition and copy preserve materialized carrier identity");
            destination = destination;
            destination = std::move(destination);
            destination = destination.boxed();
            require(destination.boxed().storageIdentity() == alias.boxed().storageIdentity(),
                "slot self assignment and own boxed assignment preserve carrier");
            destination = ScriptScalar(100002);
            require(*destination.ptr<int>() == 100002,
                "each owned slot can transition back to an inline scalar");
            auto moved = makeSlot(oldKind);
            moved = std::move(destination);
            require(*moved.ptr<int>() == 100002, "scalar move assignment across all slot kinds");
        }
    }
    auto method = makeSlot(3);
    auto literal = makeSlot(2);
    const auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    for (auto* slot : {&method, &literal}) {
        bool rejected = false;
        try {
            arp::core::CpuMemoryQuotaScope scope(quota);
            (void)slot->boxed();
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
        require(rejected, "slot materialization obeys core quota");
    }
    std::string_view view;
    require(method.method() != nullptr && method.method()->funcIndex == 7
            && method.method()->receiver.storageIdentity() == receiver.storageIdentity()
            && literal.stringView(view) && view == text,
        "failed materialization keeps method receiver and literal intact");
}

static void testInlineCollectionIteration()
{
    using namespace arp::scriptvm;
    Value element{73};
    auto list = arp::reflection::makeEmptyHList();
    list.emplace_back(element);
    arp::reflection::hdict dictionary{&arp::core::defaultCpuMemoryResource()};
    Value key{19};
    dictionary.emplace(key, element);
    ScriptBytes bytes;
    bytes.data.resize(65538, 255);
    const Value sources[]{
        Value{std::move(list)}, Value{std::move(dictionary)}, Value{std::move(bytes)}, Value{42}};
    for (const auto& source : sources) {
        for (int index : {-1, 0, 1, 65537, 65538}) {
            auto boxed = ScriptSemanticRuntime::rangeNextCollection(source, index);
            auto slots = ScriptSemanticRuntime::rangeNextCollectionSlots(source, index);
            auto step = ScriptSemanticRuntime::rangeNextCollectionStep(source, index);
            require(boxed.hasMore == slots.hasMore && boxed.diagnostic.code == slots.diagnostic.code
                    && boxed.diagnostic.message == slots.diagnostic.message
                    && step.hasMore == slots.hasMore && step.code == slots.diagnostic.code,
                "boxed and inline collection traversal agree on bounds and diagnostics");
            if (boxed.hasMore) {
                require(*boxed.key.ptr<int>() == *slots.key.ptr<int>()
                        && *boxed.value.ptr<int>() == *slots.value.ptr<int>()
                        && *step.key.ptr<int>() == *slots.key.ptr<int>()
                        && *step.value.ptr<int>() == *slots.value.ptr<int>(),
                    "boxed and inline collection traversal agree on key/value");
            }
        }
    }
    auto item = ScriptSemanticRuntime::rangeNextCollectionSlots(sources[0], 0);
    auto entry = ScriptSemanticRuntime::rangeNextCollectionSlots(sources[1], 0);
    auto listStep = ScriptSemanticRuntime::rangeNextCollectionStep(sources[0], 0);
    auto dictionaryStep = ScriptSemanticRuntime::rangeNextCollectionStep(sources[1], 0);
    *element.ptr<int>() = 81;
    *key.ptr<int>() = 20;
    require(*item.value.ptr<int>() == 81 && *entry.value.ptr<int>() == 81
            && *entry.key.ptr<int>() == 20,
        "collection slots retain element and dictionary key carriers");
    require(listStep.value.boxed().storageIdentity() == element.storageIdentity()
            && dictionaryStep.value.boxed().storageIdentity() == element.storageIdentity()
            && dictionaryStep.key.boxed().storageIdentity() == key.storageIdentity()
            && *listStep.value.ptr<int>() == 81 && *dictionaryStep.key.ptr<int>() == 20,
        "collection step: published values retain live aliases and actual key/value identity");
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int pass = 0; pass != 100; ++pass) {
        auto byte = ScriptSemanticRuntime::rangeNextCollectionSlots(sources[2], 65537);
        auto step = ScriptSemanticRuntime::rangeNextCollectionStep(sources[2], 65537);
        require(byte.ok() && byte.hasMore && *byte.key.ptr<int>() == 65537
                && *byte.value.ptr<int>() == 255 && step.ok() && step.hasMore
                && *step.key.ptr<int>() == 65537 && *step.value.ptr<int>() == 255,
            "large collection index and byte stay inline");
    }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "inline iteration does not allocate even beyond the integer cache");
    {
        auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
        arp::core::CpuMemoryQuotaScope scope(quota);
        const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
        for (const auto& source : sources) {
            for (int index : {-1, 0, 65537, 65538}) {
                const auto step = ScriptSemanticRuntime::rangeNextCollectionStep(source, index);
                require(step.code
                        == (source.is<int>() ? ScriptDiagnosticCode::BadCast
                                             : ScriptDiagnosticCode::None),
                    "collection step: status requires no diagnostic allocation even on a miss or bad source");
            }
        }
        require(quota->liveBytes() == 0u
                && arp::core::defaultCpuAllocator().stats().allocationCount == allocations,
            "collection step: no allocation or quota side effect for success, end or failure");
    }

    Program program;
    auto& function = program.functions.emplace_back("overflow");
    function.locals = 2;
    function.maxStack = 1;
    function.code.emplace_back(Opcode::LoadInt, 0, 0, 0);
    function.code.emplace_back(Opcode::MakeList, 0, 1);
    function.code.emplace_back(Opcode::RangeInit, 0, 1);
    function.code.emplace_back(Opcode::LoadInt, 0, 0, std::numeric_limits<int>::max());
    function.code.emplace_back(Opcode::StoreLocal, 1);
    function.code.emplace_back(Opcode::RangeNext, 0, 1, packRangeTargets(-1, -1));
    function.code.emplace_back(Opcode::Return, 1);
    Vm vm;
    VmError error;
    arp::core::pmr::Vector<Value> args{&arp::core::defaultCpuMemoryResource()};
    vm.runFunction(program, 0, args, &error);
    require(error.code == ScriptDiagnosticCode::IntegerOverflow,
        "range index overflow rejects before signed increment");
}

static void testInlineCollectionIndex()
{
    Value element{73};
    auto list = arp::reflection::makeEmptyHList();
    list.resize(100001, element);
    ScriptBytes bytes;
    bytes.data.resize(100001, 255);
    const Value sources[]{Value{std::move(list)}, Value{std::move(bytes)}, Value{42}};
    const Value indices[]{Value{-1},
        Value{0},
        Value{100000},
        Value{100001},
        Value{true},
        Value{0.5},
        Value{std::int64_t(100000)},
        makeString(std::string_view("0")),
        makeString(std::string_view("invalid")),
        makeNil()};
    for (const auto& source : sources) {
        for (const auto& index : indices) {
            auto boxed = ScriptSemanticRuntime::loadIndex(source, index);
            for (bool inlineScalar : {false, true}) {
                ExecutionSlot slot(index);
                ScriptScalar scalar;
                if (inlineScalar && ScriptScalar::read(index, scalar))
                    slot = scalar;
                auto inlineResult = ScriptSemanticRuntime::loadIndexSlots(source, slot);
                require(boxed.diagnostic.code == inlineResult.diagnostic.code
                        && boxed.diagnostic.message == inlineResult.diagnostic.message,
                    "slot indexing preserves conversion, bounds and error semantics");
                if (boxed.ok()) {
                    int a = 0, b = 0;
                    require(tryGetInt(boxed.value, a) && tryGetInt(inlineResult.value.boxed(), b)
                            && a == b,
                        "slot indexing returns the same converted element");
                }
            }
        }
    }
    ExecutionSlot index(ScriptScalar(100000));
    auto result = ScriptSemanticRuntime::loadIndexSlots(sources[0], index);
    require(result.ok() && result.value.boxed().storageIdentity() == element.storageIdentity(),
        "list indexing keeps the exact mutable element carrier");
    *element.ptr<int>() = 81;
    require(*result.value.scalarPointer<int>() == 81, "list indexing retains host alias mutations");
    auto& allocator = arp::core::defaultCpuAllocator();
    const auto before = allocator.stats().allocationCount;
    for (int pass = 0; pass != 100; ++pass) {
        auto item = ScriptSemanticRuntime::loadIndexSlots(sources[0], index);
        auto byte = ScriptSemanticRuntime::loadIndexSlots(sources[1], index);
        require(item.ok() && byte.ok() && *item.value.scalarPointer<int>() == 81
                && *byte.value.scalarPointer<int>() == 255,
            "cache-external index and byte stay inline");
    }
    require(allocator.stats().allocationCount == before,
        "slot list/bytes indexing allocates no carriers");
    const auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        auto byte = ScriptSemanticRuntime::loadIndexSlots(sources[1], index);
        require(byte.ok() && *byte.value.scalarPointer<int>() == 255,
            "inline byte indexing needs no allocation under an exhausted allocation quota");
    }
    arp::reflection::hdict dictionary{&arp::core::defaultCpuMemoryResource()};
    dictionary.emplace(Value{100000}, element);
    Value dict{std::move(dictionary)};
    auto found = ScriptSemanticRuntime::loadIndexSlots(dict, index);
    require(found.ok() && found.value.boxed().storageIdentity() == element.storageIdentity(),
        "dictionary slot lookup uses the existing map and preserves value aliases");
    auto missing = ScriptSemanticRuntime::loadIndexSlots(dict, ExecutionSlot(ScriptScalar(100001)));
    require(missing.ok() && missing.value.is<void>(), "missing dictionary key remains nil");
    Value mutableIndex{0};
    ExecutionSlot imported(mutableIndex);
    *mutableIndex.ptr<int>() = 100001;
    require(ScriptSemanticRuntime::loadIndexSlots(sources[0], imported).diagnostic.code
            == ScriptDiagnosticCode::IndexError,
        "index read observes mutable host scalar changes");
}

static void testDirectContainerConstruction()
{
    using namespace arp::scriptvm;
    auto& resource = arp::core::defaultCpuMemoryResource();
    Value alias{73};
    arp::core::pmr::Vector<Value> warm{&resource};
    (void)ScriptSemanticRuntime::makeList(warm, 0, 2);
    ExecutionSlotBuffer slots{&resource};
    slots.reserve(5);
    slots.emplace_back(ScriptScalar(999999));
    slots.emplace_back(alias);
    slots.emplace_back(ScriptScalar(100000));
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    const auto list = ScriptSemanticRuntime::makeList(slots, 2, 2);
    require(list.ok() && slots.size() == 1 && *slots[0].ptr<int>() == 999999,
        "direct list preserves unconsumed stack prefix");
    require(arp::core::defaultCpuAllocator().stats().allocationCount - before == 4,
        "direct list allocates buffer/carrier and one scalar, no scratch or placeholder values");
    auto* values = list.value.ptr<arp::reflection::hlist>();
    require(values->size() == 2 && *(*values)[1].ptr<int>() == 100000,
        "direct list keeps source order");
    *alias.ptr<int>() = 81;
    require(*(*values)[0].ptr<int>() == 81, "direct list preserves imported carrier aliases");
    const auto prefixAllocation = arp::core::defaultCpuAllocator().stats().allocationCount;
    (void)slots[0].boxed();
    require(arp::core::defaultCpuAllocator().stats().allocationCount == prefixAllocation + 1,
        "direct construction leaves prefix scalar inline");

    arp::core::pmr::Vector<Value> boxed{&resource};
    slots.clear();
    for (int item : {4, 11, 4, 22}) {
        slots.emplace_back(ScriptScalar(item));
        boxed.emplace_back(makeInt(item));
    }
    const auto inlineDict = ScriptSemanticRuntime::makeDict(slots, 2, 2);
    const auto boxedDict = ScriptSemanticRuntime::makeDict(boxed, 2, 2);
    const auto key = makeInt(4);
    require(inlineDict.ok() && boxedDict.ok() && slots.empty() && boxed.empty()
            && inlineDict.value.ptr<arp::reflection::hdict>()->size() == 1
            && *inlineDict.value.ptr<arp::reflection::hdict>()->at(key).ptr<int>() == 11
            && *boxedDict.value.ptr<arp::reflection::hdict>()->at(key).ptr<int>() == 11,
        "boxed and inline dicts preserve reverse duplicate-key precedence");
    slots.emplace_back(ScriptScalar(1));
    require(ScriptSemanticRuntime::makeList(slots, 2, 1).diagnostic.code
                == ScriptDiagnosticCode::ResourceLimitExceeded
            && slots.size() == 1,
        "container limit rejects before consuming operands");
    require(ScriptSemanticRuntime::makeDict(slots, 1, 1).diagnostic.code
                == ScriptDiagnosticCode::StackUnderflow
            && slots.size() == 1,
        "dict underflow does not consume prefix");
    require(ScriptSemanticRuntime::makeList(slots, -1, 1).diagnostic.code
                == ScriptDiagnosticCode::ResourceLimitExceeded
            && slots.size() == 1,
        "negative container count rejects");
    const auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    bool quotaRejected = false;
    try {
        arp::core::CpuMemoryQuotaScope scope(quota);
        (void)ScriptSemanticRuntime::makeList(slots, 1, 1);
    } catch (const std::bad_alloc&) {
        quotaRejected = true;
    }
    require(quotaRejected && slots.size() == 1,
        "direct container buffers still obey core memory quota");
}

void testScalarSpecializations()
{
    using Binary = SemanticBinaryOperation;
    using Compare = SemanticCompareOperation;
    {
        Frame frame;
        Value host{100000};
        frame.push(ScriptScalar(19));
        frame.push(host);
        frame.push(host);
        ScriptScalar lhs, rhs;
        require(frame.peekScalarPair(lhs, rhs), "pair replacement reads imported alias operands");
        frame.replaceScalarPair(ScriptSemanticRuntime::intBinary<Binary::Add>(
            static_cast<int>(lhs.integer()), static_cast<int>(rhs.integer()))
                .value);
        require(frame.stack.size() == 2 && *frame.stack[0].scalarPointer<int>() == 19
                && *frame.stack[1].scalarPointer<int>() == 200000 && *host.ptr<int>() == 100000,
            "pair replacement preserves prefix and host aliases");
        *host.ptr<int>() = 7;
        require(
            *frame.stack[1].scalarPointer<int>() == 200000, "pair result is an independent scalar");
    }
    const auto arithmetic = [](auto operation, double expected) {
        constexpr auto op = decltype(operation)::value;
        ScalarOperationResult result;
        require(ScriptSemanticRuntime::scalarBinary<op>(ScriptScalar(-7.5), ScriptScalar(2), result)
                && result.ok() && result.value.kind() == ScriptScalar::Kind::Float
                && result.value.floating() == expected,
            "specialized mixed float mathematical result");
        require(ScriptSemanticRuntime::scalarBinary<op>(ScriptScalar(true), ScriptScalar(2), result)
                == false,
            "bool arithmetic keeps dynamic ABI path");
    };
    arithmetic(std::integral_constant<Binary, Binary::Add>{}, -5.5);
    arithmetic(std::integral_constant<Binary, Binary::Sub>{}, -9.5);
    arithmetic(std::integral_constant<Binary, Binary::Mul>{}, -15.0);
    arithmetic(std::integral_constant<Binary, Binary::Div>{}, -3.75);
    arithmetic(std::integral_constant<Binary, Binary::Mod>{}, -1.5);
    require(ScriptSemanticRuntime::floatBinary<Binary::Div>(1.0, -0.0).code
            == ScriptDiagnosticCode::DivisionByZero,
        "negative zero divisor is checked");
    require(ScriptSemanticRuntime::floatBinary<Binary::Mod>(1.0, 0.0).code
            == ScriptDiagnosticCode::DivisionByZero,
        "float modulo zero is checked");
    const auto negativeZero = ScriptSemanticRuntime::floatBinary<Binary::Mul>(-0.0, 2.0);
    require(
        negativeZero.ok() && std::signbit(negativeZero.value.floating()), "signed zero preserved");
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    require(
        std::isnan(
            ScriptSemanticRuntime::floatBinary<Binary::Sub>(infinity, infinity).value.floating()),
        "IEEE NaN arithmetic preserved");
    require(
        ScriptSemanticRuntime::floatBinary<Binary::Div>(infinity, 2.0).value.floating() == infinity,
        "IEEE infinity preserved");
    ScalarOperationResult wide;
    require(ScriptSemanticRuntime::scalarBinary<Binary::Add>(
                ScriptScalar(std::int64_t(1) << 54), ScriptScalar(1), wide)
            && wide.ok() && wide.value.integer() == (std::int64_t(1) << 54) + 1
            && wide.value.kind() == ScriptScalar::Kind::Int64,
        "mixed width integers retain exact precision");
    require(ScriptSemanticRuntime::scalarBinary<Binary::Div>(
                ScriptScalar(std::numeric_limits<std::int64_t>::min()),
                ScriptScalar(std::int64_t(-1)),
                wide)
            && wide.code == ScriptDiagnosticCode::IntegerOverflow,
        "specialized entry retains wide division overflow");
    const auto comparison = [nan, infinity](auto operation) {
        constexpr auto op = decltype(operation)::value;
        bool result = false;
        // Independent expected values, including unordered IEEE comparisons.
        const bool less =
            op == Compare::NotEqual || op == Compare::Less || op == Compare::LessEqual;
        require(ScriptSemanticRuntime::scalarCompare<op>(ScriptScalar(std::int64_t(1) << 54),
                    ScriptScalar((std::int64_t(1) << 54) + 1),
                    result)
                && result == less,
            "wide comparison does not round to double");
        require(ScriptSemanticRuntime::scalarCompare<op>(
                    ScriptScalar(nan), ScriptScalar(infinity), result)
                && result == (op == Compare::NotEqual),
            "NaN comparison contract");
        const bool equal =
            op == Compare::Equal || op == Compare::LessEqual || op == Compare::GreaterEqual;
        require(
            ScriptSemanticRuntime::scalarCompare<op>(ScriptScalar(-0.0), ScriptScalar(0), result)
                && result == equal,
            "mixed signed zero comparison contract");
        require(ScriptSemanticRuntime::scalarCompare<op>(ScriptScalar(), ScriptScalar(1), result)
                && result == (op == Compare::NotEqual),
            "nil ordering remains false");
        require(ScriptSemanticRuntime::scalarCompare<op>(
                    ScriptScalar(false), ScriptScalar(true), result)
                && result == less,
            "bool comparison contract");
        require(
            !ScriptSemanticRuntime::scalarCompare<op>(ScriptScalar(true), ScriptScalar(1), result),
            "bool numeric comparison retains dynamic semantic boundary");
        require(ScriptSemanticRuntime::comparePayload<op>(
                    std::string_view("a\0b", 3), std::string_view("a\0c", 3))
                == less,
            "borrowed string comparison includes embedded NUL");
    };
    comparison(std::integral_constant<Compare, Compare::Equal>{});
    comparison(std::integral_constant<Compare, Compare::NotEqual>{});
    comparison(std::integral_constant<Compare, Compare::Less>{});
    comparison(std::integral_constant<Compare, Compare::LessEqual>{});
    comparison(std::integral_constant<Compare, Compare::Greater>{});
    comparison(std::integral_constant<Compare, Compare::GreaterEqual>{});
}

void testPrivateConstructionBorrow()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    for (bool dictionary : {false, true}) {
        ExecutionSlot local;
        if (dictionary) {
            arp::reflection::hdict values{&resource};
            values.emplace(makeInt(0), Value{100000});
            values.emplace(makeInt(1), Value{200000});
            local = Value{std::move(values)};
        } else {
            arp::reflection::hlist values{&resource};
            values.emplace_back(Value{100000});
            values.emplace_back(Value{200000});
            local = Value{std::move(values)};
        }
        const auto identity = local.boxed().storageIdentity();
        const auto mapped = [&](int index) -> Value& {
            if (!dictionary)
                return (*local.boxed().ptr<arp::reflection::hlist>())[index];
            return local.boxed().ptr<arp::reflection::hdict>()->find(makeInt(index))->second;
        };
        const auto original0 = mapped(0).storageIdentity(), original1 = mapped(1).storageIdentity();
        ExecutionSlotBuffer operands{&resource};
        operands.reserve(5);
        Value prefix{123000};
        const auto push = [&](ExecutionSlot first, ExecutionSlot second) {
            operands.clear();
            operands.emplace_back(prefix);
            if (dictionary)
                operands.emplace_back(ScriptScalar(1));
            operands.push_back(std::move(second));
            if (dictionary)
                operands.emplace_back(ScriptScalar(0));
            operands.push_back(std::move(first));
        };
        PrivateDictionaryWriteScratch scratch{&resource};
        push(ExecutionSlot(ScriptScalar(100001)), ExecutionSlot(ScriptScalar(200001)));
        Value shared = local.boxed();
        {
            ScriptSemanticRuntime::PrivateConstructionBorrow denied;
            require(!denied.bind(local, operands, 2, 2, dictionary, scratch),
                "private construction borrow: shared actual container rejects");
        }
        shared = Value::undefined();
        {
            ScriptSemanticRuntime::PrivateConstructionBorrow denied;
            require(!denied.bind(local, operands, 2, 1, dictionary, scratch),
                "private construction borrow: container limit preserves original path");
            auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u);
            arp::core::CpuMemoryQuotaScope quotaScope(quota);
            require(!denied.bind(local, operands, 2, 2, dictionary, scratch),
                "private construction borrow: active quota retains original admission/commit");
        }
        {
            ScriptSemanticRuntime::PrivateConstructionBorrow borrow;
            require(borrow.bind(local, operands, 2, 2, dictionary, scratch),
                "private construction borrow: exact topology bind");
            const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
            require(borrow.commit(operands) && operands.size() == 1
                    && operands[0].boxed().storageIdentity() == prefix.storageIdentity()
                    && *mapped(0).ptr<int>() == (dictionary ? 100001 : 200001)
                    && *mapped(1).ptr<int>() == (dictionary ? 200001 : 100001),
                "private construction borrow: original list order, reordered dictionary keys and exact prefix");
            require(mapped(0).storageIdentity() == original0
                    && mapped(1).storageIdentity() == original1
                    && local.boxed().storageIdentity() == identity
                    && arp::core::defaultCpuAllocator().stats().allocationCount == before,
                "private construction borrow: real nodes and unique integer storage retained with no allocation");
            const auto read0 = *mapped(0).ptr<int>();
            push(ExecutionSlot(ScriptScalar(100002)),
                ExecutionSlot(makeString(std::string_view("bad"))));
            require(
                !borrow.commit(operands) && *mapped(0).ptr<int>() == read0 && operands.size() > 1,
                "private construction borrow: wrong actual new payload cannot partially commit");
            Value host{333000};
            push(ExecutionSlot(host), ExecutionSlot(ScriptScalar(200002)));
            require(borrow.commit(operands),
                "private construction borrow: bare host integer copy commits");
            require(mapped(dictionary ? 0 : 1).storageIdentity() == host.storageIdentity(),
                "private construction borrow: imported carrier exact identity");
            push(ExecutionSlot(ScriptScalar(333001)), ExecutionSlot(ScriptScalar(200003)));
            require(borrow.commit(operands) && *host.ptr<int>() == 333000,
                "private construction borrow: shared imported mapped integer is never overwritten in place");
            Value cached = makeInt(37);
            (void)makeInt(38);
            const int cachedPrior = *cached.ptr<int>();
            push(ExecutionSlot(ScriptScalar(37)), ExecutionSlot(ScriptScalar(38)));
            const auto warm = arp::core::defaultCpuAllocator().stats().allocationCount;
            *cached.ptr<int>() = -87;
            require(borrow.commit(operands)
                    && mapped(dictionary ? 0 : 1).storageIdentity() == cached.storageIdentity()
                    && *mapped(dictionary ? 0 : 1).ptr<int>() == -87
                    && arp::core::defaultCpuAllocator().stats().allocationCount == warm,
                "private construction borrow: warm cache view preserves live mapped carrier identity and allocation count");
            *cached.ptr<int>() = cachedPrior;
            if (dictionary) {
                Value key = makeInt(0);
                const int original = *key.ptr<int>();
                push(ExecutionSlot(ScriptScalar(333002)), ExecutionSlot(ScriptScalar(200004)));
                *key.ptr<int>() = 13;
                require(!borrow.commit(operands),
                    "private construction borrow: observes actual cached key payload at each commit");
                *key.ptr<int>() = original;
            }
        }
        require(scratch.empty(),
            "private construction borrow: topology released before original PC replay");
        {
            ExecutionSlot fixed[5];
            std::size_t size = 0u;
            ExecutionSlotStackView view(fixed, size, 5u);
            const auto fill = [&] {
                push(ExecutionSlot(ScriptScalar(100007)), ExecutionSlot(ScriptScalar(200007)));
                size = operands.size();
                for (std::size_t index = 0; index < size; ++index)
                    fixed[index] = std::move(operands[index]);
                operands.clear();
            };
            fill();
            ScriptSemanticRuntime::PrivateConstructionBorrow borrow;
            auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1024u * 1024u);
            {
                arp::core::CpuMemoryQuotaScope scope(quota);
                require(!borrow.bind(local, view, 2, 2, dictionary, scratch) && size > 1u,
                    "fixed construction borrow: quota rejects without consuming operands");
            }
            require(borrow.bind(local, view, 2, 2, dictionary, scratch) && borrow.commit(view)
                    && size == 1u && *mapped(0).ptr<int>() == (dictionary ? 100007 : 200007),
                "fixed construction borrow: shared contract consumes only active suffix");
            borrow.reset();
            require(scratch.empty(),
                "fixed construction borrow: explicit invalidation releases topology");
            fill();
            require(borrow.bind(local, view, 2, 2, dictionary, scratch),
                "fixed construction borrow: invalidated site can rebind actual owner");
            const auto prior = *mapped(0).ptr<int>();
            fixed[size - 1u] = makeString(std::string_view("wrong-new"));
            require(!borrow.commit(view) && *mapped(0).ptr<int>() == prior && size > 1u,
                "fixed construction borrow: failed guard preserves original replay operands");
        }
        mapped(0) = makeString(std::string_view("wrong-old"));
        push(ExecutionSlot(ScriptScalar(1)), ExecutionSlot(ScriptScalar(2)));
        ScriptSemanticRuntime::PrivateConstructionBorrow denied;
        require(!denied.bind(local, operands, 2, 2, dictionary, scratch),
            "private construction borrow: noninteger old mapped payload may not enter closed mutation interval");
    }
}

void testDictionaryOrderingInvalidation()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hdict dictionary((arp::reflection::hdict::allocator_type(&resource)));
    dictionary.emplace(makeInt(0), Value{100000});
    ExecutionSlot local(Value{std::move(dictionary)});
    ExecutionSlotBuffer operands{&resource};
    operands.emplace_back(ScriptScalar(0));
    operands.emplace_back(ScriptScalar(100001));
    PrivateDictionaryWriteScratch scratch{&resource};
    auto* integers = arp::reflection::class_of<int>();
    const Value original = integers->get_field("__lt__");
    integers->def("__lt__", Value(arp::reflection::function("__lt__", [](int left, int right) {
        return left > right;
    })));
    require(!integers->hasBuiltinOrdering()
            && !ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 1, 1, scratch)
            && operands.size() == 2
            && *local.boxed().ptr<arp::reflection::hdict>()->begin()->second.ptr<int>() == 100000,
        "dictionary reuse: method replacement invalidates stock ordering before any rewrite");
    ScriptSemanticRuntime::IntegerIndexUpdate update;
    int source;
    require(!update.bind(local, ExecutionSlot(ScriptScalar(0)), source),
        "indexed update: custom ordering must retain original comparator callbacks");
    ScriptSemanticRuntime::IntegerIndexBorrow borrow;
    require(
        !borrow.bind(local), "topology borrow: overridden ordering prohibits cached node access");
    integers->def("__lt__", original);
}

void testIndexedIntegerUpdateCarrier()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hlist values((arp::reflection::hlist::allocator_type(&resource)));
    Value first{100000}, second{200000};
    values.push_back(first);
    values.push_back(second);
    ExecutionSlot base(Value{std::move(values)});
    const auto identity = base.boxed().storageIdentity();
    ScriptSemanticRuntime::IntegerIndexUpdate update;
    int source;
    Value indexCache = makeInt(0), resultCache = makeInt(1307);
    const int oldIndex = *indexCache.ptr<int>(), oldResult = *resultCache.ptr<int>();
    *indexCache.ptr<int>() = 1;
    *resultCache.ptr<int>() = -9;
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(update.bind(base, ExecutionSlot(ScriptScalar(0)), source) && source == 100000,
        "indexed update: inline read uses raw index, write uses actual cached payload");
    require(base.boxed().ptr<arp::reflection::hlist>()->at(1).storageIdentity()
            == second.storageIdentity(),
        "indexed update: prepare does not write");
    update.commit(1307);
    require(base.boxed().storageIdentity() == identity
            && base.boxed().ptr<arp::reflection::hlist>()->at(0).storageIdentity()
                == first.storageIdentity()
            && base.boxed().ptr<arp::reflection::hlist>()->at(1).storageIdentity()
                == resultCache.storageIdentity()
            && *base.boxed().ptr<arp::reflection::hlist>()->at(1).ptr<int>() == -9
            && *second.ptr<int>() == 200000,
        "indexed update: cached result identity and actual payload replace only the write destination");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "indexed update: warmed integer update has no temporary owned allocation");
    ExecutionSlot imported(Value{0});
    require(update.bind(base, imported, source) && source == 100000,
        "indexed update: imported index retains own carrier, independent of cache");
    update.commit(1307);
    require(base.boxed().ptr<arp::reflection::hlist>()->at(0).storageIdentity()
            == resultCache.storageIdentity(),
        "indexed update: boxed index writes its original destination");
    *indexCache.ptr<int>() = oldIndex;
    *resultCache.ptr<int>() = oldResult;
    *imported.boxed().ptr<int>() = -1;
    require(!update.bind(base, imported, source),
        "indexed update: bad index clears previous destination borrow");
    *imported.boxed().ptr<int>() = 0;
    require(update.bind(base, imported, source), "indexed update: rebind after failure");
    // The result allocation remains at commit. A failed reserve/allocation does
    // not rewrite the original node and the caller owns the exact StoreIndex PC.
    const auto current = base.boxed().ptr<arp::reflection::hlist>()->at(0).storageIdentity();
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    bool failed = false;
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        try {
            update.commit(-100000);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
    }
    require(failed && quota->liveBytes() == 0
            && base.boxed().ptr<arp::reflection::hlist>()->at(0).storageIdentity() == current,
        "indexed update: cold result quota failure preserves original node");
    arp::reflection::hdict dictionary((arp::reflection::hdict::allocator_type(&resource)));
    dictionary.emplace(Value{0}, first);
    ExecutionSlot dict(Value{std::move(dictionary)});
    require(update.bind(dict, imported, source) && source == 100000,
        "indexed update: real dictionary node bind");
    update.commit(1307);
    require(dict.boxed().ptr<arp::reflection::hdict>()->begin()->second.storageIdentity()
            == resultCache.storageIdentity(),
        "indexed update: original dictionary node receives cached Value");
    require(!update.bind(dict, ExecutionSlot(Value{2}), source),
        "indexed update: missing key retains original nil path");
}

void testIntegerTopologyBorrow()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hdict dictionary{&resource};
    for (int i = 0; i != 7; ++i)
        dictionary.emplace(Value{i}, Value{100000 + i});
    ExecutionSlot base(Value{std::move(dictionary)});
    ScriptSemanticRuntime::IntegerIndexBorrow borrow;
    arp::core::pmr::String literalText("wrong container", &resource);
    ExecutionSlot literal = ExecutionSlot::borrowLiteral(literalText);
    ExecutionSlot method(ScriptMethod{0, Value{100000}});
    const auto guardAllocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(!borrow.bind(literal) && !borrow.bind(method) && literal.uniqueBoxed() == nullptr
            && method.uniqueBoxed() == nullptr,
        "topology borrow: wrong Literal/Method guard rejects without materializing carriers");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == guardAllocations,
        "topology borrow: wrong-kind refusal has zero allocations");
    ScriptSemanticRuntime::IntegerIndexUpdate update;
    require(borrow.bind(base), "topology borrow: actual integer dictionary");
    const auto* node = &base.boxed().ptr<arp::reflection::hdict>()->begin()->second;
    Value alias = *node, cachedKey = makeInt(2), result = makeInt(1310);
    const int originalKey = *cachedKey.ptr<int>(), originalResult = *result.ptr<int>();
    for (int i = 0; i != 7; ++i)
        (void)makeInt(i);
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    int source;
    for (int repeat = 0; repeat != 3; ++repeat)
        for (int i = 0; i != 7; ++i) {
            ExecutionSlot index{ScriptScalar(i)};
            require(update.bind(borrow, index, source),
                "topology borrow: eviction retains original lookup");
            update.commit(1310);
            require(borrow.read(index)->storageIdentity() == result.storageIdentity(),
                "topology borrow: mapped replacement observed at actual node");
        }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "topology borrow: all warmed lookups, evictions and commits have zero allocations");
    require(node == &base.boxed().ptr<arp::reflection::hdict>()->begin()->second
            && *alias.ptr<int>() == 100000 && node->storageIdentity() == result.storageIdentity(),
        "topology borrow: stable node address and unmodified shared scalar alias");
    *cachedKey.ptr<int>() = 6;
    require(borrow.read(ExecutionSlot(ScriptScalar(2)))
            == &base.boxed().ptr<arp::reflection::hdict>()->rbegin()->second,
        "topology borrow: cached lookup uses actual key payload, not raw scalar index");
    *result.ptr<int>() = -9;
    require(update.bind(borrow, ExecutionSlot(ScriptScalar(2)), source) && source == -9,
        "topology borrow: every source read observes changed cached actual payload");
    require(update.bindInteger(borrow, 2, source) && source == -9,
        "native index update: actual cached key and mapped payload remain authoritative");
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    update.commit(1310);
    int reread;
    require(update.readSource(reread) && reread == -9,
        "topology borrow: retained real source observes post-commit cached actual payload");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == allocations,
        "topology borrow: warmed guard, cache hit and commit do not allocate scratch");
    *cachedKey.ptr<int>() = originalKey;
    *result.ptr<int>() = originalResult;
    require(!update.bind(borrow, ExecutionSlot(Value{-1}), source),
        "topology borrow: missing read/write cannot insert a node");
    require(!update.bindInteger(borrow, -1, source) && !update.readSource(source),
        "native index update: failed lookup clears source and cannot insert nodes");
    base.boxed().ptr<arp::reflection::hdict>()->begin()->second =
        makeString(std::string_view("wrong"));
    require(!update.bind(borrow, ExecutionSlot(Value{0}), source),
        "topology borrow: changed mapped type retains replay without writing");
    require(!update.bindInteger(borrow, 0, source),
        "native index update: changed mapped type rejects without writing");
    ExecutionSlot invalid(makeString(std::string_view("wrong base")));
    require(!borrow.bind(invalid) && !borrow.read(ExecutionSlot(Value{0})),
        "topology borrow: failed rebind clears old nodes");
    arp::reflection::hdict mixed{&resource};
    mixed.emplace(makeString(std::string_view("key")), Value{3});
    require(!borrow.bind(ExecutionSlot(Value{std::move(mixed)})),
        "topology borrow: actual mixed keys rejected");
    arp::reflection::hlist list{&resource};
    Value first{100000}, second{200000};
    list.push_back(first);
    list.push_back(second);
    ExecutionSlot listBase(Value{std::move(list)});
    Value zero = makeInt(0);
    const int oldZero = *zero.ptr<int>();
    *zero.ptr<int>() = 1;
    require(borrow.bind(listBase) && update.bind(borrow, ExecutionSlot(ScriptScalar(0)), source)
            && source == 100000,
        "topology borrow: separate inline read and cached write indices");
    require(update.bindInteger(borrow, 0, source) && source == 100000,
        "native index update: unboxed selector retains separate read and cached write indices");
    update.commit(1310);
    require(listBase.boxed().ptr<arp::reflection::hlist>()->at(0).storageIdentity()
                == first.storageIdentity()
            && listBase.boxed().ptr<arp::reflection::hlist>()->at(1).storageIdentity()
                == result.storageIdentity(),
        "topology borrow: actual cached write identity replaces only its destination");
    *zero.ptr<int>() = oldZero;
}

void testStableStringIndexBorrow()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hdict dictionary{&resource};
    arp::reflection::hlist selectors{&resource};
    for (const auto key : {"oak", "pine", "birch", "ash", "elm", "cedar"}) {
        dictionary.emplace(makeString(std::string_view(key)), Value{100000});
        selectors.push_back(makeString(std::string_view(key)));
    }
    ExecutionSlot owner(Value{std::move(dictionary)});
    ScriptSemanticRuntime::ReadonlyIndexBorrow borrowed;
    require(borrowed.bind(owner), "stable string relation: bind actual owned dictionary");
    ScriptSemanticRuntime::StableStringIndexBorrow relation(borrowed);
    Value replacement{200000};
    Value missing = makeString(std::string_view("missing")), wrong{17};
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    const auto afterQuota = arp::core::defaultCpuAllocator().stats().allocationCount;
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        for (int repeat = 0; repeat != 3; ++repeat) {
            for (const auto& key : selectors) {
                auto& mapped = owner.boxed().ptr<arp::reflection::hdict>()->at(key);
                const Value* node = relation.read(key);
                require(
                    node == &mapped, "stable string relation: hits and eviction retain real node");
                mapped = replacement;
                require(relation.read(key) == node
                        && node->storageIdentity() == replacement.storageIdentity(),
                    "stable string relation: mapped replacement is observed without copying its payload");
            }
        }
        require(!relation.read(missing) && !relation.read(wrong),
            "stable string relation: missing or wrong-type selectors cannot manufacture values");
    }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == afterQuota
            && quota->liveBytes() == 0u,
        "stable string relation: misses, hits and eviction have no owned allocation");
}

void testIntegerIndexCacheBorrow()
{
    int cold = std::numeric_limits<std::uint16_t>::max();
    while (cold >= 0 && findCachedIntValue(cold))
        --cold;
    require(cold >= 0, "index cache borrow: actual cold key fixture available");
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hdict values{&resource};
    values.emplace(Value{cold}, Value{123456});
    ExecutionSlot base(Value{std::move(values)});
    ScriptSemanticRuntime::IntegerIndexBorrow borrow;
    require(borrow.bind(base), "index cache borrow: owned integer topology binds");
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1);
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    ScriptSemanticRuntime::IntegerIndexUpdate update;
    int source;
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        require(!borrow.read(ExecutionSlot(ScriptScalar(cold)))
                && !borrow.destination(ExecutionSlot(ScriptScalar(cold)))
                && !update.bindInteger(borrow, cold, source) && findCachedIntValue(cold) == nullptr
                && quota->liveBytes() == 0,
            "index cache borrow: cold key guard never materializes or charges script quota");
    }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "index cache borrow: cold miss has no owned allocation");
    Value key = makeInt(cold);
    int read;
    require(borrow.readInteger(ExecutionSlot(ScriptScalar(cold)), read) && read == 123456,
        "index cache borrow: newly published warm key visible through original borrowed array");
    *key.ptr<int>() = -1;
    require(!borrow.read(ExecutionSlot(ScriptScalar(cold))),
        "index cache borrow: live key payload changes original lookup relation");
    *key.ptr<int>() = cold;
    int native = cold;
    require(!borrow.read(ExecutionSlot(Value::native_reference(&native))),
        "index cache borrow: native index preserves rejection boundary");
    const int cv = cold;
    ExecutionSlot imported(Value{cv});
    const auto identity = imported.boxed().storageIdentity();
    require(borrow.readInteger(imported, read) && read == 123456
            && imported.boxed().storageIdentity() == identity,
        "index cache borrow: boxed cv index retains original identity and payload fallback");
    auto* actualCache = const_cast<Value*>(findCachedIntValue(cold));
    Value saved = *actualCache;
    *actualCache = makeString(std::string_view("wrong cached type"));
    require(!borrow.read(ExecutionSlot(ScriptScalar(cold)))
            && !borrow.destination(ExecutionSlot(ScriptScalar(cold))),
        "index cache borrow: actual cached carrier type checked after replacement");
    *actualCache = saved;
    auto* mapped = borrow.destination(ExecutionSlot(ScriptScalar(cold)));
    require(mapped != nullptr, "index cache borrow: restored actual key resolves original node");
    *mapped = makeString(std::string_view("wrong mapped type"));
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(!borrow.readInteger(ExecutionSlot(ScriptScalar(cold)), read)
            && arp::core::defaultCpuAllocator().stats().allocationCount == allocations,
        "index cache borrow: mapped replacement checked before scalar read without allocation");
}

void testUniqueDictionaryReuse()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hdict dictionary((arp::reflection::hdict::allocator_type(&resource)));
    dictionary.emplace(makeInt(0), Value{100000});
    dictionary.emplace(makeInt(1), Value{100001});
    ExecutionSlot local(Value{std::move(dictionary)});
    const auto identity = local.boxed().storageIdentity();
    const auto& map = *local.boxed().ptr<arp::reflection::hdict>();
    const auto first = map.begin()->second.storageIdentity();
    ExecutionSlotBuffer operands{&resource};
    PrivateDictionaryWriteScratch scratch{&resource};
    const auto inputs = [&](int key0, int value0, int key1, int value1) {
        operands.clear();
        operands.emplace_back(ScriptScalar(key0));
        operands.emplace_back(ScriptScalar(value0));
        operands.emplace_back(ScriptScalar(key1));
        operands.emplace_back(ScriptScalar(value1));
    };
    inputs(0, 100002, 1, 100003);
    require(ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && operands.empty() && local.boxed().storageIdentity() == identity
            && map.begin()->second.storageIdentity() == first
            && *map.begin()->second.ptr<int>() == 100002,
        "dictionary reuse: real tree nodes and exclusive values retained");
    Value saved = map.begin()->second;
    inputs(0, 100004, 1, 100005);
    require(ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && *saved.ptr<int>() == 100002 && *map.begin()->second.ptr<int>() == 100004,
        "dictionary reuse: mapped value aliases do not mutate");
    inputs(0, 100006, 2, 100007);
    require(!ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && operands.size() == 4 && *map.begin()->second.ptr<int>() == 100004,
        "dictionary reuse: changed key set replays without modifying values");
    inputs(0, 100006, 0, 100007);
    require(!ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && operands.size() == 4 && *map.begin()->second.ptr<int>() == 100004,
        "dictionary reuse: duplicate-key precedence stays on original constructor");
    inputs(0, 100006, 1, 100007);
    require(!ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 1, scratch),
        "dictionary reuse: container limit retained");
    Value alias = local.boxed();
    require(!ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && alias.storageIdentity() == identity,
        "dictionary reuse: shared carrier rejected");
    alias = makeNil();
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int i = 0; i != 100; ++i) {
        inputs(0, 100000 + i, 1, 200000 + i);
        require(ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch),
            "dictionary reuse: repeated exact key set");
    }
    require(arp::core::defaultCpuAllocator().stats().allocationCount == before,
        "dictionary reuse: no repeated class-name lookup, tree-node or exclusive-value allocation");
}

void testDictionaryProjectionKeys()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    Value key = makeInt(1301), other = makeInt(1302), prefix{300000};
    const int original = *key.ptr<int>();
    arp::reflection::hdict dictionary{&resource};
    dictionary.emplace(key, Value{100000});
    dictionary.emplace(other, Value{200000});
    ExecutionSlot local(Value{std::move(dictionary)});
    auto& map = *local.boxed().ptr<arp::reflection::hdict>();
    const auto identity = local.boxed().storageIdentity();
    const auto firstNode = &map.begin()->second;
    PrivateDictionaryWriteScratch scratch{&resource};
    ExecutionSlotBuffer operands{&resource};
    const auto inputs = [&](int first, int second) {
        operands.clear();
        operands.emplace_back(prefix);
        operands.emplace_back(ScriptScalar(first));
        operands.emplace_back(ScriptScalar(200001));
        operands.emplace_back(ScriptScalar(second));
        operands.emplace_back(ScriptScalar(100001));
    };
    // LoadInt's cache key and the original cached carrier's actual payload
    // intentionally disagree, while the real dictionary is still sorted.
    *key.ptr<int>() = -7;
    inputs(1302, 1301);
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    bool rejected = false;
    try {
        arp::core::CpuMemoryQuotaScope scope(quota);
        (void)ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    require(rejected && operands.size() == 5 && *firstNode->ptr<int>() == 100000
            && local.boxed().storageIdentity() == identity && quota->liveBytes() == 0,
        "dictionary projection: cold scratch quota fails before operands or original nodes change");
    require(ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && operands.size() == 1
            && operands.front().boxed().storageIdentity() == prefix.storageIdentity()
            && map.begin()->first.storageIdentity() == key.storageIdentity()
            && &map.begin()->second == firstNode && *firstNode->ptr<int>() == 100001
            && *map.rbegin()->second.ptr<int>() == 200001,
        "dictionary projection: actual cached payload, reordered keys, prefix and real node identity retained");
    inputs(1302, 1302);
    require(!ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && operands.size() == 5 && scratch.empty() && *firstNode->ptr<int>() == 100001,
        "dictionary projection: duplicate target rejects before any commit");
    *key.ptr<int>() = 1303;
    inputs(1302, 1301);
    require(!ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch)
            && operands.size() == 5 && scratch.empty() && *firstNode->ptr<int>() == 100001,
        "dictionary projection: nonmonotonic actual key aliases reject before lookup or rewrite");
    *key.ptr<int>() = original;
    inputs(1302, 1301);
    require(ScriptSemanticRuntime::reuseUniqueDictionary(local, operands, 2, 2, scratch),
        "dictionary projection: scratch rebinds current real keys after failed qualification");
}

void testUniqueScalarCarrierReplacement()
{
    const int cvInitial = 100000;
    Value cvCarrier{cvInitial};
    require(exactValuePayload<int>(cvCarrier) == nullptr,
        "private scalar: explicit const-selector miss");
    const auto cvIdentity = cvCarrier.storageIdentity();
    // The original general pointer path lazily registers const int metadata.
    // Warm that same preexisting reflection edge before measuring owned payload
    // replacement, so registration allocations are not attributed to the query.
    require(
        *cvCarrier.ptr<int>() == cvInitial, "private scalar: original const pointer path warmup");
    const auto beforeCv = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(ScriptScalar(100003).replaceUniqueCarrier(cvCarrier)
            && cvCarrier.storageIdentity() == cvIdentity && *cvCarrier.ptr<int>() == 100003,
        "private scalar: selector miss preserves original exclusive in-place replacement");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == beforeCv,
        "private scalar: selector fallback cannot add owned allocations");
    Value integer{100000};
    const auto identity = integer.storageIdentity();
    require(ScriptScalar(100001).replaceUniqueCarrier(integer) && *integer.ptr<int>() == 100001
            && integer.storageIdentity() == identity,
        "private scalar: exclusive uncached payload reuses storage");
    Value alias = integer;
    require(!ScriptScalar(100002).replaceUniqueCarrier(integer) && *alias.ptr<int>() == 100001,
        "private scalar: shared payload cannot change");
    alias = makeNil();
    require(!ScriptScalar(3).replaceUniqueCarrier(integer) && *integer.ptr<int>() == 100001,
        "private scalar: target cache range requires normal materialization");
    Value cached = makeInt(3);
    require(!ScriptScalar(100002).replaceUniqueCarrier(cached),
        "private scalar: cache owner prevents mutation");
    int nativeInteger = 100000;
    Value native = Value::native_reference(&nativeInteger);
    require(!ScriptScalar(100001).replaceUniqueCarrier(native) && nativeInteger == 100000,
        "private scalar: native payload cannot change");
    Value floating{-0.0};
    require(
        ScriptScalar(0.0).replaceUniqueCarrier(floating) && !std::signbit(*floating.ptr<double>()),
        "private scalar: floating signed zero preserved");
    Value wide{std::int64_t(1) << 54};
    require(ScriptScalar((std::int64_t(1) << 54) + 1).replaceUniqueCarrier(wide)
            && *wide.ptr<std::int64_t>() == (std::int64_t(1) << 54) + 1,
        "private scalar: wide integer remains exact");
    require(!ScriptScalar(1.0).replaceUniqueCarrier(wide),
        "private scalar: payload type change uses original path");
}

void testUniqueListReuse()
{
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::reflection::hlist values((arp::reflection::hlist::allocator_type(&resource)));
    values.emplace_back(makeInt(1));
    values.emplace_back(makeInt(2));
    ExecutionSlot local(Value{std::move(values)});
    const auto identity = local.boxed().storageIdentity();
    Value imported{100000};
    ExecutionSlotBuffer operands{&resource};
    operands.emplace_back(imported);
    operands.emplace_back(ScriptScalar(3));
    (void)makeInt(3);
    (void)local.boxed().ptr<arp::reflection::hlist>();
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2) && operands.empty(),
        "list reuse: unique storage consumes exact suffix");
    const auto& list = *local.boxed().ptr<arp::reflection::hlist>();
    require(local.boxed().storageIdentity() == identity
            && list[0].storageIdentity() == imported.storageIdentity()
            && list[1].storageIdentity() == makeInt(3).storageIdentity(),
        "list reuse: original carrier, imported element and cached integer identities preserved");
    const auto after = arp::core::defaultCpuAllocator().stats().allocationCount;
    require(after == before, "list reuse: vector storage retained after shared cache warmup");
    Value saved = list[0];
    operands.emplace_back(ScriptScalar(100001));
    operands.emplace_back(ScriptScalar(100002));
    require(ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2)
            && *saved.ptr<int>() == 100000 && *imported.ptr<int>() == 100000,
        "list reuse: shared element carriers are replaced without changing aliases");
    const auto firstIdentity = list[0].storageIdentity(),
               secondIdentity = list[1].storageIdentity();
    const auto reuseBefore = arp::core::defaultCpuAllocator().stats().allocationCount;
    operands.emplace_back(ScriptScalar(100003));
    operands.emplace_back(ScriptScalar(100004));
    require(ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2)
            && list[0].storageIdentity() == firstIdentity
            && list[1].storageIdentity() == secondIdentity && *list[0].ptr<int>() == 100003
            && *list[1].ptr<int>() == 100004
            && arp::core::defaultCpuAllocator().stats().allocationCount == reuseBefore,
        "list reuse: exclusive uncached integer elements reuse exact storage without allocation");
    operands.emplace_back(ScriptScalar(3));
    operands.emplace_back(ScriptScalar(4));
    require(ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2)
            && list[0].storageIdentity() == makeInt(3).storageIdentity(),
        "list reuse: returning to cached range restores the canonical mutable carrier");
    Value alias = local.boxed();
    operands.emplace_back(ScriptScalar(4));
    require(!ScriptSemanticRuntime::reuseUniqueList(local, operands, 1, 2) && operands.size() == 1
            && alias.ptr<arp::reflection::hlist>()->size() == 2,
        "list reuse: shared host alias falls back without consuming operands");
    alias = makeNil();
    require(!ScriptSemanticRuntime::reuseUniqueList(local, operands, 1, 0)
            && !ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2)
            && !ScriptSemanticRuntime::reuseUniqueList(local, operands, -1, 2)
            && operands.size() == 1 && local.boxed().ptr<arp::reflection::hlist>()->size() == 2,
        "list reuse: quota and underflow guards have no effects");
}

void testUniqueStringAppend()
{
    const arp::core::pmr::String literal("literal", &arp::core::defaultCpuMemoryResource());
    auto borrowed = ExecutionSlot::borrowLiteral(literal);
    require(!ScriptSemanticRuntime::appendUniqueString(borrowed, "x") && literal == "literal",
        "string append: program-owned literal does not mutate or materialize");
    Value alias = makeString(std::string_view("shared"));
    ExecutionSlot shared(alias);
    require(!ScriptSemanticRuntime::appendUniqueString(shared, "x")
            && *alias.ptr<arp::core::pmr::String>() == "shared",
        "string append: shared and host carriers retain contents");
    auto& resource = arp::core::defaultCpuMemoryResource();
    arp::core::pmr::String nativeText("native", &resource);
    ExecutionSlot native(Value::native_reference(&nativeText));
    require(!ScriptSemanticRuntime::appendUniqueString(native, "x") && nativeText == "native",
        "string append: native reference cannot mutate through unique carrier");
    using Access = ScriptSemanticRuntime::UniqueStringAccess;
    require(!Access::borrow(borrowed) && !Access::borrow(shared) && !Access::borrow(native),
        "string borrow: literal, shared and native access rejected before binding");
    ExecutionSlot owned(makeString(std::string_view("owned")));
    const auto identity = owned.boxed().storageIdentity();
    const auto before = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int i = 0; i != 200; ++i)
        require(ScriptSemanticRuntime::appendUniqueString(owned, "x"),
            "string append: unique owned payload");
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount - before;
    require(allocations < 200 && owned.boxed().storageIdentity() == identity
            && owned.boxed().ptr<arp::core::pmr::String>()->size() == 205,
        "string append: capacity reused without a new carrier for each append");
    const auto assignedBefore = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (int i = 0; i != 200; ++i)
        require(ScriptSemanticRuntime::assignUniqueString(owned, "prefix", "suffix"),
            "string assignment: independent prefix and suffix");
    require(arp::core::defaultCpuAllocator().stats().allocationCount == assignedBefore
            && *owned.boxed().ptr<arp::core::pmr::String>() == "prefixsuffix"
            && owned.boxed().storageIdentity() == identity,
        "string assignment: existing capacity and identity reused");
    require(!ScriptSemanticRuntime::assignUniqueString(shared, "prefix", "suffix")
            && *alias.ptr<arp::core::pmr::String>() == "shared",
        "string assignment: shared alias preserved");
    const auto original = *owned.boxed().ptr<arp::core::pmr::String>();
    const arp::core::pmr::String suffix(10000, 'y', &resource);
    const auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    bool rejected = false;
    try {
        arp::core::CpuMemoryQuotaScope scope(quota);
        (void)ScriptSemanticRuntime::appendUniqueString(owned, suffix);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    require(rejected && *owned.boxed().ptr<arp::core::pmr::String>() == original
            && owned.boxed().storageIdentity() == identity && quota->liveBytes() == 0,
        "string append: core quota and strong failure guarantee preserved");
    rejected = false;
    try {
        arp::core::CpuMemoryQuotaScope scope(quota);
        (void)ScriptSemanticRuntime::assignUniqueString(owned, "prefix", suffix);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    require(rejected && *owned.boxed().ptr<arp::core::pmr::String>() == original
            && quota->liveBytes() == 0,
        "string assignment: quota failure preserves prior value");
    {
        auto access = Access::borrow(owned);
        require(static_cast<bool>(access), "string borrow: owned nonescaping payload bound");
        require(access.append(std::string_view("\0x", 2)) && access.assign("borrowed", "payload"),
            "string borrow: append and assignment share checked semantic operations");
        rejected = false;
        try {
            arp::core::CpuMemoryQuotaScope scope(quota);
            (void)access.append(suffix);
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
        require(rejected && *owned.boxed().ptr<arp::core::pmr::String>() == "borrowedpayload"
                && owned.boxed().storageIdentity() == identity && quota->liveBytes() == 0,
            "string borrow: failed growth preserves payload and carrier");
    }
}

int main()
{
    testUniqueDictionaryReuse();
    testDictionaryProjectionKeys();
    testUniqueScalarCarrierReplacement();
    testUniqueListReuse();
    testUniqueStringAppend();
    testScalarSpecializations();
    testDirectContainerConstruction();
    testBorrowedStringSlots();
    testInlineMethodSlots();
    {
        ExecutionSlotBuffer slots{&arp::core::defaultCpuMemoryResource()};
        slots.reserve(4);
        slots.emplace_back(ScriptScalar(0));
        slots.clear();
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (int i = 0; i < 100; ++i) {
            slots.emplace_back(ScriptScalar(i + 100000));
            require(slots.back().is<int>() && *slots.back().ptr<int>() == i + 100000,
                "inline execution scalar payload");
            slots.pop_back();
        }
        require(arp::core::defaultCpuAllocator().stats().allocationCount == before.allocationCount,
            "inline execution scalars do not allocate boxed values");
        Value host = makeInt(123);
        ExecutionSlot imported(host), alias(imported);
        *host.ptr<int>() = 456;
        require(*imported.ptr<int>() == 456 && *alias.ptr<int>() == 456,
            "host scalar Value aliases remain references");
        slots.emplace_back(ScriptScalar(100000));
        const auto exported = slots.materialize();
        Value projection = slots[0];
        require(projection.is<int>() && *projection.ptr<int>() == 100000,
            "slot conversion materializes a Value, never boxes ExecutionSlot as a custom object");
        *exported[0].ptr<int>() = 200000;
        require(*slots[0].ptr<int>() == 200000 && *projection.ptr<int>() == 200000,
            "materialized boundary retains carrier identity");
        Frame frame;
        frame.push(ScriptScalar(7.5));
        auto payload = frame.popSlot();
        require(frame.stack.empty() && payload.is<double>() && *payload.ptr<double>() == 7.5,
            "frame transfers inline scalar without reflection boxing");
        ExecutionSlotBuffer operands{&arp::core::defaultCpuMemoryResource()};
        operands.emplace_back(ScriptScalar(999999));
        operands.emplace_back(ScriptScalar(5));
        const auto list = operands.applyValueOperation(1, [](auto& values) {
            return ScriptSemanticRuntime::makeList(values, 1, 10);
        });
        require(list.ok() && operands.size() == 1 && *operands[0].ptr<int>() == 999999,
            "container ABI consumes only its operand suffix");
        const auto allocationCount = arp::core::defaultCpuAllocator().stats().allocationCount;
        (void)operands[0].boxed();
        require(arp::core::defaultCpuAllocator().stats().allocationCount == allocationCount + 1,
            "container ABI leaves earlier scalar unboxed");
    }
    {
        ScalarOperationResult scalar;
        require(ScriptSemanticRuntime::scalarBinary(
                    SemanticBinaryOperation::Add, ScriptScalar(1.25), ScriptScalar(2.5), scalar)
                && scalar.ok() && scalar.value.floating() == 3.75,
            "inline double arithmetic");
        require(ScriptSemanticRuntime::scalarBinary(SemanticBinaryOperation::Add,
                    ScriptScalar(std::numeric_limits<int>::max()),
                    ScriptScalar(1),
                    scalar)
                && scalar.code == ScriptDiagnosticCode::IntegerOverflow,
            "checked int overflow");
        require(ScriptSemanticRuntime::scalarBinary(SemanticBinaryOperation::Div,
                    ScriptScalar(std::numeric_limits<std::int64_t>::min()),
                    ScriptScalar(std::int64_t(-1)),
                    scalar)
                && scalar.code == ScriptDiagnosticCode::IntegerOverflow,
            "checked int64 division overflow");
        require(ScriptSemanticRuntime::scalarBinary(SemanticBinaryOperation::Mod,
                    ScriptScalar(std::numeric_limits<std::int64_t>::min()),
                    ScriptScalar(std::int64_t(-1)),
                    scalar)
                && scalar.ok() && scalar.value.integer() == 0,
            "int64 min modulo -1");
        require(ScriptSemanticRuntime::scalarBinary(
                    SemanticBinaryOperation::ShiftRight, ScriptScalar(-7), ScriptScalar(1), scalar)
                && scalar.ok() && scalar.value.integer() == -4,
            "deterministic arithmetic right shift");
        require(ScriptSemanticRuntime::scalarBinary(
                    SemanticBinaryOperation::ShiftLeft, ScriptScalar(1), ScriptScalar(-1), scalar)
                && scalar.code == ScriptDiagnosticCode::IntegerOverflow,
            "negative shift rejected");
        require(ScriptSemanticRuntime::scalarBinary(
                    SemanticBinaryOperation::ShiftLeft, ScriptScalar(1), ScriptScalar(32), scalar)
                && scalar.code == ScriptDiagnosticCode::IntegerOverflow,
            "oversized shift rejected");
        require(ScriptSemanticRuntime::scalarBinary(
                    SemanticBinaryOperation::Div, ScriptScalar(1.0), ScriptScalar(0.0), scalar)
                && scalar.code == ScriptDiagnosticCode::DivisionByZero,
            "double zero division");
        require(ScriptSemanticRuntime::scalarBinary(SemanticBinaryOperation::Add,
                    ScriptScalar(4),
                    ScriptScalar(std::int64_t(5)),
                    scalar)
                && scalar.ok() && scalar.value.kind() == ScriptScalar::Kind::Int64
                && scalar.value.integer() == 9,
            "mixed integer width preserves int64 representation");
        require(ScriptSemanticRuntime::scalarBinary(
                    SemanticBinaryOperation::Mod, ScriptScalar(-7.5), ScriptScalar(2.0), scalar)
                && scalar.ok() && scalar.value.floating() == -1.5,
            "double modulo preserves fmod contract");
        const auto left = makeFloat(1.25), right = makeFloat(2.5);
        (void)ScriptSemanticRuntime::binary(SemanticBinaryOperation::Add, left, right);
        const auto before = arp::core::defaultCpuAllocator().stats();
        for (int i = 0; i < 100; ++i) {
            const auto result =
                ScriptSemanticRuntime::binary(SemanticBinaryOperation::Add, left, right);
            require(
                result.ok() && *result.value.ptr<double>() == 3.75, "boxed scalar helper result");
        }
        const auto after = arp::core::defaultCpuAllocator().stats();
        require(after.allocationCount - before.allocationCount == 100,
            "double arithmetic allocates only its result, no reflected parameter vectors");
    }
    testComparisonContract();
    {
        using Op = SemanticBinaryOperation;
        const int minimum = std::numeric_limits<int>::min();
        const int maximum = std::numeric_limits<int>::max();
        require(ScriptSemanticRuntime::intBinary<Op::Add>(maximum, 1).code
                == ScriptDiagnosticCode::IntegerOverflow,
            "typed int add overflow retained");
        require(ScriptSemanticRuntime::intBinary<Op::Sub>(minimum, 1).code
                == ScriptDiagnosticCode::IntegerOverflow,
            "typed int sub overflow retained");
        require(ScriptSemanticRuntime::intBinary<Op::Mul>(maximum, maximum).code
                == ScriptDiagnosticCode::IntegerOverflow,
            "typed int mul overflow retained");
        require(ScriptSemanticRuntime::intBinary<Op::Div>(minimum, -1).code
                == ScriptDiagnosticCode::IntegerOverflow,
            "typed int division width overflow retained");
        const auto remainder = ScriptSemanticRuntime::intBinary<Op::Mod>(minimum, -1);
        require(remainder.ok() && remainder.value.integer() == 0,
            "typed int min modulo minus one is defined");
        require(ScriptSemanticRuntime::intBinary<Op::Div>(1, 0).code
                == ScriptDiagnosticCode::DivisionByZero,
            "typed int division zero retained");
        require(ScriptSemanticRuntime::intBinary<Op::Mod>(1, 0).code
                == ScriptDiagnosticCode::DivisionByZero,
            "typed int modulo zero retained");
        const auto quotient = ScriptSemanticRuntime::intBinary<Op::Div>(-7, 2);
        const auto negativeRemainder = ScriptSemanticRuntime::intBinary<Op::Mod>(-7, 2);
        require(quotient.ok() && quotient.value.integer() == -3 && negativeRemainder.ok()
                && negativeRemainder.value.integer() == -1,
            "typed int division and modulo retain truncation contract");
        const auto boundary = ScriptSemanticRuntime::intBinary<Op::Mul>(minimum, 1);
        require(boundary.ok() && boundary.value.integer() == minimum,
            "typed int valid minimum result retained");
        const int divisors[]{minimum, maximum, -1000000007, -7, -1, 0, 1, 7, 1000000007};
        const int numerators[]{minimum, maximum, -1000000007, -7, -1, 0, 1, 7, 1000000007};
        for (const auto lhs : numerators)
            for (const auto rhs : numerators) {
                const std::int64_t a = lhs, b = rhs;
                const std::int64_t wide[]{a + b, a - b, a * b};
                const ScalarOperationResult checked[]{
                    ScriptSemanticRuntime::intBinary<Op::Add>(lhs, rhs),
                    ScriptSemanticRuntime::intBinary<Op::Sub>(lhs, rhs),
                    ScriptSemanticRuntime::intBinary<Op::Mul>(lhs, rhs)};
                for (std::size_t i = 0; i < 3u; ++i) {
                    const bool valid = wide[i] >= minimum && wide[i] <= maximum;
                    require(checked[i].ok() == valid
                            && (valid ? checked[i].value.integer() == wide[i]
                                      : checked[i].code == ScriptDiagnosticCode::IntegerOverflow
                                        && std::string_view(checked[i].message)
                                            == "integer arithmetic overflow"),
                        "checked int arithmetic: exact result and diagnostic match independent wide reference");
                }
            }
        for (const auto d : divisors)
            for (const auto n : numerators) {
                const arp::core::Int32Divisor prepared(d);
                const auto expectedDiv = ScriptSemanticRuntime::intBinary<Op::Div>(n, d);
                const auto actualDiv = ScriptSemanticRuntime::intBinary<Op::Div>(n, prepared);
                const auto expectedMod = ScriptSemanticRuntime::intBinary<Op::Mod>(n, d);
                const auto actualMod = ScriptSemanticRuntime::intBinary<Op::Mod>(n, prepared);
                require(expectedDiv.code == actualDiv.code
                        && std::string_view(expectedDiv.message) == actualDiv.message
                        && (!expectedDiv.ok()
                            || expectedDiv.value.integer() == actualDiv.value.integer()),
                    "prepared divisor: exact signed quotient, zero diagnostic and overflow");
                require(expectedMod.code == actualMod.code
                        && std::string_view(expectedMod.message) == actualMod.message
                        && (!expectedMod.ok()
                            || expectedMod.value.integer() == actualMod.value.integer()),
                    "prepared divisor: exact signed remainder and zero diagnostic");
            }
    }
    testScalarPointerAlias<int>(100000, 200000);
    testActualScalarReads();
    testCheckedValueCarriers();
    testOwnedRuntimeAccessors();
    testScalarPointerAlias<std::int64_t>(std::int64_t(1) << 40, std::int64_t(1) << 41);
    testScalarPointerAlias<double>(-0.0, 2.5);
    testScalarPointerAlias<bool>(false, true);
    testSlotLifetimeTransitions();
    testScriptCallableAccess();
    testOwnedCollectionGeneratorExclusion();
    testIntegerValueRangeBuilder();
    testIntegerValueCacheBorrow();
    testInlineCollectionIteration();
    testInlineCollectionIndex();
    testConversionAndLengthContract();
    testExtendedNumericArithmeticContract();
    testVmBudgetContract();
    testSuspendedFrameContract();
    testVmSuspendedFrameSnapshot();
    testGeneratorCanonicalAdoptionIdentityAndResume();
    testInterpreterConsumesDirectNativeGeneratorBridge();
    testIndexedIntegerUpdateCarrier();
    testIntegerTopologyBorrow();
    testIntegerIndexCacheBorrow();
    testStableStringIndexBorrow();
    testPrivateConstructionBorrow();
    testDictionaryOrderingInvalidation();
    std::printf("All ScriptVM semantic runtime tests passed\n");
    return 0;
}
