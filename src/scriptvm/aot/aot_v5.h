#pragma once

// Strict production AOT contract. Unlike the legacy AOT test contract this
// descriptor cannot carry bytecode, Typed IR operations, or an executor entry.

#include "core/cpu_allocator.h"
#include "scriptvm/aot/script_value_memory_accountant.h"
#include "scriptvm/runtime/diagnostics.h"
#include "scriptvm/runtime/source_pos.h"
#include "scriptvm/runtime/suspended_frame.h"
#include "scriptvm/runtime/value.h"
#include "scriptvm/runtime/vm_error.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace arp::scriptvm {

class DirectNativeExecutionContext;

inline constexpr std::uint32_t kAotContractVersion = 5u;
inline constexpr std::uint32_t kDirectNativeEntryAbiVersion = 1u;
inline constexpr const char* kAotPackageManifestType = "arp.script.aot-package/v1";

struct AotPackageDescriptorV5;
struct AotModuleDescriptorV5;

enum class AotFunctionExecutionKind : std::uint8_t {
    DirectNative = 0,
};

enum class AotPlatform : std::uint32_t {
    None = 0u,
    Windows = 1u << 0u,
    WebWasm = 1u << 1u,
    MacOS = 1u << 2u,
};

constexpr AotPlatform operator|(AotPlatform lhs, AotPlatform rhs) noexcept
{
    return static_cast<AotPlatform>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr bool hasAotPlatform(AotPlatform value, AotPlatform expected) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(expected)) != 0u;
}

enum class AotOsrCapability : std::uint32_t {
    FunctionBoundary = 1u << 0u,
    // OperationBoundary is cooperative: generated code reaches the callback
    // at a declared operation start. It does not imply arbitrary-PC native
    // frame deoptimization.
    OperationBoundary = 1u << 1u,
    Resumable = 1u << 2u,
};

