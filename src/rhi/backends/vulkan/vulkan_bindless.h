#pragma once

#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi_vulkan {
const char* inspectVulkanBindlessOrdinaryBinding(const rhi::DescriptorBindingDesc&) noexcept;

class VulkanRhiNativeResourceAllocator;
class VulkanBindlessRootNativeClosure;

rhi::ValidationResult validateVulkanBindlessDescriptorRequirements(
    const VulkanDescriptorIndexingInspection&, const rhi::BindlessShaderArtifact&);

// Pure lowering validation. Inputs alone never authorize a device operation;
// native factories obtain the report from their runtime and a sealed package.
rhi::ValidationResult validateVulkanBindlessComputeRequirements(
    const VulkanDescriptorIndexingInspection&,
    const rhi::BindlessShaderArtifact&,
    const std::array<std::uint32_t, 3>& workgroupSize);

// Publishes only the operations covered by the public native compute path and
// its completion-gated fault collection. Other stages remain unavailable.
class VulkanBindlessCrossExtension final : public rhi::BindlessCrossExtension {
public:
    VulkanBindlessCrossExtension(const VulkanRhiRuntime* runtime,
        VulkanRhiNativeResourceAllocator* nativeResources,
        std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget = {});

    const rhi::BindlessCapabilities& capabilities() const noexcept override { return caps_; }

    core::UniquePtr<rhi::ComputePipeline> createComputePipeline(
        std::shared_ptr<const rhi::BindlessAuthorization>,
        std::string_view entry,
        core::pmr::String& diagnostic) const override;
    core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        std::shared_ptr<const rhi::BindlessAuthorization>,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        core::pmr::String& diagnostic) const override;
    core::UniquePtr<rhi::RenderPipeline> createRenderPipeline(
        std::shared_ptr<const rhi::BindlessAuthorization>,
        const rhi::RenderPipelineDesc& state,
        std::string_view vertexEntry,
        std::string_view fragmentEntry,
        const rhi::BindlessGraphicsPushRequest&,
        core::pmr::String& diagnostic) const override;
    std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareComputeBinding(
        std::shared_ptr<const rhi::BindlessComputeBindingOwners>,
        const rhi::BindlessComputeBindingBudget&,
        core::pmr::String&) const override;
    std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareGraphicsBinding(
        std::shared_ptr<const rhi::BindlessGraphicsBindingOwners>,
        const rhi::BindlessGraphicsBindingBudget&,
        core::pmr::String&) const override;

protected:
    std::shared_ptr<const rhi::OriginalInitializedGeometrySource>
    prepareOriginalInitializedBufferPayload(
        rhi::GpuDevice&, const rhi::OriginalInitializedBufferRequest&, const char*&) const override;
    std::uint64_t queryOriginalSnapshotBufferAllocation(
        const rhi::BufferDesc&, const char*&) const override;
    std::shared_ptr<const rhi::OriginalBindlessRootExecution> prepareOriginalRootExecutionPayload(
        rhi::GpuDevice&,
        const rhi::OriginalBindlessRootFenceExecutionInputs&,
        const rhi::OriginalBindlessRootFenceExecutionBudget&,
        core::pmr::String&) const override;
    std::shared_ptr<const rhi::OriginalBindlessGraphicsExecution>
    prepareOriginalGraphicsExecutionPayload(rhi::GpuDevice&,
        const rhi::OriginalBindlessGraphicsExecutionInputs&,
        const rhi::OriginalBindlessGraphicsExecutionBudget&,
        core::pmr::String&) const override;
    rhi::ValidationResult validateSnapshotCreation(const rhi::BindlessLoweringPlan&) const override;
    rhi::ValidationResult validateAuthorization(
        const rhi::BindlessShaderPackage&, const rhi::BindlessSnapshot&) const override;

private:
    core::UniquePtr<rhi::RenderPipeline> createRenderPipelineImpl(
        std::shared_ptr<const rhi::BindlessAuthorization>,
        const rhi::RenderPipelineDesc&,
        std::string_view,
        std::string_view,
        const rhi::BindlessGraphicsPushRequest*,
        core::pmr::String&) const;
    friend class VulkanBindlessRootNativeClosure;
    friend class VulkanOriginalBindlessCommand;
    friend class VulkanOriginalTextureUploadStaging;
    friend class VulkanOriginalTextureUploadCommand;
    friend class VulkanOriginalGeometryContent;
    const VulkanRhiRuntime* runtime_ = nullptr;
    VulkanRhiNativeResourceAllocator* nativeResources_ = nullptr;
    std::shared_ptr<rhi::semantics::DirectObjectBudgetTracker> directObjectBudget_;
    rhi::BindlessCapabilities caps_;
};

bool isVulkanPreparedComputeBinding(const rhi::BindlessPreparedComputeBinding&) noexcept;
bool isVulkanPreparedGraphicsBinding(const rhi::BindlessPreparedGraphicsBinding&) noexcept;

} // namespace arp::rhi_vulkan
