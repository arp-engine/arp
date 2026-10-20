#include "rhi/backends/vulkan/vulkan_native_commands.h"

#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_conformance_clear.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "rhi/backends/vulkan/vulkan_query_pool.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>

namespace arp::rhi_vulkan {

namespace {

template <typename Fn> Fn functionFrom(PFN_vkVoidFunction value)
{
    Fn result = nullptr;
    static_assert(sizeof(result) == sizeof(value), "unexpected Vulkan function pointer size");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::pmr::string vkFailure(const char* operation, VkResult result)
{
    std::ostringstream message;
    message << operation << " failed: " << static_cast<int>(result);
    return arp::core::toPmrString(message.str());
}

VkImageLayout layoutFor(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::Undefined:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case rhi::ResourceState::CopySrc:
        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case rhi::ResourceState::CopyDst:
        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case rhi::ResourceState::ShaderRead:
        return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case rhi::ResourceState::ShaderWrite:
        return VK_IMAGE_LAYOUT_GENERAL;
    case rhi::ResourceState::ColorAttachment:
        return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case rhi::ResourceState::DepthAttachment:
        return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case rhi::ResourceState::Present:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    return VK_IMAGE_LAYOUT_UNDEFINED;
}

std::uint32_t bytesPerPixelForVulkanCopy(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8_UNORM:
        return 1u;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
        return 4u;
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
        return 4u;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8u;
    case VK_FORMAT_D32_SFLOAT:
        return 4u;
    case VK_FORMAT_D24_UNORM_S8_UINT:
        return 4u;
    default:
        break;
    }
    return 0u;
}

/// True for Vulkan block-compressed formats (BC1/BC3/BC7). These require
/// block-aware upload math: bufferRowLength is in texels (a multiple of the
/// 4x4 block extent), and bytesPerRow is a byte pitch over block rows.
bool isVulkanBlockCompressed(VkFormat format) noexcept
{
    switch (format) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return true;
    default:
        return false;
    }
}

/// Bytes per 4x4 block for a Vulkan BC format (BC1=8, BC3/BC7=16). Returns 0
/// for non-block-compressed formats.
std::uint32_t vulkanBytesPerBlock(VkFormat format) noexcept
{
    switch (format) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return 8u;
    case VK_FORMAT_BC3_UNORM_BLOCK:
    case VK_FORMAT_BC3_SRGB_BLOCK:
    case VK_FORMAT_BC7_UNORM_BLOCK:
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return 16u;
    default:
        return 0u;
    }
}

/// Texel row length for a VkBufferImageCopy of a block-compressed format.
/// Vulkan requires bufferRowLength in texels and a multiple of the block
/// extent (4). Given a byte row pitch (bytesPerRow), this computes
/// (bytesPerRow / bytesPerBlock) * blockExtent = blocksPerRow * 4.
std::uint32_t vulkanBlockRowLengthTexels(std::uint32_t bytesPerRow, VkFormat format) noexcept
{
    const auto bp = vulkanBytesPerBlock(format);
    if (bp == 0u || bytesPerRow % bp != 0u)
        return 0u;
    return (bytesPerRow / bp) * 4u;
}

VkExtent3D vulkanImageCopyExtent(
    const rhi::Extent3D& extent, const rhi::TextureSubresourceRange& subresources) noexcept
{
    // The RHI copy extent uses depth for the number of contiguous slices. In
    // Vulkan, 2D-array/cube slices are represented by layerCount and the
    // image extent depth must remain one. A true 3D texture has one array
    // layer and keeps its physical depth here.
    return {
        extent.width,
        extent.height,
        subresources.arrayLayerCount > 1u ? 1u : extent.depth,
    };
}

VkPipelineStageFlags stageFor(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::CopySrc:
    case rhi::ResourceState::CopyDst:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case rhi::ResourceState::ShaderRead:
        return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case rhi::ResourceState::ShaderWrite:
        return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case rhi::ResourceState::ColorAttachment:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case rhi::ResourceState::DepthAttachment:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case rhi::ResourceState::Present:
        return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    case rhi::ResourceState::Undefined:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
}

VkAccessFlags accessFor(rhi::ResourceState state)
{
    switch (state) {
    case rhi::ResourceState::CopySrc:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case rhi::ResourceState::CopyDst:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    case rhi::ResourceState::ShaderRead:
        return VK_ACCESS_SHADER_READ_BIT;
    case rhi::ResourceState::ShaderWrite:
        return VK_ACCESS_SHADER_WRITE_BIT;
    case rhi::ResourceState::ColorAttachment:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case rhi::ResourceState::DepthAttachment:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case rhi::ResourceState::Undefined:
    case rhi::ResourceState::Present:
        return 0;
    }
    return 0;
}