constexpr AotOsrCapability operator|(AotOsrCapability lhs, AotOsrCapability rhs) noexcept
{
    return static_cast<AotOsrCapability>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

struct AotExecutionBudgetV5 {
    std::uint64_t instructionLimit = 0u;
    std::uint32_t recursionLimit = 0u;
    std::uint32_t maxFrameValues = 0u;
    std::uint32_t maxContainerValues = 0u;
    std::uint32_t maxCallArguments = 0u;
    std::uint32_t maxContinuationFrames = 0u;
    std::uint64_t maxMemoryBytes = 0u;
};

class DirectNativeFrameV5 {
public:
    DirectNativeFrameV5(const char* functionId, const char* functionIdentity) noexcept
        : functionId_(functionId), functionIdentity_(functionIdentity)
    {}

    virtual ~DirectNativeFrameV5() = default;

    const char* functionId() const noexcept { return functionId_; }

    const char* functionIdentity() const noexcept { return functionIdentity_; }

    // A native frame can hand its logical continuation charge to a transfer
    // ticket or a replacement frame without creating a second live frame.
    virtual bool continuationFrameCharge(
        const DirectNativeExecutionContext&, std::size_t&) const noexcept
    {
        return false;
    }

    virtual bool relinquishContinuationFrameCharge(
        DirectNativeExecutionContext&, std::size_t&) noexcept
    {
        return false;
    }

    virtual void relinquishDelegatedChildForTransfer() noexcept {}

    // Canonical operation-boundary state. Generated frames populate this
    // immediately before invoking the boundary hook, so an editor-only
    // migration can exchange logical state without observing a native PC or
    // relying on the generated frame's private layout.
    void captureCanonicalState(std::uint32_t pc,
        const Value* locals,
        std::size_t localCount,
        const Value* stack,
        std::size_t stackSize,
        const std::shared_ptr<ScriptCell>* captures,
        std::size_t captureCount,
        const int* handlerPcs,
        const std::size_t* handlerDepths,
        std::size_t handlerCount,
        bool awaiting = false,
        const Value& suspendedValue = Value::undefined())
    {
        canonicalPc_ = pc;
        canonicalLocals_.clear();
        canonicalStack_.clear();
        canonicalCaptureCells_.clear();
        canonicalHandlerPcs_.clear();
        canonicalHandlerDepths_.clear();
        if (locals != nullptr)
            canonicalLocals_.assign(locals, locals + localCount);
        if (stack != nullptr)
            canonicalStack_.assign(stack, stack + stackSize);
        if (captures != nullptr)
            canonicalCaptureCells_.assign(captures, captures + captureCount);
        if (handlerPcs != nullptr && handlerDepths != nullptr) {
            canonicalHandlerPcs_.assign(handlerPcs, handlerPcs + handlerCount);
            canonicalHandlerDepths_.assign(handlerDepths, handlerDepths + handlerCount);
        }
        canonicalAwaiting_ = awaiting;
        canonicalSuspendedValue_ = suspendedValue;
        canonicalDelegatedSourceSlot_ = -1;
        canonicalDelegatedRangePc_ = -1;
        canonicalDelegatedYieldPc_ = -1;
        canonicalDelegatedPendingYieldPc_ = -1;
        canonicalDelegatedActive_ = false;
        canonicalDelegatedChildAwaiting_ = false;
        canonicalValid_ = true;
    }

    bool hasCanonicalState() const noexcept { return canonicalValid_; }

    std::uint32_t canonicalPc() const noexcept { return canonicalPc_; }

    const core::pmr::Vector<Value>& canonicalLocals() const noexcept { return canonicalLocals_; }

    const core::pmr::Vector<Value>& canonicalStack() const noexcept { return canonicalStack_; }

    const core::pmr::Vector<std::shared_ptr<ScriptCell>>& canonicalCaptureCells() const noexcept
    {
        return canonicalCaptureCells_;
    }

    const core::pmr::Vector<int>& canonicalHandlerPcs() const noexcept
    {
        return canonicalHandlerPcs_;
    }

    const core::pmr::Vector<std::size_t>& canonicalHandlerDepths() const noexcept
    {
        return canonicalHandlerDepths_;
    }

    bool canonicalAwaiting() const noexcept { return canonicalAwaiting_; }

    const Value& canonicalSuspendedValue() const noexcept { return canonicalSuspendedValue_; }

    bool canonicalDelegatedActive() const noexcept { return canonicalDelegatedActive_; }

    int canonicalDelegatedSourceSlot() const noexcept { return canonicalDelegatedSourceSlot_; }

    int canonicalDelegatedYieldPc() const noexcept { return canonicalDelegatedYieldPc_; }

    int canonicalDelegatedRangePc() const noexcept { return canonicalDelegatedRangePc_; }

    bool canonicalDelegatedChildAwaiting() const noexcept
    {
        return canonicalDelegatedChildAwaiting_;
    }

    int canonicalDelegatedPendingYieldPc() const noexcept
    {
        return canonicalDelegatedPendingYieldPc_;
    }

    void captureCanonicalDelegation(int sourceSlot,
        int rangePc,
        int yieldPc,
        bool childAwaiting = false,
        int pendingYieldPc = -1) noexcept
    {
        canonicalDelegatedSourceSlot_ = sourceSlot;
        canonicalDelegatedRangePc_ = rangePc;
        canonicalDelegatedYieldPc_ = yieldPc;
        canonicalDelegatedPendingYieldPc_ = pendingYieldPc;
        canonicalDelegatedActive_ = true;
        canonicalDelegatedChildAwaiting_ = childAwaiting;
        canonicalValid_ = true;
    }

    // Await is the one operation whose pending state is discovered after the
    // operation-boundary snapshot. Preserve the host awaitable in the same
    // canonical frame so adoption can resume the declared Await operation
    // without losing the pending value or guessing a native frame layout.
    void markCanonicalAwait(const Value& suspendedValue)
    {
        // The operation-boundary snapshot is taken before Await consumes the
        // operand. Mirror that pop so the adopted frame can push a completed
        // result without growing past its declared stack bound.
        if (!canonicalStack_.empty())
            canonicalStack_.pop_back();
        canonicalAwaiting_ = true;
        canonicalSuspendedValue_ = suspendedValue;
        canonicalValid_ = true;
    }

    void setCanonicalSource(core::pmr::String file, int line, int column)
    {
        canonicalSource_ = SourcePos(std::move(file), line, column);
    }

    const SourcePos& canonicalSource() const noexcept { return canonicalSource_; }

    template <typename HandlerVector> void captureCanonicalHandlers(const HandlerVector& handlers)
    {
        canonicalHandlerPcs_.clear();
        canonicalHandlerDepths_.clear();
        canonicalHandlerPcs_.reserve(handlers.size());
        canonicalHandlerDepths_.reserve(handlers.size());
        for (const auto& handler : handlers) {
            canonicalHandlerPcs_.push_back(static_cast<int>(handler.pc));
            canonicalHandlerDepths_.push_back(handler.stackDepth);
        }
    }

    // Foreign canonical frames use owned identity strings rather than the
    // static strings emitted for native frames.
    void setCanonicalIdentity(core::pmr::String functionId, core::pmr::String functionIdentity)
    {
        canonicalFunctionId_ = std::move(functionId);
        canonicalFunctionIdentity_ = std::move(functionIdentity);
        functionId_ = canonicalFunctionId_.c_str();
        functionIdentity_ = canonicalFunctionIdentity_.c_str();
    }

private:
    friend class DirectNativeCanonicalFrameV5;
    const char* functionId_ = nullptr;
    const char* functionIdentity_ = nullptr;
    core::pmr::String canonicalFunctionId_{&core::defaultCpuMemoryResource()};
    core::pmr::String canonicalFunctionIdentity_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Value> canonicalLocals_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Value> canonicalStack_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<ScriptCell>> canonicalCaptureCells_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<int> canonicalHandlerPcs_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::size_t> canonicalHandlerDepths_{&core::defaultCpuMemoryResource()};
    Value canonicalSuspendedValue_;
    SourcePos canonicalSource_;
    std::uint32_t canonicalPc_ = 0u;
    int canonicalDelegatedSourceSlot_ = -1;
    int canonicalDelegatedRangePc_ = -1;
    int canonicalDelegatedYieldPc_ = -1;
    int canonicalDelegatedPendingYieldPc_ = -1;
    bool canonicalAwaiting_ = false;
    bool canonicalDelegatedActive_ = false;
    bool canonicalDelegatedChildAwaiting_ = false;
    bool canonicalValid_ = false;
};

// A backend-neutral frame carrier used only by editor/differential migration.
// Production DirectNative execution always consumes the generated derived
// frame type; this carrier is rejected unless the generated entry has an
// exact canonical-state lowering for the requested function.
class DirectNativeCanonicalFrameV5 final : public DirectNativeFrameV5 {
public:
    DirectNativeCanonicalFrameV5(DirectNativeExecutionContext& owner,
        core::pmr::String functionId,
        core::pmr::String functionIdentity)
        : DirectNativeFrameV5("canonical", "canonical"), owner_(&owner)
    {
        setCanonicalIdentity(std::move(functionId), std::move(functionIdentity));
    }

    ~DirectNativeCanonicalFrameV5() override;

    bool continuationFrameCharge(const DirectNativeExecutionContext& expected,
        std::size_t& outBytes) const noexcept override;
    bool relinquishContinuationFrameCharge(
        DirectNativeExecutionContext& expected, std::size_t& outBytes) noexcept override;
    void retainCharge() noexcept;

private:
    DirectNativeExecutionContext* owner_ = nullptr;
    std::size_t chargeBytes_ = 0u;
    bool ownsCharge_ = false;

    friend class DirectNativeContinuationTransferV5;
};

enum class DirectNativeSuspendKindV5 : std::uint8_t {
    None,
    Yield,
    Await,
};

enum class DirectNativeInjectionKindV5 : std::uint8_t {
    None,
    Send,
    Throw,
};

struct DirectNativeResumeStateV5 {
    // Generated code dispatches this canonical pc once when resuming. A fresh
    // invocation uses zero and then follows generated labels directly.
    std::uint32_t pc = 0u;
    std::uint32_t commitState = 0u;
    core::UniquePtr<DirectNativeFrameV5> frame;
    Value injectionValue;
    // Set only by generated code when the suspended frame owns an awaitable
    // with an explicit injection callback. It is consumed at the same Await
    // operation boundary and never implies arbitrary-PC deoptimization.
    bool awaitInjectionSupported = false;
    bool awaitInjectionReady = false;
    DirectNativeSuspendKindV5 suspendKind = DirectNativeSuspendKindV5::None;
    DirectNativeInjectionKindV5 injectionKind = DirectNativeInjectionKindV5::None;
    // Set only after a successful canonical transfer. The source generator is
    // deliberately terminal for resume purposes; its continuation cannot be
    // forked after ownership moves to an editor/differential consumer.
    bool ownershipTransferred = false;
};

// Called only at a generated operation boundary before that operation's
// effects are executed. The callback may request migration using the
// supplied canonical resume state; it is not an arbitrary native-PC
// deoptimization interface and must not infer locals, handlers, captures, or
// caller frames that are not present in the generated frame.
using DirectNativeOperationBoundaryV5 = bool (*)(
    void*, const char*, std::uint32_t, DirectNativeResumeStateV5&, VmError*);
using DirectNativeBoundCallV5 = bool (*)(
    void*, const char*, const char*, const Value*, const Value*, std::size_t, Value*, VmError*);
using DirectNativeModuleExportV5 = bool (*)(void*, const char*, const char*, Value*, VmError*);

struct DirectNativeModuleReferenceV5 {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};

    explicit DirectNativeModuleReferenceV5(core::pmr::String value) : moduleId(std::move(value)) {}
};

