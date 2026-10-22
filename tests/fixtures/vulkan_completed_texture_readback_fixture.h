#pragma once
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/frontend/transfer_validation.h"

#include <array>
#include <cassert>
#include <cstring>

namespace arp::test {
// Test-only observation after real completion. Restores the exact native
// layout, never mutates/publishes L2 state/content or manufactures completion.
class VulkanCompletedTextureReadbackFixture final {
public:
    template <class Mips>
    static core::pmr::Vector<std::byte> read(const rhi::GpuTexture& texture,
        const std::shared_ptr<rhi_vulkan::VulkanRhiRuntime>& runtime,
        const Mips& mips,
        std::size_t byteCount,
        rhi::ResourceState state)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        assert(runtime && !mips.empty() && byteCount
            && (texture.desc().usage & TextureUsage::CopySrc) != TextureUsage::None);
        const auto image = vulkanRhiNativePayload(texture);
        assert(image && runtime->logicalDevice() == image->backendVulkanDeviceHandle());
        VkImageLayout originalLayout;
        VkAccessFlags originalAccess;
        VkPipelineStageFlags originalStages;
        switch (state) {
        case ResourceState::ShaderRead:
            originalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            originalAccess = VK_ACCESS_SHADER_READ_BIT;
            originalStages =
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        case ResourceState::ColorAttachment:
            originalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            originalAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            originalStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            break;
        case ResourceState::DepthAttachment:
            originalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            originalAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            originalStages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            break;
        default:
            assert(false && "Undeclared diagnostic source state");
            return {};
        }
        BufferDesc desc;
        desc.label = "Completed diagnostic texture observation";
        desc.sizeBytes = byteCount;
        desc.usage = ResourceUsage::CopyDst;
        desc.memory = MemoryUsage::GpuToCpu;
        VulkanRhiAllocatorDesc allocationDesc;
        allocationDesc.label = desc.label;
        const char* id = nullptr;
        VulkanRhiNativeResourceAllocator query(runtime);
        allocationDesc.memoryBudgetBytes = query.queryBufferAllocationBytes(desc, id);
        assert(allocationDesc.memoryBudgetBytes && !id);
        VulkanRhiAllocator allocator(allocationDesc, runtime);
        VulkanRhiNativeResourceAllocator resources(runtime, &allocator);
        auto output = resources.createBuffer(desc);
        assert(output.success && output.payload && output.payload->backendMappedData());
        const auto device = runtime->logicalDevice();
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = runtime->graphicsQueueFamilyIndex();
        VkCommandPool pool = VK_NULL_HANDLE;
        assert(vkCreateCommandPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS);
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = pool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1u;
        VkCommandBuffer command = VK_NULL_HANDLE;
        assert(vkAllocateCommandBuffers(device, &allocate, &command) == VK_SUCCESS);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        assert(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS);
        for (const auto& mip : mips) {
            assert(mip.range.mipLevelCount == 1u && mip.extent.depth == 1u);
            assert(semantics::TextureTransferValidation::copyLayoutValid(texture.desc(),
                mip.range,
                mip.extent,
                mip.packedBytesPerRow,
                mip.rowCount,
                mip.packedOffset,
                byteCount));
            const auto observed = texture.observeOriginalCommittedState(mip.range);
            assert(observed && observed->state() == state);
            const auto aspect = mip.range.aspect == TextureAspect::Depth
                ? VK_IMAGE_ASPECT_DEPTH_BIT
                : VK_IMAGE_ASPECT_COLOR_BIT;
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            barrier.image = image->backendVulkanImageHandle();
            barrier.subresourceRange = {static_cast<VkImageAspectFlags>(aspect),
                mip.range.baseMipLevel,
                1u,
                mip.range.baseArrayLayer,
                mip.range.arrayLayerCount};
            barrier.oldLayout = originalLayout;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = originalAccess;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkCmdPipelineBarrier(command,
                originalStages,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0u,
                0u,
                nullptr,
                0u,
                nullptr,
                1u,
                &barrier);
            const auto texelBytes =
                semantics::TextureTransferValidation::copyRowBytes(texture.desc().format, 1u);
            assert(texelBytes && mip.packedBytesPerRow % texelBytes == 0u);
            VkBufferImageCopy copy{};
            copy.bufferOffset = mip.packedOffset;
            copy.bufferRowLength = mip.packedBytesPerRow / texelBytes;
            copy.bufferImageHeight = mip.rowCount;
            copy.imageSubresource = {static_cast<VkImageAspectFlags>(aspect),
                mip.range.baseMipLevel,
                mip.range.baseArrayLayer,
                mip.range.arrayLayerCount};
            copy.imageExtent = {mip.extent.width, mip.extent.height, 1u};
            vkCmdCopyImageToBuffer(command,
                barrier.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                output.payload->backendVulkanBufferHandle(),
                1u,
                &copy);
            std::swap(barrier.oldLayout, barrier.newLayout);
            std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
            vkCmdPipelineBarrier(command,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                originalStages,
                0u,
                0u,
                nullptr,
                0u,
                nullptr,
                1u,
                &barrier);
        }
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.buffer = output.payload->backendVulkanBufferHandle();
        host.size = byteCount;
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            0u,
            0u,
            nullptr,
            1u,
            &host,
            0u,
            nullptr);
        assert(vkEndCommandBuffer(command) == VK_SUCCESS);
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        assert(vkCreateFence(device, &fenceInfo, nullptr, &fence) == VK_SUCCESS);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1u;
        submit.pCommandBuffers = &command;
        assert(vkQueueSubmit(runtime->graphicsQueue(), 1u, &submit, fence) == VK_SUCCESS);
        assert(vkWaitForFences(device, 1u, &fence, VK_TRUE, 15000000000ull) == VK_SUCCESS);
        core::pmr::String diagnostic;
        assert(output.payload->backendInvalidateMappedRead(0u, byteCount, diagnostic));
        core::pmr::Vector<std::byte> bytes(byteCount);
        std::memcpy(bytes.data(), output.payload->backendMappedData(), byteCount);
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
        return bytes;
    }
};
} // namespace arp::test