VkPipelineStageFlags stageMaskFor(rhi::PipelineStage stages)
{
    VkPipelineStageFlags result = 0;
    const auto has = [&](rhi::PipelineStage stage) {
        return static_cast<std::uint32_t>(stages & stage) != 0;
    };
    if (has(rhi::PipelineStage::TopOfPipe))
        result |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (has(rhi::PipelineStage::Transfer))
        result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (has(rhi::PipelineStage::VertexShader))
        result |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (has(rhi::PipelineStage::FragmentShader))
        result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (has(rhi::PipelineStage::ComputeShader))
        result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (has(rhi::PipelineStage::ColorAttachmentOutput))
        result |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (has(rhi::PipelineStage::DepthStencil)) {
        result |=
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    if (has(rhi::PipelineStage::Present))
        result |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (has(rhi::PipelineStage::DrawIndirect))
        result |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    if (has(rhi::PipelineStage::VertexInput))
        result |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    return result;
}

VkAccessFlags accessMaskFor(rhi::ResourceAccess accesses)
{
    VkAccessFlags result = 0;
    const auto has = [&](rhi::ResourceAccess access) {
        return static_cast<std::uint32_t>(accesses & access) != 0;
    };
    if (has(rhi::ResourceAccess::TransferRead))
        result |= VK_ACCESS_TRANSFER_READ_BIT;
    if (has(rhi::ResourceAccess::TransferWrite))
        result |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (has(rhi::ResourceAccess::ShaderRead))
        result |= VK_ACCESS_SHADER_READ_BIT;
    if (has(rhi::ResourceAccess::ShaderWrite))
        result |= VK_ACCESS_SHADER_WRITE_BIT;
    if (has(rhi::ResourceAccess::ColorAttachmentRead))
        result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (has(rhi::ResourceAccess::ColorAttachmentWrite))
        result |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (has(rhi::ResourceAccess::DepthStencilRead))
        result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (has(rhi::ResourceAccess::DepthStencilWrite))
        result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (has(rhi::ResourceAccess::PresentRead))
        result |= VK_ACCESS_MEMORY_READ_BIT;
    if (has(rhi::ResourceAccess::IndirectCommandRead))
        result |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    if (has(rhi::ResourceAccess::VertexAttributeRead))
        result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (has(rhi::ResourceAccess::IndexRead))
        result |= VK_ACCESS_INDEX_READ_BIT;
    return result;
}

VkImageAspectFlags aspectFor(rhi::TextureAspect aspect)
{
    switch (aspect) {
    case rhi::TextureAspect::Color:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    case rhi::TextureAspect::Depth:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case rhi::TextureAspect::Stencil:
        return VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

constexpr std::size_t kMaxReusableCommandResources = 8;
constexpr std::size_t kMaxReusableFences = 8;

struct ReusableCommandResources {
    std::shared_ptr<VulkanRhiRuntime> runtime;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex = 0u;
};

std::mutex g_reusableCommandResourcesMutex;
std::deque<ReusableCommandResources> g_reusableCommandResources;

struct ReusableFence {
    std::shared_ptr<VulkanRhiRuntime> runtime;
    VkFence fence = VK_NULL_HANDLE;
};

std::mutex g_reusableFencesMutex;
std::deque<ReusableFence> g_reusableFences;

void destroyReusableFence(ReusableFence fence)
{
    if (!fence.runtime || !fence.fence)
        return;
    auto* destroyFence =
        functionFrom<PFN_vkDestroyFence>(fence.runtime->getDeviceProcAddr("vkDestroyFence"));
    if (destroyFence) {
        destroyFence(fence.runtime->logicalDevice(), fence.fence, nullptr);
    }
}

bool acquireReusableFence(const std::shared_ptr<VulkanRhiRuntime>& runtime, VkFence& fence)
{
    if (!runtime)
        return false;
    auto* resetFences =
        functionFrom<PFN_vkResetFences>(runtime->getDeviceProcAddr("vkResetFences"));
    if (!resetFences)
        return false;

    std::lock_guard lock(g_reusableFencesMutex);
    for (auto it = g_reusableFences.begin(); it != g_reusableFences.end(); ++it) {
        if (it->runtime.get() != runtime.get())
            continue;
        const auto reset = resetFences(runtime->logicalDevice(), 1u, &it->fence);
        if (reset != VK_SUCCESS) {
            auto stale = std::move(*it);
            g_reusableFences.erase(it);
            destroyReusableFence(std::move(stale));
            return false;
        }
        fence = it->fence;
        g_reusableFences.erase(it);
        return true;
    }
    return false;
}

void releaseReusableFence(std::shared_ptr<VulkanRhiRuntime> runtime, const VkFence fence)
{
    if (!runtime || !fence)
        return;
    ReusableFence reusable{std::move(runtime), fence};
    std::lock_guard lock(g_reusableFencesMutex);
    if (g_reusableFences.size() >= kMaxReusableFences) {
        destroyReusableFence(std::move(reusable));
        return;
    }
    g_reusableFences.push_back(std::move(reusable));
}

void clearReusableFences()
{
    std::lock_guard lock(g_reusableFencesMutex);
    for (auto& fence : g_reusableFences) {
        destroyReusableFence(std::move(fence));
    }
    g_reusableFences.clear();
}

void destroyReusableCommandResources(ReusableCommandResources resources)
{
    if (!resources.runtime)
        return;
    auto* freeCommandBuffers = functionFrom<PFN_vkFreeCommandBuffers>(
        resources.runtime->getDeviceProcAddr("vkFreeCommandBuffers"));
    auto* destroyCommandPool = functionFrom<PFN_vkDestroyCommandPool>(
        resources.runtime->getDeviceProcAddr("vkDestroyCommandPool"));
    if (freeCommandBuffers && resources.commandPool && resources.commandBuffer) {
        freeCommandBuffers(resources.runtime->logicalDevice(),
            resources.commandPool,
            1u,
            &resources.commandBuffer);
    }
    if (destroyCommandPool && resources.commandPool) {
        destroyCommandPool(resources.runtime->logicalDevice(), resources.commandPool, nullptr);
    }
}

bool acquireReusableCommandResources(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    const std::uint32_t queueFamilyIndex,
    VkCommandPool& commandPool,
    VkCommandBuffer& commandBuffer)
{
    if (!runtime)
        return false;
    auto* resetCommandPool =
        functionFrom<PFN_vkResetCommandPool>(runtime->getDeviceProcAddr("vkResetCommandPool"));
    if (!resetCommandPool)
        return false;

    std::lock_guard lock(g_reusableCommandResourcesMutex);
    for (auto it = g_reusableCommandResources.begin(); it != g_reusableCommandResources.end();
        ++it) {
        if (it->runtime.get() != runtime.get() || it->queueFamilyIndex != queueFamilyIndex) {
            continue;
        }
        const auto reset = resetCommandPool(runtime->logicalDevice(), it->commandPool, 0u);
        if (reset != VK_SUCCESS) {
            auto stale = std::move(*it);
            g_reusableCommandResources.erase(it);
            destroyReusableCommandResources(std::move(stale));
            return false;
        }
        commandPool = it->commandPool;
        commandBuffer = it->commandBuffer;
        g_reusableCommandResources.erase(it);
        return true;
    }
    return false;
}

void releaseReusableCommandResources(std::shared_ptr<VulkanRhiRuntime> runtime,
    const std::uint32_t queueFamilyIndex,
    const VkCommandPool commandPool,
    const VkCommandBuffer commandBuffer)
{
    if (!runtime || !commandPool || !commandBuffer)
        return;
    ReusableCommandResources resources{
        std::move(runtime),
        commandPool,
        commandBuffer,
        queueFamilyIndex,
    };
    std::lock_guard lock(g_reusableCommandResourcesMutex);
    if (g_reusableCommandResources.size() >= kMaxReusableCommandResources) {
        destroyReusableCommandResources(std::move(resources));
        return;
    }
    g_reusableCommandResources.push_back(std::move(resources));
}

void clearReusableCommandResources()
{
    std::lock_guard lock(g_reusableCommandResourcesMutex);
    for (auto& resources : g_reusableCommandResources) {
        destroyReusableCommandResources(std::move(resources));
    }
    g_reusableCommandResources.clear();
}

std::array<std::shared_ptr<VulkanRhiNativeCommandPayload>, kVulkanMaxAsyncSubmittedCommands>&
asyncSubmittedCommands()
{
    // Fixed physical slots match the existing backend-owned retirement ceiling.
    // First submission needs no allocator warm-up or global per-Source charge.
    static std::array<std::shared_ptr<VulkanRhiNativeCommandPayload>,
        kVulkanMaxAsyncSubmittedCommands>
        commands;
    return commands;
}

std::uint64_t& completedSubmittedFrameSerial()
{
    static std::uint64_t serial = 0;
    return serial;
}

struct CompletedGpuTiming {
    std::atomic<std::uint64_t> frameSerial{0u};
    std::atomic<std::uint64_t> elapsedNanoseconds{0u};
    std::atomic<std::uint32_t> scopeCount{0u};
    std::array<std::atomic<std::uint64_t>, rhi::GpuTimingSample::kMaxScopes> scopeIds{};
    std::array<std::atomic<std::uint64_t>, rhi::GpuTimingSample::kMaxScopes> scopeNanoseconds{};
};

CompletedGpuTiming& completedGpuTiming()
{
    static CompletedGpuTiming timing;
    return timing;
}

void publishCompletedTimingPool(const VulkanRhiRuntime& runtime,
    const VulkanRhiQueryPoolPayload& pool,
    const std::uint64_t frameSerial)
{
    const auto& scopeIds = pool.scopeIds();
    if (scopeIds.empty() || scopeIds.size() >= rhi::GpuTimingSample::kMaxScopes)
        return;
    const auto getResults =
        functionFrom<PFN_vkGetQueryPoolResults>(runtime.getDeviceProcAddr("vkGetQueryPoolResults"));
    if (!getResults)
        return;
    core::pmr::Vector<std::uint64_t> timestamps(
        scopeIds.size() * 2u, &arp::core::defaultCpuMemoryResource());
    const auto queryResult = getResults(runtime.logicalDevice(),
        pool.nativeHandle(),
        0u,
        static_cast<std::uint32_t>(timestamps.size()),
        timestamps.size() * sizeof(std::uint64_t),
        timestamps.data(),
        sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (queryResult != VK_SUCCESS) {
        if (std::getenv("ARP_FRAME_GRAPH_GPU_TIMING_DIAG") != nullptr) {
            std::fprintf(stderr,
                "[frame-graph-gpu-timing] readback-failed result=%d queries=%u\n",
                static_cast<int>(queryResult),
                static_cast<unsigned>(timestamps.size()));
        }
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(runtime.physicalDevice(), &properties);
    auto& result = completedGpuTiming();
    // Preserve the command payload's full-frame scope alongside the named
    // FrameGraph samples. Adding pass profiling must not turn frame timing
    // into a partial sum.
    std::uint32_t outputIndex = 0u;
    result.scopeIds[outputIndex].store(0u, std::memory_order_relaxed);
    result.scopeNanoseconds[outputIndex].store(
        result.elapsedNanoseconds.load(std::memory_order_relaxed), std::memory_order_relaxed);
    ++outputIndex;
    for (std::size_t i = 0; i < scopeIds.size(); ++i, ++outputIndex) {
        const auto elapsed = timestamps[i * 2u + 1u] >= timestamps[i * 2u]
            ? static_cast<std::uint64_t>(
                  static_cast<double>(timestamps[i * 2u + 1u] - timestamps[i * 2u])
                  * static_cast<double>(properties.limits.timestampPeriod))
            : 0u;
        result.scopeIds[outputIndex].store(scopeIds[i], std::memory_order_relaxed);
        result.scopeNanoseconds[outputIndex].store(elapsed, std::memory_order_relaxed);
    }
    result.scopeCount.store(outputIndex, std::memory_order_relaxed);
    result.frameSerial.store(frameSerial, std::memory_order_relaxed);
    if (std::getenv("ARP_FRAME_GRAPH_GPU_TIMING_DIAG") != nullptr) {
        std::fprintf(stderr,
            "[frame-graph-gpu-timing] published scopes=%u frame=%llu\n",
            outputIndex,
            static_cast<unsigned long long>(frameSerial));
    }
}

void retireCompletedSubmittedCommands()
{
    auto& commands = asyncSubmittedCommands();
    for (auto& command : commands) {
        if (!command || !command->submittedFenceCompleted())
            continue;
        completedSubmittedFrameSerial() =
            std::max(completedSubmittedFrameSerial(), command->observedFrameSerial());
        command.reset();
    }
}

bool retainSubmittedCommandForAsyncRetirement(
    std::shared_ptr<VulkanRhiNativeCommandPayload> command, std::pmr::string& diagnostics)
{
    retireCompletedSubmittedCommands();
    auto& commands = asyncSubmittedCommands();
    const auto slot = std::find(commands.begin(), commands.end(), nullptr);
    if (slot == commands.end()) {
        diagnostics = "RHI-VULKAN-SUBMISSION-RETIREMENT-CAPACITY";
        return false;
    }
    *slot = std::move(command);
    return true;
}

bool asyncSubmittedCommandRetirementHasCapacity(std::pmr::string& diagnostics)
{
    retireCompletedSubmittedCommands();
    auto& commands = asyncSubmittedCommands();
    if (std::find(commands.begin(), commands.end(), nullptr) == commands.end()) {
        diagnostics = "RHI-VULKAN-SUBMISSION-RETIREMENT-CAPACITY";
        return false;
    }
    // Physical slots already exist; acceptance and retention never allocate.
    return true;
}

} // namespace

VulkanRhiNativeCommandPayload::VulkanRhiNativeCommandPayload(core::UniquePtr<Impl> impl)
    : impl_(std::move(impl))
{}

VulkanRhiNativeCommandPayload::~VulkanRhiNativeCommandPayload()
{
    if (!impl_) {
        return;
    }
    auto* waitForFences =
        functionFrom<PFN_vkWaitForFences>(impl_->device.getDeviceProcAddr("vkWaitForFences"));
    auto* destroyFence =
        functionFrom<PFN_vkDestroyFence>(impl_->device.getDeviceProcAddr("vkDestroyFence"));
    bool commandResourcesSafeToRecycle = !impl_->submitted;
    if (impl_->submitFence) {
        bool fenceWaited = impl_->submitFenceCompleted;
        if (!impl_->submitFenceCompleted && impl_->submitted && waitForFences) {
            const auto result = waitForFences(impl_->device.logicalDevice(),
                1,
                &impl_->submitFence,
                VK_TRUE,
                VulkanRhiFrameWaitPolicy::frameFenceTimeoutNanoseconds);
            impl_->device.captureOriginalDeviceLoss(result);
            fenceWaited = result == VK_SUCCESS;
        }
        commandResourcesSafeToRecycle = fenceWaited || !impl_->submitted;
        if (fenceWaited && impl_->submitted)
            resolveSubmittedCompletion();
        if (fenceWaited) {
            for (const auto& query : impl_->recordedTimestampQueries) {
                if (query.pool) {
                    query.pool->markCompleted(impl_->device, query.queryIndex);
                }
            }
        } else if (!impl_->submitted) {
            for (const auto& query : impl_->recordedTimestampQueries) {
                if (query.pool) {
                    query.pool->rollbackRecorded(query.queryIndex);
                }
            }
        }
        if (fenceWaited) {
            const auto reusableFence = impl_->submitFence;
            impl_->submitFence = VK_NULL_HANDLE;
            releaseReusableFence(impl_->deviceOwner, reusableFence);
        }
        if (destroyFence && impl_->submitFence) {
            destroyFence(impl_->device.logicalDevice(), impl_->submitFence, nullptr);
        }
    } else if (!impl_->submitted) {
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (query.pool) {
                query.pool->rollbackRecorded(query.queryIndex);
            }
        }
    }
    if (!impl_->submitted)
        for (auto& fault : impl_->pendingBindlessFaults)
            fault.result->abandon();
    if (impl_->timestampQueryPool) {
        if (auto* destroyQueryPool = functionFrom<PFN_vkDestroyQueryPool>(
                impl_->device.getDeviceProcAddr("vkDestroyQueryPool"))) {
            destroyQueryPool(impl_->device.logicalDevice(), impl_->timestampQueryPool, nullptr);
        }
    }
    if (!impl_->originalRenderPassBorrowed)
        destroyVulkanRhiNativeRenderPassResources(impl_->device, impl_->activeRenderPass);
    for (std::uint32_t index = 0; index < impl_->retainedRenderPassCount; ++index) {
        destroyVulkanRhiNativeRenderPassResources(
            impl_->device, impl_->retainedRenderPasses[index]);
    }
    impl_->scratchIndexAllocationPayload.reset();
    impl_->scratchIndexBuffer = VK_NULL_HANDLE;
    impl_->scratchIndexCapacity = 0;
    if (commandResourcesSafeToRecycle) {
        if (impl_->bindlessFaultPreamble) {
            if (auto* freeCommandBuffers = functionFrom<PFN_vkFreeCommandBuffers>(
                    impl_->device.getDeviceProcAddr("vkFreeCommandBuffers"))) {
                freeCommandBuffers(impl_->device.logicalDevice(),
                    impl_->commandPool,
                    1u,
                    &impl_->bindlessFaultPreamble);
            }
            impl_->bindlessFaultPreamble = VK_NULL_HANDLE;
        }
        releaseReusableCommandResources(impl_->deviceOwner,
            impl_->device.graphicsQueueFamilyIndex(),
            impl_->commandPool,
            impl_->commandBuffer);
    } else {
        destroyReusableCommandResources({
            impl_->deviceOwner,
            impl_->commandPool,
            impl_->commandBuffer,
            impl_->device.graphicsQueueFamilyIndex(),
        });
    }
}

bool VulkanRhiNativeCommandPayload::submittedFenceCompleted()
{
    if (!impl_ || !impl_->submitted)
        return false;
    if (impl_->submitFenceCompleted)
        return true;
    if (impl_->device.syntheticRuntime()) {
        // Explicit validator runtime only. This never authorizes Native work.
        resolveSubmittedCompletion();
        return true;
    }
    if (!impl_->submitFence)
        return false;
    const auto getFenceStatus =
        functionFrom<PFN_vkGetFenceStatus>(impl_->device.getDeviceProcAddr("vkGetFenceStatus"));
    if (!getFenceStatus)
        return false;
    const auto status = getFenceStatus(impl_->device.logicalDevice(), impl_->submitFence);
    impl_->device.captureOriginalDeviceLoss(status);
    if (status == VK_SUCCESS) {
        resolveSubmittedCompletion();
        return true;
    }
    if (status != VK_NOT_READY && impl_->originalBindlessCustody) {
        impl_->originalSubmissionUnproven = true;
        impl_->originalBindlessCustody->submissionUnproven(*this);
    }
    return false;
}

bool VulkanRhiNativeCommandPayload::waitSubmittedFence(std::pmr::string& diagnostics)
{
    return waitSubmittedFenceUntil(std::chrono::steady_clock::now()
            + std::chrono::nanoseconds(VulkanRhiFrameWaitPolicy::frameFenceTimeoutNanoseconds),
        diagnostics);
}

bool VulkanRhiNativeCommandPayload::waitSubmittedFenceUntil(
    std::chrono::steady_clock::time_point deadline, std::pmr::string& diagnostics)
{
    const auto now = std::chrono::steady_clock::now();
    if (deadline == std::chrono::steady_clock::time_point{}) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-INVALID";
        return false;
    }
    if (deadline <= now) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
        return false;
    }
    if (!impl_ || !impl_->submitted) {
        diagnostics = "RHI-VULKAN-COMPLETION-NOT-SUBMITTED";
        return false;
    }
    if (submittedFenceCompleted()) {
        diagnostics.clear();
        return true;
    }
    if (!impl_->submitFence) {
        diagnostics = "RHI-VULKAN-COMPLETION-FENCE-MISSING";
        return false;
    }
    const auto waitForFences =
        functionFrom<PFN_vkWaitForFences>(impl_->device.getDeviceProcAddr("vkWaitForFences"));
    if (!waitForFences) {
        diagnostics = "RHI-VULKAN-COMPLETION-WAIT-UNAVAILABLE";
        return false;
    }
    const auto beforeWait = std::chrono::steady_clock::now();
    if (deadline <= beforeWait) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
        return false;
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - beforeWait).count();
    const auto waited = waitForFences(impl_->device.logicalDevice(),
        1,
        &impl_->submitFence,
        VK_TRUE,
        static_cast<std::uint64_t>(remaining));
    impl_->device.captureOriginalDeviceLoss(waited);
    if (waited != VK_SUCCESS) {
        if (waited != VK_TIMEOUT && impl_->originalBindlessCustody) {
            impl_->originalSubmissionUnproven = true;
            impl_->originalBindlessCustody->submissionUnproven(*this);
        }
        diagnostics = waited == VK_TIMEOUT && impl_->originalSubmissionUnproven
            ? "RHI-VULKAN-ROOT-FENCE-QUEUE-QUARANTINED"
            : waited == VK_TIMEOUT           ? "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED"
            : waited == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-COMPLETION-DEVICE-LOST"
                                             : "RHI-VULKAN-COMPLETION-WAIT-FAILED";
        return false;
    }
    resolveSubmittedCompletion();
    diagnostics.clear();
    return true;
}

