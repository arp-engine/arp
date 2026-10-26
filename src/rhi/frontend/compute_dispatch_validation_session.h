#pragma once

#include "rhi/frontend/compute_dispatch_validation.h"
#include "rhi/frontend/resource_device_lifetime.h"

#include <array>

namespace arp::rhi {

struct ComputeDispatchValidationSessionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPendingSubmissions = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u, maxNativeAllocationBytes = 0u;
    std::uint64_t maxArtifactFileBytes = 0u, maxArtifactTotalBytes = 0u;
    std::uint64_t maxArtifactCpuBytes = 0u, maxInputCpuBytes = 0u;
    std::uint64_t maxPipelineCpuBytes = 0u, maxPipelineTargetBytes = 0u;
};

struct ComputeDispatchValidationSessionQuotas {
    std::shared_ptr<core::CpuMemoryQuota> session, artifact, input, pipeline;
};

class ComputeDispatchValidationSessionContract final {
public:
    static const char* inspectQuotas(const ComputeDispatchValidationSessionBudget& budget,
        const ComputeDispatchValidationSessionQuotas& q) noexcept
    {
        const std::array quotas{q.session, q.artifact, q.input, q.pipeline};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                quotas.size(), [&](std::size_t i) -> const auto& {
                    return quotas[i];
                }))
            return "RHI-DISPATCH-SESSION-QUOTA-OWNERS";
        const std::uint64_t limits[]{budget.maxCpuMetadataBytes,
            budget.maxArtifactCpuBytes,
            budget.maxInputCpuBytes,
            budget.maxPipelineCpuBytes};
        for (std::size_t i = 0u; i < quotas.size(); ++i)
            if (quotas[i]->limitBytes() != limits[i])
                return "RHI-DISPATCH-SESSION-QUOTA-LIMIT";
        return nullptr;
    }

    static constexpr const char* reservationDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Metal:
            return nullptr;
        case Backend::Null:
            return "RHI-DISPATCH-VALIDATION-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::WebGPU:
            return "RHI-DISPATCH-VALIDATION-QUERIED-LIMITS";
        case Backend::WebGL:
            return "RHI-DISPATCH-VALIDATION-UNSUPPORTED";
        }
        return "RHI-DISPATCH-VALIDATION-BACKEND";
    }

    static const char* inspect(const ComputeDispatchValidationRequest& request,
        const ComputeDispatchValidationSessionBudget& budget) noexcept
    {
        ComputeDispatchValidationPlan plan;
        if (const auto* id = ComputeDispatchValidationContract::inspect(request, plan))
            return id;
        if (budget.contractVersion != 1u)
            return "RHI-DISPATCH-SESSION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-DISPATCH-SESSION-OPT-IN";
        // This protocol reuses a single result buffer only after its preceding
        // row completed and every covered/uncovered/guard word was checked.
        if (budget.maxPendingSubmissions != 1u)
            return "RHI-DISPATCH-SESSION-PENDING-BUDGET";
        if (!budget.maxCpuMetadataBytes || !budget.maxArtifactFileBytes
            || !budget.maxArtifactTotalBytes || !budget.maxArtifactCpuBytes
            || !budget.maxInputCpuBytes || !budget.maxPipelineCpuBytes
            || !budget.maxPipelineTargetBytes)
            return "RHI-DISPATCH-SESSION-CPU-BUDGET";
        if (budget.maxNativeAllocationBytes < plan.nativeResultBytes)
            return "RHI-DISPATCH-SESSION-NATIVE-BUDGET";
        return nullptr;
    }
};

// CPU admission only, issued by the original device. Its last owner returns
// the one pending-session slot. It proves neither native completion nor coverage.
// The backend's native callback must retain it with the original resource graph.
class ComputeDispatchValidationReservation final {
    friend class GpuDevice;

    class ConstructionKey {
        friend class GpuDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    ComputeDispatchValidationReservation(
        ConstructionKey, const std::shared_ptr<OriginalDeviceLifetime>& lifetime) noexcept
        : lifetime_(lifetime)
    {}

