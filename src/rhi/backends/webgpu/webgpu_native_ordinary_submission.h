#pragma once
#include "core/runtime/completion_drain_consumer.h"
#include "core/runtime/completion_evidence_batch.h"
#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_native_completion_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_error_scope_batch.h"
#include "rhi/backends/webgpu/webgpu_recorded_command_proof.h"

namespace arp::rhi_webgpu {
struct WebGpuOrdinarySubmissionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSubmissions = 0u, maxCallbacks = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// WebOrdinaryClosedQueueSubmission/1. Generic/staging/upload remains closed.
class WebGpuOrdinarySubmission final : public core::QuotaBoundSharedObjectIdentity {
    friend class WebGpuBindlessRootIssuerPreparation;
    using TypedState = WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuOriginalTypedColdCommand>;
    using RootState = WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuBindlessRootColdCommand>;

    class ConstructionKey {
        friend class WebGpuOrdinarySubmission;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

    struct Callback {
        std::shared_ptr<WebGpuOrdinarySubmission> owner;
        std::uint32_t participant = 0u;
    };

public:
    static constexpr std::uint32_t callbackCount = 1u + WebGpuNativeErrorScopeBatch::count;
    enum class Status { Ready, Pending, Completed, Unproven, DeviceLost };

    WebGpuOrdinarySubmission(
        ConstructionKey, std::shared_ptr<const WebGpuRecordedCommandProof> recording)
        : recording_(std::move(recording))
    {}

    WebGpuOrdinarySubmission(ConstructionKey,
        std::shared_ptr<const WebGpuRecordedCommandProof> recording,
        std::weak_ptr<WebGpuCanonicalCallbackCustody::State> state)
        : recording_(std::move(recording)), canonicalState_(std::move(state)), canonicalMode_(true)
    {}

    WebGpuOrdinarySubmission(ConstructionKey,
        std::shared_ptr<const WebGpuRecordedCommandProof> recording,
        std::weak_ptr<TypedState> state)
        : recording_(std::move(recording)), canonicalState_(std::move(state)), canonicalMode_(true),
          typedCanonicalMode_(true)
    {}

    ~WebGpuOrdinarySubmission();

    WebGpuOrdinarySubmission(ConstructionKey,
        std::shared_ptr<const WebGpuRecordedCommandProof> recording,
        std::weak_ptr<RootState> state)
        : recording_(std::move(recording)), canonicalState_(std::move(state)), canonicalMode_(true),
          rootCanonicalMode_(true)
    {}

    static const char* validateBudget(const WebGpuOrdinarySubmissionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-WEBGPU-ORDINARY-SUBMISSION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-WEBGPU-ORDINARY-SUBMISSION-OPT-IN";
        if (!budget.maxSubmissions || budget.maxCallbacks < callbackCount
            || !budget.maxCpuMetadataBytes)
            return "RHI-WEBGPU-ORDINARY-SUBMISSION-BUDGET";
        return nullptr;
    }

    static std::shared_ptr<WebGpuOrdinarySubmission> prepare(
        std::shared_ptr<const WebGpuRecordedCommandProof>,
        const WebGpuOrdinarySubmissionBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);
    static const char* submit(const std::shared_ptr<WebGpuOrdinarySubmission>&) noexcept;
    Status status() const noexcept;
    const char* diagnostic() const noexcept;

    const auto& originalRecording() const noexcept { return recording_; }

    bool callbacksIssued() const noexcept { return callbacksIssued_; }

    bool callbacksDrained() const noexcept { return callbacksIssued_ && evidence_.allReceived(); }

    bool scopeCompletionsObserved() const noexcept
    {
        for (std::uint32_t i = 1u; i < callbackCount; ++i)
            if (!evidence_.hasReceived(i))
                return false;
        return true;
    }

    bool queueCompletionObserved() const noexcept { return evidence_.hasReceived(0u); }

private:
    friend class WebGpuCanonicalCallbackCustody;
    friend class WebGpuCanonicalIssuerPreparation;
    static const char* submitCanonical(std::shared_ptr<WebGpuOrdinarySubmission>) noexcept;
    static void completeCanonical(void*, WebGpuNativeCompletionReceipt::Outcome) noexcept;
    void finishCanonicalSubmission() noexcept;
    bool canonicalInvocationProven_ = false, canonicalIssuanceComplete_ = false;
    static std::shared_ptr<WebGpuOrdinarySubmission> allocateCanonical(
        std::shared_ptr<const WebGpuRecordedCommandProof>,
        std::weak_ptr<WebGpuCanonicalCallbackCustody::State>);
    std::shared_ptr<WebGpuCanonicalCallbackCustody::State> legacyCanonicalState() const noexcept;
    std::shared_ptr<TypedState> typedCanonicalState() const noexcept;
    std::shared_ptr<RootState> rootCanonicalState() const noexcept;
    static std::shared_ptr<WebGpuOrdinarySubmission> allocateRoot(
        std::shared_ptr<const WebGpuRecordedCommandProof>, std::weak_ptr<RootState>);
    static void completeRoot(void*, WebGpuNativeCompletionReceipt::Outcome) noexcept;
    template <class State>
    static void completePrepared(void*,
        WebGpuNativeCompletionReceipt::Outcome,
        WebGpuNativeCompletionReceipt::Callback) noexcept;
    const char* rootColdDiagnostic() const noexcept;
    static std::shared_ptr<WebGpuOrdinarySubmission> allocateTyped(
        std::shared_ptr<const WebGpuRecordedCommandProof>, std::weak_ptr<TypedState>);
    static void completeTyped(void*, WebGpuNativeCompletionReceipt::Outcome) noexcept;
    friend class WebGpuOriginalTypedIssuerPreparation;
    static const char* submitTyped(std::shared_ptr<WebGpuOrdinarySubmission>) noexcept;
    const char* typedColdDiagnostic() const noexcept;
    Status canonicalStatus() const noexcept;
    const char* canonicalDiagnostic() const noexcept;
    friend class WebGpuOriginalTextureReadbackPublication;
    void observe(std::uint32_t participant, bool proven) noexcept;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void completeQueue(WGPUQueueWorkDoneStatus, WGPUStringView, void*, void*) noexcept;
    static void completeScope(
        WGPUPopErrorScopeStatus, WGPUErrorType, WGPUStringView, void*, void*) noexcept;
#endif
    std::shared_ptr<core::runtime::CompletionDrainConsumer> readbackDrainConsumer_;
    bool callbacksIssued_ = false;
    const std::shared_ptr<const WebGpuRecordedCommandProof> recording_;
    // Identity retention only. Private constructor tag selects the exact type;
    // no public untyped owner or execution interface exists.
    const std::weak_ptr<void> canonicalState_;
    const bool canonicalMode_ = false;
    const bool typedCanonicalMode_ = false;
    const bool rootCanonicalMode_ = false;
    std::array<core::UniquePtr<Callback>, callbackCount> callbacks_;
    core::runtime::CompletionEvidenceBatch evidence_{callbackCount};
    Status status_ = Status::Ready;
};
} // namespace arp::rhi_webgpu