void VulkanRhiNativeCommandPayload::resolveSubmittedCompletion()
{
    impl_->submitFenceCompleted = true;
    // ScopedPresentRetirement/1: only this frame's actual native completion
    // persists after command retirement. Validator and unproven work cannot
    // authorize native frame-slot recycling.
    if (!impl_->device.syntheticRuntime() && !impl_->originalSubmissionUnproven && impl_->submitted
        && impl_->swapchainFrameSyncState) {
        impl_->swapchainFrameSyncState->graphicsCompleted_ = true;
    }
    if (impl_->originalBindlessCustody) {
        impl_->originalBindlessCustody->fenceCompleted(*this);
        impl_->originalBindlessCustody.reset();
    }
    resolveBindlessFaultReadbacks();
    // An uncertain queue invocation may never have executed the query reset
    // or either timestamp write. Real device-idle proves safe retirement,
    // not query initialization or publishable timing results.
    if (!impl_->originalSubmissionUnproven && !impl_->timestampResolved && impl_->timestampQueryPool
        && impl_->timestampPeriodNs > 0.0f) {
        if (auto* getResults = functionFrom<PFN_vkGetQueryPoolResults>(
                impl_->device.getDeviceProcAddr("vkGetQueryPoolResults"))) {
            std::uint64_t timestamps[2] = {};
            const auto result = getResults(impl_->device.logicalDevice(),
                impl_->timestampQueryPool,
                0u,
                2u,
                sizeof(timestamps),
                timestamps,
                sizeof(std::uint64_t),
                VK_QUERY_RESULT_64_BIT);
            if (result == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
                completedGpuTiming().elapsedNanoseconds.store(
                    static_cast<std::uint64_t>(static_cast<double>(timestamps[1] - timestamps[0])
                        * static_cast<double>(impl_->timestampPeriodNs)),
                    std::memory_order_relaxed);
                completedGpuTiming().frameSerial.store(
                    impl_->frameSerial, std::memory_order_relaxed);
            }
        }
        impl_->timestampResolved = true;
    }
    for (const auto& pool : impl_->retainedTimingPools) {
        if (pool)
            publishCompletedTimingPool(impl_->device, *pool, impl_->frameSerial);
    }
    impl_->retainedTimingPools.clear();
    for (const auto& query : impl_->recordedTimestampQueries) {
        if (query.pool) {
            query.pool->markCompleted(impl_->device, query.queryIndex);
        }
    }
    if (impl_->stagingUploads)
        (void)impl_->stagingUploads->retireCompleted();
    impl_->retainedResources.clear();
    impl_->preparedBindings.clear();
    impl_->indexedPipelines.clear();
    impl_->indexedSnapshots.clear();
    impl_->indexedFaultLatches.clear();
    impl_->submittedTimeline = {};
}

void VulkanRhiNativeCommandPayload::resolveBindlessFaultReadbacks()
{
    for (auto& pending : impl_->pendingBindlessFaults) {
        pending.lease.markFaultReadbackCompleted();
        core::pmr::String diagnostic;
        if (!pending.destination->backendMappedData()
            || !pending.destination->backendInvalidateMappedRead(
                0, sizeof(std::uint32_t), diagnostic)) {
            pending.result->fail();
            continue;
        }
        std::uint32_t bits = 0;
        std::memcpy(&bits, pending.destination->backendMappedData(), sizeof(bits));
        pending.result->complete(bits);
    }
    impl_->pendingBindlessFaults.clear();
}

bool VulkanRhiNativeCommandPayload::retainIndexedPipeline(
    const std::shared_ptr<const void>& object, core::pmr::String& diagnostic)
{
    if (!impl_ || !impl_->recording || impl_->ended || impl_->submitted) {
        diagnostic = "RHI-VULKAN-BINDLESS-RETENTION-NOT-RECORDING";
        return false;
    }
    if (!object) {
        diagnostic = "RHI-VULKAN-BINDLESS-RETENTION-PAYLOAD-MISSING";
        return false;
    }
    if (!impl_->indexedPipelines.retain(object)) {
        diagnostic = "RHI-BINDLESS-COMMAND-PIPELINE-BUDGET";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool VulkanRhiNativeCommandPayload::canRetainPreparedBinding(
    const std::shared_ptr<const rhi::BindlessPreparedBinding>& binding) const noexcept
{
    return impl_ && impl_->recording && !impl_->ended && !impl_->submitted && binding
        && binding->originalFactoryOwnerMatches(binding)
        && impl_->preparedBindings.canRetain(binding);
}

bool VulkanRhiNativeCommandPayload::retainPreparedBinding(
    const std::shared_ptr<const rhi::BindlessPreparedBinding>& binding,
    core::pmr::String& diagnostic)
{
    if (!canRetainPreparedBinding(binding) || !impl_->preparedBindings.retain(binding)) {
        diagnostic = "RHI-VULKAN-BINDLESS-PREPARED-RETENTION-REJECTED";
        return false;
    }
    diagnostic.clear();
    return true;
}

bool VulkanRhiNativeCommandPayload::retainIndexedSnapshot(
    const std::shared_ptr<const void>& object, core::pmr::String& diagnostic)
{
    if (!impl_ || !impl_->recording || impl_->ended || impl_->submitted) {
        diagnostic = "RHI-VULKAN-BINDLESS-RETENTION-NOT-RECORDING";
        return false;
    }
    if (!object) {
        diagnostic = "RHI-VULKAN-BINDLESS-RETENTION-PAYLOAD-MISSING";
        return false;
    }
    if (!impl_->indexedSnapshots.retain(object)) {
        diagnostic = "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET";
        return false;
    }
    diagnostic.clear();
    return true;
}

void VulkanRhiNativeCommandPayload::retainResource(
    std::shared_ptr<VulkanRhiNativeResourcePayload> payload)
{
    if (impl_ && payload) {
        impl_->retainedResources.push_back(std::move(payload));
    }
}

void VulkanRhiNativeCommandPayload::observeFrameSerial(std::uint64_t frameSerial)
{
    if (impl_) {
        impl_->frameSerial = std::max(impl_->frameSerial, frameSerial);
    }
}

std::uint64_t VulkanRhiNativeCommandPayload::observedFrameSerial() const
{
    return impl_ ? impl_->frameSerial : 0;
}

void retireCompletedVulkanRhiSubmittedCommands()
{
    retireCompletedSubmittedCommands();
}

const char* waitIdleAndRetireVulkanRhiSubmittedCommands(
    const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    if (!runtime || runtime->syntheticRuntime() || !runtime->logicalDevice())
        return "RHI-VULKAN-IDLE-COMPLETION-NATIVE-RUNTIME";
    const auto lowering =
        rhi::semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
            rhi::Backend::Vulkan, 6u);
    if (!lowering.completionAvailable || lowering.diagnostic)
        return lowering.diagnostic ? lowering.diagnostic : "RHI-VULKAN-IDLE-COMPLETION-LOWERING";
    const auto wait =
        functionFrom<PFN_vkDeviceWaitIdle>(runtime->getDeviceProcAddr("vkDeviceWaitIdle"));
    if (!wait)
        return "RHI-VULKAN-IDLE-COMPLETION-ENTRYPOINT";
    const auto result = wait(runtime->logicalDevice());
    runtime->captureOriginalDeviceLoss(result);
    if (result != VK_SUCCESS)
        return result == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-IDLE-COMPLETION-DEVICE-LOST"
                                              : "RHI-VULKAN-IDLE-COMPLETION-FAILED";
    // This observation is owned by the native call above. Do not probe or
    // retire another runtime's fences, and never reuse a global idle flag.
    for (auto& command : asyncSubmittedCommands()) {
        if (!command || !command->impl_->submitted
            || !core::SharedOwnerProjection::sameOwner(command->impl_->deviceOwner, runtime))
            continue;
        if (command->impl_->originalBindlessCustody
            && !rhi::semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
                rhi::Backend::Vulkan, command->impl_->originalBindlessCustody->lifecycleVersion())
                .completionAvailable)
            continue;
        command->resolveSubmittedCompletion();
        completedSubmittedFrameSerial() =
            std::max(completedSubmittedFrameSerial(), command->observedFrameSerial());
        command.reset();
    }
    return nullptr;
}

void VulkanRhiNativeCommandPayload::retainTimingQueryPool(
    std::shared_ptr<VulkanRhiQueryPoolPayload> payload)
{
    if (!impl_ || !payload)
        return;
    const auto existing = std::find_if(impl_->retainedTimingPools.begin(),
        impl_->retainedTimingPools.end(),
        [&](const std::shared_ptr<VulkanRhiQueryPoolPayload>& candidate) {
            return candidate.get() == payload.get();
        });
    if (existing == impl_->retainedTimingPools.end()) {
        impl_->retainedTimingPools.push_back(std::move(payload));
    }
}

void waitForVulkanRhiSubmittedCommandsForResourcePressure()
{
    for (const auto& command : asyncSubmittedCommands()) {
        if (!command) {
            continue;
        }
        std::pmr::string diagnostics;
        command->waitSubmittedFence(diagnostics);
    }
    retireCompletedSubmittedCommands();
}

bool drainVulkanRhiSubmittedCommandsUntil(
    std::chrono::steady_clock::time_point deadline, std::pmr::string& diagnostics)
{
    if (deadline == std::chrono::steady_clock::time_point{}) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-INVALID";
        return false;
    }
    if (deadline <= std::chrono::steady_clock::now()) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
        return false;
    }
    retireCompletedSubmittedCommands();
    for (const auto& command : asyncSubmittedCommands()) {
        if (command && !command->waitSubmittedFenceUntil(deadline, diagnostics)) {
            retireCompletedSubmittedCommands();
            return false;
        }
    }
    retireCompletedSubmittedCommands();
    if (std::any_of(asyncSubmittedCommands().begin(),
            asyncSubmittedCommands().end(),
            [](const auto& command) {
                return bool(command);
            })) {
        diagnostics = "RHI-VULKAN-SHUTDOWN-DRAIN-INCOMPLETE";
        return false;
    }
    diagnostics.clear();
    return true;
}

void drainVulkanRhiSubmittedCommandsForShutdown()
{
    retireCompletedSubmittedCommands();
    // Device-wrapper destruction is not completion for any submitted stream.
    // Retain every remaining native closure and charge until its real issuer
    // proves retirement; the independent deadline API reports unresolved work.
    for (const auto& command : asyncSubmittedCommands())
        if (command)
            return;
    clearReusableFences();
    clearReusableCommandResources();
}

std::uint64_t completedVulkanRhiSubmittedFrameSerial()
{
    retireCompletedSubmittedCommands();
    return completedSubmittedFrameSerial();
}

bool VulkanRhiNativeCommandPayload::conformanceComputePass(
    bool begin, core::pmr::String& diagnostic)
{
    return !impl_ || !impl_->conformanceClear
        || impl_->conformanceClear->computePass(begin, diagnostic);
}

bool VulkanRhiNativeCommandPayload::rejectConformanceClearWork(core::pmr::String& diagnostic)
{
    return !impl_ || !impl_->conformanceClear || impl_->conformanceClear->reject(diagnostic);
}

bool VulkanRhiNativeCommandPayload::record(std::pmr::string& diagnostics)
{
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    // `record()` is the semantic-command accounting hook. Resource ordering
    // is emitted by the explicit transition/alias-barrier paths; recording a
    // TOP_OF_PIPE -> BOTTOM_OF_PIPE empty pipeline barrier for every command
    // only serialized the queue and inflated the DDGI CPU record/submit path.
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::retainRecordedStagingUploads(
    core::UniquePtr<rhi::RecordedStagingUploadJournal>& journal, std::pmr::string& diagnostics)
{
    if (!impl_ || impl_->submitted || impl_->stagingUploads || !journal) {
        diagnostics = "RHI-VULKAN-STAGING-RECORDING-OWNERSHIP";
        return false;
    }
    impl_->stagingUploads = std::move(journal);
    return true;
}

bool VulkanRhiNativeCommandPayload::flushPendingBarriers(std::pmr::string& diagnostics)
{
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (impl_->pendingImageBarriers.empty() && impl_->pendingBufferBarriers.empty()
        && impl_->pendingMemoryBarriers.empty()) {
        return true;
    }
    if (impl_->device.syntheticRuntime()) {
        impl_->pendingImageBarriers.clear();
        impl_->pendingBufferBarriers.clear();
        impl_->pendingMemoryBarriers.clear();
        impl_->pendingSourceStages = 0;
        impl_->pendingDestinationStages = 0;
        return true;
    }
    auto* cmdPipelineBarrier = functionFrom<PFN_vkCmdPipelineBarrier>(
        impl_->device.getDeviceProcAddr("vkCmdPipelineBarrier"));
    if (!cmdPipelineBarrier) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdPipelineBarrier";
        return false;
    }
    cmdPipelineBarrier(impl_->commandBuffer,
        impl_->pendingSourceStages,
        impl_->pendingDestinationStages,
        0,
        static_cast<std::uint32_t>(impl_->pendingMemoryBarriers.size()),
        impl_->pendingMemoryBarriers.empty() ? nullptr : impl_->pendingMemoryBarriers.data(),
        static_cast<std::uint32_t>(impl_->pendingBufferBarriers.size()),
        impl_->pendingBufferBarriers.empty() ? nullptr : impl_->pendingBufferBarriers.data(),
        static_cast<std::uint32_t>(impl_->pendingImageBarriers.size()),
        impl_->pendingImageBarriers.empty() ? nullptr : impl_->pendingImageBarriers.data());
    impl_->pendingImageBarriers.clear();
    impl_->pendingBufferBarriers.clear();
    impl_->pendingMemoryBarriers.clear();
    impl_->pendingSourceStages = 0;
    impl_->pendingDestinationStages = 0;
    return true;
}

