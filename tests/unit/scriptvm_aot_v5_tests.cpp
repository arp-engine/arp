#include "scriptvm/aot/aot_codegen_v5.h"
#include "scriptvm/aot/aot_v5.h"
#include "scriptvm/aot/native_slot_operations.h"
#include "scriptvm/compile/frontend.h"
#include "scriptvm/interpreter/vm.h"
#include "scriptvm/ir/typed_ir.h"
#include "scriptvm/runtime/execution_slot_values.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace {

using namespace arp::scriptvm;

void inlineLeafAdmissionMatchesTheCanonicalGate()
{
    DirectNativeExecutionContext context;
    context.budget.recursionLimit = 2u;
    context.budget.maxMemoryBytes = 1u;
    context.memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(16u);
    for (const auto live : {0u, 2u}) {
        if (live != 0u)
            assert(context.memoryQuota->tryReserve(live));
        for (const auto depth : {0u, 1u, 2u}) {
            for (const bool failed : {false, true}) {
                context.memoryBudgetFailed = failed;
                context.recursionDepth = depth;
                VmError canonicalError;
                const bool canonical = context.enterFunction("unit", &canonicalError);
                const auto canonicalDepth = context.recursionDepth;
                context.recursionDepth = depth;
                VmError leafError;
                const bool leaf = context.enterLeafFunction("unit", &leafError);
                assert(leaf == canonical && context.recursionDepth == canonicalDepth);
                assert(leafError.code == canonicalError.code
                    && leafError.message == canonicalError.message);
                if (leaf) {
                    context.leaveLeafFunction();
                    assert(context.recursionDepth == depth);
                }
                context.recursionDepth = depth;
                VmError nativeError;
                const bool native = context.enterNativeFunction("unit", &nativeError);
                assert(native == canonical && context.recursionDepth == canonicalDepth);
                assert(nativeError.code == canonicalError.code
                    && nativeError.message == canonicalError.message);
                if (native) {
                    context.leaveNativeFunction();
                    assert(context.recursionDepth == depth);
                }
            }
        }
        if (live != 0u)
            context.memoryQuota->release(live);
    }
}

void nativeSlotsPreserveValueEdgesAndCheckedSemantics()
{
    using Operations = DirectNativeSlotOperations;
    auto sum = Operations::binary<SemanticBinaryOperation::Add>(
        ExecutionSlot(ScriptScalar(17)), ExecutionSlot(ScriptScalar(25)));
    assert(sum.ok() && sum.value.inlineScalar() && *sum.value.scalarPointer<int>() == 42);
    auto fractional = Operations::binary<SemanticBinaryOperation::Mul>(
        ExecutionSlot(ScriptScalar(1.25)), ExecutionSlot(ScriptScalar(2)));
    assert(fractional.ok() && fractional.value.inlineScalar()
        && *fractional.value.scalarPointer<double>() == 2.5);
    auto overflow = Operations::binary<SemanticBinaryOperation::Add>(
        ExecutionSlot(ScriptScalar(std::numeric_limits<int>::max())),
        ExecutionSlot(ScriptScalar(1)));
    assert(overflow.diagnostic.code == ScriptDiagnosticCode::IntegerOverflow);
    auto zero = Operations::binary<SemanticBinaryOperation::Div>(
        ExecutionSlot(ScriptScalar(7)), ExecutionSlot(ScriptScalar(0)));
    assert(zero.diagnostic.code == ScriptDiagnosticCode::DivisionByZero);
    const arp::core::pmr::String literal("native literal", &arp::core::defaultCpuMemoryResource());
    std::array<ExecutionSlot, 4> slots;
    slots[0] = ScriptScalar(2.5);
    slots[1] = ExecutionSlot::borrowLiteral(literal);
    const Value imported{73};
    slots[2] = imported;
    {
        ExecutionSlotValues roots(slots, 0u, slots.size(), true);
        assert(slots[0].inlineScalar() && slots[1].immutableLiteral());
        assert(roots.data()[2].storageIdentity() == imported.storageIdentity());
    }
    {
        const auto values = materializeExecutionSlots(slots, 1u, 2u);
        assert(values.data()[0].is<arp::core::pmr::String>());
        assert(values.data()[2].is_undefined());
        assert(values.data()[1].storageIdentity() == imported.storageIdentity());
        assert(slots[0].inlineScalar());
    }
    int hostInteger = 3;
    slots[3] = Value::native_reference(&hostInteger);
    const void* hostIdentity = slots[3].boxedCarrier()->storageIdentity();
    hostInteger = 11;
    auto updated =
        Operations::binary<SemanticBinaryOperation::Add>(slots[3], ExecutionSlot(ScriptScalar(1)));
    const auto nativeSemantic =
        ScriptSemanticRuntime::binary(SemanticBinaryOperation::Add, slots[3].boxed(), makeInt(1));
    assert(updated.diagnostic.code == nativeSemantic.diagnostic.code);
    if (updated.ok())
        assert(updated.value.boxed() == nativeSemantic.value);
    assert(slots[3].boxedCarrier()->storageIdentity() == hostIdentity && hostInteger == 11);
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u);
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        auto inlineOnly = Operations::binary<SemanticBinaryOperation::Add>(
            ExecutionSlot(ScriptScalar(70000)), ExecutionSlot(ScriptScalar(1)));
        assert(inlineOnly.ok() && quota->liveBytes() == 0u);
        bool exhausted = false;
        try {
            (void)inlineOnly.value.takeValue();
        } catch (const std::bad_alloc&) {
            exhausted = true;
        }
        assert(exhausted);
    }
}

void nativeFixedStackReusesSharedConstructionContracts()
{
    std::array<ExecutionSlot, 4> slots;
    std::size_t size = 2u;
    ExecutionSlotStackView operands(slots.data(), size, slots.size());
    slots[0] = ScriptScalar(100000);
    slots[1] = ScriptScalar(100001);
    auto built = ScriptSemanticRuntime::makeList(operands, 2, 2u);
    assert(built.ok() && size == 0u);
    ExecutionSlot local(std::move(built.value));
    Value alias = local.boxed();
    slots[0] = ScriptScalar(100002);
    slots[1] = ScriptScalar(100003);
    size = 2u;
    assert(!ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2u) && size == 2u);
    alias = Value::undefined();
    assert(ScriptSemanticRuntime::reuseUniqueList(local, operands, 2, 2u) && size == 0u);
    const auto* list = local.ptr<arp::reflection::hlist>();
    assert(list && *(*list)[0].ptr<int>() == 100002 && *(*list)[1].ptr<int>() == 100003);
    slots[0] = ScriptScalar(1);
    slots[1] = ScriptScalar(10);
    slots[2] = ScriptScalar(1);
    slots[3] = ScriptScalar(20);
    size = 4u;
    auto duplicate = ScriptSemanticRuntime::makeDict(operands, 2, 2u);
    assert(duplicate.ok() && size == 0u);
    const auto* dictionary = duplicate.value.ptr<arp::reflection::hdict>();
    assert(dictionary && dictionary->size() == 1u && *dictionary->begin()->second.ptr<int>() == 10);
    size = 2u;
    auto limited = ScriptSemanticRuntime::makeList(operands, 2, 1u);
    assert(limited.diagnostic.code == ScriptDiagnosticCode::ResourceLimitExceeded && size == 2u);
    arp::reflection::hdict strings{&arp::core::defaultCpuMemoryResource()};
    strings.emplace(makeString(std::string_view("oak")), makeInt(3));
    ExecutionSlot stringDictionary(Value{std::move(strings)});
    ScriptSemanticRuntime::ReadonlyIndexBorrow borrow;
    assert(borrow.bind(stringDictionary));
    int value = 0;
    {
        ExecutionSlot key(makeString(std::string_view("oak")));
        assert(borrow.readInteger(key, value) && value == 3);
    }
    // The cached relation retains dictionary-owned key bytes. A temporary
    // lookup key may die, and replacing the mapped carrier is observed.
    stringDictionary.ptr<arp::reflection::hdict>()->begin()->second = makeInt(7);
    ExecutionSlot nextKey(makeString(std::string_view("oak")));
    assert(borrow.readInteger(nextKey, value) && value == 7);
    const auto* carrierResult = borrow.readCarrier(nextKey.boxed());
    assert(carrierResult && *carrierResult->ptr<int>() == 7);
    ExecutionSlot missing(makeString(std::string_view("pine")));
    assert(borrow.read(missing) == nullptr);
    assert(borrow.readCarrier(missing.boxed()) == nullptr);
    assert(borrow.readCarrier(makeInt(0)) == nullptr);
    assert(!borrow.bind(ExecutionSlot(ScriptScalar(1))));
}

constexpr const char* kHash =
    "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

Value directEntry(
    DirectNativeExecutionContext&, const Value*, std::size_t, DirectNativeResumeStateV5*, VmError*)
{
    return Value::undefined();
}

std::size_t observedMethodArgumentCount = 0u;
bool observedMethodReceiver = false;

Value receiverMethodEntry(DirectNativeExecutionContext&,
    const Value* arguments,
    std::size_t argumentCount,
    DirectNativeResumeStateV5*,
    VmError* error)
{
    observedMethodArgumentCount = argumentCount;
    observedMethodReceiver =
        argumentCount == 1u && arguments != nullptr && arguments[0].is<arp::reflection::instance>();
    if (!observedMethodReceiver && error != nullptr) {
        error->code = ScriptDiagnosticCode::AotArtifactInvalid;
        error->message = "instance method receiver argument contract mismatch";
        return makeNil();
    }
    return makeBool(true);
}

Value budgetedRecursiveGeneratorEntry(DirectNativeExecutionContext& context,
    const Value* args,
    std::size_t argumentCount,
    DirectNativeResumeStateV5* suppliedResume,
    VmError* error)
{
    if (suppliedResume == nullptr)
        return makeDirectNativeGeneratorV5(
            context, &budgetedRecursiveGeneratorEntry, args, argumentCount, error);
    if (!context.enterFunction("budgeted-recursive-generator", error))
        return makeNil();

    struct Scope final {
        DirectNativeExecutionContext& context;

        ~Scope() { context.leaveFunction(); }
    } scope{context};

    Value child =
        makeDirectNativeGeneratorV5(context, &budgetedRecursiveGeneratorEntry, nullptr, 0u, error);
    auto* childGenerator = asDirectNativeGeneratorV5(child);
    if (childGenerator == nullptr)
        return makeNil();
    VmError childError;
    const Value result = resumeDirectNativeGeneratorV5(*childGenerator, &childError);
    if (childError.code != ScriptDiagnosticCode::None) {
        if (error != nullptr)
            *error = std::move(childError);
        return makeNil();
    }
    return result;
}

bool acceptResourcePluginDependency(void*, const AotPluginDependencyV5& dependency, VmError* error)
{
    if (std::string_view(dependency.moduleId) != "arp_resources" || dependency.moduleVersion != 1u
        || std::string_view(dependency.exportContractHash) != kHash
        || std::string_view(dependency.reflectionRegistryHash) != kHash
        || dependency.serviceCount != 1u
        || std::string_view(dependency.services[0].serviceId) != "arp.runtime.async"
        || dependency.services[0].version != 1u) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotModuleUnavailable;
            error->message = "native Plugin dependency contract mismatch";
        }
        return false;
    }
    return true;
}

class QuotaTestFrame final : public DirectNativeFrameV5 {
public:
    explicit QuotaTestFrame(DirectNativeExecutionContext* owner = nullptr)
        : DirectNativeFrameV5("quota-generator", "quota-generator#identity"), owner_(owner)
    {
        if (owner_ != nullptr)
            owner_->retainContinuationFrame(sizeof(QuotaTestFrame));
    }

    ~QuotaTestFrame() override
    {
        if (owner_ != nullptr)
            owner_->releaseContinuationFrame(sizeof(QuotaTestFrame));
    }

    arp::core::pmr::Vector<Value> retained{&arp::core::defaultCpuMemoryResource()};

private:
    DirectNativeExecutionContext* owner_ = nullptr;
};

class ExternalValueQuotaFrame final : public DirectNativeFrameV5 {
public:
    ExternalValueQuotaFrame(DirectNativeExecutionContext& owner, Value value)
        : DirectNativeFrameV5("external-value-generator", "external-value-generator#identity"),
          owner_(&owner), retained_(std::move(value))
    {
        owner_->retainContinuationFrame(sizeof(ExternalValueQuotaFrame));
    }

