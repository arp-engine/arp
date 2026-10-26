#pragma once

#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_synchronous_graphics_creation.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/semantics/native_backend_capabilities.h"

namespace arp::rhi_metal {
struct MetalRhiNativePipelinePayload;
class MetalRhiDevice;

class MetalOriginalArtifactGraphicsCreation final : public rhi::OriginalGraphicsPipelineCreation {
    friend class MetalRhiDevice;
    friend class MetalAcceptedOriginalGraphicsPipelineFactory;

    class ConstructionKey {
        friend class MetalOriginalArtifactGraphicsCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::OriginalSynchronousGraphicsCreationContract::Budget;
    MetalOriginalArtifactGraphicsCreation(ConstructionKey,
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>,
        std::shared_ptr<rhi::PipelineLayout>,
        const rhi::RenderPipelineDesc&,
        Budget,
        std::shared_ptr<core::CpuMemoryQuota>,
        rhi::OriginalDeviceLifetime::Ticket,
        rhi::OriginalDeviceNativeLossObservation,
        rhi::semantics::DirectObjectBudgetLease);

    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&
    originalInput() const noexcept override
    {
        return input_;
    }

    const std::shared_ptr<rhi::PipelineLayout>& originalLayout() const noexcept override
    {
        return layout_;
    }

    const rhi::RenderPipelineDesc& definition() const noexcept override { return definition_; }

    const char* validate() const noexcept override;
    bool usesCpuQuotaDomain(const std::shared_ptr<core::CpuMemoryQuota>&) const noexcept override;

private:
    static std::shared_ptr<const MetalOriginalArtifactGraphicsCreation> create(
        const rhi::GpuDevice&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const std::shared_ptr<rhi::PipelineLayout>&,
        const rhi::RenderPipelineDesc&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        rhi::semantics::DirectObjectBudgetLease,
        const char*&);

    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> input_;
    const std::shared_ptr<rhi::PipelineLayout> layout_;
    const Budget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> nativeQuota_;
    const rhi::OriginalDeviceLifetime::Ticket lifetime_;
    const rhi::OriginalDeviceNativeLossObservation loss_;
    rhi::RenderPipelineDesc definition_;
    rhi::semantics::DirectObjectBudgetLease reservation_;
    std::shared_ptr<MetalRhiNativePipelinePayload> payload_;
    mutable const char* failure_ = nullptr;
};
} // namespace arp::rhi_metal