bool VulkanRhiNativeCommandPayload::recordBufferBarrier(
    const VulkanRhiNativeResourcePayload& buffer,
    const rhi::ResourceBarrierDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->bufferBarrier(buffer, desc, diagnostics))
        return false;
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeBufferBarrierRecordCount;
        }
        return true;
    }
    const auto handle = buffer.backendVulkanBufferHandle();
    if (!handle) {
        diagnostics = "Vulkan RHI native buffer barrier requires a native buffer payload";
        return false;
    }
    const auto sourceStages = desc.sourceStages == rhi::PipelineStage::None
        ? stageFor(desc.before)
        : stageMaskFor(desc.sourceStages);
    const auto destinationStages = desc.destinationStages == rhi::PipelineStage::None
        ? stageFor(desc.after)
        : stageMaskFor(desc.destinationStages);
    const auto sourceAccess = desc.sourceAccess == rhi::ResourceAccess::None
        ? accessFor(desc.before)
        : accessMaskFor(desc.sourceAccess);
    const auto destinationAccess = desc.destinationAccess == rhi::ResourceAccess::None
        ? accessFor(desc.after)
        : accessMaskFor(desc.destinationAccess);
    if (sourceStages == 0 || destinationStages == 0) {
        diagnostics = "Vulkan RHI buffer barrier requires non-empty stage masks";
        return false;
    }
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = handle;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    impl_->pendingSourceStages |= sourceStages;
    impl_->pendingDestinationStages |= destinationStages;
    impl_->pendingBufferBarriers.push_back(barrier);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeBufferBarrierRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordAliasingBarrier(
    const rhi::AliasingBarrierDesc& desc, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (desc.aliasGroup == 0 || desc.ownershipHandoff
        || desc.sourceQueueRole != desc.destinationQueueRole) {
        diagnostics = "RHI-ALIAS-BARRIER-DESC-INVALID";
        return false;
    }
    const auto sourceStages = stageMaskFor(desc.sourceStages);
    const auto destinationStages = stageMaskFor(desc.destinationStages);
    if (sourceStages == 0 || destinationStages == 0) {
        diagnostics = "RHI-ALIAS-BARRIER-STAGE-MASK-INVALID";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats)
            ++impl_->stats->nativeCommandBufferRecordCount;
        return true;
    }
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = accessMaskFor(desc.sourceAccess);
    barrier.dstAccessMask = accessMaskFor(desc.destinationAccess);
    impl_->pendingSourceStages |= sourceStages;
    impl_->pendingDestinationStages |= destinationStages;
    impl_->pendingMemoryBarriers.push_back(barrier);
    if (impl_->stats)
        ++impl_->stats->nativeCommandBufferRecordCount;
    return true;
}

bool VulkanRhiNativeCommandPayload::recordResourceMemoryBarrier(
    const rhi::ResourceMemoryBarrierDesc& desc, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    const auto sourceStages = stageMaskFor(desc.sourceStages);
    const auto destinationStages = stageMaskFor(desc.destinationStages);
    if (sourceStages == 0 || destinationStages == 0) {
        diagnostics = "RHI-RESOURCE-MEMORY-BARRIER-STAGE-MASK-INVALID";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats)
            ++impl_->stats->nativeCommandBufferRecordCount;
        return true;
    }
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = accessMaskFor(desc.sourceAccess);
    barrier.dstAccessMask = accessMaskFor(desc.destinationAccess);
    impl_->pendingSourceStages |= sourceStages;
    impl_->pendingDestinationStages |= destinationStages;
    impl_->pendingMemoryBarriers.push_back(barrier);
    if (impl_->stats)
        ++impl_->stats->nativeCommandBufferRecordCount;
    return true;
}

bool VulkanRhiNativeCommandPayload::bindSwapchainFrameSync(
    const VulkanRhiNativeResourcePayload& image,
    rhi::ResourceState firstUseState,
    std::pmr::string& diagnostics)
{
    if (!impl_ || !image.backendVulkanSwapchainFrameSyncAvailable()) {
        return true;
    }

    const auto waitSemaphore = image.backendVulkanAcquireSemaphore();
    const auto signalSemaphore = image.backendVulkanRenderFinishedSemaphore();
    const auto frameSerial = image.backendVulkanSwapchainFrameSerial();
    const auto syncState = image.backendVulkanSwapchainFrameSyncState();
    if (!syncState || frameSerial == 0
        || (!impl_->device.syntheticRuntime() && (!waitSemaphore || !signalSemaphore))) {
        diagnostics = "RHI-VULKAN-SWAPCHAIN-SYNC-CONTRACT-INCOMPLETE";
        return false;
    }
    if (!syncState->belongsTo(impl_->deviceOwner)) {
        diagnostics = "RHI-VULKAN-SWAPCHAIN-SYNC-OWNER-MISMATCH";
        return false;
    }
    if (syncState->renderSubmitted()) {
        diagnostics = "RHI-VULKAN-SWAPCHAIN-SYNC-ALREADY-SUBMITTED";
        return false;
    }
    if (impl_->hasSwapchainFrameSync) {
        if (impl_->waitSemaphore != waitSemaphore || impl_->signalSemaphore != signalSemaphore
            || impl_->swapchainFrameSyncState.get() != syncState.get()) {
            diagnostics = "RHI-VULKAN-SWAPCHAIN-SYNC-MULTIPLE-FRAMES";
            return false;
        }
        return true;
    }

    impl_->hasSwapchainFrameSync = true;
    impl_->waitSemaphore = waitSemaphore;
    impl_->signalSemaphore = signalSemaphore;
    impl_->waitStageMask = stageFor(firstUseState);
    impl_->swapchainFrameSyncState = std::move(syncState);
    impl_->frameSerial = std::max(impl_->frameSerial, frameSerial);
    return true;
}

bool VulkanRhiNativeCommandPayload::recordImageBarrier(const VulkanRhiNativeResourcePayload& image,
    const rhi::TextureSubresourceRange& range,
    rhi::ResourceState before,
    rhi::ResourceState after,
    std::pmr::string& diagnostics)
{
    if (image.backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    return recordImageBarrier(image,
        rhi::ResourceBarrierDesc{
            {},
            range,
            before,
            after,
        },
        diagnostics);
}

bool VulkanRhiNativeCommandPayload::recordImageBarrier(const VulkanRhiNativeResourcePayload& image,
    const rhi::ResourceBarrierDesc& desc,
    std::pmr::string& diagnostics)

{
    if (image.backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->transition(*this, image, desc, diagnostics);
    return recordValidatedOriginalImageBarrier(image, desc, diagnostics);
}

bool VulkanRhiNativeCommandPayload::recordValidatedOriginalImageBarrier(
    const VulkanRhiNativeResourcePayload& image,
    const rhi::ResourceBarrierDesc& desc,
    core::pmr::String& diagnostics)
{
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeImageBarrierRecordCount;
        }
        return true;
    }
    const auto handle = image.backendVulkanImageHandle();
    if (!handle) {
        diagnostics = "Vulkan RHI native image barrier requires a native image payload";
        return false;
    }
    const auto sourceStages = desc.sourceStages == rhi::PipelineStage::None
        ? stageFor(desc.before)
        : stageMaskFor(desc.sourceStages);
    const auto destinationStages = desc.destinationStages == rhi::PipelineStage::None
        ? stageFor(desc.after)
        : stageMaskFor(desc.destinationStages);
    if (!sourceStages || !destinationStages) {
        diagnostics = "RHI-VULKAN-IMAGE-BARRIER-STAGE-MASK-INVALID";
        return false;
    }
    const auto& range = desc.textureSubresources;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = desc.sourceStages == rhi::PipelineStage::None
        ? accessFor(desc.before)
        : accessMaskFor(desc.sourceAccess);
    barrier.dstAccessMask = desc.destinationStages == rhi::PipelineStage::None
        ? accessFor(desc.after)
        : accessMaskFor(desc.destinationAccess);
    barrier.oldLayout = layoutFor(desc.before);
    barrier.newLayout = layoutFor(desc.after);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = handle;
    barrier.subresourceRange.aspectMask = aspectFor(range.aspect);
    barrier.subresourceRange.baseMipLevel = range.baseMipLevel;
    barrier.subresourceRange.levelCount = range.mipLevelCount;
    barrier.subresourceRange.baseArrayLayer = range.baseArrayLayer;
    barrier.subresourceRange.layerCount = range.arrayLayerCount;
    impl_->pendingSourceStages |= sourceStages;
    impl_->pendingDestinationStages |= destinationStages;
    impl_->pendingImageBarriers.push_back(barrier);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeImageBarrierRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordBufferCopy(const VulkanRhiNativeResourcePayload& source,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::BufferCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (impl_ && impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeBufferCopyRecordCount;
        }
        return true;
    }
    const auto sourceHandle = source.backendVulkanBufferHandle();
    const auto destinationHandle = destination.backendVulkanBufferHandle();
    if (!sourceHandle || !destinationHandle) {
        diagnostics = "Vulkan RHI native buffer copy requires native buffer payloads";
        return false;
    }
    auto* cmdCopyBuffer =
        functionFrom<PFN_vkCmdCopyBuffer>(impl_->device.getDeviceProcAddr("vkCmdCopyBuffer"));
    if (!cmdCopyBuffer) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdCopyBuffer";
        return false;
    }
    VkBufferCopy region{};
    region.srcOffset = desc.sourceOffsetBytes;
    region.dstOffset = desc.destinationOffsetBytes;
    region.size = desc.sizeBytes;
    cmdCopyBuffer(impl_->commandBuffer, sourceHandle, destinationHandle, 1, &region);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeBufferCopyRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordBindlessFaultLatchClear(
    const rhi::GpuBuffer& latch, rhi::ResourceState before, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording || impl_->ended || impl_->submitted
        || impl_->device.syntheticRuntime() || impl_->activeRenderPass.renderPass) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-NOT-RECORDING";
        return false;
    }
    if (before != rhi::ResourceState::Undefined && before != rhi::ResourceState::CopySrc) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-PRIOR-STATE";
        return false;
    }
    const auto requiredUsage = rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc
        | rhi::ResourceUsage::CopyDst;
    if (latch.desc().sizeBytes != sizeof(std::uint32_t)
        || (latch.desc().usage & requiredUsage) != requiredUsage) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-BUFFER-INVALID";
        return false;
    }
    auto source = vulkanRhiNativePayload(latch);
    if (!source || !source->backendVulkanBufferHandle()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-PAYLOAD-MISSING";
        return false;
    }
    if (source->backendVulkanDeviceHandle() != impl_->device.logicalDevice()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-DEVICE-MISMATCH";
        return false;
    }
    if (impl_->indexedFaultLatches.contains(source)) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-ALREADY-RECORDED";
        return false;
    }
    if (!recordBindlessFaultLatchClearPreamble(source, before, diagnostics))
        return false;
    diagnostics.clear();
    return true;
}

std::shared_ptr<VulkanRhiBindlessFaultReadback>
VulkanRhiNativeCommandPayload::scheduleBindlessFaultReadback(
    const rhi::GpuBuffer& latch, const rhi::GpuBuffer& destination, std::pmr::string& diagnostics)
{
    if (!impl_ || !impl_->recording || impl_->ended || impl_->submitted
        || impl_->device.syntheticRuntime() || impl_->activeRenderPass.renderPass) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-NOT-RECORDING";
        return {};
    }
    if (impl_->pendingBindlessFaults.size() == impl_->pendingBindlessFaults.capacity()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-CAPACITY";
        return {};
    }
    if (latch.desc().sizeBytes < sizeof(std::uint32_t)
        || (latch.desc().usage & (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc))
            != (rhi::ResourceUsage::StorageBuffer | rhi::ResourceUsage::CopySrc)
        || destination.desc().sizeBytes < sizeof(std::uint32_t)
        || (destination.desc().usage & rhi::ResourceUsage::CopyDst) != rhi::ResourceUsage::CopyDst
        || destination.desc().memory != rhi::MemoryUsage::GpuToCpu) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-BUFFER-INVALID";
        return {};
    }
    auto source = vulkanRhiNativePayload(latch);
    auto target = vulkanRhiNativePayload(destination);
    if (!source || !source->backendVulkanBufferHandle() || !target
        || !target->backendVulkanBufferHandle() || !target->backendMappedData()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-PAYLOAD-MISSING";
        return {};
    }
    if (source->backendVulkanDeviceHandle() != impl_->device.logicalDevice()
        || target->backendVulkanDeviceHandle() != impl_->device.logicalDevice()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-DEVICE-MISMATCH";
        return {};
    }
    for (const auto& pending : impl_->pendingBindlessFaults) {
        if (pending.latch == source) {
            diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-DUPLICATE";
            return {};
        }
    }
    rhi::ResourceBarrierDesc barrier;
    barrier.resource = latch.id();
    barrier.before = rhi::ResourceState::ShaderWrite;
    barrier.after = rhi::ResourceState::CopySrc;
    barrier.sourceStages = rhi::PipelineStage::ComputeShader | rhi::PipelineStage::FragmentShader;
    barrier.destinationStages = rhi::PipelineStage::Transfer;
    barrier.sourceAccess = rhi::ResourceAccess::ShaderWrite;
    barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
    if (!recordBufferBarrier(*source, barrier, diagnostics) || !flushPendingBarriers(diagnostics)
        || !recordBufferCopy(*source,
            *target,
            {latch.id(), destination.id(), 0, 0, sizeof(std::uint32_t)},
            diagnostics))
        return {};
    auto result = core::makeShared<VulkanRhiBindlessFaultReadback>();
    impl_->pendingBindlessFaults.push_back({std::move(source), std::move(target), result, {}, {}});
    diagnostics.clear();
    return result;
}