    ~ExternalValueQuotaFrame() override
    {
        if (owner_ == nullptr)
            return;
        owner_->releaseMemoryRoots(this);
        owner_->releaseContinuationFrame(sizeof(ExternalValueQuotaFrame));
    }

    const Value& retained() const noexcept { return retained_; }

private:
    DirectNativeExecutionContext* owner_ = nullptr;
    Value retained_;
};

struct OpaqueQuotaValue {
    int value = 0;
};

void retainedValueGraphUsesOneQuotaFactSource()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->budget = {1000u, 8u, 64u, 128u, 16u, 8u, 4096u};
    context->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(context->budget.maxMemoryBytes);
    context->resetBudget(context->budget);

    Value large = makeString(
        std::string_view("012345678901234567890123456789012345678901234567890123456789"));
    const ScriptValueSpan single{&large, 1u};
    VmError error;
    const bool singleSynchronized = context->synchronizeMemoryRoots(&single, 1u, nullptr, &error);
    if (!singleSynchronized)
        std::abort();
    assert(singleSynchronized);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(context->memoryAccountant != nullptr);
    const auto oneValueBytes = context->memoryAccountant->externalBytes();
    assert(oneValueBytes >= 60u);

    const Value shared[]{large, large};
    const ScriptValueSpan duplicated{shared, 2u};
    const bool sharedSynchronized =
        context->synchronizeMemoryRoots(&duplicated, 1u, nullptr, &error);
    if (!sharedSynchronized)
        std::abort();
    assert(sharedSynchronized);
    assert(context->memoryAccountant->externalBytes() == oneValueBytes);

    auto stringQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(4096u);
    auto listQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(4096u);
    Value mixedString;
    {
        arp::core::CpuMemoryQuotaScope scope(stringQuota);
        mixedString = makeString(
            std::string_view("mixed-quota-string-0123456789012345678901234567890123456789"));
    }
    Value mixedList;
    {
        arp::core::CpuMemoryQuotaScope scope(listQuota);
        mixedList = Value{arp::reflection::makeEmptyHList()};
        mixedList.ptr<arp::reflection::hlist>()->push_back(mixedString);
    }
    arp::core::pmr::Set<const void*> mixedIdentities{&arp::core::defaultCpuMemoryResource()};
    std::size_t mixedExpectedBytes = 0u;
    const auto recordExpected = [&](const void* address) {
        const auto allocation = arp::core::inspectCpuAllocationContaining(address);
        if (!allocation.found)
            std::abort();
        if (mixedIdentities.insert(allocation.allocationIdentity).second)
            mixedExpectedBytes += allocation.sizeBytes;
    };
    recordExpected(mixedList.storageIdentity());
    recordExpected(mixedList.ptr<arp::reflection::hlist>()->data());
    recordExpected(mixedString.storageIdentity());
    recordExpected(mixedString.ptr<arp::core::pmr::String>()->data());
    const ScriptValueSpan mixedRoot{&mixedList, 1u};
    error = {};
    if (!context->synchronizeMemoryRoots(&mixedRoot, 1u, nullptr, &error))
        std::abort();
    assert(error.code == ScriptDiagnosticCode::None);
    assert(context->memoryAccountant->externalBytes() == mixedExpectedBytes);

    Value cyclic{arp::reflection::makeEmptyHList()};
    cyclic.ptr<arp::reflection::hlist>()->push_back(cyclic);
    const ScriptValueSpan cycleRoot{&cyclic, 1u};
    const bool cycleSynchronized = context->synchronizeMemoryRoots(&cycleRoot, 1u, nullptr, &error);
    if (!cycleSynchronized)
        std::abort();
    assert(cycleSynchronized);
    assert(context->memoryAccountant->visitedValueCount() == 1u);

    Value opaque{OpaqueQuotaValue{7}};
    const ScriptValueSpan opaqueRoot{&opaque, 1u};
    const auto retainedBytesBeforeOpaque = context->memoryAccountant->externalBytes();
    const auto liveBytesBeforeOpaque = context->memoryQuota->liveBytes();
    error = {};
    const bool opaqueSynchronized =
        context->synchronizeMemoryRoots(&opaqueRoot, 1u, nullptr, &error);
    if (opaqueSynchronized)
        std::abort();
    assert(!opaqueSynchronized);
    assert(error.code == ScriptDiagnosticCode::AotSwitchUnsupported);
    arp::core::pmr::String expectedOpaqueDiagnostic(
        "DirectNative opaque Value has no memory descriptor",
        &arp::core::defaultCpuMemoryResource());
    if (opaque.class_()) {
        expectedOpaqueDiagnostic += ": ";
        expectedOpaqueDiagnostic += opaque.class_()->name();
    }
    assert(error.message == expectedOpaqueDiagnostic);
    assert(context->memoryAccountant->externalBytes() == retainedBytesBeforeOpaque);
    assert(context->memoryQuota->liveBytes() == liveBytesBeforeOpaque);

    auto limited = arp::core::makeShared<DirectNativeExecutionContext>();
    limited->budget = context->budget;
    limited->budget.maxMemoryBytes = 32u;
    limited->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(32u);
    limited->resetBudget(limited->budget);
    error = {};
    const bool limitedSynchronized = limited->synchronizeMemoryRoots(&single, 1u, nullptr, &error);
    if (limitedSynchronized)
        std::abort();
    assert(!limitedSynchronized);
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(limited->memoryBudgetFailed);
}

// Bounded reverse bridge fixture: a DirectNative parent receives a real
// Interpreter ScriptGenerator through the module-export contract and forwards
// only the declared V3 protocol operations. This deliberately stays in the
// contract test target, which explicitly links the Interpreter runtime.
struct ReverseInterpreterExportState {
    Value child;
    arp::core::CpuMemoryQuotaLease memoryLease;
};

bool describeReverseInterpreterValue(
    void* userData, const Value& value, ScriptValueMemoryDescriptor& descriptor) noexcept
{
    auto* state = static_cast<ReverseInterpreterExportState*>(userData);
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

bool resolveReverseInterpreterChild(
    void* userData, const char* moduleId, const char* exportName, Value* result, VmError* error)
{
    auto* state = static_cast<ReverseInterpreterExportState*>(userData);
    if (state == nullptr || result == nullptr || moduleId == nullptr || exportName == nullptr
        || std::string_view(moduleId) != "interp" || std::string_view(exportName) != "child") {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotMissingEntry;
            error->message = "reverse Interpreter child export is unavailable";
        }
        return false;
    }
    *result = state->child;
    return true;
}

class ReverseInterpreterParentFrame final : public DirectNativeFrameV5 {
public:
    explicit ReverseInterpreterParentFrame(DirectNativeExecutionContext* owner = nullptr)
        : DirectNativeFrameV5("reverse-interpreter-parent", "reverse-interpreter-parent#identity"),
          owner_(owner)
    {
        if (owner_ != nullptr)
            owner_->retainContinuationFrame(sizeof(*this));
    }

    ~ReverseInterpreterParentFrame() override
    {
        if (!isNil(child)) {
            if (const auto* bridge = optionalGeneratorBridgeV3(); bridge != nullptr
                && bridge->isGenerator != nullptr && bridge->isGenerator(child)
                && bridge->close != nullptr)
                bridge->close(child);
        }
        if (owner_ != nullptr)
            owner_->releaseContinuationFrame(sizeof(*this));
    }

    DirectNativeExecutionContext* owner_ = nullptr;
    Value child;
};

Value reverseInterpreterParentEntry(DirectNativeExecutionContext& context,
    const Value* args,
    std::size_t argumentCount,
    DirectNativeResumeStateV5* suppliedResume,
    VmError* error)
{
    if (suppliedResume == nullptr)
        return makeDirectNativeGeneratorV5(
            context, &reverseInterpreterParentEntry, args, argumentCount, error);
    auto& resume = *suppliedResume;
    if (resume.frame == nullptr) {
        if (!context.beginContinuationFrame(sizeof(ReverseInterpreterParentFrame), error))
            return makeNil();
        resume.frame = arp::core::makeUnique<ReverseInterpreterParentFrame>(&context);
    }
    auto* frame = static_cast<ReverseInterpreterParentFrame*>(resume.frame.get());
    if (frame == nullptr) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotArtifactInvalid;
            error->message = "reverse Interpreter parent frame is invalid";
        }
        return makeNil();
    }
    if (isNil(frame->child) && !context.resolveModuleExport("interp", "child", frame->child, error))
        return makeNil();
    const auto* bridge = optionalGeneratorBridgeV3();
    GeneratorIdentityV3 identity;
    if (bridge == nullptr || bridge->isGenerator == nullptr || bridge->queryIdentity == nullptr
        || !bridge->queryIdentity(frame->child, identity)
        || !context.acceptsGeneratorIdentity(identity)) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotSwitchUnsupported;
            error->message = "reverse Interpreter child identity negotiation failed";
        }
        return makeNil();
    }
    VmError childError;
    Value result;
    if (resume.injectionKind == DirectNativeInjectionKindV5::Send
        && bridge->resumeSend != nullptr) {
        result = bridge->resumeSend(frame->child, resume.injectionValue, &childError);
    } else if (resume.injectionKind == DirectNativeInjectionKindV5::Throw
        && bridge->resumeThrow != nullptr) {
        result = bridge->resumeThrow(frame->child, resume.injectionValue, &childError);
    } else if (resume.injectionKind == DirectNativeInjectionKindV5::None
        && bridge->resume != nullptr) {
        result = bridge->resume(frame->child, &childError);
    } else {
        childError.code = ScriptDiagnosticCode::AotSwitchUnsupported;
        childError.message = "reverse Interpreter child bridge operation is unavailable";
    }
    resume.injectionKind = DirectNativeInjectionKindV5::None;
    resume.injectionValue = makeNil();
    if (childError.code == ScriptDiagnosticCode::Suspended) {
        // A bridge suspension is injectable only when the separate capability
        // contract confirms an explicit pending-await injection operation.
        const auto* capability = optionalGeneratorBridgeV3Capability();
        resume.awaitInjectionSupported = capability != nullptr && capability->isGenerator != nullptr
            && capability->isGenerator(frame->child)
            && capability->supportsPendingAwaitInjection != nullptr
            && capability->supportsPendingAwaitInjection(frame->child);
        resume.suspendKind = DirectNativeSuspendKindV5::Await;
        if (error != nullptr)
            *error = std::move(childError);
        return result;
    }
    if (childError.code != ScriptDiagnosticCode::None) {
        if (error != nullptr)
            *error = std::move(childError);
        return makeNil();
    }
    if (bridge->isDone != nullptr && bridge->isDone(frame->child)) {
        resume.frame.reset();
        resume.suspendKind = DirectNativeSuspendKindV5::None;
        return makeInt(99);
    }
    // The Interpreter bridge intentionally normalizes a yielded child result
    // to a successful call. The parent restores the DirectNative suspension
    // marker from the provider's done bit without treating this as completion.
    resume.suspendKind = DirectNativeSuspendKindV5::Yield;
    resume.awaitInjectionSupported = false;
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::Suspended;
        error->message = "reverse Interpreter child yielded";
    }
    return result;
}

Value quotaGeneratorEntry(DirectNativeExecutionContext& context,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5* resume,
    VmError* error)
{
    if (resume == nullptr)
        return makeNil();
    if (!context.beginContinuationFrame(sizeof(QuotaTestFrame), error))
        return makeNil();
    auto frame = arp::core::makeUnique<QuotaTestFrame>(&context);
    frame->retained.resize(16u, makeInt(1));
    resume->frame = std::move(frame);
    resume->suspendKind = DirectNativeSuspendKindV5::Yield;
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::Suspended;
        error->message = "quota test yield";
    }
    return makeInt(1);
}