struct DirectNativeExecutionContext {
    AotExecutionBudgetV5 budget;
    // Stable identity copied from the registered module descriptor. Native
    // generators expose this through GeneratorBridgeV3 for reverse delegation.
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0u;
    core::pmr::String artifactIdentity{&core::defaultCpuMemoryResource()};
    const AotPackageDescriptorV5* packageDescriptor = nullptr;
    const AotModuleDescriptorV5* moduleDescriptor = nullptr;
    std::uint64_t remainingInstructions = 0u;
    std::uint32_t recursionDepth = 0u;
    std::uint32_t continuationFrameCount = 0u;
    // Persistent generated frames have inline Value/stack storage which is
    // not visible to CpuMemoryQuota. Keep that storage in the same cumulative
    // memory contract as heap-backed containers and captures.
    std::uint64_t continuationFrameBytes = 0u;
    std::shared_ptr<core::CpuMemoryQuota> memoryQuota;
    std::shared_ptr<ScriptValueMemoryAccountant> memoryAccountant;
    ScriptValueMemoryDescriptorFn valueMemoryDescriptor = nullptr;
    void* valueMemoryDescriptorUserData = nullptr;
    // Dynamic package generators retain the loaded module owner through their
    // context so an entry function can never outlive its DLL.
    std::shared_ptr<void> packageOwner;
    // Keeps package callback bridges alive for generators that outlive the
    // dispatcher. The bridge owns only a weak runtime reference, so retaining
    // it cannot create a runtime/context ownership cycle.
    std::shared_ptr<void> callbackOwner;
    DirectNativeOperationBoundaryV5 operationBoundary = nullptr;
    void* operationBoundaryUserData = nullptr;
    DirectNativeBoundCallV5 boundCall = nullptr;
    void* boundCallUserData = nullptr;
    DirectNativeModuleExportV5 moduleExport = nullptr;
    void* moduleExportUserData = nullptr;
    core::pmr::Vector<Value> globals{&core::defaultCpuMemoryResource()};
    const core::pmr::Vector<std::shared_ptr<ScriptCell>>* activeCaptures = nullptr;
    bool moduleInitialized = false;
    bool memoryBudgetFailed = false;
    std::weak_ptr<DirectNativeExecutionContext> lifetimeOwner;
    // Stable private token for globals that must remain accounted between
    // invocations even after frame/generator owners are released.
    std::uint8_t globalMemoryRootOwnerToken = 0u;