bool VulkanRhiNativeCommandPayload::recordBindlessFaultLatchClear(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
    rhi::ResourceState before,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_
        || impl_->leasedBindlessFaultLatches.size()
            == impl_->leasedBindlessFaultLatches.capacity()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-CAPACITY";
        return false;
    }
    auto lease = rhi::BindlessFaultSubmissionLease::acquire(snapshot, diagnostics);
    if (!lease.valid())
        return false;
    const auto* latch = lease.faultLatchBuffer();
    auto native = latch ? vulkanRhiNativePayload(*latch) : nullptr;
    if (!native || !native->backendVulkanBufferHandle()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-PAYLOAD-MISSING";
        return false;
    }
    if (!recordBindlessFaultLatchClearPreamble(native, before, diagnostics))
        return false;
    impl_->leasedBindlessFaultLatches.push_back(
        {std::move(snapshot), std::move(lease), std::move(native)});
    diagnostics.clear();
    return true;
}

bool VulkanRhiNativeCommandPayload::recordBindlessFaultLatchClearPreamble(
    const std::shared_ptr<VulkanRhiNativeResourcePayload>& latch,
    rhi::ResourceState before,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording || impl_->ended || impl_->submitted
        || impl_->device.syntheticRuntime()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-NOT-RECORDING";
        return false;
    }
    if (before != rhi::ResourceState::Undefined && before != rhi::ResourceState::CopySrc) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-PRIOR-STATE";
        return false;
    }
    if (!latch || latch->backendVulkanDeviceHandle() != impl_->device.logicalDevice()
        || !latch->backendVulkanBufferHandle()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-DEVICE-MISMATCH";
        return false;
    }
    if (impl_->indexedFaultLatches.contains(latch)) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-ALREADY-RECORDED";
        return false;
    }
    auto* allocateCommandBuffers = functionFrom<PFN_vkAllocateCommandBuffers>(
        impl_->device.getDeviceProcAddr("vkAllocateCommandBuffers"));
    auto* beginCommandBuffer = functionFrom<PFN_vkBeginCommandBuffer>(
        impl_->device.getDeviceProcAddr("vkBeginCommandBuffer"));
    auto* cmdPipelineBarrier = functionFrom<PFN_vkCmdPipelineBarrier>(
        impl_->device.getDeviceProcAddr("vkCmdPipelineBarrier"));
    auto* fillBuffer =
        functionFrom<PFN_vkCmdFillBuffer>(impl_->device.getDeviceProcAddr("vkCmdFillBuffer"));
    auto* freeCommandBuffers = functionFrom<PFN_vkFreeCommandBuffers>(
        impl_->device.getDeviceProcAddr("vkFreeCommandBuffers"));
    if (!allocateCommandBuffers || !beginCommandBuffer || !cmdPipelineBarrier || !fillBuffer
        || !freeCommandBuffers) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-PREAMBLE-ENTRYPOINT-MISSING";
        return false;
    }
    const bool createdPreamble = !impl_->bindlessFaultPreamble;
    if (createdPreamble) {
        VkCommandBufferAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocation.commandPool = impl_->commandPool;
        allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocation.commandBufferCount = 1u;
        const auto allocated = allocateCommandBuffers(
            impl_->device.logicalDevice(), &allocation, &impl_->bindlessFaultPreamble);
        if (allocated != VK_SUCCESS) {
            impl_->bindlessFaultPreamble = VK_NULL_HANDLE;
            diagnostics = vkFailure("vkAllocateCommandBuffers(bindless fault preamble)", allocated);
            return false;
        }
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        const auto began = beginCommandBuffer(impl_->bindlessFaultPreamble, &begin);
        if (began != VK_SUCCESS) {
            freeCommandBuffers(impl_->device.logicalDevice(),
                impl_->commandPool,
                1u,
                &impl_->bindlessFaultPreamble);
            impl_->bindlessFaultPreamble = VK_NULL_HANDLE;
            diagnostics = vkFailure("vkBeginCommandBuffer(bindless fault preamble)", began);
            return false;
        }
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferAllocateCount;
            ++impl_->stats->nativeCommandBufferBeginCount;
        }
    }
    if (!impl_->indexedFaultLatches.retain(latch)) {
        if (createdPreamble) {
            freeCommandBuffers(impl_->device.logicalDevice(),
                impl_->commandPool,
                1u,
                &impl_->bindlessFaultPreamble);
            impl_->bindlessFaultPreamble = VK_NULL_HANDLE;
        }
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-CLEAR-RETENTION-CAPACITY";
        return false;
    }
    VkBufferMemoryBarrier toTransfer{};
    toTransfer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toTransfer.srcAccessMask =
        before == rhi::ResourceState::CopySrc ? VK_ACCESS_TRANSFER_READ_BIT : 0u;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.buffer = latch->backendVulkanBufferHandle();
    toTransfer.offset = 0u;
    toTransfer.size = sizeof(std::uint32_t);
    cmdPipelineBarrier(impl_->bindlessFaultPreamble,
        before == rhi::ResourceState::CopySrc ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                              : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        nullptr,
        1u,
        &toTransfer,
        0u,
        nullptr);
    fillBuffer(impl_->bindlessFaultPreamble,
        latch->backendVulkanBufferHandle(),
        0u,
        sizeof(std::uint32_t),
        0u);
    VkBufferMemoryBarrier toShader{};
    toShader.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.buffer = latch->backendVulkanBufferHandle();
    toShader.offset = 0u;
    toShader.size = sizeof(std::uint32_t);
    cmdPipelineBarrier(impl_->bindlessFaultPreamble,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0u,
        0u,
        nullptr,
        1u,
        &toShader,
        0u,
        nullptr);
    if (impl_->stats)
        impl_->stats->nativeCommandBufferRecordCount += 3u;
    diagnostics.clear();
    return true;
}

std::shared_ptr<VulkanRhiBindlessFaultReadback>
VulkanRhiNativeCommandPayload::scheduleBindlessFaultReadback(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
    const rhi::GpuBuffer& destination,
    std::pmr::string& diagnostics)
{
    if (!impl_ || !snapshot) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-SNAPSHOT-MISSING";
        return {};
    }
    auto leased = std::find_if(impl_->leasedBindlessFaultLatches.begin(),
        impl_->leasedBindlessFaultLatches.end(),
        [&](const auto& pending) {
            return pending.snapshot.get() == snapshot.get();
        });
    if (leased == impl_->leasedBindlessFaultLatches.end()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-NOT-CLEARED";
        return {};
    }
    if (impl_->bindlessFaultOutcomes.size() == impl_->bindlessFaultOutcomes.capacity()) {
        diagnostics = "RHI-BINDLESS-COMMAND-SNAPSHOT-BUDGET";
        return {};
    }
    for (const auto& outcome : impl_->bindlessFaultOutcomes) {
        if (outcome.snapshotGeneration == snapshot->generation()) {
            diagnostics = "RHI-BINDLESS-COMMAND-SNAPSHOT-DUPLICATE";
            return {};
        }
    }
    const auto* latch = leased->lease.faultLatchBuffer();
    if (!latch) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-PAYLOAD-MISSING";
        return {};
    }
    auto result = scheduleBindlessFaultReadback(*latch, destination, diagnostics);
    if (!result)
        return {};
    auto& pending = impl_->pendingBindlessFaults.back();
    pending.snapshot = std::move(leased->snapshot);
    pending.lease = std::move(leased->lease);
    impl_->bindlessFaultOutcomes.push_back({pending.snapshot->generation(), pending.result});
    impl_->leasedBindlessFaultLatches.erase(leased);
    return result;
}

std::shared_ptr<VulkanRhiBindlessFaultReadback>
VulkanRhiNativeCommandPayload::scheduleBindlessFaultReadback(
    std::shared_ptr<const rhi::BindlessSnapshot> snapshot, std::pmr::string& diagnostics)
{
    if (!snapshot) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-SNAPSHOT-MISSING";
        return {};
    }
    const auto* destination = snapshot->faultReadbackBuffer();
    if (!destination) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-READBACK-PAYLOAD-MISSING";
        return {};
    }
    return scheduleBindlessFaultReadback(std::move(snapshot), *destination, diagnostics);
}

