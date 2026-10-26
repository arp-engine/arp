#pragma once

#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_synchronous_graphics_creation.h"

namespace arp::rhi_vulkan {

class VulkanRhiDevice;

rhi::PipelineValidationReport validateVulkanOriginalGraphicsDefinition(
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
    const rhi::RenderPipelineDesc&);

class VulkanOriginalArtifactGraphicsCreation final : public rhi::OriginalGraphicsPipelineCreation {
    friend class VulkanRhiDevice;
    friend class VulkanAcceptedOriginalGraphicsPipelineFactory;
    friend class VulkanOriginalGraphicsBindingInspection;

    class ConstructionKey {
        friend class VulkanOriginalArtifactGraphicsCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::OriginalSynchronousGraphicsCreationContract;
    using Budget = Contract::Budget;

    VulkanOriginalArtifactGraphicsCreation(ConstructionKey,
        std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>,
        std::shared_ptr<rhi::PipelineLayout>,
        const rhi::RenderPipelineDesc&,
        Budget,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<VulkanRhiRuntime>);

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
    static std::shared_ptr<const VulkanOriginalArtifactGraphicsCreation> create(
        const rhi::GpuDevice&,
        VulkanRhiNativeResourceAllocator&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
        const std::shared_ptr<rhi::PipelineLayout>&,
        const rhi::RenderPipelineDesc&,
        Budget,
        const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
        rhi::semantics::DirectObjectBudgetLease,
        const char*&);
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> input_;
    const std::shared_ptr<rhi::PipelineLayout> layout_;
    const Budget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> nativeQuota_;
    const std::shared_ptr<VulkanRhiRuntime> runtime_;
    const VkDevice device_;
    rhi::RenderPipelineDesc definition_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload_;
};

class VulkanAcceptedOriginalGraphicsPipelineFactory final {
public:
    static core::UniquePtr<rhi::RenderPipeline> create(const rhi::RenderPipelineDesc&,
        const std::shared_ptr<const rhi::OriginalGraphicsPipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const VulkanRhiRuntime*,
        const char*&);
};

// Device-owned cold opt-in only. The common acceptance/publication transaction
// remains GpuDevice::createOriginalArtifactGraphicsPipeline, version 2.
std::shared_ptr<const VulkanOriginalArtifactGraphicsCreation> createVulkanOriginalGraphicsCreation(
    rhi::GpuDevice&,
    const std::shared_ptr<const rhi::OriginalGraphicsPipelineInput>&,
    const std::shared_ptr<rhi::PipelineLayout>&,
    const rhi::RenderPipelineDesc&,
    VulkanOriginalArtifactGraphicsCreation::Budget,
    const std::shared_ptr<core::CpuMemoryQuota>& ownerQuota,
    const std::shared_ptr<core::CpuMemoryQuota>& nativeQuota,
    const char*&);

} // namespace arp::rhi_vulkan