Value externalValueGeneratorEntry(DirectNativeExecutionContext& context,
    const Value* arguments,
    std::size_t argumentCount,
    DirectNativeResumeStateV5* resume,
    VmError* error)
{
    if (resume == nullptr || arguments == nullptr || argumentCount != 1u) {
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::AotArtifactInvalid;
            error->message = "external Value generator arguments are invalid";
        }
        return makeNil();
    }
    if (!context.beginContinuationFrame(sizeof(ExternalValueQuotaFrame), error))
        return makeNil();
    auto frame = arp::core::makeUnique<ExternalValueQuotaFrame>(context, arguments[0]);
    const ScriptValueSpan frameRoot{&frame->retained(), 1u};
    if (!context.synchronizeMemoryRootsForOwner(frame.get(), &frameRoot, 1u, nullptr, error))
        return makeNil();
    resume->frame = std::move(frame);
    resume->suspendKind = DirectNativeSuspendKindV5::Yield;
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::Suspended;
        error->message = "external Value retained";
    }
    return makeInt(1);
}

Value failingGeneratorEntry(DirectNativeExecutionContext&,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5* resume,
    VmError* error)
{
    if (resume != nullptr)
        resume->frame = arp::core::makeUnique<QuotaTestFrame>();
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::UserRaise;
        error->message = "terminal generator failure";
    }
    return makeInt(99);
}

Value suspendedGeneratorWithoutErrorEntry(DirectNativeExecutionContext&,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5* resume,
    VmError*)
{
    if (resume != nullptr) {
        resume->frame = arp::core::makeUnique<QuotaTestFrame>();
        resume->pc = 1u;
        resume->suspendKind = DirectNativeSuspendKindV5::Yield;
    }
    return makeInt(7);
}

Value pendingAwaitGeneratorEntry(DirectNativeExecutionContext&,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5* resume,
    VmError* error)
{
    if (resume != nullptr) {
        resume->frame = arp::core::makeUnique<QuotaTestFrame>();
        resume->suspendKind = DirectNativeSuspendKindV5::Await;
    }
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::Suspended;
        error->message = "pending await test";
    }
    return makeNil();
}

Value injectablePendingAwaitEntry(DirectNativeExecutionContext&,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5* resume,
    VmError* error)
{
    if (resume == nullptr)
        return makeNil();
    if (resume->frame == nullptr) {
        resume->frame = arp::core::makeUnique<QuotaTestFrame>();
        resume->awaitInjectionSupported = true;
        resume->suspendKind = DirectNativeSuspendKindV5::Await;
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::Suspended;
            error->message = "injectable pending await test";
        }
        return makeNil();
    }
    if (resume->injectionKind == DirectNativeInjectionKindV5::Send) {
        resume->injectionKind = DirectNativeInjectionKindV5::None;
        resume->injectionValue = makeNil();
        resume->awaitInjectionSupported = false;
        resume->suspendKind = DirectNativeSuspendKindV5::Yield;
        resume->pc = 1u;
        if (error != nullptr) {
            error->code = ScriptDiagnosticCode::Suspended;
            error->message = "injected send yielded";
        }
        return makeInt(42);
    }
    return makeNil();
}

Value resourceLimitedEntry(DirectNativeExecutionContext&,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5*,
    VmError* error)
{
    if (error != nullptr) {
        error->code = ScriptDiagnosticCode::ResourceLimitExceeded;
        error->message = "synthetic resource limit";
    }
    return makeInt(123);
}

bool initializerObservedPackageOwner = false;

Value ownerProbeInitializer(DirectNativeExecutionContext& context,
    const Value*,
    std::size_t,
    DirectNativeResumeStateV5*,
    VmError*)
{
    initializerObservedPackageOwner = context.packageOwner != nullptr;
    return makeNil();
}

AotPackageDescriptorV5 validPackage()
{
    static const AotFunctionDescriptorV5 functions[] = {
        {"main",
            "pkg/module@1/main#identity",
            kHash,
            "arp_aot_main",
            &directEntry,
            kDirectNativeEntryAbiVersion,
            AotFunctionExecutionKind::DirectNative,
            AotOsrCapability::FunctionBoundary | AotOsrCapability::OperationBoundary},
    };
    static AotModuleDescriptorV5 modules[] = {
        {kAotContractVersion,
            "pkg@1#identity",
            "module",
            1u,
            "module@1#identity",
            kHash,
            kHash,
            kHash,
            kHash,
            kSemanticAbiVersion,
            kDirectNativeEntryAbiVersion,
            currentAotPlatform(),
            {10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u},
            nullptr,
            0u,
            functions,
            1u,
            0u,
            nullptr,
            kHash},
    };
    static AotPackageDescriptorV5 package{kAotContractVersion,
        kAotPackageManifestType,
        "pkg",
        1u,
        "pkg@1#identity",
        kHash,
        AotPlatform::None,
        modules,
        1u};
    package.platforms = currentAotPlatform();
    modules[0].platforms = currentAotPlatform();
    return package;
}

void strictContractRejectsLegacyAndIncompleteArtifacts()
{
    auto package = validPackage();
    assert(validateAotPackageV5(package, currentAotPlatform()).ok());

    auto legacy = package;
    legacy.contractVersion = 4u;
    const auto legacyResult = validateAotPackageV5(legacy, currentAotPlatform());
    assert(legacyResult.failed());
    assert(legacyResult.diagnostic().code == ScriptDiagnosticCode::AotVersionMismatch);

    auto legacyType = package;
    legacyType.manifestType = "arp.script.aot-package/v4";
    const auto legacyTypeResult = validateAotPackageV5(legacyType, currentAotPlatform());
    assert(legacyTypeResult.failed());
    assert(legacyTypeResult.diagnostic().code == ScriptDiagnosticCode::AotArtifactInvalid);

    auto unknownPlatform = package;
    unknownPlatform.platforms = static_cast<AotPlatform>(8u);
    const auto unknownPlatformResult = validateAotPackageV5(unknownPlatform, currentAotPlatform());
    assert(unknownPlatformResult.failed());
    assert(unknownPlatformResult.diagnostic().code == ScriptDiagnosticCode::AotArtifactInvalid);

    auto unboundedModule = package.modules[0];
    unboundedModule.budget.maxMemoryBytes = 0u;
    auto unbounded = package;
    unbounded.modules = &unboundedModule;
    assert(validateAotPackageV5(unbounded, currentAotPlatform()).failed());

    auto wrongPlatformModule = package.modules[0];
    wrongPlatformModule.platforms = AotPlatform::None;
    auto wrongPlatform = package;
    wrongPlatform.modules = &wrongPlatformModule;
    assert(validateAotPackageV5(wrongPlatform, currentAotPlatform()).failed());

    auto dependentModule = package.modules[0];
    auto dependencyModule = package.modules[0];
    dependencyModule.moduleId = "dependency";
    dependencyModule.artifactIdentity = "dependency@1#identity";
    AotModuleDependencyV5 dependency{dependencyModule.moduleId,
        dependencyModule.moduleVersion,
        dependencyModule.artifactIdentity,
        dependencyModule.exportContractHash};
    dependentModule.dependencies = &dependency;
    dependentModule.dependencyCount = 1u;
    AotModuleDescriptorV5 closedModules[] = {dependentModule, dependencyModule};
    auto closedPackage = package;
    closedPackage.modules = closedModules;
    closedPackage.moduleCount = 2u;
    assert(validateAotPackageV5(closedPackage, currentAotPlatform()).ok());
    dependency.artifactIdentity = "stale-dependency@1#identity";
    assert(validateAotPackageV5(closedPackage, currentAotPlatform()).failed());
}

void pluginDependencyContractIsValidatedBeforeRegistration()
{
    auto package = validPackage();
    static const AotServiceDependencyV5 services[] = {
        {"arp.runtime.async", 1u},
    };
    static const AotPluginDependencyV5 plugins[] = {
        {"arp_resources", 1u, kHash, kHash, services, 1u},
    };
    auto module = package.modules[0];
    module.pluginDependencies = plugins;
    module.pluginDependencyCount = 1u;
    package.modules = &module;
    assert(validateAotPackageV5(package, currentAotPlatform()).ok());
    ScriptAotPackageRegistry registry;
    registry.setPluginDependencyResolver(&acceptResourcePluginDependency, nullptr);
    assert(registry.registerPackage(package).ok());

    ScriptAotPackageRegistry missingResolver;
    assert(missingResolver.registerPackage(package).failed());

    auto bad = module;
    static const AotServiceDependencyV5 duplicateServices[] = {
        {"arp.runtime.async", 1u},
        {"arp.runtime.async", 2u},
    };
    static const AotPluginDependencyV5 duplicatePlugin[] = {
        {"arp_resources", 1u, kHash, kHash, duplicateServices, 2u},
    };
    bad.pluginDependencies = duplicatePlugin;
    auto badPackage = package;
    badPackage.modules = &bad;
    assert(validateAotPackageV5(badPackage, currentAotPlatform()).failed());
}

void registryUsesFullPackageIdentity()
{
    auto package = validPackage();
    ScriptAotPackageRegistry registry;
    assert(registry.registerPackage(package).ok());
    assert(registry.findPackage("pkg@1#identity") == &package);
    assert(registry.findModule("pkg@1#identity", "module") == &package.modules[0]);
    assert(registry.findModule("pkg", "module") == nullptr);

    const auto duplicate = registry.registerPackage(package);
    assert(duplicate.failed());
    assert(duplicate.diagnostic().code == ScriptDiagnosticCode::AotDuplicateEntry);

    auto host = registry.registrationHost();
    assert(host.contractVersion == 5u);
    assert(host.registerPackage != nullptr);

    ScriptAotModuleInstanceV5 instance;
    assert(createAotOnlyModuleInstanceV5(registry, "pkg@1#identity", "module", instance).ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    VmError invokeError;
    (void)dispatcher.invoke("main", nullptr, 0u, &invokeError);
    assert(invokeError.code == ScriptDiagnosticCode::None);
    assert(dispatcher.telemetry().size() == 1u);
    assert(dispatcher.telemetry()[0].policy == "AotOnly");
    assert(dispatcher.telemetry()[0].osrBoundary == "operation");

    ScriptAotModuleInstanceV5 missing;
    const auto missingResult =
        createAotOnlyModuleInstanceV5(registry, "pkg@1#identity", "missing", missing);
    assert(missingResult.failed());
    assert(missingResult.diagnostic().code == ScriptDiagnosticCode::AotModuleUnavailable);
}

void invokeMethodPrependsExactlyOneReceiver()
{
    auto package = validPackage();
    AotFunctionDescriptorV5 method = *package.modules[0].functions;
    method.functionId = "method";
    method.functionIdentity = "pkg/module@1/method#identity";
    method.entrySymbol = "arp_aot_method";
    method.entry = &receiverMethodEntry;
    AotModuleDescriptorV5 module = package.modules[0];
    module.functions = &method;
    module.functionCount = 1u;
    package.modules = &module;

    ScriptAotPackageRegistry registry;
    assert(registry.registerPackage(package).ok());
    ScriptAotModuleInstanceV5 instance;
    assert(
        createAotOnlyModuleInstanceV5(registry, package.packageIdentity, module.moduleId, instance)
            .ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));

    auto* receiverClass = arp::reflection::db::instance().find_class("AotDispatcherReceiver");
    if (receiverClass == nullptr) {
        receiverClass = arp::reflection::db::instance().register_class(
            "AotDispatcherReceiver", arp::reflection::instance_t, 0u, 0u, nullptr);
    }
    assert(receiverClass != nullptr);
    arp::reflection::instance reflectedReceiver{receiverClass};
    const Value receiver{reflectedReceiver};

    observedMethodArgumentCount = 0u;
    observedMethodReceiver = false;
    VmError error;
    const auto result = dispatcher.invokeMethod("method", receiver, nullptr, 0u, &error);
    bool success = false;
    assert(error.code == ScriptDiagnosticCode::None);
    assert(tryGetBool(result, success) && success);
    assert(observedMethodArgumentCount == 1u);
    assert(observedMethodReceiver);
    assert(dispatcher.telemetry().size() == 1u);
    assert(dispatcher.telemetry()[0].argumentCount == 1u);
    assert(dispatcher.telemetry()[0].explicitArgumentCount == 0u);
    assert(dispatcher.telemetry()[0].receiverPrepended);
}