    void resetBudget(const AotExecutionBudgetV5& value) noexcept
    {
        budget = value;
        if (memoryQuota != nullptr) {
            memoryQuota->setLimitBytes(value.maxMemoryBytes);
            // Retained globals, captures, containers, and continuation frames
            // remain charged across invocations.  Lowering the budget must
            // therefore become a terminal execution gate immediately rather
            // than waiting for the next allocation.
        }
        const auto liveBytes = memoryQuota != nullptr ? memoryQuota->liveBytes() : 0u;
        if (value.maxMemoryBytes > 0u && liveBytes > value.maxMemoryBytes)
            memoryBudgetFailed = true;
        remainingInstructions = value.instructionLimit;
        recursionDepth = 0u;
    }

    ARP_API bool enterFunction(const char* functionId, VmError* error);
    ARP_API bool ensureGlobals(std::size_t count, VmError* error);
    ARP_API bool synchronizeMemoryRoots(const ScriptValueSpan* spans,
        std::size_t spanCount,
        const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
        VmError* error);
    ARP_API bool synchronizeMemoryRootsForOwner(const void* owner,
        const ScriptValueSpan* spans,
        std::size_t spanCount,
        const core::pmr::Vector<std::shared_ptr<ScriptCell>>* captures,
        VmError* error);
    ARP_API void releaseMemoryRoots(const void* owner) noexcept;
    ARP_API bool transferMemoryRoots(const void* sourceOwner, const void* targetOwner) noexcept;
    // Reserve the budget for one persistent generated continuation frame.
    // The frame constructor increments continuationFrameCount only after
    // allocation succeeds; this gate therefore accounts both the cumulative
    // byte budget and the frame-count budget without partially publishing a
    // continuation.
    ARP_API bool beginContinuationFrame(std::size_t frameBytes, VmError* error);
    ARP_API bool canReplaceContinuationFrame(
        std::size_t oldFrameBytes, std::size_t newFrameBytes, VmError* error) const;
    ARP_API void replaceContinuationFrameCharge(
        std::size_t oldFrameBytes, std::size_t newFrameBytes) noexcept;
    ARP_API void retainContinuationFrame(std::size_t frameBytes) noexcept;
    ARP_API void releaseContinuationFrame(std::size_t frameBytes) noexcept;
    ARP_API void leaveFunction() noexcept;
    ARP_API bool beginOperation(const char* functionId,
        std::uint32_t pc,
        DirectNativeResumeStateV5& resume,
        VmError* error);
    ARP_API bool invokeBoundCall(const char* bindingId,
        const char* targetId,
        const Value* receiver,
        const Value* arguments,
        std::size_t argumentCount,
        Value& result,
        VmError* error);
    ARP_API bool resolveModuleExport(
        const char* moduleId, const char* exportName, Value& result, VmError* error);
    ARP_API bool acceptsGeneratorIdentity(const GeneratorIdentityV3& identity) const noexcept;
    ARP_API void prepareSideEffect(DirectNativeResumeStateV5& resume, std::uint32_t pc) noexcept;
    ARP_API void commitSideEffect(DirectNativeResumeStateV5& resume, std::uint32_t pc) noexcept;
};