rhi::BindlessFaultReport VulkanRhiNativeCommandPayload::bindlessFaultReport(
    std::uint64_t snapshotGeneration) const noexcept
{
    if (!snapshotGeneration || !impl_) {
        return {snapshotGeneration, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
    }
    for (const auto& outcome : impl_->bindlessFaultOutcomes) {
        if (outcome.snapshotGeneration == snapshotGeneration && outcome.result) {
            return {snapshotGeneration, outcome.result->status(), outcome.result->faultBits()};
        }
    }
    return {snapshotGeneration, rhi::BindlessFaultValidationStatus::Unavailable, 0u};
}

bool VulkanRhiNativeCommandPayload::recordUploadStagingBufferCopy(VkBuffer stagingBuffer,
    std::uint64_t stagingOffsetBytes,
    const VulkanRhiNativeResourcePayload& destination,
    std::uint64_t sizeBytes,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (sizeBytes == 0) {
        diagnostics = "Vulkan RHI native upload copy requires non-zero bytes";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeBufferCopyRecordCount;
        }
        return true;
    }
    const auto destinationHandle = destination.backendVulkanBufferHandle();
    if (!stagingBuffer || !destinationHandle) {
        diagnostics = "Vulkan RHI native upload copy requires staging and destination buffers";
        return false;
    }
    auto* cmdCopyBuffer =
        functionFrom<PFN_vkCmdCopyBuffer>(impl_->device.getDeviceProcAddr("vkCmdCopyBuffer"));
    if (!cmdCopyBuffer) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdCopyBuffer";
        return false;
    }
    VkBufferCopy region{};
    region.srcOffset = stagingOffsetBytes;
    region.dstOffset = 0;
    region.size = sizeBytes;
    cmdCopyBuffer(impl_->commandBuffer, stagingBuffer, destinationHandle, 1, &region);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeBufferCopyRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordUploadStagingTextureCopy(VkBuffer stagingBuffer,
    std::uint64_t stagingOffsetBytes,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::UploadTextureDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (destination.backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    return recordValidatedOriginalTextureCopy(
        stagingBuffer, stagingOffsetBytes, destination, desc, diagnostics);
}

bool VulkanRhiNativeCommandPayload::recordValidatedOriginalTextureCopy(VkBuffer stagingBuffer,
    std::uint64_t stagingOffsetBytes,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::UploadTextureDesc& desc,
    core::pmr::String& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (desc.pixelDataSize == 0 || desc.extent.width == 0 || desc.extent.height == 0
        || desc.extent.depth == 0) {
        diagnostics = "Vulkan RHI native texture upload requires non-zero data and extent";
        return false;
    }
    if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
        diagnostics = "Vulkan RHI native texture upload requires explicit row/slice pitch";
        return false;
    }
    if ((desc.bytesPerRow % 256u) != 0u) {
        diagnostics = "Vulkan RHI native texture upload row pitch must be 256-byte aligned";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeBufferToImageCopyRecordCount;
        }
        return true;
    }
    const auto destinationHandle = destination.backendVulkanImageHandle();
    if (!stagingBuffer || !destinationHandle) {
        diagnostics =
            "Vulkan RHI native texture upload requires staging buffer and destination texture";
        return false;
    }
    const auto vkFormat = destination.backendVulkanImageFormat();
    // Block-compressed formats (BC1/BC3/BC7) require block-aware copy math:
    // bufferRowLength must be in texels (a multiple of the 4x4 block extent),
    // not bytes/texelBytes. The uncompressed path divides the byte row pitch
    // by per-texel bytes; the BC path divides by per-block bytes then
    // multiplies by the block extent.
    const bool blockCompressed = isVulkanBlockCompressed(vkFormat);
    std::uint32_t rowLengthTexels = 0u;
    if (blockCompressed) {
        rowLengthTexels = vulkanBlockRowLengthTexels(desc.bytesPerRow, vkFormat);
        if (rowLengthTexels == 0u) {
            diagnostics =
                "Vulkan RHI native BC texture upload requires bytesPerRow aligned to block size";
            return false;
        }
    } else {
        const auto texelBytes = bytesPerPixelForVulkanCopy(vkFormat);
        if (texelBytes == 0u) {
            diagnostics = "Vulkan RHI native texture upload requires known texture format";
            return false;
        }
        if (desc.bytesPerRow % texelBytes != 0u) {
            diagnostics =
                "Vulkan RHI native texture upload requires bytesPerRow aligned to texel size";
            return false;
        }
        rowLengthTexels = desc.bytesPerRow / texelBytes;
    }
    auto* cmdCopyBufferToImage = functionFrom<PFN_vkCmdCopyBufferToImage>(
        impl_->device.getDeviceProcAddr("vkCmdCopyBufferToImage"));
    if (!cmdCopyBufferToImage) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdCopyBufferToImage";
        return false;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = stagingOffsetBytes;
    region.bufferRowLength = rowLengthTexels;
    region.bufferImageHeight = desc.rowsPerImage;
    region.imageSubresource.aspectMask = aspectFor(desc.subresources.aspect);
    region.imageSubresource.mipLevel =
        desc.mipLevel != 0 ? desc.mipLevel : desc.subresources.baseMipLevel;
    region.imageSubresource.baseArrayLayer =
        desc.arrayLayer != 0 ? desc.arrayLayer : desc.subresources.baseArrayLayer;
    region.imageSubresource.layerCount = desc.subresources.arrayLayerCount;
    if (region.imageSubresource.layerCount == 0) {
        region.imageSubresource.layerCount = 1;
    }
    // L2 destinationOffset lowers to the VkOffset3D origin of the
    // buffer-to-image copy. Without this, every sub-rect upload (e.g. dynamic
    // glyph cache updates) overwrites the texture's top-left corner instead of
    // landing in the glyph's atlas slot.
    region.imageOffset = {
        static_cast<std::int32_t>(desc.destinationOffset.width),
        static_cast<std::int32_t>(desc.destinationOffset.height),
        static_cast<std::int32_t>(desc.destinationOffset.depth),
    };
    region.imageExtent = vulkanImageCopyExtent(desc.extent, desc.subresources);
    cmdCopyBufferToImage(impl_->commandBuffer,
        stagingBuffer,
        destinationHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeBufferToImageCopyRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordBufferToImageCopy(
    const VulkanRhiNativeResourcePayload& source,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::BufferToTextureCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (destination.backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        diagnostics = "Vulkan RHI native buffer-to-image copy requires non-zero extent";
        return false;
    }
    if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
        diagnostics = "Vulkan RHI native texture upload requires explicit row and slice pitch";
        return false;
    }
    if ((desc.bytesPerRow % 256u) != 0u) {
        diagnostics = "Vulkan RHI native texture upload row pitch must be 256-byte aligned";
        return false;
    }
    if (impl_ && impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeBufferToImageCopyRecordCount;
        }
        return true;
    }
    const auto sourceHandle = source.backendVulkanBufferHandle();
    const auto destinationHandle = destination.backendVulkanImageHandle();
    if (!sourceHandle || !destinationHandle) {
        diagnostics = "Vulkan RHI native buffer-to-image copy requires native payloads";
        return false;
    }
    const auto vkFormat = destination.backendVulkanImageFormat();
    // Block-aware row-length math mirrors recordUploadStagingTextureCopy: BC
    // formats need texel-row-length as a multiple of the 4x4 block extent.
    const bool blockCompressed = isVulkanBlockCompressed(vkFormat);
    std::uint32_t rowLengthTexels = 0u;
    if (blockCompressed) {
        rowLengthTexels = vulkanBlockRowLengthTexels(desc.bytesPerRow, vkFormat);
        if (rowLengthTexels == 0u) {
            diagnostics = "Vulkan RHI native BC buffer-to-image copy requires bytesPerRow aligned "
                          "to block size";
            return false;
        }
    } else {
        const auto texelBytes = bytesPerPixelForVulkanCopy(vkFormat);
        if (texelBytes == 0u || desc.bytesPerRow == 0u || desc.bytesPerRow % texelBytes != 0u) {
            diagnostics = "Vulkan RHI native buffer-to-image copy requires bytesPerRow aligned to "
                          "texture texel size";
            return false;
        }
        rowLengthTexels = desc.bytesPerRow / texelBytes;
    }
    auto* cmdCopyBufferToImage = functionFrom<PFN_vkCmdCopyBufferToImage>(
        impl_->device.getDeviceProcAddr("vkCmdCopyBufferToImage"));
    if (!cmdCopyBufferToImage) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdCopyBufferToImage";
        return false;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = desc.bufferOffsetBytes;
    region.bufferRowLength = rowLengthTexels;
    region.bufferImageHeight = desc.rowsPerImage;
    region.imageSubresource.aspectMask = aspectFor(desc.textureSubresources.aspect);
    region.imageSubresource.mipLevel = desc.textureSubresources.baseMipLevel;
    region.imageSubresource.baseArrayLayer = desc.textureSubresources.baseArrayLayer;
    region.imageSubresource.layerCount = desc.textureSubresources.arrayLayerCount;
    region.imageExtent = vulkanImageCopyExtent(desc.extent, desc.textureSubresources);
    cmdCopyBufferToImage(impl_->commandBuffer,
        sourceHandle,
        destinationHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeBufferToImageCopyRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordImageToBufferCopy(
    const VulkanRhiNativeResourcePayload& source,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::TextureToBufferCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->internalCopy_)
        return impl_->conformanceClear->reject(diagnostics);
    if (source.backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    if (impl_ && impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeImageToBufferCopyRecordCount;
        }
        return true;
    }
    const auto sourceHandle = source.backendVulkanImageHandle();
    const auto destinationHandle = destination.backendVulkanBufferHandle();
    if (!sourceHandle || !destinationHandle) {
        diagnostics = "Vulkan RHI native image-to-buffer copy requires native payloads";
        return false;
    }
    const auto vkFormat = source.backendVulkanImageFormat();
    // Block-aware row-length math mirrors recordUploadStagingTextureCopy: BC
    // formats need texel-row-length as a multiple of the 4x4 block extent.
    const bool blockCompressed = isVulkanBlockCompressed(vkFormat);
    std::uint32_t rowLengthTexels = 0u;
    if (blockCompressed) {
        rowLengthTexels = vulkanBlockRowLengthTexels(desc.bytesPerRow, vkFormat);
        if (rowLengthTexels == 0u) {
            diagnostics = "Vulkan RHI native BC image-to-buffer copy requires bytesPerRow aligned "
                          "to block size";
            return false;
        }
    } else {
        const auto texelBytes = bytesPerPixelForVulkanCopy(vkFormat);
        if (texelBytes == 0u || desc.bytesPerRow == 0u || desc.bytesPerRow % texelBytes != 0u) {
            diagnostics = "Vulkan RHI native image-to-buffer copy requires bytesPerRow aligned to "
                          "texture texel size";
            return false;
        }
        rowLengthTexels = desc.bytesPerRow / texelBytes;
    }
    auto* cmdCopyImageToBuffer = functionFrom<PFN_vkCmdCopyImageToBuffer>(
        impl_->device.getDeviceProcAddr("vkCmdCopyImageToBuffer"));
    if (!cmdCopyImageToBuffer) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdCopyImageToBuffer";
        return false;
    }
    VkBufferImageCopy region{};
    region.bufferOffset = desc.bufferOffsetBytes;
    region.bufferRowLength = rowLengthTexels;
    region.bufferImageHeight = desc.rowsPerImage;
    region.imageSubresource.aspectMask = aspectFor(desc.textureSubresources.aspect);
    region.imageSubresource.mipLevel = desc.textureSubresources.baseMipLevel;
    region.imageSubresource.baseArrayLayer = desc.textureSubresources.baseArrayLayer;
    region.imageSubresource.layerCount = desc.textureSubresources.arrayLayerCount;
    region.imageExtent = vulkanImageCopyExtent(desc.extent, desc.textureSubresources);
    cmdCopyImageToBuffer(impl_->commandBuffer,
        sourceHandle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destinationHandle,
        1,
        &region);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeImageToBufferCopyRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::scheduleTextureReadback(
    const VulkanRhiNativeResourcePayload& source,
    std::shared_ptr<VulkanRhiNativeResourcePayload> destination,
    std::shared_ptr<void> allocationToken,
    void* mappedData,
    std::shared_ptr<VulkanRhiMappedBufferReadback> readback,
    const rhi::TextureDesc& textureDesc,
    std::string_view label,
    std::pmr::string& diagnostics)
{
    (void)source;
    (void)destination;
    (void)allocationToken;
    (void)mappedData;
    (void)readback;
    (void)textureDesc;
    (void)label;
    diagnostics =
        rhi::BorrowedTextureReadbackRetirementContract::lowering(rhi::Backend::Vulkan).diagnostic;
    return false;
}