void packageOwnerIsAvailableDuringModuleInitialization()
{
    auto package = validPackage();
    AotFunctionDescriptorV5 initializer = *package.modules[0].functions;
    initializer.functionId = "init";
    initializer.functionIdentity = "pkg/module@1/init#identity";
    initializer.entrySymbol = "arp_aot_init";
    initializer.entry = &ownerProbeInitializer;
    AotModuleDescriptorV5 module = package.modules[0];
    module.functions = &initializer;
    module.functionCount = 1u;
    module.moduleInitFunctionId = "init";
    package.modules = &module;

    ScriptAotPackageRegistry registry;
    auto owner = arp::core::makeShared<int>(7);
    assert(registry.registerPackage(package, owner).ok());
    initializerObservedPackageOwner = false;
    ScriptAotModuleInstanceV5 instance;
    assert(
        createAotOnlyModuleInstanceV5(registry, package.packageIdentity, module.moduleId, instance)
            .ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    VmError error;
    (void)dispatcher.invoke("init", nullptr, 0u, &error);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(initializerObservedPackageOwner);
}

void moduleInitializerTelemetryUsesZeroArguments()
{
    auto package = validPackage();
    AotFunctionDescriptorV5 functions[2]{
        *package.modules[0].functions,
        *package.modules[0].functions,
    };
    functions[0].functionId = "init";
    functions[0].functionIdentity = "pkg/module@1/init#identity";
    functions[0].entrySymbol = "arp_aot_init";
    functions[1].functionId = "main";
    functions[1].functionIdentity = "pkg/module@1/main#identity";
    functions[1].entrySymbol = "arp_aot_main";
    AotModuleDescriptorV5 module = package.modules[0];
    module.functions = functions;
    module.functionCount = 2u;
    module.moduleInitFunctionId = "init";
    package.modules = &module;

    ScriptAotPackageRegistry registry;
    assert(registry.registerPackage(package).ok());
    ScriptAotModuleInstanceV5 instance;
    assert(
        createAotOnlyModuleInstanceV5(registry, package.packageIdentity, module.moduleId, instance)
            .ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    VmError error;
    (void)dispatcher.invoke("main", nullptr, 0u, &error);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(dispatcher.telemetry().size() == 2u);
    assert(dispatcher.telemetry()[0].functionId == "init");
    assert(dispatcher.telemetry()[0].argumentCount == 0u);
    assert(dispatcher.telemetry()[0].explicitArgumentCount == 0u);
    assert(!dispatcher.telemetry()[0].receiverPrepended);
    assert(dispatcher.telemetry()[1].functionId == "main");
    assert(dispatcher.telemetry()[1].argumentCount == 0u);
}

void directNativeMemoryQuotaIsEnforcedCumulatively()
{
    auto globalsPackage = validPackage();
    auto globalsModule = globalsPackage.modules[0];
    globalsModule.globalCount = 4096u;
    globalsModule.moduleInitFunctionId = "main";
    globalsModule.budget.maxMemoryBytes = 64u;
    globalsPackage.modules = &globalsModule;
    ScriptAotPackageRegistry globalsRegistry;
    assert(globalsRegistry.registerPackage(globalsPackage).ok());
    ScriptAotModuleInstanceV5 globalsInstance;
    const auto globalsResult = createAotOnlyModuleInstanceV5(
        globalsRegistry, globalsPackage.packageIdentity, globalsModule.moduleId, globalsInstance);
    assert(globalsResult.failed());
    assert(globalsResult.diagnostic().code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(globalsInstance.context == nullptr);

    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u});
    VmError error;
    Value firstValue =
        makeDirectNativeGeneratorV5(*context, &quotaGeneratorEntry, nullptr, 0u, &error);
    Value secondValue =
        makeDirectNativeGeneratorV5(*context, &quotaGeneratorEntry, nullptr, 0u, &error);
    auto* first = asDirectNativeGeneratorV5(firstValue);
    auto* second = asDirectNativeGeneratorV5(secondValue);
    assert(first != nullptr && second != nullptr);
    int yielded = 0;
    assert(tryGetInt(resumeDirectNativeGeneratorV5(*first, &error), yielded) && yielded == 1);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(first->state->resume.frame != nullptr);

    const auto retainedBytes = context->memoryQuota->liveBytes();
    context->memoryQuota->setLimitBytes(retainedBytes + 1u);
    (void)resumeDirectNativeGeneratorV5(*second, &error);
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(second->state->resume.frame == nullptr);
    assert(second->state->captures.empty());
    assert(second->state->done);
    assert(context->memoryBudgetFailed);
    assert(first->state->resume.frame != nullptr);
}

void retainedValueGraphKeepsIndependentGeneratorOwners()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 8u, 1u << 20u});

    const Value firstArgument = makeString(std::string_view(
        "first-owner-012345678901234567890123456789012345678901234567890123456789"));
    const Value secondArgument = makeString(std::string_view(
        "second-owner-987654321098765432109876543210987654321098765432109876543210"));
    VmError error;
    Value firstValue = makeDirectNativeGeneratorV5(
        *context, &externalValueGeneratorEntry, &firstArgument, 1u, &error);
    auto* first = asDirectNativeGeneratorV5(firstValue);
    assert(first != nullptr);
    const auto firstOwnerBytes = context->memoryAccountant->externalBytes();
    assert(firstOwnerBytes > 64u);

    Value secondValue = makeDirectNativeGeneratorV5(
        *context, &externalValueGeneratorEntry, &secondArgument, 1u, &error);
    auto* second = asDirectNativeGeneratorV5(secondValue);
    assert(second != nullptr);
    const auto bothOwnerBytes = context->memoryAccountant->externalBytes();
    assert(bothOwnerBytes > firstOwnerBytes);

    int yielded = 0;
    assert(tryGetInt(resumeDirectNativeGeneratorV5(*first, &error), yielded));
    assert(error.code == ScriptDiagnosticCode::None && yielded == 1);
    assert(tryGetInt(resumeDirectNativeGeneratorV5(*second, &error), yielded));
    assert(error.code == ScriptDiagnosticCode::None && yielded == 1);
    const auto suspendedOwnerBytes = context->memoryAccountant->externalBytes();
    assert(suspendedOwnerBytes > firstOwnerBytes);

    closeDirectNativeGeneratorV5(*first);
    const auto secondOwnerBytes = context->memoryAccountant->externalBytes();
    assert(secondOwnerBytes > 64u && secondOwnerBytes < suspendedOwnerBytes);
    assert(second->state->resume.frame != nullptr);

    closeDirectNativeGeneratorV5(*second);
    assert(context->memoryAccountant->externalBytes() == 0u);
}

void reusedAccountingWorkspaceDoesNotRetainVisitedIdentities()
{
    auto quota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    ScriptValueMemoryAccountant accountant(quota);
    arp::core::pmr::Vector<Value> globals;
    Value root{arp::reflection::makeEmptyHList()};
    root.ptr<arp::reflection::hlist>()->push_back(root);
    ScriptValueSpan span{&root, 1};
    VmError error;
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        assert(accountant.synchronize(globals, &span, 1, nullptr, 128, nullptr, nullptr, &error));
        assert(accountant.visitedValueCount() == 1);
    }
    // The root address/identity is unchanged, but its reachable graph grows.
    root.ptr<arp::reflection::hlist>()->push_back(
        makeString(std::string_view("new reachable value")));
    assert(accountant.synchronize(globals, &span, 1, nullptr, 128, nullptr, nullptr, &error));
    assert(accountant.visitedValueCount() == 2);
    const auto charged = accountant.externalBytes();
    assert(!accountant.synchronize(globals, &span, 1, nullptr, 1, nullptr, nullptr, &error));
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded
        && accountant.externalBytes() == charged);
    error = {};
    assert(accountant.synchronize(globals, &span, 1, nullptr, 128, nullptr, nullptr, &error));
    assert(accountant.visitedValueCount() == 2);
    accountant.reset();
    assert(!accountant.externalBytes() && !quota->liveBytes());
    assert(accountant.synchronize(globals, &span, 1, nullptr, 128, nullptr, nullptr, &error));
    assert(accountant.externalBytes() == charged);

    struct RecursiveDescriptorState {
        ScriptValueMemoryAccountant* accountant;
        const arp::core::pmr::Vector<Value>* globals;
        const ScriptValueSpan* inner;
        bool visited = false;
    } state{&accountant, &globals, &span};

    const auto describe = +[](void* user,
                               const Value&,
                               ScriptValueMemoryDescriptor& result) noexcept {
        auto& nested = *static_cast<RecursiveDescriptorState*>(user);
        VmError innerError;
        nested.visited = nested.accountant->synchronizeOwner(
            &nested, *nested.globals, nested.inner, 1, nullptr, 128, nullptr, nullptr, &innerError);
        result = {&nested, 0, true};
        return nested.visited;
    };
    Value opaque{OpaqueQuotaValue{7}};
    const ScriptValueSpan outer{&opaque, 1};
    assert(accountant.synchronize(globals, &outer, 1, nullptr, 128, describe, &state, &error));
    assert(state.visited && accountant.visitedValueCount() == 1);
    accountant.releaseOwner(&state);
    root.ptr<arp::reflection::hlist>()->clear(); // Break the intentional cycle.
    accountant.reset();
    Value ownedGraph;
    {
        arp::core::CpuMemoryQuotaScope scope(quota);
        ownedGraph = Value{arp::reflection::makeEmptyHList()};
        auto& nodes = *ownedGraph.ptr<arp::reflection::hlist>();
        for (int i = 0; i < 128; ++i)
            nodes.push_back(Value{i});
    }
    const ScriptValueSpan ownedRoot{&ownedGraph, 1};
    for (unsigned i = 0; i < 2; ++i)
        assert(
            accountant.synchronize(globals, &ownedRoot, 1, nullptr, 256, nullptr, nullptr, &error));
    const auto allocationsBefore = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (unsigned i = 0; i < 8; ++i) {
        assert(
            accountant.synchronize(globals, &ownedRoot, 1, nullptr, 256, nullptr, nullptr, &error));
        assert(accountant.visitedValueCount() == 129);
    }
    const auto allocationsAfter = arp::core::defaultCpuAllocator().stats().allocationCount;
    std::fprintf(stderr,
        "[accounting-workspace] repeatedAllocations=%llu externalBytes=%zu\n",
        static_cast<unsigned long long>(allocationsAfter - allocationsBefore),
        accountant.externalBytes());
    assert(allocationsAfter == allocationsBefore);
}

void directNativeContinuationFrameCountIsEnforced()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 1u, 1u << 20u});

    VmError error;
    const Value firstValue =
        makeDirectNativeGeneratorV5(*context, &quotaGeneratorEntry, nullptr, 0u, &error);
    const Value secondValue =
        makeDirectNativeGeneratorV5(*context, &quotaGeneratorEntry, nullptr, 0u, &error);
    auto* first = asDirectNativeGeneratorV5(firstValue);
    auto* second = asDirectNativeGeneratorV5(secondValue);
    assert(first != nullptr && second != nullptr);

    int yielded = 0;
    assert(tryGetInt(resumeDirectNativeGeneratorV5(*first, &error), yielded) && yielded == 1);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(context->continuationFrameCount == 1u);

    const Value rejected = resumeDirectNativeGeneratorV5(*second, &error);
    assert(isNil(rejected));
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(second->state->resume.frame == nullptr);
    assert(second->state->done);
    assert(context->continuationFrameCount == 1u);

    closeDirectNativeGeneratorV5(*first);
    assert(context->continuationFrameCount == 0u);
}

void directNativeContinuationFrameBytesAreCumulative()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>();
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 4u, sizeof(QuotaTestFrame) * 2u - 1u});

    VmError error;
    assert(context->beginContinuationFrame(sizeof(QuotaTestFrame), &error));
    arp::core::CpuMemoryQuotaScope quotaScope{context->memoryQuota};
    auto first = arp::core::makeUnique<QuotaTestFrame>(context.get());
    assert(context->continuationFrameBytes == sizeof(QuotaTestFrame));
    assert(context->memoryQuota->liveBytes() >= sizeof(QuotaTestFrame));
    assert(context->beginContinuationFrame(sizeof(QuotaTestFrame), &error));
    bool allocationRejected = false;
    try {
        auto second = arp::core::makeUnique<QuotaTestFrame>(context.get());
    } catch (const std::bad_alloc&) {
        allocationRejected = true;
    }
    assert(allocationRejected);
    assert(context->continuationFrameCount == 1u);

    auto lowered = context->budget;
    lowered.maxMemoryBytes = context->memoryQuota->liveBytes() - 1u;
    context->resetBudget(lowered);
    assert(context->memoryBudgetFailed);
    first.reset();
    assert(context->continuationFrameBytes == 0u);
}