using DirectNativeEntryV5 = Value (*)(
    DirectNativeExecutionContext&, const Value*, std::size_t, DirectNativeResumeStateV5*, VmError*);

struct AotModuleDescriptorV5;

class DirectNativeContinuationTransferV5 final {
public:
    DirectNativeContinuationTransferV5(std::shared_ptr<DirectNativeExecutionContext> sourceOwner,
        std::size_t chargedBytes) noexcept;
    ~DirectNativeContinuationTransferV5();

    DirectNativeContinuationTransferV5(const DirectNativeContinuationTransferV5&) = delete;
    DirectNativeContinuationTransferV5& operator=(
        const DirectNativeContinuationTransferV5&) = delete;

    bool claim() noexcept;
    void abandonClaim() noexcept;
    void assumeSourceCharge() noexcept;
    void releaseSourceCharge() noexcept;
    bool assumeMemoryRoots(const void* sourceOwner) noexcept;
    void releaseMemoryRoots() noexcept;

private:
    std::shared_ptr<DirectNativeExecutionContext> sourceOwner_;
    std::size_t chargedBytes_ = 0u;
    std::atomic_bool claimed_{false};
    bool sourceChargeLive_ = true;
    bool memoryRootsLive_ = false;
};

// Read-only diagnostic materialization at a declared operation boundary. The
// resulting snapshot intentionally has no DirectNative ownership ticket and
// cannot be adopted as a live continuation.
ARP_API ScriptResult snapshotDirectNativeResumeStateV5(const DirectNativeExecutionContext& context,
    const DirectNativeResumeStateV5& resume,
    SuspendedScriptFrame& out);
// Transfer, rather than clone, one DirectNative continuation. On success the
// source resume is permanently fail-closed and `out` becomes single-consumer.
ARP_API ScriptResult transferDirectNativeResumeStateV5(DirectNativeExecutionContext& context,
    DirectNativeResumeStateV5& resume,
    SuspendedScriptFrame& out);
// Kept for source compatibility with read-only snapshots; it always rejects
// because a const frame cannot consume the single-owner transfer ticket.
ARP_API ScriptResult adoptDirectNativeSuspendedFrameV5(DirectNativeExecutionContext& context,
    const AotModuleDescriptorV5& module,
    const SuspendedScriptFrame& frame,
    DirectNativeResumeStateV5& resume);
