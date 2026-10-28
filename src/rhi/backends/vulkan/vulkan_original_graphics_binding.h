#pragma once

#include "rhi/backends/vulkan/vulkan_original_graphics_pipeline.h"
#include "rhi/frontend/descriptor_graphics_binding_inspection.h"

namespace arp::rhi_vulkan {

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanOriginalSynchronousDescriptorPayload(
    const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>&);

std::shared_ptr<VulkanRhiNativeResourcePayload> vulkanOriginalGraphicsDescriptorPayload(
    const std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>&);

// Native fields are borrowed only while the caller retains the same original
// inventory. Image views are described, never created here. No descriptor set,
// second source inventory, persistent owner or execution receipt is issued.
class VulkanOriginalGraphicsBindingInspection final {
public:
    using Contract = rhi::OriginalGraphicsBindingInspectionContract;

    struct Observation {
        bool available = false;
        VkDevice device = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::uint32_t set = 0u, binding = 0u;
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
        VkDescriptorBufferInfo buffer{};
        VkImageViewCreateInfo imageView{};
        VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkSampler sampler = VK_NULL_HANDLE;
        std::uint64_t staticOccupiedBytes = 0u, runtimeStrideBytes = 0u, runtimeElementCount = 0u;

        bool nativeExecutionProven() const noexcept { return false; }
    };

    static const char* inspect(Contract::Request,
        const rhi::GpuDevice&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::size_t pipelineSourceIndex,
        std::uint32_t logicalSet,
        const rhi::OriginalCommandDescriptorSetElement&,
        Observation&) noexcept;

private:
    template <bool compute>
    static const char* inspectOriginal(const rhi::GpuDevice&,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>&,
        std::size_t,
        std::uint32_t,
        const rhi::OriginalCommandDescriptorSetElement&,
        Observation&,
        rhi::semantics::OriginalOrdinarySampledColorContract::Intent) noexcept;
};

} // namespace arp::rhi_vulkan
