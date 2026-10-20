#pragma once
#include "original_storage_execution_fixture.h"
#include "vulkan_original_root_terminal_fixture.h"

namespace arp::test {
struct VulkanStorageExecutionBackendFixture {
    static constexpr bool callback = false;
    using Closure = rhi_vulkan::VulkanBindlessRootNativeClosure;
    static constexpr unsigned nativePayloadObjects = 7u;
    inline static constexpr const char* label = "vulkan";
    inline static constexpr const char* marker = "VULKAN";
    inline static std::shared_ptr<rhi_vulkan::VulkanRhiRuntime> runtime;
    // Additional storage-specific failure terminals reuse terminal2's strict
    // invalidation/no-publication oracle: unknown queue, unknown fence, no invocation.
    inline static unsigned failure = 0u;

    template <class Execution, class Result, class Quotas, class Capacity, class Snapshot>
    static void complete(rhi::GpuDevice& device,
        Execution& execution,
        const Result& result,
        const Quotas& q,
        const Capacity& capacity,
        const Snapshot& snapshot,
        unsigned terminal,
        core::pmr::String& diagnostic,
        unsigned executionVersion = 9u)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        using Hooks = RootFenceNegativeObservation;
        assert(!failure || terminal == 2u);
        Hooks::withholdCompletion = true;
        Hooks::uncertainQueueResult = failure == 1u || failure == 3u;
        Hooks::rejectQueueInvocation = failure == 3u;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
            const auto* id =
                OriginalBindlessRootExecution::submit(execution, {executionVersion, true});
            if (Hooks::uncertainQueueResult) {
                assert(id);
                return nullptr;
            }
            return id;
        }));
        Hooks::uncertainQueueResult = Hooks::rejectQueueInvocation = false;
        assert(execution->pendingRecords() == 1u && !execution->pollCompletion()
            && !result->publishedBytes());
        assert(capacity->used() == 2u && snapshot->originalSubmissionBudgetOwner()->used() == 1u);
        if (terminal == 1u)
            execution.reset();
        else
            assert(execution->cancelExecution());
        if (terminal == 2u && !failure)
            device.retireOriginalDeviceLifetime();
        // Ensure the actual queue finished before replacing its fence observation
        // with an error. This wait grants no L2 completion or publication.
        if (failure == 2u)
            assert(vkQueueWaitIdle(runtime->graphicsQueue()) == VK_SUCCESS);
        Hooks::withholdCompletion = false;
        Hooks::uncertainFenceResult = failure == 2u;
        const auto* id =
            CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                if (failure == 1u || failure == 3u)
                    device.waitIdle();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                while (snapshot->originalSubmissionBudgetOwner()->used()) {
                    if (execution)
                        execution->pollCompletion();
                    retireCompletedVulkanRhiSubmittedCommands();
                    if (failure == 2u) {
                        Hooks::uncertainFenceResult = false;
                        device.waitIdle();
                    }
                    if (std::chrono::steady_clock::now() > deadline)
                        return "VULKAN-STORAGE-COMPLETION-TIMEOUT";
                    std::this_thread::yield();
                }
                return nullptr;
            });
        Hooks::uncertainFenceResult = false;
        if (id)
            std::fprintf(stderr, "Vulkan storage completion: %s %s\n", id, diagnostic.c_str());
        assert(!id);
        if (failure)
            std::printf(
                "BINDLESS_VULKAN_PUBLIC_STORAGE_FAILURE failure=%u actual-drain invalidated unpublished hot0\n",
                failure);
    }

    template <class T, std::size_t N>
    static bool readBuffer(
        const rhi::GpuBuffer& buffer, std::array<T, N>& values, core::pmr::String& diagnostic)
    {
        const auto payload = rhi_vulkan::vulkanRhiNativePayload(buffer);
        if (!payload || !payload->backendMappedData()
            || !payload->backendInvalidateMappedRead(0u, sizeof(values), diagnostic))
            return false;
        std::memcpy(values.data(), payload->backendMappedData(), sizeof(values));
        return true;
    }

    // Independent observation after genuine public completion. This test-only
    // copy restores the exact layout and never publishes L2 state or content.
    static std::array<std::uint16_t, 20u> readPixels(const rhi::GpuTexture& texture)
    {
        using namespace rhi;
        using namespace rhi_vulkan;
        assert(runtime && (texture.desc().usage & TextureUsage::CopySrc) != TextureUsage::None);
        const auto image = vulkanRhiNativePayload(texture);
        assert(image);
        const auto device = runtime->logicalDevice();
        assert(device == image->backendVulkanDeviceHandle());
        BufferDesc desc;
        desc.label = "Storage completed native observation";
        desc.sizeBytes = 40u;
        desc.usage = ResourceUsage::CopyDst;
        desc.memory = MemoryUsage::GpuToCpu;
        VulkanRhiAllocatorDesc allocationDesc;
        allocationDesc.label = desc.label;
        const char* queryDiagnostic = nullptr;
        VulkanRhiNativeResourceAllocator query(runtime);
        allocationDesc.memoryBudgetBytes = query.queryBufferAllocationBytes(desc, queryDiagnostic);
        assert(allocationDesc.memoryBudgetBytes && !queryDiagnostic);
        VulkanRhiAllocator allocator(allocationDesc, runtime);
        VulkanRhiNativeResourceAllocator resources(runtime, &allocator);
        auto output = resources.createBuffer(desc);
        assert(output.success && output.payload && output.payload->backendMappedData());
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
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = image->backendVulkanImageHandle();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 1u, 2u, 0u, 1u};
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            1u,
            &barrier);
        std::array<VkBufferImageCopy, 2u> copies{};
        for (unsigned i = 0u; i != 2u; ++i) {
            copies[i].bufferOffset = i ? 32u : 0u;
            copies[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i + 1u, 0u, 1u};
            copies[i].imageExtent = {i ? 1u : 2u, i ? 1u : 2u, 1u};
        }
        vkCmdCopyImageToBuffer(command,
            barrier.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            output.payload->backendVulkanBufferHandle(),
            copies.size(),
            copies.data());
        std::swap(barrier.oldLayout, barrier.newLayout);
        std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
        vkCmdPipelineBarrier(command,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0u,
            0u,
            nullptr,
            0u,
            nullptr,
            1u,
            &barrier);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        host.buffer = output.payload->backendVulkanBufferHandle();
        host.offset = 0u;
        host.size = desc.sizeBytes;
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
        assert(output.payload->backendInvalidateMappedRead(0u, desc.sizeBytes, diagnostic));
        std::array<std::uint16_t, 20u> values;
        std::memcpy(values.data(), output.payload->backendMappedData(), sizeof(values));
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, pool, nullptr);
        return values;
    }
};

using VulkanOriginalStorageExecutionFixture =
    OriginalStorageExecutionFixture<VulkanStorageExecutionBackendFixture>;
} // namespace arp::test
