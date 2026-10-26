#pragma once

#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/frontend/pipeline_synchronous_compute_creation.h"

namespace arp::rhi_vulkan {

class VulkanRhiDevice;

rhi::PipelineValidationReport validateVulkanOriginalComputeDefinition(
    const std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>&,
    const rhi::ComputePipelineDesc&);

class VulkanOriginalArtifactComputeCreation final : public rhi::OriginalComputePipelineCreation {
    friend class VulkanRhiDevice;
    friend class VulkanAcceptedOriginalComputePipelineFactory;
    friend class VulkanOriginalGraphicsBindingInspection;

    class ConstructionKey {
        friend class VulkanOriginalArtifactComputeCreation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::OriginalSynchronousComputeCreationContract;
    using Budget = Contract::Budget;

    VulkanOriginalArtifactComputeCreation(ConstructionKey,
        std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput>,
        std::shared_ptr<rhi::PipelineLayout>,
        const rhi::ComputePipelineDesc&,
        Budget,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<VulkanRhiRuntime>);

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
    static std::shared_ptr<const VulkanOriginalArtifactComputeCreation> create(
        const rhi::GpuDevice&,
        VulkanRhiNativeResourceAllocator&,
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
    const std::shared_ptr<VulkanRhiRuntime> runtime_;
    const VkDevice device_;
    rhi::ComputePipelineDesc definition_;
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload_;
};

class VulkanAcceptedOriginalComputePipelineFactory final {
public:
    static core::UniquePtr<rhi::ComputePipeline> create(const rhi::ComputePipelineDesc&,
        const std::shared_ptr<const rhi::OriginalComputePipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const VulkanRhiRuntime*,
        const char*&);
};

} // namespace arp::rhi_vulkan
