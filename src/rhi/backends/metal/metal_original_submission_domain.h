#pragma once
#include "rhi/frontend/original_submission_completion.h"

namespace arp::rhi_metal {
class MetalRhiDevice;
class MetalRhiNativeCommandPayload;
class MetalRhiCompletionState;

// One explicitly budgeted owner/result domain per concrete device. Actual
// native callbacks and cold command preparation alone can access its tickets.
class MetalOriginalSubmissionDomain final {
    friend class MetalRhiDevice;
    friend class MetalRhiNativeCommandPayload;
    friend class MetalRhiCompletionState;
    using Storage = rhi::OriginalSubmissionCompletionDomain;

    class ConstructionKey {
        friend class MetalOriginalSubmissionDomain;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalOriginalSubmissionDomain(const MetalOriginalSubmissionDomain&) = delete;
    MetalOriginalSubmissionDomain& operator=(const MetalOriginalSubmissionDomain&) = delete;

    MetalOriginalSubmissionDomain(ConstructionKey,
        std::shared_ptr<Storage> storage,
        rhi::OriginalDeviceLifetime::Ticket device)
        : storage_(std::move(storage)), device_(std::move(device))
    {}

private:
    static std::shared_ptr<MetalOriginalSubmissionDomain> prepare(const rhi::GpuDevice& device,
        const rhi::semantics::OriginalSubmissionCompletionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        diagnostic =
            rhi::semantics::OriginalSubmissionCompletionContract::ordinaryOwnerJoinLowering(
                device.backend(), budget.contractVersion);
        if (diagnostic)
            return {};
        auto storage = Storage::prepareStorage(device, budget, quota, diagnostic);
        if (!storage)
            return {};
        try {
            core::CpuMemoryQuotaScope scope(quota);
            return core::makeShared<MetalOriginalSubmissionDomain>(
                ConstructionKey{}, std::move(storage), device.originalDeviceLifetimeTicket());
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-SUBMISSION-COMPLETION-ALLOCATION";
            return {};
        }
    }

    const char* poll() noexcept
    {
        return device_.terminallyRevoked() ? storage_->retireValidatorAfterResults()
                                           : storage_->pollValidator();
    }

    const char* retire() noexcept { return storage_->retireValidatorAfterResults(); }

    const char* drain(const rhi::semantics::ShutdownDrainRequest& request)
    {
        if (const auto* id = retire())
            return id;
        return storage_->drainRetiredValidatorUntil(request);
    }

    const std::shared_ptr<Storage> storage_;
    const rhi::OriginalDeviceLifetime::Ticket device_;
};
} // namespace arp::rhi_metal
