#pragma once

#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/frontend/pipeline_synchronous_compute_creation.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/semantics/native_backend_capabilities.h"

namespace arp::rhi_metal {
struct MetalRhiNativePipelinePayload;
class MetalRhiDevice;

class MetalOriginalArtifactComputeCreation final : public rhi::OriginalComputePipelineCreation {
    friend class MetalRhiDevice;
    friend class MetalAcceptedOriginalComputePipelineFactory;

    class ConstructionKey {
        friend class MetalOriginalArtifactComputeCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalSynchronousComputeCreationContract::Budget;
    MetalOriginalArtifactComputeCreation(ConstructionKey,
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>,
        std::shared_ptr<rhi::PipelineLayout>,
        const rhi::ComputePipelineDesc&,
        Budget,
        std::shared_ptr<core::CpuMemoryQuota>,
        rhi::OriginalDeviceLifetime::Ticket,
        rhi::OriginalDeviceNativeLossObservation,
        rhi::semantics::DirectObjectBudgetLease);

    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&
    originalInput() const noexcept override
    {
        return input_;
    }

    const std::shared_ptr<rhi::PipelineLayout>& originalLayout() const noexcept override
    {
        return layout_;
    }

    const rhi::ComputePipelineDesc& definition() const noexcept override { return definition_; }

    const char* validate() const noexcept override;
    bool usesCpuQuotaDomain(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept override;

private:
    static std::shared_ptr<const MetalOriginalArtifactComputeCreation> create(const rhi::GpuDevice&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
        const std::shared_ptr<rhi::PipelineLayout>&,
        const rhi::ComputePipelineDesc&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        rhi::semantics::DirectObjectBudgetLease,
        const char*&);
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> input_;
    const std::shared_ptr<rhi::PipelineLayout> layout_;
    const Budget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> nativeQuota_;
    const rhi::OriginalDeviceLifetime::Ticket lifetime_;
    const rhi::OriginalDeviceNativeLossObservation loss_;
    rhi::ComputePipelineDesc definition_;
    // Release the payload before its explicit direct-object reservation.
    rhi::semantics::DirectObjectBudgetLease reservation_;
    std::shared_ptr<MetalRhiNativePipelinePayload> payload_;
    mutable const char* failure_ = nullptr;
};
} // namespace arp::rhi_metal