ARP_API ScriptResult adoptDirectNativeSuspendedFrameV5(DirectNativeExecutionContext& context,
    const AotModuleDescriptorV5& module,
    SuspendedScriptFrame& frame,
    DirectNativeResumeStateV5& resume);

struct DirectNativeGeneratorStateV5 {
    ~DirectNativeGeneratorStateV5();

    DirectNativeEntryV5 entry = nullptr;
    std::shared_ptr<DirectNativeExecutionContext> context;
    DirectNativeResumeStateV5 resume;
    core::pmr::Vector<Value> initialArguments{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<ScriptCell>> captures{&core::defaultCpuMemoryResource()};
    bool started = false;
    bool done = false;
};

struct DirectNativeGeneratorV5 {
    std::shared_ptr<DirectNativeGeneratorStateV5> state;
};

ARP_API Value makeDirectNativeGeneratorV5(DirectNativeExecutionContext& context,
    DirectNativeEntryV5 entry,
    const Value* arguments,
    std::size_t argumentCount,
    VmError* error);
ARP_API bool isDirectNativeGeneratorV5(const Value& value);
ARP_API DirectNativeGeneratorV5* asDirectNativeGeneratorV5(const Value& value);
// Returns the optional runtime bridge when an Interpreter/runtime provider is
// linked. A minimal AOT-only process may have no provider and receives null.
ARP_API const GeneratorBridgeV2* optionalGeneratorBridgeV2() noexcept;
ARP_API const GeneratorBridgeV3* optionalGeneratorBridgeV3() noexcept;
ARP_API const GeneratorBridgeV3Capability* optionalGeneratorBridgeV3Capability() noexcept;
ARP_API Value resumeDirectNativeGeneratorV5(DirectNativeGeneratorV5& generator, VmError* error);
ARP_API Value sendDirectNativeGeneratorV5(
    DirectNativeGeneratorV5& generator, const Value& value, VmError* error);
ARP_API Value throwDirectNativeGeneratorV5(
    DirectNativeGeneratorV5& generator, const Value& value, VmError* error);
ARP_API void closeDirectNativeGeneratorV5(DirectNativeGeneratorV5& generator) noexcept;

struct AotFunctionDescriptorV5 {
    const char* functionId = nullptr;
    const char* functionIdentity = nullptr;
    const char* signatureHash = nullptr;
    const char* entrySymbol = nullptr;
    DirectNativeEntryV5 entry = nullptr;
    std::uint32_t entryAbiVersion = kDirectNativeEntryAbiVersion;
    AotFunctionExecutionKind executionKind = AotFunctionExecutionKind::DirectNative;
    AotOsrCapability osrCapability = AotOsrCapability::FunctionBoundary;
};

struct AotModuleDependencyV5 {
    const char* moduleId = nullptr;
    std::uint32_t moduleVersion = 0u;
    const char* artifactIdentity = nullptr;
    const char* exportContractHash = nullptr;
};

// A compiled script module may import a native ARP Plugin. Keep this contract
// separate from script-module dependencies so the runtime can reject a
// package before execution when the native export surface, reflection table,
// or registration-time service versions differ from the artifact.
struct AotServiceDependencyV5 {
    const char* serviceId = nullptr;
    std::uint32_t version = 0u;
};

struct AotPluginDependencyV5 {
    const char* moduleId = nullptr;
    std::uint32_t moduleVersion = 0u;
    const char* exportContractHash = nullptr;
    const char* reflectionRegistryHash = nullptr;
    const AotServiceDependencyV5* services = nullptr;
    std::size_t serviceCount = 0u;
};

struct AotModuleExportV5 {
    const char* name = nullptr;
    std::size_t globalSlot = 0u;
};

struct AotModuleDescriptorV5 {
    std::uint32_t contractVersion = kAotContractVersion;
    const char* packageIdentity = nullptr;
    const char* moduleId = nullptr;
    std::uint32_t moduleVersion = 0u;
    const char* artifactIdentity = nullptr;
    const char* sourceHash = nullptr;
    const char* dependencyGraphHash = nullptr;
    const char* reflectionRegistryHash = nullptr;
    const char* hostContractHash = nullptr;
    std::uint32_t semanticAbiVersion = 0u;
    std::uint32_t entryAbiVersion = kDirectNativeEntryAbiVersion;
    AotPlatform platforms = AotPlatform::None;
    AotExecutionBudgetV5 budget;
    const AotModuleDependencyV5* dependencies = nullptr;
    std::size_t dependencyCount = 0u;
    const AotFunctionDescriptorV5* functions = nullptr;
    std::size_t functionCount = 0u;
    std::size_t globalCount = 0u;
    const char* moduleInitFunctionId = nullptr;
    const char* exportContractHash = nullptr;
    const AotModuleExportV5* exports = nullptr;
    std::size_t exportCount = 0u;
    const AotPluginDependencyV5* pluginDependencies = nullptr;
    std::size_t pluginDependencyCount = 0u;
};

struct AotPackageDescriptorV5 {
    std::uint32_t contractVersion = kAotContractVersion;
    const char* manifestType = kAotPackageManifestType;
    const char* packageId = nullptr;
    std::uint32_t packageVersion = 0u;
    const char* packageIdentity = nullptr;
    const char* manifestHash = nullptr;
    AotPlatform platforms = AotPlatform::None;
    const AotModuleDescriptorV5* modules = nullptr;
    std::size_t moduleCount = 0u;
};

using AotRegisterPackageV5Fn = bool (*)(const AotPackageDescriptorV5*, void*);

using AotPluginDependencyResolverV5 = bool (*)(void*, const AotPluginDependencyV5&, VmError*);

struct AotRegistrationHostV5 {
    std::uint32_t contractVersion = kAotContractVersion;
    AotRegisterPackageV5Fn registerPackage = nullptr;
    void* userData = nullptr;
};

struct AotPackageManifestFunctionV5 {
    core::pmr::String functionId{&core::defaultCpuMemoryResource()};
    core::pmr::String functionIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&core::defaultCpuMemoryResource()};
    core::pmr::String entrySymbol{&core::defaultCpuMemoryResource()};
    std::uint32_t entryAbiVersion = kDirectNativeEntryAbiVersion;
    AotFunctionExecutionKind executionKind = AotFunctionExecutionKind::DirectNative;
    AotOsrCapability osrCapability = AotOsrCapability::FunctionBoundary;
};