bool VulkanRhiNativeCommandPayload::recordImageToImageCopy(
    const VulkanRhiNativeResourcePayload& source,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::TextureToTextureCopyDesc& desc,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (source.backendVulkanOriginalContentExclusive()
        || destination.backendVulkanOriginalContentExclusive()) {
        diagnostics = "RHI-TEXTURE-CONTENT-EXCLUSIVE-USE";
        return false;
    }
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        diagnostics = "Vulkan RHI native image-to-image copy requires non-zero extent";
        return false;
    }
    if (impl_ && impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeImageToImageCopyRecordCount;
        }
        return true;
    }
    const auto sourceHandle = source.backendVulkanImageHandle();
    const auto destinationHandle = destination.backendVulkanImageHandle();
    if (!sourceHandle || !destinationHandle) {
        diagnostics = "Vulkan RHI native image-to-image copy requires native payloads";
        return false;
    }
    // vkCmdCopyImage requires matching formats when copying the depth aspect
    // (VUID-vkCmdCopyImage-srcImage-01552). Both textures are expected to be
    // the same format (D32Float->D32Float for depth, Rgba16Float->Rgba16Float
    // for color) - the caller ensures this by creating the copy target with
    // the source's format.
    auto* cmdCopyImage =
        functionFrom<PFN_vkCmdCopyImage>(impl_->device.getDeviceProcAddr("vkCmdCopyImage"));
    if (!cmdCopyImage) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdCopyImage";
        return false;
    }
    VkImageCopy region{};
    region.srcSubresource.aspectMask = aspectFor(desc.sourceSubresources.aspect);
    region.srcSubresource.mipLevel = desc.sourceSubresources.baseMipLevel;
    region.srcSubresource.baseArrayLayer = desc.sourceSubresources.baseArrayLayer;
    region.srcSubresource.layerCount = desc.sourceSubresources.arrayLayerCount;
    region.dstSubresource.aspectMask = aspectFor(desc.destinationSubresources.aspect);
    region.dstSubresource.mipLevel = desc.destinationSubresources.baseMipLevel;
    region.dstSubresource.baseArrayLayer = desc.destinationSubresources.baseArrayLayer;
    region.dstSubresource.layerCount = desc.destinationSubresources.arrayLayerCount;
    const auto copyExtent = vulkanImageCopyExtent(desc.extent,
        desc.sourceSubresources.arrayLayerCount > 1u ? desc.sourceSubresources
                                                     : desc.destinationSubresources);
    region.extent = copyExtent;
    cmdCopyImage(impl_->commandBuffer,
        sourceHandle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        destinationHandle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeImageToImageCopyRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordComputePipelineBind(
    const VulkanRhiNativeResourcePayload& pipeline, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->computePipeline(pipeline, diagnostics))
        return false;
    if (impl_ && impl_->device.syntheticRuntime()) {
        impl_->boundComputePipelineLayout = syntheticHandle<VkPipelineLayout>(1u);
        impl_->boundComputePushConstantStages = pipeline.backendVulkanPushConstantStageFlags();
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeComputePipelineBindRecordCount;
        }
        return true;
    }
    const auto handle = pipeline.backendVulkanComputePipelineHandle();
    if (!impl_ || pipeline.backendVulkanDeviceHandle() != impl_->device.logicalDevice()) {
        diagnostics = "RHI-VULKAN-NATIVE-PIPELINE-DEVICE-MISMATCH";
        return false;
    }
    if (!handle) {
        diagnostics = "Vulkan RHI native compute bind requires a native pipeline payload";
        return false;
    }
    auto* cmdBindPipeline =
        functionFrom<PFN_vkCmdBindPipeline>(impl_->device.getDeviceProcAddr("vkCmdBindPipeline"));
    if (!cmdBindPipeline) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdBindPipeline";
        return false;
    }
    cmdBindPipeline(impl_->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, handle);
    impl_->boundComputePipelineLayout = pipeline.backendVulkanComputePipelineLayoutHandle();
    impl_->boundComputePushConstantStages = pipeline.backendVulkanPushConstantStageFlags();
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeComputePipelineBindRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordComputePushConstants(std::uint32_t offsetBytes,
    std::uint32_t sizeBytes,
    const void* data,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native compute push constants require a recording command buffer";
        return false;
    }
    if (sizeBytes == 0u || !data) {
        diagnostics = "Vulkan RHI native compute push constants require explicit data";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
        }
        return true;
    }
    if (!impl_->boundComputePipelineLayout) {
        diagnostics =
            "Vulkan RHI native compute push constants require a bound compute pipeline layout";
        return false;
    }
    auto* cmdPushConstants =
        functionFrom<PFN_vkCmdPushConstants>(impl_->device.getDeviceProcAddr("vkCmdPushConstants"));
    if (!cmdPushConstants) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdPushConstants";
        return false;
    }
    const auto stageFlags = impl_->boundComputePushConstantStages != 0
        ? impl_->boundComputePushConstantStages
        : VK_SHADER_STAGE_COMPUTE_BIT;
    cmdPushConstants(impl_->commandBuffer,
        impl_->boundComputePipelineLayout,
        stageFlags,
        offsetBytes,
        sizeBytes,
        data);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDescriptorSetBind(
    const VulkanRhiNativeResourcePayload& descriptorSet,
    std::uint32_t setIndex,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->descriptors(
            descriptorSet, setIndex, dynamicOffsetCount, diagnostics))
        return false;
    if (dynamicOffsetCount > 0u && dynamicOffsets == nullptr) {
        diagnostics = "Vulkan RHI native descriptor bind has null dynamic offsets";
        return false;
    }
    if (impl_ && impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeDescriptorBindRecordCount;
        }
        return true;
    }
    const auto handle = descriptorSet.backendVulkanDescriptorSetHandle();
    if (!impl_ || descriptorSet.backendVulkanDeviceHandle() != impl_->device.logicalDevice()) {
        diagnostics = "RHI-VULKAN-NATIVE-DESCRIPTOR-DEVICE-MISMATCH";
        return false;
    }
    const bool graphicsBind = impl_->activeRenderPass.renderPass != VK_NULL_HANDLE;
    const auto layout =
        graphicsBind ? impl_->boundGraphicsPipelineLayout : impl_->boundComputePipelineLayout;
    if (!handle || !layout) {
        diagnostics = graphicsBind
            ? "Vulkan RHI native descriptor bind requires a descriptor set and graphics layout"
            : "Vulkan RHI native descriptor bind requires a descriptor set and compute layout";
        return false;
    }
    auto* cmdBindDescriptorSets = functionFrom<PFN_vkCmdBindDescriptorSets>(
        impl_->device.getDeviceProcAddr("vkCmdBindDescriptorSets"));
    if (!cmdBindDescriptorSets) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdBindDescriptorSets";
        return false;
    }
    cmdBindDescriptorSets(impl_->commandBuffer,
        graphicsBind ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE,
        layout,
        setIndex,
        1,
        &handle,
        dynamicOffsetCount,
        dynamicOffsets);
    if (std::getenv("ARP_FORWARD_PLUS_DESCRIPTOR_TRACE") != nullptr && graphicsBind
        && setIndex == 1u) {
        std::cerr << "[vulkan-descriptor-bind-trace] set="
                  << descriptorSet.backendVulkanDescriptorSetHandle() << " setIndex=" << setIndex
                  << " pipelineLayout=" << layout << " descriptorPayload=" << &descriptorSet
                  << '\n';
    }
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeDescriptorBindRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDispatch(std::uint32_t groupCountX,
    std::uint32_t groupCountY,
    std::uint32_t groupCountZ,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear
        && !impl_->conformanceClear->dispatch(groupCountX, groupCountY, groupCountZ, diagnostics))
        return false;
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->backendDispatchRecordCount;
            ++impl_->stats->nativeExecutableDispatchRecordCount;
        }
        return true;
    }
    auto* cmdDispatch =
        functionFrom<PFN_vkCmdDispatch>(impl_->device.getDeviceProcAddr("vkCmdDispatch"));
    if (!cmdDispatch) {
        diagnostics = "Vulkan RHI native command buffer missing vkCmdDispatch";
        return false;
    }
    cmdDispatch(impl_->commandBuffer, groupCountX, groupCountY, groupCountZ);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->backendDispatchRecordCount;
        ++impl_->stats->nativeExecutableDispatchRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordDispatchIndirect(
    const VulkanRhiNativeResourcePayload& indirectBuffer,
    const std::uint64_t indirectOffset,
    std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer is not recording";
        return false;
    }
    if ((indirectOffset & 3u) != 0u) {
        diagnostics = "Vulkan RHI indirect dispatch offset must be 4-byte aligned";
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->backendDispatchRecordCount;
            ++impl_->stats->nativeExecutableDispatchRecordCount;
        }
        return true;
    }
    const auto handle = indirectBuffer.backendVulkanBufferHandle();
    if (!handle) {
        diagnostics = "Vulkan RHI native indirect dispatch requires a native args buffer";
        return false;
    }
    auto* cmdDispatchIndirect = functionFrom<PFN_vkCmdDispatchIndirect>(
        impl_->device.getDeviceProcAddr("vkCmdDispatchIndirect"));
    if (!cmdDispatchIndirect) {
        diagnostics =
            "Vulkan RHI native indirect dispatch missing vkCmdDispatchIndirect entrypoint";
        return false;
    }
    cmdDispatchIndirect(impl_->commandBuffer, handle, indirectOffset);
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->backendDispatchRecordCount;
        ++impl_->stats->nativeExecutableDispatchRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordPushDebugGroup(
    std::string_view label, std::pmr::string& diagnostics)
{
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer not recording";
        return false;
    }
    auto* fn = functionFrom<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        impl_->device.getDeviceProcAddr("vkCmdBeginDebugUtilsLabelEXT"));
    if (!fn)
        return true; // extension not available, silently skip

    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = label.data();
    fn(impl_->commandBuffer, &labelInfo);
    return true;
}

bool VulkanRhiNativeCommandPayload::recordPopDebugGroup(std::pmr::string& diagnostics)
{
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer not recording";
        return false;
    }
    auto* fn = functionFrom<PFN_vkCmdEndDebugUtilsLabelEXT>(
        impl_->device.getDeviceProcAddr("vkCmdEndDebugUtilsLabelEXT"));
    if (!fn)
        return true;

    fn(impl_->commandBuffer);
    return true;
}

bool VulkanRhiNativeCommandPayload::recordInsertDebugMarker(
    std::string_view label, std::pmr::string& diagnostics)
{
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan RHI native command buffer not recording";
        return false;
    }
    auto* fn = functionFrom<PFN_vkCmdInsertDebugUtilsLabelEXT>(
        impl_->device.getDeviceProcAddr("vkCmdInsertDebugUtilsLabelEXT"));
    if (!fn)
        return true;

    VkDebugUtilsLabelEXT labelInfo{};
    labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    labelInfo.pLabelName = label.data();
    fn(impl_->commandBuffer, &labelInfo);
    return true;
}

bool VulkanRhiNativeCommandPayload::recordTimestamp(
    const rhi::QueryPool& queryPool, const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan timestamp write requires an active native command buffer";
        return false;
    }
    const auto* nativePool = asVulkanRhiQueryPool(queryPool);
    if (!nativePool || queryIndex >= nativePool->count()) {
        diagnostics = "Vulkan timestamp write received an incompatible query pool or index";
        return false;
    }
    const auto write = functionFrom<PFN_vkCmdWriteTimestamp>(
        impl_->device.getDeviceProcAddr("vkCmdWriteTimestamp"));
    if (!write) {
        diagnostics = "Vulkan timestamp write entrypoint is unavailable";
        return false;
    }
    if (!nativePool->payload() || !nativePool->payload()->markRecorded(queryIndex, diagnostics)) {
        return false;
    }
    const auto nativeHandle = nativePool->nativeHandle();
    retainTimingQueryPool(nativePool->payload());
    impl_->recordedTimestampQueries.push_back({nativePool->payload(), queryIndex});
    write(impl_->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, nativeHandle, queryIndex);
    return true;
}

bool VulkanRhiNativeCommandPayload::end(std::pmr::string& diagnostics)
{
    if (!impl_) {
        diagnostics = "Vulkan RHI native command buffer payload is missing";
        return false;
    }
    if (impl_->ended) {
        return true;
    }
    if (!impl_->leasedBindlessFaultLatches.empty()) {
        diagnostics = "RHI-VULKAN-BINDLESS-FAULT-LEASE-READBACK-MISSING";
        return false;
    }
    if (!flushPendingBarriers(diagnostics)) {
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        impl_->recording = false;
        impl_->ended = true;
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferEndCount;
        }
        return true;
    }
    if (impl_->timestampQueryPool) {
        if (auto* writeTimestamp = functionFrom<PFN_vkCmdWriteTimestamp>(
                impl_->device.getDeviceProcAddr("vkCmdWriteTimestamp"))) {
            writeTimestamp(impl_->commandBuffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                impl_->timestampQueryPool,
                1u);
        }
    }
    auto* endCommandBuffer =
        functionFrom<PFN_vkEndCommandBuffer>(impl_->device.getDeviceProcAddr("vkEndCommandBuffer"));
    if (!endCommandBuffer) {
        diagnostics = "Vulkan RHI native command buffer missing vkEndCommandBuffer";
        return false;
    }
    if (impl_->bindlessFaultPreamble) {
        const auto preambleEnded = endCommandBuffer(impl_->bindlessFaultPreamble);
        if (preambleEnded != VK_SUCCESS) {
            diagnostics = vkFailure("vkEndCommandBuffer(bindless fault preamble)", preambleEnded);
            return false;
        }
        if (impl_->stats)
            ++impl_->stats->nativeCommandBufferEndCount;
    }
    const auto ended = endCommandBuffer(impl_->commandBuffer);
    if (ended != VK_SUCCESS) {
        diagnostics = vkFailure("vkEndCommandBuffer", ended);
        return false;
    }
    impl_->recording = false;
    impl_->ended = true;
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferEndCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::submit(std::pmr::string& diagnostics)
{
    return submit({}, diagnostics);
}