    ComputeDispatchValidationReservation(const ComputeDispatchValidationReservation&) = delete;
    ComputeDispatchValidationReservation& operator=(
        const ComputeDispatchValidationReservation&) = delete;

    ~ComputeDispatchValidationReservation()
    {
        if (const auto lifetime = lifetime_.lock())
            lifetime->computeDispatchValidationPending_.store(false, std::memory_order_release);
    }

    bool nativeExecutionProven() const noexcept { return false; }

private:
    const std::weak_ptr<OriginalDeviceLifetime> lifetime_;
};

// Serialized protocol metadata, not an execution receipt. Backend observations
// supply completion and counts; this public state cannot authenticate them.
// Timeout/loss/cancellation terminate verification but never drain native work.
class ComputeDispatchValidationProgress final {
public:
    explicit ComputeDispatchValidationProgress(const ComputeDispatchValidationRequest& request)
        : request_(request),
          diagnostic_(ComputeDispatchValidationContract::inspect(request_, plan_))
    {}

    ComputeDispatchValidationProgress(const ComputeDispatchValidationProgress&) = delete;
    ComputeDispatchValidationProgress& operator=(const ComputeDispatchValidationProgress&) = delete;

    const auto& request() const noexcept { return request_; }

    const auto& plan() const noexcept { return plan_; }

    const char* diagnostic() const noexcept { return diagnostic_; }

    bool pending() const noexcept { return pending_; }

    bool verified() const noexcept
    {
        return !diagnostic_ && !pending_ && checkedRows_ == plan_.queueSubmissions;
    }

    std::uint32_t nextRow() const noexcept { return checkedRows_ + 1u; }

    std::uint32_t checkedRows() const noexcept { return checkedRows_; }

    const char* observeDeadline(std::uint64_t elapsedNanoseconds) noexcept
    {
        if (!verified() && elapsedNanoseconds >= request_.budget.maxColdValidationNanoseconds)
            invalidate("RHI-DISPATCH-VALIDATION-TIMEOUT");
        return diagnostic_;
    }

    const char* beginRow(std::uint64_t elapsedNanoseconds) noexcept
    {
        if (const auto* id = observeDeadline(elapsedNanoseconds))
            return id;
        if (pending_ || verified())
            return "RHI-DISPATCH-SESSION-ROW-STATE";
        pending_ = true;
        return nullptr;
    }

    // Real terminal completion (successful OR failed) drains pending ownership.
    // The backend must never call this for a host timeout or semantic loss.
    const char* completeRow(bool successful,
        std::uint64_t dispatches,
        std::uint64_t workgroups,
        std::uint64_t checks,
        std::uint64_t elapsedNanoseconds) noexcept
    {
        if (!pending_)
            return "RHI-DISPATCH-SESSION-ROW-STATE";
        observeDeadline(elapsedNanoseconds);
        pending_ = false;
        if (diagnostic_)
            return diagnostic_;
        if (!successful)
            return invalidate("RHI-DISPATCH-SESSION-NATIVE-FAILURE");
        const auto x = std::uint64_t(plan_.groups.width);
        const auto expectedGroups = plan_.workgroups / (x * (x + 1u) / 2u) * nextRow();
        if (dispatches != std::uint64_t(plan_.groups.height) * plan_.groups.depth
            || workgroups != expectedGroups
            || checks
                != plan_.nativeResultBytes / ComputeDispatchValidationContract::resultWordBytes)
            return invalidate("RHI-DISPATCH-SESSION-INCOMPLETE-RESULTS");
        ++checkedRows_;
        return nullptr;
    }

    const char* invalidate(const char* diagnostic) noexcept
    {
        if (!diagnostic_)
            diagnostic_ = diagnostic ? diagnostic : "RHI-DISPATCH-SESSION-CANCELLED";
        return diagnostic_;
    }

private:
    const ComputeDispatchValidationRequest request_;
    ComputeDispatchValidationPlan plan_;
    const char* diagnostic_ = nullptr;
    std::uint32_t checkedRows_ = 0u;
    bool pending_ = false;
};

} // namespace arp::rhi