struct AotServiceDependencyManifestV5 {
    core::pmr::String serviceId{&core::defaultCpuMemoryResource()};
    std::uint32_t version = 0u;
};

struct AotPluginDependencyManifestV5 {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0u;
    core::pmr::String exportContractHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AotServiceDependencyManifestV5> services{&core::defaultCpuMemoryResource()};
};

struct AotPackageManifestModuleV5 {
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 0u;
    core::pmr::String artifactIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String sourceHash{&core::defaultCpuMemoryResource()};
    core::pmr::String dependencyGraphHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
    core::pmr::String hostContractHash{&core::defaultCpuMemoryResource()};
    std::uint32_t semanticAbiVersion = 0u;
    std::uint32_t entryAbiVersion = kDirectNativeEntryAbiVersion;
    AotPlatform platforms = AotPlatform::None;
    AotExecutionBudgetV5 budget;
    core::pmr::Vector<AotPluginDependencyManifestV5> pluginDependencies{
        &core::defaultCpuMemoryResource()};
    std::size_t globalCount = 0u;
    core::pmr::String moduleInitFunctionId{&core::defaultCpuMemoryResource()};
    core::pmr::String exportContractHash{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<AotPackageManifestFunctionV5> functions{&core::defaultCpuMemoryResource()};
};

struct AotPackageManifestV5 {
    core::pmr::String type = kAotPackageManifestType;
    std::uint32_t contractVersion = kAotContractVersion;
    core::pmr::String packageId{&core::defaultCpuMemoryResource()};
    std::uint32_t packageVersion = 0u;
    core::pmr::String packageIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String manifestHash{&core::defaultCpuMemoryResource()};
    AotPlatform platforms = AotPlatform::None;
    core::pmr::Vector<AotPackageManifestModuleV5> modules{&core::defaultCpuMemoryResource()};
};

ARP_API AotPlatform currentAotPlatform() noexcept;
ARP_API ScriptResult validateAotModuleV5(
    const AotModuleDescriptorV5& module, AotPlatform requiredPlatform);
ARP_API ScriptResult validateAotPackageV5(
    const AotPackageDescriptorV5& package, AotPlatform requiredPlatform);
ARP_API ScriptResult validateAotPackageManifestV5(
    const AotPackageManifestV5& manifest, AotPlatform requiredPlatform);

class ARP_API ScriptAotPackageRegistry final {
public:
    ScriptResult registerPackage(
        const AotPackageDescriptorV5& package, std::shared_ptr<void> owner = {});
    const AotPackageDescriptorV5* findPackage(const char* packageIdentity) const;
    const AotModuleDescriptorV5* findModule(
        const char* packageIdentity, const char* moduleId) const;
    std::shared_ptr<void> findOwner(const char* packageIdentity) const;