bool VulkanRhiNativeCommandPayload::prepareOriginalSubmissionFence(core::pmr::String& diagnostic)
{
    if (!impl_ || impl_->device.syntheticRuntime() || impl_->submitted || impl_->submitFence) {
        diagnostic = "RHI-VULKAN-ROOT-FENCE-COLD-FENCE-STATUS";
        return false;
    }
    if (acquireReusableFence(impl_->deviceOwner, impl_->submitFence))
        return true;
    const auto createFence =
        functionFrom<PFN_vkCreateFence>(impl_->device.getDeviceProcAddr("vkCreateFence"));
    if (!createFence) {
        diagnostic = "RHI-VULKAN-ROOT-FENCE-CREATE-UNAVAILABLE";
        return false;
    }
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    const auto status =
        createFence(impl_->device.logicalDevice(), &info, nullptr, &impl_->submitFence);
    if (status != VK_SUCCESS) {
        diagnostic = vkFailure("vkCreateFence", status);
        return false;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::submit(const VulkanRhiTimelineSubmitInfo& timeline,
    std::pmr::string& diagnostics,
    bool* acceptedThisAttempt)
{
    if (impl_ && impl_->conformanceClear
        && (!impl_->conformanceClear->submitAuthorized_ || impl_->conformanceClear->failure_
            || impl_->conformanceClear->state_ != VulkanConformanceClearFrame::State::Copied))
        return impl_->conformanceClear->reject(diagnostics);

    if (acceptedThisAttempt)
        *acceptedThisAttempt = false;
    if (!impl_ || !impl_->ended) {
        diagnostics = "Vulkan RHI native command buffer must be ended before submit";
        return false;
    }
    if (impl_->submitted) {
        diagnostics = "Vulkan RHI native command buffer was already submitted";
        return false;
    }
    if ((timeline.waitSemaphore && !timeline.waitSemaphore->nativeHandle())
        || (timeline.signalSemaphore && !timeline.signalSemaphore->nativeHandle())) {
        diagnostics = "RHI-VULKAN-TIMELINE-SYNC-PAYLOAD-MISSING";
        return false;
    }
    if ((timeline.waitSemaphore && !timeline.waitSemaphore->belongsTo(impl_->device))
        || (timeline.signalSemaphore && !timeline.signalSemaphore->belongsTo(impl_->device))) {
        diagnostics = "RHI-VULKAN-TIMELINE-SYNC-OWNER-MISMATCH";
        return false;
    }
    if (impl_->hasSwapchainFrameSync && impl_->swapchainFrameSyncState
        && impl_->swapchainFrameSyncState->renderSubmitted()) {
        diagnostics = "RHI-VULKAN-SWAPCHAIN-SYNC-ALREADY-SUBMITTED";
        return false;
    }
    if (!asyncSubmittedCommandRetirementHasCapacity(diagnostics)) {
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (!query.pool || !query.pool->markSubmitted(query.queryIndex, diagnostics)) {
                return false;
            }
        }
        impl_->submitted = true;
        if (acceptedThisAttempt)
            *acceptedThisAttempt = true;
        if (impl_->hasSwapchainFrameSync && impl_->swapchainFrameSyncState) {
            impl_->swapchainFrameSyncState->renderSubmitted_ = true;
            impl_->swapchainFrameSyncState->submittedCommand_ = shared_from_this();
        }
        if (!retainSubmittedCommandForAsyncRetirement(shared_from_this(), diagnostics)) {
            return false;
        }
        if (impl_->stats) {
            ++impl_->stats->nativeQueueSubmitCount;
        }
        return true;
    }
    auto* createFence =
        functionFrom<PFN_vkCreateFence>(impl_->device.getDeviceProcAddr("vkCreateFence"));
    auto* queueSubmit =
        functionFrom<PFN_vkQueueSubmit>(impl_->device.getDeviceProcAddr("vkQueueSubmit"));
    if (!createFence || !queueSubmit) {
        diagnostics = "Vulkan RHI native queue submit missing Vulkan entrypoints";
        return false;
    }
    const bool hasTimelineWait = bool(timeline.waitSemaphore);
    const bool hasTimelineSignal = bool(timeline.signalSemaphore);

    if (!impl_->submitFence && !acquireReusableFence(impl_->deviceOwner, impl_->submitFence)) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        const auto fenceCreated =
            createFence(impl_->device.logicalDevice(), &fenceInfo, nullptr, &impl_->submitFence);
        if (fenceCreated != VK_SUCCESS) {
            diagnostics = vkFailure("vkCreateFence", fenceCreated);
            return false;
        }
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    std::array<VkCommandBuffer, 2> commandBuffers{};
    std::uint32_t commandBufferCount = 0u;
    if (impl_->bindlessFaultPreamble) {
        commandBuffers[commandBufferCount++] = impl_->bindlessFaultPreamble;
    }
    commandBuffers[commandBufferCount++] = impl_->commandBuffer;
    submitInfo.commandBufferCount = commandBufferCount;
    submitInfo.pCommandBuffers = commandBuffers.data();
    std::array<VkSemaphore, 2> waitSemaphores{};
    std::array<VkPipelineStageFlags, 2> waitStages{};
    std::array<VkSemaphore, 2> signalSemaphores{};
    std::array<std::uint64_t, 2> waitValues{};
    std::array<std::uint64_t, 2> signalValues{};
    std::uint32_t waitCount = 0u;
    std::uint32_t signalCount = 0u;
    if (hasTimelineWait) {
        waitSemaphores[waitCount] = timeline.waitSemaphore->nativeHandle();
        waitStages[waitCount] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        waitValues[waitCount++] = timeline.waitValue;
    }
    if (impl_->hasSwapchainFrameSync) {
        waitSemaphores[waitCount] = impl_->waitSemaphore;
        waitStages[waitCount++] = impl_->waitStageMask;
    }
    if (hasTimelineSignal) {
        signalSemaphores[signalCount] = timeline.signalSemaphore->nativeHandle();
        signalValues[signalCount++] = timeline.signalValue;
    }
    if (impl_->hasSwapchainFrameSync) {
        signalSemaphores[signalCount++] = impl_->signalSemaphore;
    }
    submitInfo.waitSemaphoreCount = waitCount;
    submitInfo.pWaitSemaphores = waitCount ? waitSemaphores.data() : nullptr;
    submitInfo.pWaitDstStageMask = waitCount ? waitStages.data() : nullptr;
    submitInfo.signalSemaphoreCount = signalCount;
    submitInfo.pSignalSemaphores = signalCount ? signalSemaphores.data() : nullptr;
    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    if (hasTimelineWait || hasTimelineSignal) {
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount = waitCount;
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = signalCount;
        timelineInfo.pSignalSemaphoreValues = signalValues.data();
        submitInfo.pNext = &timelineInfo;
    }
    std::size_t submittedQueryCount = 0u;
    for (const auto& query : impl_->recordedTimestampQueries) {
        if (!query.pool || !query.pool->markSubmitted(query.queryIndex, diagnostics)) {
            for (std::size_t index = 0; index < submittedQueryCount; ++index) {
                const auto& submittedQuery = impl_->recordedTimestampQueries[index];
                if (submittedQuery.pool) {
                    submittedQuery.pool->rollbackSubmitted(submittedQuery.queryIndex, diagnostics);
                }
            }
            return false;
        }
        ++submittedQueryCount;
    }
    if (impl_->originalBindlessCustody
        && !impl_->originalBindlessCustody->beforeSubmit(*this, diagnostics))
        return false;
    // Reserve native retirement BEFORE queue invocation for canonical custody.
    // Queue failure is not proof of non-execution and must never roll back.
    const bool retainBeforeInvocation =
        impl_->originalBindlessCustody || hasTimelineWait || hasTimelineSignal;
    if (retainBeforeInvocation
        && !retainSubmittedCommandForAsyncRetirement(shared_from_this(), diagnostics)) {
        return false;
    }
    if (impl_->originalBindlessCustody) {
        if (const auto* id = impl_->originalBindlessCustody->maySubmit()) {
            diagnostics = id;
            return false;
        }
    }
    impl_->submittedTimeline = timeline;
    const auto submitted =
        queueSubmit(impl_->device.graphicsQueue(), 1, &submitInfo, impl_->submitFence);
    impl_->device.captureOriginalDeviceLoss(submitted);
    if (submitted != VK_SUCCESS) {
        if (retainBeforeInvocation) {
            impl_->submitted = true;
            impl_->originalSubmissionUnproven = true;
            if (impl_->originalBindlessCustody)
                impl_->originalBindlessCustody->submissionUnproven(*this);
        }
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (query.pool) {
                query.pool->rollbackSubmitted(query.queryIndex, diagnostics);
            }
        }
        if (impl_->originalBindlessCustody)
            diagnostics = submitted == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-ROOT-FENCE-SUBMIT-DEVICE-LOST"
                : "RHI-VULKAN-ROOT-FENCE-SUBMIT-UNPROVEN";
        else if (hasTimelineWait || hasTimelineSignal)
            diagnostics = submitted == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-TIMELINE-SUBMIT-DEVICE-LOST"
                : "RHI-VULKAN-TIMELINE-SUBMIT-UNPROVEN";
        else
            diagnostics = vkFailure("vkQueueSubmit", submitted);
        return false;
    }
    impl_->submitted = true;
    if (acceptedThisAttempt)
        *acceptedThisAttempt = true;
    if (impl_->hasSwapchainFrameSync && impl_->swapchainFrameSyncState) {
        impl_->swapchainFrameSyncState->renderSubmitted_ = true;
        impl_->swapchainFrameSyncState->submittedCommand_ = shared_from_this();
    }
    if (!retainBeforeInvocation
        && !retainSubmittedCommandForAsyncRetirement(shared_from_this(), diagnostics)) {
        return false;
    }
    if (impl_->stats) {
        ++impl_->stats->nativeQueueSubmitCount;
        if (impl_->hasSwapchainFrameSync) {
            ++impl_->stats->nativeSwapchainSubmitWaitCount;
            ++impl_->stats->nativeSwapchainSubmitSignalCount;
        }
    }
    return true;
}

std::shared_ptr<VulkanRhiNativeCommandPayload> createVulkanRhiNativeCommandPayload(
    const VulkanRhiNativeResourceAllocator& nativeResources,
    std::pmr::string& diagnostics,
    const rhi::CommandEncoderDesc& retention)
{
    if (!nativeResources.connected() || !nativeResources.runtime()) {
        diagnostics = "Vulkan RHI native command buffer requires a native device";
        return {};
    }
    auto runtime = nativeResources.runtime();
    const auto& device = *runtime;
    // Reserve indexed ownership before creating any Vulkan object. Allocation
    // failure cannot leak a pool and can never trigger a recording-time growth.
    std::shared_ptr<VulkanRhiNativeCommandPayload> payload;
    try {
        payload = core::makeShared<VulkanRhiNativeCommandPayload>(
            core::makeUnique<VulkanRhiNativeCommandPayload::Impl>(runtime,
                nativeResources.allocator(),
                nativeResources.sharedStats(),
                VK_NULL_HANDLE,
                VK_NULL_HANDLE,
                retention));
    } catch (const std::bad_alloc&) {
        diagnostics = "RHI-VULKAN-BINDLESS-RETENTION-ALLOCATION-FAILED";
        return {};
    }
    auto* impl = payload->impl_.get();
    if (device.syntheticRuntime()) {
        if (auto stats = nativeResources.sharedStats()) {
            ++stats->nativeCommandPoolCreateCount;
            ++stats->nativeCommandBufferAllocateCount;
            ++stats->nativeCommandBufferBeginCount;
        }
        impl->recording = true;
        return payload;
    }
    auto* createCommandPool =
        functionFrom<PFN_vkCreateCommandPool>(device.getDeviceProcAddr("vkCreateCommandPool"));
    auto* destroyCommandPool =
        functionFrom<PFN_vkDestroyCommandPool>(device.getDeviceProcAddr("vkDestroyCommandPool"));
    auto* allocateCommandBuffers = functionFrom<PFN_vkAllocateCommandBuffers>(
        device.getDeviceProcAddr("vkAllocateCommandBuffers"));
    auto* beginCommandBuffer =
        functionFrom<PFN_vkBeginCommandBuffer>(device.getDeviceProcAddr("vkBeginCommandBuffer"));
    if (!createCommandPool || !destroyCommandPool || !allocateCommandBuffers
        || !beginCommandBuffer) {
        diagnostics = "Vulkan RHI native command buffer missing Vulkan entrypoints";
        return {};
    }

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    const bool reused = acquireReusableCommandResources(
        runtime, device.graphicsQueueFamilyIndex(), commandPool, commandBuffer);
    if (!reused) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = device.graphicsQueueFamilyIndex();
        const auto poolCreated =
            createCommandPool(device.logicalDevice(), &poolInfo, nullptr, &commandPool);
        if (poolCreated != VK_SUCCESS) {
            diagnostics = vkFailure("vkCreateCommandPool", poolCreated);
            return {};
        }
        if (auto stats = nativeResources.sharedStats()) {
            ++stats->nativeCommandPoolCreateCount;
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        const auto allocated =
            allocateCommandBuffers(device.logicalDevice(), &allocateInfo, &commandBuffer);
        if (allocated != VK_SUCCESS) {
            destroyCommandPool(device.logicalDevice(), commandPool, nullptr);
            diagnostics = vkFailure("vkAllocateCommandBuffers", allocated);
            return {};
        }
        if (auto stats = nativeResources.sharedStats()) {
            ++stats->nativeCommandBufferAllocateCount;
        }
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const auto began = beginCommandBuffer(commandBuffer, &beginInfo);
    if (began != VK_SUCCESS) {
        destroyReusableCommandResources({
            runtime,
            commandPool,
            commandBuffer,
            device.graphicsQueueFamilyIndex(),
        });
        diagnostics = vkFailure("vkBeginCommandBuffer", began);
        return {};
    }
    if (auto stats = nativeResources.sharedStats()) {
        ++stats->nativeCommandBufferBeginCount;
    }

    impl->commandPool = commandPool;
    impl->commandBuffer = commandBuffer;
    impl->recording = true;
    auto* createQueryPool =
        functionFrom<PFN_vkCreateQueryPool>(device.getDeviceProcAddr("vkCreateQueryPool"));
    auto* resetQueryPool =
        functionFrom<PFN_vkCmdResetQueryPool>(device.getDeviceProcAddr("vkCmdResetQueryPool"));
    auto* writeTimestamp =
        functionFrom<PFN_vkCmdWriteTimestamp>(device.getDeviceProcAddr("vkCmdWriteTimestamp"));
    if (createQueryPool && resetQueryPool && writeTimestamp) {
        VkQueryPoolCreateInfo queryInfo{};
        queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 2u;
        if (createQueryPool(device.logicalDevice(), &queryInfo, nullptr, &impl->timestampQueryPool)
            == VK_SUCCESS) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device.physicalDevice(), &properties);
            impl->timestampPeriodNs = properties.limits.timestampPeriod;
            resetQueryPool(commandBuffer, impl->timestampQueryPool, 0u, 2u);
            writeTimestamp(
                commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, impl->timestampQueryPool, 0u);
        }
    }
    return payload;
}

bool recordVulkanRhiNativeCommand(
    const std::shared_ptr<VulkanRhiNativeCommandPayload>& nativePayload,
    std::pmr::string& diagnostics)
{
    if (!nativePayload) {
        diagnostics = "RHI-VULKAN-NATIVE-COMMAND-PAYLOAD-MISSING";
        return false;
    }
    return nativePayload->record(diagnostics);
}

rhi::GpuTimingSample latestVulkanRhiGpuTimingSample()
{
    rhi::GpuTimingSample result;
    const auto serial = completedGpuTiming().frameSerial.load(std::memory_order_relaxed);
    if (serial == 0u) {
        result.availability = rhi::GpuTimingAvailability::Pending;
        result.diagnostic = "Vulkan timestamp result is pending a completed frame fence";
        return result;
    }
    result.availability = rhi::GpuTimingAvailability::Available;
    result.completedFrameSerial = serial;
    const auto scopeCount = completedGpuTiming().scopeCount.load(std::memory_order_relaxed);
    result.scopeCount = scopeCount;
    for (std::uint32_t i = 0; i < scopeCount; ++i) {
        result.scopes[i].scopeId = completedGpuTiming().scopeIds[i].load(std::memory_order_relaxed);
        result.scopes[i].elapsedMs =
            static_cast<float>(
                completedGpuTiming().scopeNanoseconds[i].load(std::memory_order_relaxed))
            / 1000000.0f;
    }
    if (result.scopeCount == 0u) {
        result.scopeCount = 1u;
        result.scopes[0].scopeId = 0u;
        result.scopes[0].elapsedMs =
            static_cast<float>(
                completedGpuTiming().elapsedNanoseconds.load(std::memory_order_relaxed))
            / 1000000.0f;
    }
    result.diagnostic = "Vulkan timestamp query";
    return result;
}

} // namespace arp::rhi_vulkan