void directNativeBudgetResetRejectsRetainedState()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u});
    VmError error;
    Value value = makeDirectNativeGeneratorV5(*context, &quotaGeneratorEntry, nullptr, 0u, &error);
    auto* generator = asDirectNativeGeneratorV5(value);
    assert(generator != nullptr);
    (void)resumeDirectNativeGeneratorV5(*generator, &error);
    assert(generator->state->resume.frame != nullptr);
    const auto retainedBytes = context->memoryQuota->liveBytes();
    assert(retainedBytes > 1u);

    auto lowered = context->budget;
    lowered.maxMemoryBytes = retainedBytes - 1u;
    context->resetBudget(lowered);
    assert(context->memoryBudgetFailed);

    VmError gateError;
    assert(!context->ensureGlobals(0u, &gateError));
    assert(gateError.code == ScriptDiagnosticCode::ResourceLimitExceeded);

    const Value rejected = resumeDirectNativeGeneratorV5(*generator, &error);
    assert(isNil(rejected));
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(generator->state->done);
    assert(generator->state->resume.frame == nullptr);
}

void directNativeGeneratorFailureIsTerminalAndNoReplay()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u});
    VmError error;
    Value value =
        makeDirectNativeGeneratorV5(*context, &failingGeneratorEntry, nullptr, 0u, &error);
    auto* generator = asDirectNativeGeneratorV5(value);
    assert(generator != nullptr);
    const Value failed = resumeDirectNativeGeneratorV5(*generator, &error);
    assert(error.code == ScriptDiagnosticCode::UserRaise);
    assert(isNil(failed));
    assert(generator->state->done);
    assert(generator->state->resume.frame == nullptr);
    const Value replay = resumeDirectNativeGeneratorV5(*generator, &error);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(isNil(replay));
}

void directNativeResourceLimitDiscardsReturnedValue()
{
    auto package = validPackage();
    AotFunctionDescriptorV5 function = *package.modules[0].functions;
    function.entry = &resourceLimitedEntry;
    AotModuleDescriptorV5 module = package.modules[0];
    module.functions = &function;
    package.modules = &module;

    ScriptAotPackageRegistry registry;
    assert(registry.registerPackage(package).ok());
    ScriptAotModuleInstanceV5 instance;
    assert(
        createAotOnlyModuleInstanceV5(registry, package.packageIdentity, module.moduleId, instance)
            .ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    VmError error;
    const Value result = dispatcher.invoke("main", nullptr, 0u, &error);
    assert(isNil(result));
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(dispatcher.telemetry().back().diagnostic == ScriptDiagnosticCode::ResourceLimitExceeded);
}

void directNativeOperationBoundaryHonorsCumulativeQuota()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(64u);
    context->resetBudget({100u, 8u, 16u, 16u, 8u, 4u, 8u});
    context->memoryQuota->setLimitBytes(64u);
    assert(context->memoryQuota->tryReserve(16u));

    DirectNativeResumeStateV5 resume;
    VmError error;
    assert(!context->beginOperation("main", 0u, resume, &error));
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
    assert(context->memoryBudgetFailed);
    context->memoryQuota->release(16u);
}

void directNativeEntryArgumentBudgetIsEnforced()
{
    auto package = validPackage();
    ScriptAotPackageRegistry registry;
    assert(registry.registerPackage(package).ok());
    ScriptAotModuleInstanceV5 instance;
    assert(
        createAotOnlyModuleInstanceV5(registry, package.packageIdentity, "module", instance).ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    arp::core::pmr::Vector<Value> arguments{&arp::core::defaultCpuMemoryResource()};
    arguments.resize(129u, makeNil());
    VmError error;
    (void)dispatcher.invoke("main", arguments.data(), arguments.size(), &error);
    assert(error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
}

void directNativeNullArgumentsFailClosed()
{
    auto package = validPackage();
    ScriptAotPackageRegistry registry;
    assert(registry.registerPackage(package).ok());
    ScriptAotModuleInstanceV5 instance;
    assert(
        createAotOnlyModuleInstanceV5(registry, package.packageIdentity, "module", instance).ok());
    DirectNativeDispatcherV5 dispatcher(std::move(instance));
    VmError error;
    const Value result = dispatcher.invoke("main", nullptr, 1u, &error);
    assert(isNil(result));
    assert(error.code == ScriptDiagnosticCode::AotArtifactInvalid);
    assert(dispatcher.telemetry().back().diagnostic == ScriptDiagnosticCode::AotArtifactInvalid);
}

void directNativeGeneratorSuspensionDoesNotRequireErrorPointer()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u});
    VmError error;
    Value value = makeDirectNativeGeneratorV5(
        *context, &suspendedGeneratorWithoutErrorEntry, nullptr, 0u, &error);
    auto* generator = asDirectNativeGeneratorV5(value);
    assert(generator != nullptr);
    const Value yielded = resumeDirectNativeGeneratorV5(*generator, nullptr);
    int number = 0;
    assert(tryGetInt(yielded, number) && number == 7);
    assert(!generator->state->done);
    assert(generator->state->resume.frame != nullptr);
    closeDirectNativeGeneratorV5(*generator);
    assert(generator->state->done);
}

void directNativePendingAwaitInjectionFailsClosed()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u});
    VmError error;
    Value value =
        makeDirectNativeGeneratorV5(*context, &pendingAwaitGeneratorEntry, nullptr, 0u, &error);
    auto* generator = asDirectNativeGeneratorV5(value);
    assert(generator != nullptr);
    (void)resumeDirectNativeGeneratorV5(*generator, &error);
    assert(error.code == ScriptDiagnosticCode::Suspended);
    assert(generator->state->resume.suspendKind == DirectNativeSuspendKindV5::Await);
    assert(generator->state->resume.frame != nullptr);

    (void)sendDirectNativeGeneratorV5(*generator, makeInt(1), &error);
    assert(error.code == ScriptDiagnosticCode::AotSwitchUnsupported);
    assert(generator->state->resume.suspendKind == DirectNativeSuspendKindV5::Await);
    assert(generator->state->resume.frame != nullptr);

    (void)throwDirectNativeGeneratorV5(*generator, makeString(std::string_view("boom")), &error);
    assert(error.code == ScriptDiagnosticCode::AotSwitchUnsupported);
    assert(generator->state->resume.suspendKind == DirectNativeSuspendKindV5::Await);
    assert(generator->state->resume.frame != nullptr);

    // close() has a defined backend-neutral meaning: discard the pending
    // await continuation and make the generator terminal. It does not claim
    // an awaitable cancellation callback that the host contract does not
    // provide.
    closeDirectNativeGeneratorV5(*generator);
    assert(generator->state->done);
    assert(generator->state->resume.frame == nullptr);
    assert(generator->state->resume.suspendKind == DirectNativeSuspendKindV5::None);
    (void)resumeDirectNativeGeneratorV5(*generator, &error);
    assert(error.code == ScriptDiagnosticCode::None);
}

void directNativePendingAwaitInjectionConsumesExplicitCapability()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(1u << 20u);
    context->lifetimeOwner = context;
    context->resetBudget({10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u});
    VmError error;
    Value value =
        makeDirectNativeGeneratorV5(*context, &injectablePendingAwaitEntry, nullptr, 0u, &error);
    auto* generator = asDirectNativeGeneratorV5(value);
    assert(generator != nullptr);
    (void)resumeDirectNativeGeneratorV5(*generator, &error);
    assert(error.code == ScriptDiagnosticCode::Suspended);
    assert(generator->state->resume.awaitInjectionSupported);

    const Value injected = sendDirectNativeGeneratorV5(*generator, makeInt(7), &error);
    int result = 0;
    assert(tryGetInt(injected, result) && result == 42);
    assert(error.code == ScriptDiagnosticCode::None);
    assert(generator->state->resume.suspendKind == DirectNativeSuspendKindV5::Yield);
    closeDirectNativeGeneratorV5(*generator);
}

void manifestRequiresDirectNativeAndExplicitBudgets()
{
    AotPackageManifestV5 manifest;
    manifest.packageId = "pkg";
    manifest.packageVersion = 1u;
    manifest.packageIdentity = "pkg@1#identity";
    manifest.manifestHash = kHash;
    manifest.platforms = currentAotPlatform();
    AotPackageManifestModuleV5 module;
    module.moduleId = "module";
    module.moduleVersion = 1u;
    module.artifactIdentity = "module@1#identity";
    module.exportContractHash = kHash;
    module.sourceHash = kHash;
    module.dependencyGraphHash = kHash;
    module.reflectionRegistryHash = kHash;
    module.hostContractHash = kHash;
    module.semanticAbiVersion = kSemanticAbiVersion;
    module.platforms = currentAotPlatform();
    module.budget = {10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u};
    AotPackageManifestFunctionV5 function;
    function.functionId = "main";
    function.functionIdentity = "pkg/module@1/main#identity";
    function.signatureHash = kHash;
    function.entrySymbol = "arp_aot_main";
    function.osrCapability = AotOsrCapability::FunctionBoundary;
    module.functions.push_back(std::move(function));
    manifest.modules.push_back(std::move(module));
    assert(validateAotPackageManifestV5(manifest, currentAotPlatform()).ok());

    manifest.modules[0].functions[0].entrySymbol.clear();
    assert(validateAotPackageManifestV5(manifest, currentAotPlatform()).failed());
}

DirectNativeCodegenOptionsV5 codegenOptions()
{
    DirectNativeCodegenOptionsV5 options;
    options.packageId = "unit";
    options.packageVersion = 1u;
    options.packageIdentity = "unit@1#identity";
    options.sourceHash = kHash;
    options.dependencyGraphHash = kHash;
    options.reflectionRegistryHash = kHash;
    options.hostContractHash = kHash;
    options.platforms = currentAotPlatform();
    options.budget = {10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u};
    return options;
}