    // A package that declares native Plugin dependencies is rejected unless
    // the embedding runtime supplies a resolver. Resolution happens during
    // registration, before any AOT module can be instantiated or executed.
    void setPluginDependencyResolver(
        AotPluginDependencyResolverV5 resolver, void* userData) noexcept
    {
        pluginDependencyResolver_ = resolver;
        pluginDependencyResolverUserData_ = userData;
    }

    AotRegistrationHostV5 registrationHost();

private:
    struct Entry {
        const AotPackageDescriptorV5* package = nullptr;
        std::shared_ptr<void> owner;
    };

    static bool registerCallback(const AotPackageDescriptorV5* package, void* userData);

    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
    AotPluginDependencyResolverV5 pluginDependencyResolver_ = nullptr;
    void* pluginDependencyResolverUserData_ = nullptr;
};

class DirectNativePackageRuntimeV5;

struct ScriptAotModuleInstanceV5 {
    core::pmr::String packageIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    const AotModuleDescriptorV5* module = nullptr;
    const AotPackageDescriptorV5* package = nullptr;
    std::shared_ptr<void> packageOwner;
    std::shared_ptr<DirectNativePackageRuntimeV5> packageRuntime;
    std::shared_ptr<DirectNativeExecutionContext> context;
};

struct DirectNativeTelemetryEventV5 {
    core::pmr::String packageIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String moduleId{&core::defaultCpuMemoryResource()};
    core::pmr::String functionId{&core::defaultCpuMemoryResource()};
    core::pmr::String functionIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::String policy = "AotOnly";
    core::pmr::String osrBoundary = "function";
    AotExecutionBudgetV5 budget;
    // Counts observed at the dispatcher/entry boundary.  The generated entry
    // receives argumentCount exactly as recorded here; explicitArgumentCount
    // excludes the receiver added by invokeMethod().
    std::size_t argumentCount = 0u;
    std::size_t explicitArgumentCount = 0u;
    bool receiverPrepended = false;
    ScriptDiagnosticCode diagnostic = ScriptDiagnosticCode::None;
};

ARP_API ScriptResult createAotOnlyModuleInstanceV5(const ScriptAotPackageRegistry& registry,
    const char* packageIdentity,
    const char* moduleId,
    ScriptAotModuleInstanceV5& output);
ARP_API ScriptResult configureAotOnlyBoundCallV5(
    ScriptAotModuleInstanceV5& instance, DirectNativeBoundCallV5 callback, void* userData);
ARP_API ScriptResult configureAotOnlyModuleExportV5(
    ScriptAotModuleInstanceV5& instance, DirectNativeModuleExportV5 callback, void* userData);

class ARP_API DirectNativeDispatcherV5 final {
public:
    explicit DirectNativeDispatcherV5(ScriptAotModuleInstanceV5 module);
    bool hasFunction(const char* functionId) const;
    Value invoke(const char* functionId,
        const Value* args,
        std::size_t argumentCount,
        VmError* error = nullptr);
    // Invoke a statically generated script instance method using its retained
    // receiver. This is the AOT counterpart of Vm::runMethodByName.
    Value invokeMethod(const char* methodId,
        const Value& receiver,
        const Value* args,
        std::size_t argumentCount,
        VmError* error = nullptr);
    void setValueMemoryDescriptor(
        ScriptValueMemoryDescriptorFn descriptor, void* userData) noexcept;

    const core::pmr::Vector<DirectNativeTelemetryEventV5>& telemetry() const { return telemetry_; }

    void clearTelemetry() noexcept { telemetry_.clear(); }

private:
    ScriptAotModuleInstanceV5 module_;
    core::pmr::Vector<DirectNativeTelemetryEventV5> telemetry_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::scriptvm