void generatedPluginDependencyContractIsEmbedded()
{
    auto frontend =
        ScriptFrontend::compile(arp::core::pmr::String("def main() -> int:\n    return 1\n",
                                    &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("plugin_dependency.arp", &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("plugin_dependency", &arp::core::defaultCpuMemoryResource()));
    assert(frontend.ok());
    const auto lowered = ScriptLowerer::lower(frontend.module);
    assert(lowered.ok());

    auto options = codegenOptions();
    AotPluginDependencyManifestV5 dependency;
    dependency.moduleId = "arp_resources";
    dependency.moduleVersion = 1u;
    dependency.exportContractHash = kHash;
    dependency.reflectionRegistryHash = kHash;
    dependency.services.push_back(AotServiceDependencyManifestV5{"arp.runtime.async", 1u});
    options.pluginDependencies.push_back(std::move(dependency));
    const auto generated = generateDirectNativeModuleV5(lowered.module, options);
    assert(generated.ok());
    assert(generated.manifest.modules.size() == 1u);
    assert(generated.manifest.modules[0].pluginDependencies.size() == 1u);
    assert(generated.manifest.modules[0].pluginDependencies[0].moduleId == "arp_resources");
    assert(generated.manifestYaml.find("plugin-dependencies") != arp::core::pmr::String::npos);
    assert(generated.manifestYaml.find("arp.runtime.async") != arp::core::pmr::String::npos);
    assert(generated.source.find("AotPluginDependencyV5") != arp::core::pmr::String::npos);
    assert(generated.source.find("plugin_services_0") != arp::core::pmr::String::npos);
}

void generatedControlFlowHasNoExecutorTable()
{
    auto frontend = ScriptFrontend::compile(
        arp::core::pmr::String("def add(a: int, b: int) -> int:\n    return a + b\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("unit.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("unit", &arp::core::defaultCpuMemoryResource()));
    assert(frontend.ok());
    const auto lowered = ScriptLowerer::lower(frontend.module);
    assert(lowered.ok());
    const auto generated = generateDirectNativeModuleV5(lowered.module, codegenOptions());
    assert(generated.ok());
    assert(generated.source.find("goto aot_pc_") != arp::core::pmr::String::npos
        || generated.source.find("aot_pc_") != arp::core::pmr::String::npos);
    assert(generated.source.find("AotFunctionExecutionKind::DirectNative")
        != arp::core::pmr::String::npos);
    assert(generated.source.find("AotOperationV1") == arp::core::pmr::String::npos);
    assert(generated.source.find("executeAotFunction") == arp::core::pmr::String::npos);
    assert(
        generated.source.find("executeAotNativeValueFrameMachine") == arp::core::pmr::String::npos);
    assert(generated.source.find("beginContinuationFrame") != arp::core::pmr::String::npos);
    assert(generated.manifestYaml.find("bytecode") == arp::core::pmr::String::npos);

    auto constrainedOptions = codegenOptions();
    constrainedOptions.budget.maxFrameValues = 1u;
    const auto constrained = generateDirectNativeModuleV5(lowered.module, constrainedOptions);
    assert(!constrained.ok());
    assert(constrained.error.code == ScriptDiagnosticCode::ResourceLimitExceeded);
}

void unsupportedShapeFailsAtBuildTime()
{
    auto frontend = ScriptFrontend::compile(
        arp::core::pmr::String("def invoke(callable: () -> int) -> int:\n    return callable()\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("unsupported.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("unsupported", &arp::core::defaultCpuMemoryResource()));
    assert(frontend.ok());
    const auto lowered = ScriptLowerer::lower(frontend.module);
    assert(lowered.ok());
    const auto generated = generateDirectNativeModuleV5(lowered.module, codegenOptions());
    assert(!generated.ok());
    assert(generated.error.code == ScriptDiagnosticCode::AotArtifactInvalid);
    assert(generated.error.message.find("opcode=CallValue") != arp::core::pmr::String::npos);
    assert(generated.error.line > 0);
}

void nativeLoopRegionsRequireAClosedExecutionProof()
{
    const auto generate = [](const char* text) {
        auto frontend = ScriptFrontend::compile(
            arp::core::pmr::String(text, &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("region.arp", &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("region", &arp::core::defaultCpuMemoryResource()));
        assert(frontend.ok());
        const auto ir = ScriptLowerer::lower(frontend.module);
        assert(ir.ok());
        return generateDirectNativeModuleV5(ir.module, codegenOptions());
    };
    const auto pure = generate("def run(n: int) -> int:\n"
                               "    var i: int = 0\n"
                               "    var total: int = 0\n"
                               "    while i < n:\n"
                               "        total = total + i\n"
                               "        i = i + 1\n"
                               "    return total\n");
    assert(pure.ok());
    assert(pure.source.find("native_loop_local_") != arp::core::pmr::String::npos);
    assert(pure.source.find("aot_replay_pc_") != arp::core::pmr::String::npos);
    assert(pure.source.find("!arp::core::currentCpuMemoryQuota()") == arp::core::pmr::String::npos);
    const auto mutation = generate("def mutate(values: [int]) -> int:\n"
                                   "    values[0] = values[0] + 1\n"
                                   "    return values[0]\n"
                                   "def run(n: int) -> int:\n"
                                   "    var values: [int] = [0]\n"
                                   "    var i: int = 0\n"
                                   "    while i < n:\n"
                                   "        i = i + mutate(values)\n"
                                   "    return i\n");
    assert(mutation.ok());
    assert(mutation.source.find("native_loop_local_") == arp::core::pmr::String::npos);
}

void malformedHandlerCfgFailsDeterministically()
{
    auto frontend =
        ScriptFrontend::compile(arp::core::pmr::String("def guarded(value: int) -> int:\n"
                                                       "    try:\n"
                                                       "        return value\n"
                                                       "    except:\n"
                                                       "        return 0\n",
                                    &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("handler_cfg.arp", &arp::core::defaultCpuMemoryResource()),
            arp::core::pmr::String("handler_cfg", &arp::core::defaultCpuMemoryResource()));
    assert(frontend.ok());
    const auto lowered = ScriptLowerer::lower(frontend.module);
    assert(lowered.ok());

    auto malformed = lowered.module;
    auto function = std::find_if(
        malformed.functions.begin(), malformed.functions.end(), [](const auto& candidate) {
            return !candidate.exceptionRegions.empty();
        });
    assert(function != malformed.functions.end());
    function->exceptionRegions.front().handlerBlockId.clear();

    const auto generated = generateDirectNativeModuleV5(malformed, codegenOptions());
    assert(!generated.ok());
    assert(generated.error.code == ScriptDiagnosticCode::TypedIrInvalid);
    assert(generated.error.message.find("typed IR exception region is incomplete")
        != arp::core::pmr::String::npos);
}

void generatedResumableContractIsExplicitAndFailClosed()
{
    auto generatorFrontend = ScriptFrontend::compile(
        arp::core::pmr::String(
            "def values(value: int) -> int:\n    yield value\n    return value + 1\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("generator.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("generator", &arp::core::defaultCpuMemoryResource()));
    assert(generatorFrontend.ok());
    const auto generatorIr = ScriptLowerer::lower(generatorFrontend.module);
    assert(generatorIr.ok());
    const auto generated = generateDirectNativeModuleV5(generatorIr.module, codegenOptions());
    assert(generated.ok());
    assert(generated.manifest.modules[0].functions[0].osrCapability
        == (AotOsrCapability::FunctionBoundary | AotOsrCapability::OperationBoundary
            | AotOsrCapability::Resumable));
    assert(generated.manifestYaml.find("osr: function-boundary,operation-boundary,resumable")
        != arp::core::pmr::String::npos);
    assert(generated.source.find("makeDirectNativeGeneratorV5") != arp::core::pmr::String::npos);
    assert(generated.source.find("canonical frame shape is not admissible")
        != arp::core::pmr::String::npos);
    assert(generated.source.find("setCanonicalSource") != arp::core::pmr::String::npos);
    assert(generated.source.find("scriptvm/interpreter/vm.h") == arp::core::pmr::String::npos);

    auto awaitFrontend = ScriptFrontend::compile(
        arp::core::pmr::String("def wait(gate: any) -> any:\n    yield await gate\n    return 1\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("await.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("await", &arp::core::defaultCpuMemoryResource()));
    assert(awaitFrontend.ok());
    const auto awaitIr = ScriptLowerer::lower(awaitFrontend.module);
    assert(awaitIr.ok());
    const auto unsupportedAwait = generateDirectNativeModuleV5(awaitIr.module, codegenOptions());
    assert(unsupportedAwait.ok());
    assert(unsupportedAwait.source.find("awaitInjectionSupported") != arp::core::pmr::String::npos);
    assert(unsupportedAwait.source.find("injectResult") != arp::core::pmr::String::npos);

    auto delegatedFrontend = ScriptFrontend::compile(
        arp::core::pmr::String("def child() -> int:\n    yield 1\n    return 2\n\n"
                               "def parent() -> int:\n    yield from child()\n    return 3\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("delegated.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("delegated", &arp::core::defaultCpuMemoryResource()));
    assert(delegatedFrontend.ok());
    const auto delegatedIr = ScriptLowerer::lower(delegatedFrontend.module);
    assert(delegatedIr.ok());
    const auto delegated = generateDirectNativeModuleV5(delegatedIr.module, codegenOptions());
    assert(delegated.ok());
    assert(delegated.source.find("delegatedChild") != arp::core::pmr::String::npos);
    assert(delegated.source.find("sendDirectNativeGeneratorV5") != arp::core::pmr::String::npos);
    assert(delegated.source.find("child owner identity is unavailable")
        != arp::core::pmr::String::npos);
    assert(delegated.source.find("closeDirectNativeGeneratorV5") != arp::core::pmr::String::npos);
    assert(delegated.source.find("optionalGeneratorBridgeV3") != arp::core::pmr::String::npos);
    assert(delegated.source.find("child artifact identity negotiation failed")
        != arp::core::pmr::String::npos);
    assert(delegated.source.find("canonical delegated metadata is not admissible")
        != arp::core::pmr::String::npos);
    assert(delegated.source.find("captureCanonicalDelegation") != arp::core::pmr::String::npos);

    auto collectionDelegatedFrontend = ScriptFrontend::compile(
        arp::core::pmr::String("def child() -> any:\n    return [1, 2]\n\n"
                               "def parent() -> int:\n    yield from child()\n    return 3\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("collection_delegated.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("collection_delegated", &arp::core::defaultCpuMemoryResource()));
    assert(collectionDelegatedFrontend.ok());
    const auto collectionDelegatedIr = ScriptLowerer::lower(collectionDelegatedFrontend.module);
    assert(collectionDelegatedIr.ok());
    const auto collectionDelegated =
        generateDirectNativeModuleV5(collectionDelegatedIr.module, codegenOptions());
    assert(collectionDelegated.ok());
    assert(collectionDelegated.source.find("rangeNextCollection") != arp::core::pmr::String::npos);

    auto scalarDelegatedFrontend = ScriptFrontend::compile(
        arp::core::pmr::String("def child() -> any:\n    return 1\n\n"
                               "def parent() -> int:\n    yield from child()\n    return 3\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("scalar_delegated.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("scalar_delegated", &arp::core::defaultCpuMemoryResource()));
    assert(scalarDelegatedFrontend.ok());
    const auto scalarDelegatedIr = ScriptLowerer::lower(scalarDelegatedFrontend.module);
    assert(scalarDelegatedIr.ok());
    const auto rejectedScalarDelegated =
        generateDirectNativeModuleV5(scalarDelegatedIr.module, codegenOptions());
    assert(!rejectedScalarDelegated.ok());
    assert(rejectedScalarDelegated.error.message.find(
               "not a statically proven generator or collection")
        != arp::core::pmr::String::npos);

    auto dynamicDelegatedFrontend = ScriptFrontend::compile(
        arp::core::pmr::String(
            "def parent(source: any) -> int:\n    yield from source\n    return 3\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("dynamic_delegated.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("dynamic_delegated", &arp::core::defaultCpuMemoryResource()));
    assert(dynamicDelegatedFrontend.ok());
    const auto dynamicDelegatedIr = ScriptLowerer::lower(dynamicDelegatedFrontend.module);
    assert(dynamicDelegatedIr.ok());
    const auto rejectedDelegated =
        generateDirectNativeModuleV5(dynamicDelegatedIr.module, codegenOptions());
    assert(rejectedDelegated.ok());
    assert(rejectedDelegated.source.find(
               "DirectNative yield-from child artifact identity negotiation failed")
        != arp::core::pmr::String::npos);
    assert(rejectedDelegated.source.find("ScriptSemanticRuntime::rangeNextCollection")
        != arp::core::pmr::String::npos);

    auto selfRecursiveFrontend = ScriptFrontend::compile(
        arp::core::pmr::String("def recursive() -> int:\n    yield from recursive()\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("recursive_delegated.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String("recursive_delegated", &arp::core::defaultCpuMemoryResource()));
    assert(selfRecursiveFrontend.ok());
    const auto selfRecursiveIr = ScriptLowerer::lower(selfRecursiveFrontend.module);
    assert(selfRecursiveIr.ok());
    const auto selfRecursive =
        generateDirectNativeModuleV5(selfRecursiveIr.module, codegenOptions());
    assert(selfRecursive.ok());
    assert(selfRecursive.source.find("enterFunction") != arp::core::pmr::String::npos);

    auto mutualRecursiveFrontend = ScriptFrontend::compile(
        arp::core::pmr::String("def first() -> int:\n    yield from second()\n\n"
                               "def second() -> int:\n    yield from first()\n",
            &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String(
            "mutual_recursive_delegated.arp", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String(
            "mutual_recursive_delegated", &arp::core::defaultCpuMemoryResource()));
    assert(mutualRecursiveFrontend.ok());
    const auto mutualRecursiveIr = ScriptLowerer::lower(mutualRecursiveFrontend.module);
    assert(mutualRecursiveIr.ok());
    const auto mutualRecursive =
        generateDirectNativeModuleV5(mutualRecursiveIr.module, codegenOptions());
    assert(mutualRecursive.ok());
}

void generatorBridgeV3RequiresCompleteIdentity()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->lifetimeOwner = context;
    context->moduleId = "bridge-module";
    context->moduleVersion = 7u;
    context->artifactIdentity = "bridge-artifact";
    context->budget = {10000u, 64u, 1024u, 4096u, 128u, 64u, 1u << 20u};
    context->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(context->budget.maxMemoryBytes);
    VmError error;
    Value generator = makeDirectNativeGeneratorV5(
        *context, suspendedGeneratorWithoutErrorEntry, nullptr, 0u, &error);
    assert(!isNil(generator));
    const auto* bridge = optionalGeneratorBridgeV3();
    const auto* capability = optionalGeneratorBridgeV3Capability();
    assert(bridge != nullptr && bridge->contractVersion == kGeneratorBridgeV3ContractVersion
        && bridge->structSize == sizeof(GeneratorBridgeV3) && bridge->queryIdentity != nullptr
        && capability != nullptr
        && capability->contractVersion == kGeneratorBridgeV3CapabilityContractVersion
        && capability->structSize == sizeof(GeneratorBridgeV3Capability)
        && capability->supportsPendingAwaitInjection != nullptr);
    GeneratorIdentityV3 identity;
    assert(bridge->queryIdentity(generator, identity));
    assert(identity.complete());
    assert(std::string_view(identity.moduleId) == "bridge-module");
    assert(identity.moduleVersion == 7u);
    assert(std::string_view(identity.artifactIdentity) == "bridge-artifact");
    assert(!capability->supportsPendingAwaitInjection(generator));
    assert(
        bridge->delegatedAwaitDepth != nullptr && bridge->delegatedAwaitDepth(generator, 0) == 1);

    AotModuleDependencyV5 dependency{"dependency-module", 9u, "dependency-artifact", kHash};
    AotModuleDescriptorV5 moduleDescriptor;
    moduleDescriptor.dependencies = &dependency;
    moduleDescriptor.dependencyCount = 1u;
    AotPackageDescriptorV5 packageDescriptor;
    context->moduleDescriptor = &moduleDescriptor;
    context->packageDescriptor = &packageDescriptor;
    GeneratorIdentityV3 dependencyIdentity{"dependency-module", 9u, "dependency-artifact"};
    assert(context->acceptsGeneratorIdentity(dependencyIdentity));
    GeneratorIdentityV3 undeclaredIdentity{"dependency-module", 9u, "other-artifact"};
    assert(!context->acceptsGeneratorIdentity(undeclaredIdentity));

    context->artifactIdentity.clear();
    GeneratorIdentityV3 incomplete;
    assert(!bridge->queryIdentity(generator, incomplete));
}

void directNativeParentConsumesInterpreterChild()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->lifetimeOwner = context;
    context->moduleId = "module";
    context->moduleVersion = 1u;
    context->artifactIdentity = "module@1#identity";
    context->budget = validPackage().modules[0].budget;
    context->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(context->budget.maxMemoryBytes);

    auto program = arp::core::makeShared<Program>();
    Function child("child");
    child.params = 1;
    child.locals = 1;
    child.maxStack = 1;
    child.isGenerator = true;
    child.code.emplace_back(Opcode::LoadLocal, 0);
    child.code.emplace_back(Opcode::Yield);
    child.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    child.code.emplace_back(Opcode::Return, 1);
    program->functions.push_back(std::move(child));
    program->funcIndex.emplace("child", 0);
    Function maker("make_child");
    maker.params = 1;
    maker.locals = 1;
    maker.maxStack = 1;
    maker.code.emplace_back(Opcode::LoadLocal, 0);
    maker.code.emplace_back(Opcode::Call, 0, 1);
    maker.code.emplace_back(Opcode::Return, 1);
    program->functions.push_back(std::move(maker));
    program->funcIndex.emplace("make_child", 1);

    auto childVm = arp::core::makeShared<Vm>();
    childVm->setOwner(childVm);
    childVm->setProgramOwner(program);
    childVm->setModuleIdentity("module", 1u);
    childVm->setBridgeArtifactIdentity("module@1#identity");
    arp::core::pmr::Vector<Value> childArgs{&arp::core::defaultCpuMemoryResource()};
    childArgs.push_back(makeInt(7));
    VmError error;
    ReverseInterpreterExportState exports;
    if (!exports.memoryLease.acquire(context->memoryQuota, 4096u)) {
        std::fprintf(stderr, "reverse Interpreter child quota lease acquisition failed\n");
        std::abort();
    }
    exports.child = childVm->runFunction(*program, 1, childArgs, &error);
    if (!(error.code == ScriptDiagnosticCode::None && isScriptGenerator(exports.child))) {
        std::fprintf(stderr,
            "reverse child create failed code=%d message=%s\n",
            static_cast<int>(error.code),
            error.message.c_str());
        std::abort();
    }
    context->moduleExport = &resolveReverseInterpreterChild;
    context->moduleExportUserData = &exports;
    context->valueMemoryDescriptor = &describeReverseInterpreterValue;
    context->valueMemoryDescriptorUserData = &exports;

    Value parentValue =
        makeDirectNativeGeneratorV5(*context, &reverseInterpreterParentEntry, nullptr, 0u, &error);
    auto* parent = asDirectNativeGeneratorV5(parentValue);
    assert(parent != nullptr && error.code == ScriptDiagnosticCode::None);
    int yielded = 0;
    const auto firstYield = resumeDirectNativeGeneratorV5(*parent, &error);
    if (!(tryGetInt(firstYield, yielded) && yielded == 7
            && error.code == ScriptDiagnosticCode::None)) {
        std::fprintf(stderr,
            "reverse parent first resume failed code=%d message=%s value=%d\n",
            static_cast<int>(error.code),
            error.message.c_str(),
            yielded);
        std::abort();
    }
    int returned = 0;
    assert(tryGetInt(sendDirectNativeGeneratorV5(*parent, makeInt(42), &error), returned)
        && returned == 99 && error.code == ScriptDiagnosticCode::None && parent->state != nullptr
        && parent->state->done);

    // A child throw is terminal and must not leave a replayable parent frame.
    childVm = arp::core::makeShared<Vm>();
    childVm->setOwner(childVm);
    childVm->setProgramOwner(program);
    childVm->setModuleIdentity("module", 1u);
    childVm->setBridgeArtifactIdentity("module@1#identity");
    error = {};
    exports.child = childVm->runFunction(*program, 1, childArgs, &error);
    assert(error.code == ScriptDiagnosticCode::None);
    parentValue =
        makeDirectNativeGeneratorV5(*context, &reverseInterpreterParentEntry, nullptr, 0u, &error);
    parent = asDirectNativeGeneratorV5(parentValue);
    assert(parent != nullptr);
    (void)resumeDirectNativeGeneratorV5(*parent, &error);
    assert(error.code == ScriptDiagnosticCode::None);
    (void)throwDirectNativeGeneratorV5(
        *parent, makeString(std::string_view("reverse-child-error")), &error);
    assert(error.code == ScriptDiagnosticCode::UserRaise && parent->state != nullptr
        && parent->state->done);

    // close() owns the child shutdown and subsequent resume is no-replay.
    childVm = arp::core::makeShared<Vm>();
    childVm->setOwner(childVm);
    childVm->setProgramOwner(program);
    childVm->setModuleIdentity("module", 1u);
    childVm->setBridgeArtifactIdentity("module@1#identity");
    error = {};
    exports.child = childVm->runFunction(*program, 1, childArgs, &error);
    parentValue =
        makeDirectNativeGeneratorV5(*context, &reverseInterpreterParentEntry, nullptr, 0u, &error);
    parent = asDirectNativeGeneratorV5(parentValue);
    assert(parent != nullptr);
    (void)resumeDirectNativeGeneratorV5(*parent, &error);
    closeDirectNativeGeneratorV5(*parent);
    assert(parent->state != nullptr && parent->state->done);
    error = {};
    assert(isNil(resumeDirectNativeGeneratorV5(*parent, &error))
        && error.code == ScriptDiagnosticCode::None);

    // A pending await in a real Interpreter child may be resolved by the
    // explicit injection contract. The native parent must preserve the
    // await boundary, forward send/throw exactly once, and never poll the
    // child awaitable again after injection.
    auto pendingProgram = arp::core::makeShared<Program>();
    Function pendingChild("pending_child");
    pendingChild.params = 1;
    pendingChild.locals = 1;
    pendingChild.maxStack = 1;
    pendingChild.isGenerator = true;
    pendingChild.code.emplace_back(Opcode::LoadLocal, 0);
    pendingChild.code.emplace_back(Opcode::Await);
    pendingChild.code.emplace_back(Opcode::Yield);
    pendingChild.code.emplace_back(Opcode::LoadInt, 0, 0, 99);
    pendingChild.code.emplace_back(Opcode::Return, 1);
    pendingProgram->functions.push_back(std::move(pendingChild));
    pendingProgram->funcIndex.emplace("pending_child", 0);
    Function pendingMaker("make_pending");
    pendingMaker.params = 1;
    pendingMaker.locals = 1;
    pendingMaker.maxStack = 1;
    pendingMaker.code.emplace_back(Opcode::LoadLocal, 0);
    pendingMaker.code.emplace_back(Opcode::Call, 0, 1);
    pendingMaker.code.emplace_back(Opcode::Return, 1);
    pendingProgram->functions.push_back(std::move(pendingMaker));
    pendingProgram->funcIndex.emplace("make_pending", 1);

    bool awaitReady = false;
    std::size_t awaitPolls = 0u;
    ScriptAwaitable injectableAwait;
    injectableAwait.poll = [&awaitReady, &awaitPolls](Value& result, arp::core::pmr::String&) {
        ++awaitPolls;
        if (!awaitReady)
            return ScriptAwaitStatus::Pending;
        result = makeInt(70);
        return ScriptAwaitStatus::Ready;
    };
    injectableAwait.inject = [](ScriptAwaitInjectionKind kind,
                                 const Value& injected,
                                 Value& result,
                                 arp::core::pmr::String&) {
        if (kind != ScriptAwaitInjectionKind::Send && kind != ScriptAwaitInjectionKind::Throw)
            return ScriptAwaitInjectionStatus::Rejected;
        int value = 0;
        if (!tryGetInt(injected, value))
            return ScriptAwaitInjectionStatus::Failed;
        result = makeInt(value + 1);
        return ScriptAwaitInjectionStatus::Ready;
    };
    arp::core::pmr::Vector<Value> pendingArgs{&arp::core::defaultCpuMemoryResource()};
    pendingArgs.push_back(Value{injectableAwait});
    childVm = arp::core::makeShared<Vm>();
    childVm->setOwner(childVm);
    childVm->setProgramOwner(pendingProgram);
    childVm->setModuleIdentity("module", 1u);
    childVm->setBridgeArtifactIdentity("module@1#identity");
    error = {};
    exports.child = childVm->runFunction(*pendingProgram, 1, pendingArgs, &error);
    if (error.code != ScriptDiagnosticCode::None || !isScriptGenerator(exports.child)) {
        std::fprintf(stderr,
            "pending reverse child create failed code=%d message=%s generator=%d\n",
            static_cast<int>(error.code),
            error.message.c_str(),
            isScriptGenerator(exports.child) ? 1 : 0);
        std::abort();
    }
    parentValue =
        makeDirectNativeGeneratorV5(*context, &reverseInterpreterParentEntry, nullptr, 0u, &error);
    parent = asDirectNativeGeneratorV5(parentValue);
    assert(parent != nullptr);
    const Value pendingYield = resumeDirectNativeGeneratorV5(*parent, &error);
    assert(isNil(pendingYield) && error.code == ScriptDiagnosticCode::Suspended
        && parent->state->resume.suspendKind == DirectNativeSuspendKindV5::Await
        && awaitPolls == 1u);
    int injectedYield = 0;
    const Value sendYield = sendDirectNativeGeneratorV5(*parent, makeInt(41), &error);
    assert(tryGetInt(sendYield, injectedYield) && injectedYield == 42
        && error.code == ScriptDiagnosticCode::None && awaitPolls == 1u && parent->state != nullptr
        && !parent->state->done);
    int pendingReturn = 0;
    const Value completedPending = sendDirectNativeGeneratorV5(*parent, makeInt(5), &error);
    assert(tryGetInt(completedPending, pendingReturn) && pendingReturn == 99
        && error.code == ScriptDiagnosticCode::None && parent->state != nullptr
        && parent->state->done);

    // The throw direction uses the same explicit await contract and must not
    // re-poll or replay the pending child operation.
    awaitReady = false;
    awaitPolls = 0u;
    childVm = arp::core::makeShared<Vm>();
    childVm->setOwner(childVm);
    childVm->setProgramOwner(pendingProgram);
    childVm->setModuleIdentity("module", 1u);
    childVm->setBridgeArtifactIdentity("module@1#identity");
    error = {};
    exports.child = childVm->runFunction(*pendingProgram, 1, pendingArgs, &error);
    assert(error.code == ScriptDiagnosticCode::None);
    parentValue =
        makeDirectNativeGeneratorV5(*context, &reverseInterpreterParentEntry, nullptr, 0u, &error);
    parent = asDirectNativeGeneratorV5(parentValue);
    assert(parent != nullptr);
    (void)resumeDirectNativeGeneratorV5(*parent, &error);
    assert(error.code == ScriptDiagnosticCode::Suspended && awaitPolls == 1u);
    int thrownYield = 0;
    const Value throwYield = throwDirectNativeGeneratorV5(*parent, makeInt(4), &error);
    assert(tryGetInt(throwYield, thrownYield) && thrownYield == 5
        && error.code == ScriptDiagnosticCode::None && awaitPolls == 1u);
    closeDirectNativeGeneratorV5(*parent);
    assert(parent->state != nullptr && parent->state->done);
}

void canonicalDirectNativeContinuationIsBounded()
{
    auto package = validPackage();
    auto& module = package.modules[0];
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->moduleId = module.moduleId;
    context->moduleVersion = module.moduleVersion;
    context->artifactIdentity = module.artifactIdentity;
    context->moduleDescriptor = &module;
    context->packageDescriptor = &package;
    context->budget = module.budget;
    context->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(module.budget.maxMemoryBytes);
    context->lifetimeOwner = context;
    context->packageOwner = arp::core::makeShared<int>(1);

    VmError chargeError;
    assert(context->beginContinuationFrame(sizeof(DirectNativeCanonicalFrameV5), &chargeError));
    auto carrier = arp::core::makeUnique<DirectNativeCanonicalFrameV5>(*context,
        arp::core::pmr::String("main", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String(
            "pkg/module@1/main#identity", &arp::core::defaultCpuMemoryResource()));
    carrier->retainCharge();
    Value locals[] = {makeInt(7)};
    Value stack[] = {makeInt(11)};
    carrier->captureCanonicalState(2u, locals, 1u, stack, 1u, nullptr, 0u, nullptr, nullptr, 0u);
    carrier->setCanonicalSource(
        arp::core::pmr::String("canonical.arp", &arp::core::defaultCpuMemoryResource()), 4, 3);

    DirectNativeResumeStateV5 suspended;
    suspended.pc = 2u;
    suspended.frame = std::move(carrier);
    SuspendedScriptFrame snapshot;

    // A caller cannot stamp an arbitrary frame with this context's module
    // identity: the frame itself must name the exact generated function.
    suspended.frame->setCanonicalIdentity(
        arp::core::pmr::String("main", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String(
            "foreign-function#identity", &arp::core::defaultCpuMemoryResource()));
    const auto provenanceResult = snapshotDirectNativeResumeStateV5(*context, suspended, snapshot);
    assert(provenanceResult.failed());
    assert(provenanceResult.diagnostic().code == ScriptDiagnosticCode::AotFingerprintMismatch);
    suspended.frame->setCanonicalIdentity(
        arp::core::pmr::String("main", &arp::core::defaultCpuMemoryResource()),
        arp::core::pmr::String(
            "pkg/module@1/main#identity", &arp::core::defaultCpuMemoryResource()));

    const auto snapshotResult = snapshotDirectNativeResumeStateV5(*context, suspended, snapshot);
    assert(snapshotResult.ok());
    assert(snapshot.source.file == "canonical.arp");
    assert(snapshot.source.line == 4 && snapshot.source.column == 3);
    assert(snapshot.locals.size() == 1u && snapshot.operandStack.size() == 1u);

    suspended.commitState = 1u;
    const auto uncommittedResult = snapshotDirectNativeResumeStateV5(*context, suspended, snapshot);
    assert(uncommittedResult.failed());
    assert(uncommittedResult.diagnostic().code == ScriptDiagnosticCode::AotSwitchUnsupported);

    suspended.commitState = 3u;
    const auto invalidCommitResult =
        snapshotDirectNativeResumeStateV5(*context, suspended, snapshot);
    assert(invalidCommitResult.failed());
    assert(invalidCommitResult.diagnostic().code == ScriptDiagnosticCode::AotArtifactInvalid);
    suspended.commitState = 0u;

    DirectNativeResumeStateV5 readOnlyResume;
    const auto readOnlyAdopt =
        adoptDirectNativeSuspendedFrameV5(*context, module, snapshot, readOnlyResume);
    assert(readOnlyAdopt.failed());
    assert(readOnlyAdopt.diagnostic().code == ScriptDiagnosticCode::AotSwitchUnsupported);

    SuspendedScriptFrame transferable;
    const auto transferResult =
        transferDirectNativeResumeStateV5(*context, suspended, transferable);
    assert(transferResult.ok());
    assert(suspended.frame == nullptr && suspended.ownershipTransferred);
    const auto transferredSourceResult =
        snapshotDirectNativeResumeStateV5(*context, suspended, snapshot);
    assert(transferredSourceResult.failed());
    assert(transferredSourceResult.diagnostic().code == ScriptDiagnosticCode::AotSwitchUnsupported);

    // Copies retain the same opaque ticket, not a second continuation owner.
    auto duplicateTransfer = transferable;
    const auto makeTargetContext = [&]() {
        auto target = arp::core::makeShared<DirectNativeExecutionContext>();
        target->moduleId = module.moduleId;
        target->moduleVersion = module.moduleVersion;
        target->artifactIdentity = module.artifactIdentity;
        target->moduleDescriptor = &module;
        target->packageDescriptor = &package;
        target->budget = module.budget;
        target->memoryQuota =
            arp::core::makeShared<arp::core::CpuMemoryQuota>(target->budget.maxMemoryBytes);
        target->packageOwner = context->packageOwner;
        target->lifetimeOwner = target;
        return target;
    };

    auto frameLimitedTarget = makeTargetContext();
    frameLimitedTarget->budget.maxContinuationFrames = 0u;
    DirectNativeResumeStateV5 frameLimitedResume;
    const auto frameLimitResult = adoptDirectNativeSuspendedFrameV5(
        *frameLimitedTarget, module, transferable, frameLimitedResume);
    assert(frameLimitResult.failed());
    assert(frameLimitResult.diagnostic().code == ScriptDiagnosticCode::ResourceLimitExceeded);

    auto memoryLimitedTarget = makeTargetContext();
    memoryLimitedTarget->budget.maxMemoryBytes = sizeof(DirectNativeCanonicalFrameV5) - 1u;
    memoryLimitedTarget->memoryQuota = arp::core::makeShared<arp::core::CpuMemoryQuota>(
        memoryLimitedTarget->budget.maxMemoryBytes);
    DirectNativeResumeStateV5 memoryLimitedResume;
    const auto memoryLimitResult = adoptDirectNativeSuspendedFrameV5(
        *memoryLimitedTarget, module, transferable, memoryLimitedResume);
    assert(memoryLimitResult.failed());
    assert(memoryLimitResult.diagnostic().code == ScriptDiagnosticCode::ResourceLimitExceeded);

    // The package lifetime is part of the transfer identity. A forged owner
    // must reject before claiming the opaque transfer ticket, leaving the
    // original frame available to the correct package owner.
    auto wrongOwnerTransfer = transferable;
    wrongOwnerTransfer.moduleOwner = arp::core::makeShared<int>(2);
    auto wrongOwnerTarget = makeTargetContext();
    DirectNativeResumeStateV5 wrongOwnerResume;
    const auto wrongOwnerResult = adoptDirectNativeSuspendedFrameV5(
        *wrongOwnerTarget, module, wrongOwnerTransfer, wrongOwnerResume);
    assert(wrongOwnerResult.failed());
    assert(wrongOwnerResult.diagnostic().code == ScriptDiagnosticCode::AotFingerprintMismatch);
    assert(wrongOwnerResume.frame == nullptr);

    auto adoptedTarget = makeTargetContext();
    DirectNativeResumeStateV5 adopted;
    const auto adoptResult =
        adoptDirectNativeSuspendedFrameV5(*adoptedTarget, module, transferable, adopted);
    assert(adoptResult.ok());
    assert(adopted.frame != nullptr && adopted.pc == 2u);
    assert(context->continuationFrameCount == 0u);
    assert(adoptedTarget->continuationFrameCount == 1u);

    DirectNativeResumeStateV5 duplicateResume;
    const auto duplicateResult = adoptDirectNativeSuspendedFrameV5(
        *adoptedTarget, module, duplicateTransfer, duplicateResume);
    assert(duplicateResult.failed());
    assert(duplicateResult.diagnostic().code == ScriptDiagnosticCode::AotSwitchUnsupported);

    auto wrongIdentity = snapshot;
    wrongIdentity.artifactIdentity = "other-artifact";
    DirectNativeResumeStateV5 rejected;
    const auto identityResult =
        adoptDirectNativeSuspendedFrameV5(*context, module, wrongIdentity, rejected);
    assert(identityResult.failed());
    assert(identityResult.diagnostic().code == ScriptDiagnosticCode::AotFingerprintMismatch);

    auto missingSource = snapshot;
    missingSource.source = SourcePos{};
    const auto sourceResult =
        adoptDirectNativeSuspendedFrameV5(*context, module, missingSource, rejected);
    assert(sourceResult.failed());
    assert(sourceResult.diagnostic().code == ScriptDiagnosticCode::TypedIrInvalid);
}

void budgetedRecursiveGeneratorIsFailClosed()
{
    auto context = arp::core::makeShared<DirectNativeExecutionContext>();
    context->lifetimeOwner = context;
    context->budget = {10000u, 3u, 64u, 128u, 16u, 16u, 1u << 20u};
    context->memoryQuota =
        arp::core::makeShared<arp::core::CpuMemoryQuota>(context->budget.maxMemoryBytes);
    context->resetBudget(context->budget);
    VmError error;
    Value root = makeDirectNativeGeneratorV5(
        *context, &budgetedRecursiveGeneratorEntry, nullptr, 0u, &error);
    auto* generator = asDirectNativeGeneratorV5(root);
    assert(generator != nullptr && error.code == ScriptDiagnosticCode::None);
    (void)resumeDirectNativeGeneratorV5(*generator, &error);
    assert(error.code == ScriptDiagnosticCode::RecursionLimitExceeded);
    assert(generator->state != nullptr && generator->state->done
        && generator->state->resume.frame == nullptr);
}

} // namespace

int main()
{
    inlineLeafAdmissionMatchesTheCanonicalGate();
    nativeSlotsPreserveValueEdgesAndCheckedSemantics();
    nativeFixedStackReusesSharedConstructionContracts();
    retainedValueGraphUsesOneQuotaFactSource();
    strictContractRejectsLegacyAndIncompleteArtifacts();
    pluginDependencyContractIsValidatedBeforeRegistration();
    registryUsesFullPackageIdentity();
    invokeMethodPrependsExactlyOneReceiver();
    packageOwnerIsAvailableDuringModuleInitialization();
    moduleInitializerTelemetryUsesZeroArguments();
    directNativeMemoryQuotaIsEnforcedCumulatively();
    retainedValueGraphKeepsIndependentGeneratorOwners();
    reusedAccountingWorkspaceDoesNotRetainVisitedIdentities();
    directNativeContinuationFrameCountIsEnforced();
    directNativeContinuationFrameBytesAreCumulative();
    directNativeBudgetResetRejectsRetainedState();
    directNativeGeneratorFailureIsTerminalAndNoReplay();
    directNativeResourceLimitDiscardsReturnedValue();
    directNativeOperationBoundaryHonorsCumulativeQuota();
    directNativeEntryArgumentBudgetIsEnforced();
    directNativeNullArgumentsFailClosed();
    directNativeGeneratorSuspensionDoesNotRequireErrorPointer();
    directNativePendingAwaitInjectionFailsClosed();
    directNativePendingAwaitInjectionConsumesExplicitCapability();
    manifestRequiresDirectNativeAndExplicitBudgets();
    generatedControlFlowHasNoExecutorTable();
    nativeLoopRegionsRequireAClosedExecutionProof();
    generatedPluginDependencyContractIsEmbedded();
    unsupportedShapeFailsAtBuildTime();
    malformedHandlerCfgFailsDeterministically();
    generatedResumableContractIsExplicitAndFailClosed();
    generatorBridgeV3RequiresCompleteIdentity();
    directNativeParentConsumesInterpreterChild();
    canonicalDirectNativeContinuationIsBounded();
    budgetedRecursiveGeneratorIsFailClosed();
    return 0;
}
