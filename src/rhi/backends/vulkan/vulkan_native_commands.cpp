#include "rhi/backends/vulkan/vulkan_native_commands.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_conformance_clear.h"
#include "rhi/backends/vulkan/vulkan_native_commands_impl.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "rhi/backends/vulkan/vulkan_query_pool.h"
#include "rhi/backends/vulkan/vulkan_retained_command.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/command_retention_validation.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/transfer_readback_borrowed_retirement.h"
#include "rhi/semantics/native_command_lowering.h"
#include "rhi/semantics/query_lowering.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
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

using ReusableCommandResources = VulkanRhiCommandDomain::ReusableCommandResources;

void destroyReusableCommandResources(
    const VulkanRhiRuntime& runtime, ReusableCommandResources resources)
{
    const auto destroy =
        functionFrom<PFN_vkDestroyCommandPool>(runtime.getDeviceProcAddr("vkDestroyCommandPool"));
    if (destroy && resources.commandPool)
        destroy(runtime.logicalDevice(), resources.commandPool, nullptr);
}

bool acquireReusableFence(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    VkFence& fence,
    core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (!runtime)
        return false;
    if (const auto* id = runtime->commandDomain().ownerDiagnostic()) {
        diagnostics = id;
        return false;
    }
    for (auto& cached : runtime->commandDomain().reusableFences) {
        if (!cached)
            continue;
        const auto reset =
            functionFrom<PFN_vkResetFences>(runtime->getDeviceProcAddr("vkResetFences"));
        if (!reset) {
            diagnostics = "RHI-VULKAN-REUSE-FENCE-RESET-ENTRYPOINT";
            return false;
        }
        if (reset(runtime->logicalDevice(), 1u, &cached) != VK_SUCCESS) {
            diagnostics = "RHI-VULKAN-REUSE-FENCE-RESET-FAILED";
            return false;
        }
        fence = std::exchange(cached, VK_NULL_HANDLE);
        return true;
    }
    return false;
}

void releaseReusableFence(const std::shared_ptr<VulkanRhiRuntime>& runtime, VkFence fence)
{
    if (!runtime || !fence)
        return;
    if (!runtime->commandDomain().ownerDiagnostic())
        for (auto& slot : runtime->commandDomain().reusableFences)
            if (!slot) {
                slot = fence;
                return;
            }
    const auto destroy =
        functionFrom<PFN_vkDestroyFence>(runtime->getDeviceProcAddr("vkDestroyFence"));
    if (destroy)
        destroy(runtime->logicalDevice(), fence, nullptr);
}

void clearReusableFences(const VulkanRhiRuntime& runtime)
{
    const auto destroy =
        functionFrom<PFN_vkDestroyFence>(runtime.getDeviceProcAddr("vkDestroyFence"));
    if (!destroy)
        return;
    for (auto& fence : runtime.commandDomain().reusableFences)
        if (fence)
            destroy(runtime.logicalDevice(), std::exchange(fence, VK_NULL_HANDLE), nullptr);
}

bool acquireReusableCommandResources(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    std::uint32_t family,
    VkCommandPool& pool,
    VkCommandBuffer& buffer,
    core::pmr::String& diagnostics)
{
    diagnostics.clear();
    if (!runtime)
        return false;
    if (const auto* id = runtime->commandDomain().ownerDiagnostic()) {
        diagnostics = id;
        return false;
    }
    for (auto& cached : runtime->commandDomain().reusableCommands) {
        if (!cached.commandPool || cached.queueFamilyIndex != family)
            continue;
        const auto reset =
            functionFrom<PFN_vkResetCommandPool>(runtime->getDeviceProcAddr("vkResetCommandPool"));
        if (!reset) {
            diagnostics = "RHI-VULKAN-REUSE-COMMAND-RESET-ENTRYPOINT";
            return false;
        }
        if (reset(runtime->logicalDevice(), cached.commandPool, 0u) != VK_SUCCESS) {
            diagnostics = "RHI-VULKAN-REUSE-COMMAND-RESET-FAILED";
            return false;
        }
        pool = cached.commandPool;
        buffer = cached.commandBuffer;
        cached = {};
        return true;
    }
    return false;
}

void releaseReusableCommandResources(const std::shared_ptr<VulkanRhiRuntime>& runtime,
    std::uint32_t family,
    VkCommandPool pool,
    VkCommandBuffer buffer)
{
    if (!runtime || !pool)
        return;
    if (!runtime->commandDomain().ownerDiagnostic())
        for (auto& slot : runtime->commandDomain().reusableCommands)
            if (!slot.commandPool) {
                slot = {pool, buffer, family};
                return;
            }
    destroyReusableCommandResources(*runtime, {pool, buffer, family});
}

void clearReusableCommandResources(const VulkanRhiRuntime& runtime)
{
    for (auto& cached : runtime.commandDomain().reusableCommands) {
        destroyReusableCommandResources(runtime, cached);
        cached = {};
    }
}

// One existing fixed cache, one bounded publication attempt. Atomics avoid
// reader data races; the sequence prevents a mixed frame/count/scope snapshot.
// Completion owners serialize their work; a concurrent publisher fails closed
// instead of spinning or blocking the recording/completion path.
bool publishCompletedTiming(
    const VulkanRhiRuntime& runtime, const rhi::GpuTimingSample& sample) noexcept
{
    auto& result = runtime.commandDomain().timing;
    auto sequence = result.sequence.load(std::memory_order_acquire);
    if (sample.scopeCount > rhi::GpuTimingSample::kMaxScopes || (sequence & 1u)
        || sequence > UINT64_MAX - 2u
        || !result.sequence.compare_exchange_strong(
            sequence, sequence + 1u, std::memory_order_acq_rel, std::memory_order_acquire))
        return false;
    // Pair with the reader's acquire fence through any newly observed payload
    // atomic. Seeing new fields then requires observing at least the odd
    // sequence, including on weakly ordered hosts.
    std::atomic_thread_fence(std::memory_order_release);
    result.frameSerial.store(sample.completedFrameSerial, std::memory_order_relaxed);
    result.availability.store(sample.availability, std::memory_order_relaxed);
    result.diagnostic.store(sample.diagnostic, std::memory_order_relaxed);
    for (std::uint32_t i = 0u; i < sample.scopeCount; ++i) {
        result.scopeIds[i].store(sample.scopes[i].scopeId, std::memory_order_relaxed);
        result.scopeMilliseconds[i].store(sample.scopes[i].elapsedMs, std::memory_order_relaxed);
    }
    result.scopeCount.store(sample.scopeCount, std::memory_order_relaxed);
    result.sequence.store(sequence + 2u, std::memory_order_release);
    return true;
}

void publishCompletedTimingPool(const VulkanRhiRuntime& runtime,
    const VulkanRhiQueryPoolPayload& pool,
    const std::uint64_t frameSerial,
    const std::uint64_t* fullFrameNanoseconds)
{
    auto sample = pool.completedTimingSample(runtime, frameSerial);
    if (sample.availability != rhi::GpuTimingAvailability::Available)
        return;
    // Include this command's actual full-frame result when space permits.
    // A previous command's result cannot supply this scope. All 32 names fit.
    if (fullFrameNanoseconds && sample.scopeCount < rhi::GpuTimingSample::kMaxScopes) {
        for (auto i = sample.scopeCount; i > 0u; --i)
            sample.scopes[i] = sample.scopes[i - 1u];
        sample.scopes[0] = {0u, static_cast<float>(*fullFrameNanoseconds) / 1000000.0f};
        ++sample.scopeCount;
    }
    sample.diagnostic = "Vulkan timestamp query";
    (void)publishCompletedTiming(runtime, sample);
}

void retireCompletedSubmittedCommands(const VulkanRhiRuntime& runtime)
{
    if (runtime.commandDomain().ownerDiagnostic())
        return;
    auto& commands = runtime.commandDomain().submitted;
    for (auto& command : commands) {
        if (!command || !command->submittedFenceCompleted())
            continue;
        runtime.commandDomain().completedFrameSerial =
            std::max(runtime.commandDomain().completedFrameSerial, command->observedFrameSerial());
        command.reset();
    }
}

bool retainSubmittedCommandForAsyncRetirement(const VulkanRhiRuntime& runtime,
    std::shared_ptr<VulkanRhiNativeCommandPayload> command,
    std::pmr::string& diagnostics)
{
    retireCompletedSubmittedCommands(runtime);
    auto& commands = runtime.commandDomain().submitted;
    const auto slot = std::find(commands.begin(), commands.end(), nullptr);
    if (slot == commands.end()) {
        diagnostics = "RHI-VULKAN-SUBMISSION-RETIREMENT-CAPACITY";
        return false;
    }
    *slot = std::move(command);
    return true;
}

bool asyncSubmittedCommandRetirementHasCapacity(
    const VulkanRhiRuntime& runtime, std::pmr::string& diagnostics)
{
    retireCompletedSubmittedCommands(runtime);
    auto& commands = runtime.commandDomain().submitted;
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
                if (query.recorded && query.pool) {
                    query.pool->markCompleted(impl_->device, query.queryIndex);
                }
            }
        } else if (!impl_->submitted) {
            for (const auto& query : impl_->recordedTimestampQueries) {
                if (query.recorded && query.pool && !query.originalSlot) {
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
            if (query.recorded && query.pool && !query.originalSlot) {
                query.pool->rollbackRecorded(query.queryIndex);
            }
        }
    }
    if (!impl_->submitted)
        for (auto& fault : impl_->pendingBindlessFaults)
            fault.result->abandon();
    if (!impl_->submitted && impl_->retainedResourceStates)
        impl_->retainedResourceStates->rollback();
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
    // Private cold passes reference the retained images. Destroy native pass
    // handles before cancellation drops their admitted image owners.
    if (!impl_->submitted && impl_->recordingRetention)
        (void)impl_->recordingRetention->abandonUnsubmitted();
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
        destroyReusableCommandResources(impl_->device,
            {
                impl_->commandPool,
                impl_->commandBuffer,
                impl_->device.graphicsQueueFamilyIndex(),
            });
    }
}

const std::shared_ptr<VulkanRhiRuntime>& VulkanRhiNativeCommandPayload::runtime() const noexcept
{
    return impl_->deviceOwner;
}

bool VulkanRhiNativeCommandPayload::submittedFenceCompleted()
{
    if (ordinaryTextureUploadThreadDiagnostic())
        return false;
    if (!impl_ || !impl_->submitted)
        return false;
    if (impl_->device.commandDomain().ownerDiagnostic())
        return false;
    if (impl_->submitFenceCompleted)
        return true;
    // A failed query submission has no usable fence provenance. Only this
    // device's explicit idle-retirement path may release its quarantine.
    if (impl_->originalSubmissionUnproven
        && (impl_->ordinaryJournalAliased || impl_->hasRecordedTimestampQueries()
            || impl_->internalProfiling))
        return false;
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
    if (const auto* id = ordinaryTextureUploadThreadDiagnostic()) {
        diagnostics = id;
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (const auto* id = impl_->device.commandDomain().ownerDiagnostic()) {
        diagnostics = id;
        return false;
    }
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
    if (impl_->originalSubmissionUnproven
        && (impl_->ordinaryJournalAliased || impl_->hasRecordedTimestampQueries()
            || impl_->internalProfiling)) {
        diagnostics = impl_->recordingStorage && impl_->recordingStorage->originalProgram
            ? "RHI-VULKAN-ORIGINAL-STREAM-QUEUE-QUARANTINED"
            : "RHI-VULKAN-QUERY-QUEUE-QUARANTINED";
        return false;
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
    if (ordinaryTextureUploadThreadDiagnostic())
        return;
    if (impl_->device.commandDomain().ownerDiagnostic())
        return;
    if (impl_->submitFenceCompleted)
        return;
    if ((!impl_->ordinaryTextureUploads.empty() || hasOrdinaryAttachments())
        && !impl_->originalSubmissionUnproven) {
        const char* failure = validateOrdinaryAttachmentsPending();
        for (const auto& upload : impl_->ordinaryTextureUploads) {
            if (failure)
                break;
            if (upload.publication && (failure = upload.publication->publicationThreadDiagnostic()))
                break;
            const auto& object =
                *upload.packing->originalSource()->originalDestinationLease()->originalObject();
            failure = upload.pending
                ? rhi::OriginalCommandStateTransaction::validateOrdinaryWholeUploadPending(
                      object, *impl_->retainedResourceStates, *upload.pending)
                : "RHI-ORDINARY-TEXTURE-UPLOAD-CONTENT-PENDING";
            if (failure)
                break;
        }
        if (!failure)
            for (const auto& upload : impl_->ordinaryTextureUploads) {
                const auto& object =
                    *upload.packing->originalSource()->originalDestinationLease()->originalObject();
                failure = rhi::OriginalCommandStateTransaction::completeOrdinaryWholeUpload(
                    object, *impl_->retainedResourceStates, *upload.pending);
                if (failure)
                    break;
            }
        if (!failure)
            failure = completeOrdinaryAttachments();
        if (failure) {
            rejectRecordingStorage(failure);
            (void)impl_->retainedResourceStates->invalidate();
        } else {
            impl_->retainedResourceStates->commit();
            for (const auto& upload : impl_->ordinaryTextureUploads)
                if (upload.publication)
                    upload.publication->publish();
        }
    }
    // The private bounded transfer domain retains its buffers only here.
    // Retire them before publishing completion; preserve the existing guard
    // against reentrant retirement while other native owners are released.
    // Finished ordinary wrappers alias this exact journal through the native
    // payload owner. Keep its empty object alive for that owner's full lifetime.
    if (!impl_->ordinaryJournalAliased)
        impl_->retainedResourceStates.reset();
    // Both retained commands and private ordinary cold preparation use this
    // same native owner array. Actual completion retires its objects before
    // any original source ownership can be released.
    for (std::uint32_t i = 0u; i < impl_->retainedRenderPassCount; ++i)
        destroyVulkanRhiNativeRenderPassResources(impl_->device, impl_->retainedRenderPasses[i]);
    impl_->retainedRenderPassCount = 0u;
    if (impl_->recordingRetention) {
        (void)impl_->recordingRetention->retireAfterCompletion(true);
        impl_->recordingRetention.reset();
    }
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
    std::uint64_t fullFrameNanoseconds = 0u;
    bool fullFrameAvailable = false;
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
            impl_->device.captureOriginalDeviceLoss(result);
            const auto duration = rhi::semantics::QueryLowering::timestampDuration(
                timestamps[0], timestamps[1], impl_->timestampPeriodNs);
            if (result == VK_SUCCESS && !duration.diagnostic) {
                fullFrameNanoseconds = duration.nanoseconds;
                fullFrameAvailable = true;
                if (!impl_->internalProfiling) {
                    rhi::GpuTimingSample sample;
                    sample.availability = rhi::GpuTimingAvailability::Available;
                    sample.completedFrameSerial = impl_->frameSerial;
                    sample.scopeCount = 1u;
                    sample.scopes[0] = {0u, static_cast<float>(fullFrameNanoseconds) / 1000000.0f};
                    sample.diagnostic = "Vulkan timestamp query";
                    (void)publishCompletedTiming(impl_->device, sample);
                }
            }
        }
        impl_->timestampResolved = true;
    }
    for (const auto& query : impl_->recordedTimestampQueries) {
        if (query.recorded && query.pool) {
            if (impl_->originalSubmissionUnproven)
                query.pool->markDiagnosticFailure(
                    query.queryIndex, "RHI-VULKAN-TIMESTAMP-RESULT-SUBMISSION-UNPROVEN");
            else
                query.pool->markCompleted(impl_->device, query.queryIndex);
        }
    }
    if (impl_->internalProfiling && !impl_->originalSubmissionUnproven) {
        rhi::GpuTimingSample sample;
        sample.completedFrameSerial = impl_->frameSerial;
        sample.availability = rhi::GpuTimingAvailability::Available;
        sample.diagnostic = "Vulkan budgeted command timestamps";
        if (fullFrameAvailable) {
            sample.scopes[0] = {rhi::CommandProfilingContract::wholeCommandScopeId,
                static_cast<float>(fullFrameNanoseconds) / 1000000.0f};
            sample.scopeCount = 1u;
        } else {
            impl_->internalTimingFailure = "RHI-COMMAND-PROFILING-RESULT-UNAVAILABLE";
        }
        const auto limit =
            impl_->recordingStorage->originalQueries->profilingBudget->maxPublishedScopes;
        for (const auto& pool : impl_->retainedTimingPools) {
            if (!pool || impl_->internalTimingFailure)
                break;
            const auto named = pool->completedTimingSample(impl_->device, impl_->frameSerial);
            if (named.availability != rhi::GpuTimingAvailability::Available
                || named.scopeCount > limit - sample.scopeCount) {
                impl_->internalTimingFailure = "RHI-COMMAND-PROFILING-NAMED-RESULT-UNAVAILABLE";
                break;
            }
            for (std::uint32_t i = 0u; i < named.scopeCount; ++i)
                sample.scopes[sample.scopeCount++] = named.scopes[i];
        }
        if (impl_->internalTimingFailure) {
            sample.availability = rhi::GpuTimingAvailability::DiagnosticFailure;
            sample.scopeCount = 0u;
            sample.diagnostic = impl_->internalTimingFailure;
        }
        if (!publishCompletedTiming(impl_->device, sample))
            impl_->internalTimingFailure = "RHI-COMMAND-PROFILING-PUBLICATION-CONTENTION";
    } else if (!impl_->originalSubmissionUnproven && impl_->recordingStorage
        && impl_->recordingStorage->originalProgram && impl_->recordingStorage->originalQueries) {
        // Aggregate the complete authored set once. A per-pool publication
        // would overwrite earlier pools and silently truncate the Program.
        rhi::GpuTimingSample sample;
        sample.completedFrameSerial = impl_->frameSerial;
        sample.availability = rhi::GpuTimingAvailability::Available;
        sample.diagnostic = "Vulkan mixed pass timestamps";
        const auto limit = impl_->recordingStorage->originalProgram->originalInputs()
                               ->originalBudget()
                               .passTiming->maxPublishedScopes;
        for (const auto& pool : impl_->retainedTimingPools) {
            const auto named = pool->completedTimingSample(impl_->device, impl_->frameSerial);
            if (named.availability != rhi::GpuTimingAvailability::Available
                || named.scopeCount > limit - sample.scopeCount) {
                impl_->internalTimingFailure = "RHI-MIXED-PASS-TIMING-RESULT-UNAVAILABLE";
                break;
            }
            for (std::uint32_t i = 0u; i < named.scopeCount; ++i)
                sample.scopes[sample.scopeCount++] = named.scopes[i];
        }
        if (impl_->internalTimingFailure) {
            sample.availability = rhi::GpuTimingAvailability::DiagnosticFailure;
            sample.scopeCount = 0u;
            sample.diagnostic = impl_->internalTimingFailure;
        }
        if (!publishCompletedTiming(impl_->device, sample))
            impl_->internalTimingFailure = "RHI-MIXED-PASS-TIMING-PUBLICATION-CONTENTION";
    } else if (!impl_->originalSubmissionUnproven) {
        for (const auto& pool : impl_->retainedTimingPools) {
            if (pool)
                publishCompletedTimingPool(impl_->device,
                    *pool,
                    impl_->frameSerial,
                    fullFrameAvailable ? &fullFrameNanoseconds : nullptr);
        }
    }
    impl_->retainedTimingPools.clear();
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
    if (impl_ && (payload || recordingStorageBound())) {
        (void)retainResources(&payload, 1u);
    }
}

const char* VulkanRhiNativeCommandPayload::recordingStorageDiagnostic() const noexcept
{
    if (const auto* id = ordinaryTextureUploadThreadDiagnostic())
        return id;
    if (impl_ && impl_->recordingStorageFailure)
        return impl_->recordingStorageFailure;
    return originalQuerySlotDiagnostic();
}

const char* VulkanRhiNativeCommandPayload::originalQuerySlotDiagnostic() const noexcept
{
    if (!impl_ || !impl_->recordingStorage || !impl_->recordingStorage->originalQueries)
        return nullptr;
    for (const auto& query : impl_->recordedTimestampQueries) {
        if (query.recorded)
            continue;
        if (!query.pool || !query.originalSlot)
            return "RHI-VULKAN-COLD-QUERY-SLOT-MISSING";
        if (const auto* id = query.pool->preflightRecord(*query.originalSlot))
            return id;
    }
    return nullptr;
}

bool VulkanRhiNativeCommandPayload::recordingStorageBound() const noexcept
{
    return impl_ && impl_->recordingStorage.has_value();
}

void VulkanRhiNativeCommandPayload::abandonOrdinaryRecording() noexcept
{
    if (!impl_ || !impl_->ordinaryJournalAliased || impl_->submitted)
        return;
    if (impl_->retainedResourceStates)
        impl_->retainedResourceStates->rollback();
    if (impl_->stagingUploads)
        (void)impl_->stagingUploads->rollback();
    rejectRecordingStorage("RHI-VULKAN-ORIGINAL-RECORDING-CANCELLED");
    impl_->ended = false;
}

const char* VulkanRhiNativeCommandPayload::internalTimingDiagnostic() const noexcept
{
    if (impl_ && impl_->internalTimingFailure)
        return impl_->internalTimingFailure;
    if (impl_ && impl_->internalProfiling)
        return impl_->timestampQueryPool ? nullptr : "RHI-COMMAND-PROFILING-POOL-MISSING";
    if (impl_ && impl_->recordingStorage && impl_->recordingStorage->originalQueries)
        return "RHI-VULKAN-COLD-INTERNAL-PROFILING-DISABLED";
    return impl_ && impl_->timestampQueryPool ? nullptr : "RHI-VULKAN-INTERNAL-TIMING-UNAVAILABLE";
}

const rhi::common::CommandRetentionStorage*
VulkanRhiNativeCommandPayload::queryRecordingStorage() const noexcept
{
    return impl_ && impl_->recordingStorage && impl_->recordingStorage->originalQueries
        ? impl_->recordingRetention.get()
        : nullptr;
}

void VulkanRhiNativeCommandPayload::rejectRecordingStorage(const char* diagnostic) noexcept
{
    if (impl_ && impl_->recordingStorage && !impl_->recordingStorageFailure) {
        impl_->recordingStorageFailure = diagnostic;
        impl_->recording = false;
    }
}

const char* VulkanRhiNativeCommandPayload::resourceRetentionStorageDiagnostic(
    std::size_t count) const noexcept
{
    if (const auto* id = recordingStorageDiagnostic())
        return id;
    if (impl_ && impl_->recordingStorage
        && count > impl_->recordingStorage->resourceReferences - impl_->retainedResources.size())
        return "RHI-VULKAN-COLD-RESOURCE-REFERENCE-CAPACITY";
    return nullptr;
}

const char* VulkanRhiNativeCommandPayload::recordingBarrierStorageDiagnostic(
    rhi::CommandUsageKind kind) const noexcept
{
    if (const auto* id = recordingStorageDiagnostic())
        return id;
    if (!impl_ || !impl_->recordingStorage)
        return nullptr;
    using K = rhi::CommandUsageKind;
    const auto& storage = *impl_->recordingStorage;
    switch (kind) {
    case K::BufferBarrier:
    case K::BufferMemoryBarrier:
        return impl_->recordedBufferBarriers < storage.bufferBarriers
            ? nullptr
            : "RHI-VULKAN-COLD-BUFFER-BARRIER-CAPACITY";
    case K::TextureBarrier:
        return impl_->recordedImageBarriers < storage.imageBarriers
            ? nullptr
            : "RHI-VULKAN-COLD-IMAGE-BARRIER-CAPACITY";
    case K::TextureMemoryBarrier:
    case K::AliasBarrier:
        return impl_->recordedMemoryBarriers < storage.memoryBarriers
            ? nullptr
            : "RHI-VULKAN-COLD-MEMORY-BARRIER-CAPACITY";
    default:
        return "RHI-VULKAN-COLD-BARRIER-STORAGE-KIND";
    }
}

const char* VulkanRhiNativeCommandPayload::retainResources(
    const std::shared_ptr<VulkanRhiNativeResourcePayload>* payloads, std::size_t count)
{
    if (!impl_)
        return "RHI-VULKAN-COLD-RESOURCE-PAYLOAD-MISSING";
    if (const auto* id = resourceRetentionStorageDiagnostic(count)) {
        rejectRecordingStorage(id);
        return id;
    }
    if (count && !payloads) {
        rejectRecordingStorage("RHI-VULKAN-COLD-RESOURCE-PAYLOAD-MISSING");
        return "RHI-VULKAN-COLD-RESOURCE-PAYLOAD-MISSING";
    }
    if (recordingStorageBound()) {
        for (std::size_t i = 0u; i < count; ++i) {
            if (!payloads[i]) {
                rejectRecordingStorage("RHI-VULKAN-COLD-RESOURCE-PAYLOAD-MISSING");
                return "RHI-VULKAN-COLD-RESOURCE-PAYLOAD-MISSING";
            }
        }
    }
    for (std::size_t i = 0u; i < count; ++i)
        if (payloads[i])
            impl_->retainedResources.push_back(payloads[i]);
    return nullptr;
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

void retireCompletedVulkanRhiSubmittedCommands(const VulkanRhiRuntime& runtime)
{
    if (const auto* id = runtime.commandDomain().ownerDiagnostic())
        throw core::DiagnosticError(id);
    retireCompletedSubmittedCommands(runtime);
}

const char* waitIdleAndRetireVulkanRhiSubmittedCommands(
    const std::shared_ptr<VulkanRhiRuntime>& runtime)
{
    if (!runtime || runtime->syntheticRuntime() || !runtime->logicalDevice())
        return "RHI-VULKAN-IDLE-COMPLETION-NATIVE-RUNTIME";
    if (const auto* id = runtime->commandDomain().ownerDiagnostic())
        return id;
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
    for (auto& command : runtime->commandDomain().submitted) {
        if (!command || !command->impl_->submitted
            || !core::SharedOwnerProjection::sameOwner(command->impl_->deviceOwner, runtime))
            continue;
        if (command->impl_->originalBindlessCustody
            && !rhi::semantics::OriginalCommandNativeLifecycleLowering::deviceIdleLowering(
                rhi::Backend::Vulkan, command->impl_->originalBindlessCustody->lifecycleVersion())
                .completionAvailable)
            continue;
        command->resolveSubmittedCompletion();
        runtime->commandDomain().completedFrameSerial =
            std::max(runtime->commandDomain().completedFrameSerial, command->observedFrameSerial());
        command.reset();
    }
    return nullptr;
}

void VulkanRhiNativeCommandPayload::retainTimingQueryPool(
    std::shared_ptr<VulkanRhiQueryPoolPayload> payload)
{
    if (recordingStorageBound()) {
        rejectRecordingStorage("RHI-VULKAN-COLD-QUERY-STORAGE-UNAVAILABLE");
        return;
    }
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

void waitForVulkanRhiSubmittedCommandsForResourcePressure(const VulkanRhiRuntime& runtime)
{
    if (const auto* id = runtime.commandDomain().ownerDiagnostic())
        throw core::DiagnosticError(id);
    for (const auto& command : runtime.commandDomain().submitted) {
        if (!command) {
            continue;
        }
        std::pmr::string diagnostics;
        command->waitSubmittedFence(diagnostics);
    }
    retireCompletedSubmittedCommands(runtime);
}

bool drainVulkanRhiSubmittedCommandsUntil(const VulkanRhiRuntime& runtime,
    std::chrono::steady_clock::time_point deadline,
    std::pmr::string& diagnostics)
{
    if (const auto* id = runtime.commandDomain().ownerDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (deadline == std::chrono::steady_clock::time_point{}) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-INVALID";
        return false;
    }
    if (deadline <= std::chrono::steady_clock::now()) {
        diagnostics = "RHI-VULKAN-COMPLETION-DEADLINE-EXPIRED";
        return false;
    }
    retireCompletedSubmittedCommands(runtime);
    for (const auto& command : runtime.commandDomain().submitted) {
        if (command && !command->waitSubmittedFenceUntil(deadline, diagnostics)) {
            retireCompletedSubmittedCommands(runtime);
            return false;
        }
    }
    retireCompletedSubmittedCommands(runtime);
    if (std::any_of(runtime.commandDomain().submitted.begin(),
            runtime.commandDomain().submitted.end(),
            [](const auto& command) {
                return bool(command);
            })) {
        diagnostics = "RHI-VULKAN-SHUTDOWN-DRAIN-INCOMPLETE";
        return false;
    }
    diagnostics.clear();
    return true;
}

void drainVulkanRhiSubmittedCommandsForShutdown(const VulkanRhiRuntime& runtime)
{
    if (runtime.commandDomain().ownerDiagnostic())
        return;
    retireCompletedSubmittedCommands(runtime);
    // Device-wrapper destruction is not completion for any submitted stream.
    // Retain every remaining native closure and charge until its real issuer
    // proves retirement; the independent deadline API reports unresolved work.
    for (const auto& command : runtime.commandDomain().submitted)
        if (command)
            return;
    clearReusableFences(runtime);
    clearReusableCommandResources(runtime);
}

std::uint64_t completedVulkanRhiSubmittedFrameSerial(const VulkanRhiRuntime& runtime)
{
    retireCompletedVulkanRhiSubmittedCommands(runtime);
    return runtime.commandDomain().completedFrameSerial;
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
    if (const auto* id = recordingStorageDiagnostic()) {
        diagnostics = id;
        return false;
    }
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
    if (const auto* id = recordingBarrierStorageDiagnostic(rhi::CommandUsageKind::BufferBarrier)) {
        rejectRecordingStorage(id);
        diagnostics = id;
        return false;
    }
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
    ++impl_->recordedBufferBarriers;
    if (impl_->stats) {
        ++impl_->stats->nativeCommandBufferRecordCount;
        ++impl_->stats->nativeBufferBarrierRecordCount;
    }
    return true;
}

bool VulkanRhiNativeCommandPayload::recordAliasingBarrier(
    const rhi::AliasingBarrierDesc& desc, std::pmr::string& diagnostics)
{
    if (const auto* id = recordingBarrierStorageDiagnostic(rhi::CommandUsageKind::AliasBarrier)) {
        rejectRecordingStorage(id);
        diagnostics = id;
        return false;
    }
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
    ++impl_->recordedMemoryBarriers;
    if (impl_->stats)
        ++impl_->stats->nativeCommandBufferRecordCount;
    return true;
}

bool VulkanRhiNativeCommandPayload::recordResourceMemoryBarrier(
    const rhi::ResourceMemoryBarrierDesc& desc, std::pmr::string& diagnostics)
{
    if (const auto* id =
            recordingBarrierStorageDiagnostic(rhi::CommandUsageKind::TextureMemoryBarrier)) {
        rejectRecordingStorage(id);
        diagnostics = id;
        return false;
    }
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
    ++impl_->recordedMemoryBarriers;
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
    if (const auto* id = recordingBarrierStorageDiagnostic(rhi::CommandUsageKind::TextureBarrier)) {
        rejectRecordingStorage(id);
        diagnostics = id;
        return false;
    }
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
    ++impl_->recordedImageBarriers;
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
    if (recordingStorageBound()) {
        rejectRecordingStorage(VulkanRhiRecordingStorageLimits::unsupportedStagingPoolDiagnostic());
        diagnostics.clear();
        return false;
    }
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

namespace {
rhi::UploadTextureDesc ordinaryPackedMip(
    const rhi::semantics::TextureUploadPackedMip& mip, rhi::TextureFormat format)
{
    rhi::UploadTextureDesc result;
    result.format = format;
    result.extent = mip.extent;
    result.destinationOffset = mip.origin;
    result.subresources = mip.range;
    result.bytesPerRow = mip.packedBytesPerRow;
    result.rowsPerImage = mip.rowCount;
    result.pixelDataSize = mip.packedSize;
    return result;
}
} // namespace

bool VulkanRhiNativeCommandPayload::acceptOrdinaryRecordedOwnership() const noexcept
{
    if (!impl_ || !impl_->retainedResourceStates)
        return false;
    if (!impl_->ordinaryTextureUploads.empty() || hasOrdinaryAttachments())
        return impl_->submitted && !impl_->originalSubmissionUnproven;
    impl_->retainedResourceStates->commit();
    return true;
}

bool VulkanRhiNativeCommandPayload::hasOrdinaryTextureUploads() const noexcept
{
    return impl_ && !impl_->ordinaryTextureUploads.empty();
}

const char* VulkanRhiNativeCommandPayload::ordinaryTextureUploadThreadDiagnostic() const noexcept
{
    if (impl_)
        for (const auto& upload : impl_->ordinaryTextureUploads)
            if (upload.publication)
                if (const auto* id = upload.publication->publicationThreadDiagnostic())
                    return id;
    return nullptr;
}

bool VulkanRhiNativeCommandPayload::prepareOrdinaryTextureUploads(rhi::GpuDevice& device,
    VulkanRhiStagingUploadPool& pool,
    rhi::RecordedStagingUploadJournal& journal,
    std::uint64_t serial,
    const rhi::OriginalOrdinaryTextureUploadContract::Budget& budget,
    const rhi::OriginalOrdinaryTextureUploadContract::Binding* bindings,
    std::size_t count,
    core::pmr::String& diagnostic,
    const rhi::OriginalTextureUploadPreparationRequest* publicRequest,
    const std::shared_ptr<core::CpuMemoryQuota>& publicationQuota,
    core::pmr::Vector<std::shared_ptr<const rhi::OriginalTextureUploadPublication>>* publications)
{
    const auto reject = [&](const char* id) {
        diagnostic = id;
        return false;
    };
    if (!impl_ || !impl_->recording || !impl_->recordingStorage
        || !impl_->recordingStorage->originalProgram || !impl_->ordinaryJournalAliased
        || !impl_->retainedResourceStates || !impl_->retainedResourceStates->empty()
        || !impl_->ordinaryTextureUploads.empty())
        return reject("RHI-ORDINARY-TEXTURE-UPLOAD-COLD-OWNER");
    const auto& program = impl_->recordingStorage->originalProgram;
    if (const auto* id =
            rhi::OriginalOrdinaryTextureUploadContract::inspect(program, budget, bindings, count))
        return reject(id);
    impl_->ordinaryTextureUploads.reserve(count);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& binding = bindings[i];
        const auto& object =
            *binding.packing->originalSource()->originalDestinationLease()->originalObject();
        const auto content = object.observeOriginalContent();
        const auto state = object.observeOriginalCommittedState(
            rhi::defaultTextureSubresourceRange(object.desc()));
        if (!content || !state || state->state() != rhi::ResourceState::Undefined)
            return reject("RHI-ORDINARY-TEXTURE-UPLOAD-FRESH-DESTINATION");
        if (const auto* id = rhi::OriginalCommandStateTransaction::preflightOrdinaryWholeUpload(
                object, *content, *impl_->retainedResourceStates))
            return reject(id);
        impl_->ordinaryTextureUploads.push_back(
            {binding.commandIndex, binding.packing, *content, *state, {}, {}, {}});
        if (publicRequest) {
            core::CpuMemoryQuotaScope publicationScope(publicationQuota);
            impl_->ordinaryTextureUploads.back().publication =
                rhi::OriginalTextureUploadPublication::issue(program, binding);
        }
    }
    const rhi::OriginalTexturePoolInitializationBudget initialization{
        2u, true, budget.maxPackedBytes, budget.maxMipMappings};
    for (auto& upload : impl_->ordinaryTextureUploads) {
        upload.slice = pool.initializeOriginalTexture(program->originalInputs(),
            program->originalBudget(),
            upload.commandIndex,
            upload.packing,
            initialization,
            journal,
            serial,
            &device);
        if (!upload.slice.success)
            return reject(upload.slice.storageDiagnostic
                    ? upload.slice.storageDiagnostic
                    : "RHI-ORDINARY-TEXTURE-UPLOAD-POOL-INITIALIZATION");
        const auto& object =
            *upload.packing->originalSource()->originalDestinationLease()->originalObject();
        const auto native = vulkanRhiNativePayload(object);
        if (!native)
            return reject("RHI-ORDINARY-TEXTURE-UPLOAD-DESTINATION-PAYLOAD");
        for (const auto& mip : upload.packing->packing()->mipLayouts()) {
            const auto desc = ordinaryPackedMip(mip, object.desc().format);
            if (!recordValidatedOriginalTextureCopy(upload.slice.buffer,
                    upload.slice.offsetBytes + mip.packedOffset,
                    *native,
                    desc,
                    diagnostic,
                    true,
                    false))
                return reject(recordingStorageDiagnostic());
        }
    }
    if (publications)
        for (const auto& upload : impl_->ordinaryTextureUploads)
            publications->push_back(upload.publication);
    return true;
}

const char* VulkanRhiNativeCommandPayload::preflightOrdinaryTextureTransition(
    const rhi::GpuTexture& object,
    const rhi::ResourceBarrierDesc& desc,
    std::uint32_t commandIndex) const noexcept
{
    if (hasOrdinaryAttachments())
        return preflightOrdinaryAttachmentTransition(object, desc, commandIndex);
    if (!hasOrdinaryTextureUploads())
        return nullptr;
    if (!object.originalContentExclusive())
        return nullptr;
    if (desc.textureSubresources != rhi::defaultTextureSubresourceRange(object.desc()))
        return "RHI-ORDINARY-TEXTURE-UPLOAD-TRANSITION-RANGE";
    if (!impl_->retainedResourceStates
        || !vulkanRhiTextureCanTransition(object, *impl_->retainedResourceStates)
        || object.state() != desc.before)
        return "RHI-ORDINARY-TEXTURE-UPLOAD-TRANSITION-STATE";
    const auto* input =
        impl_->recordingStorage->originalProgram->originalInputs()->descriptorAt(commandIndex);
    const auto* expected = input ? std::get_if<rhi::ResourceDependencyArguments>(input) : nullptr;
    if (!expected || !expected->matches(rhi::CommandUsageKind::TextureBarrier, desc))
        return "RHI-ORDINARY-TEXTURE-UPLOAD-TRANSITION-JOIN";
    for (const auto& upload : impl_->ordinaryTextureUploads) {
        if (upload.packing->originalSource()->originalDestinationLease()->originalObject()
            != &object)
            continue;
        if (upload.pending)
            return rhi::OriginalCommandStateTransaction::validateOrdinaryWholeUploadPending(
                object, *impl_->retainedResourceStates, *upload.pending);
        if (const auto* id = object.validateOriginalCommittedState(upload.initialState))
            return id;
        if (!(desc.before == rhi::ResourceState::Undefined
                && desc.after == rhi::ResourceState::CopyDst
                && desc.textureSubresources == rhi::defaultTextureSubresourceRange(object.desc())))
            return "RHI-ORDINARY-TEXTURE-UPLOAD-INITIAL-TRANSITION";
        return rhi::OriginalCommandStateTransaction::preflightOrdinaryWholeUpload(
            object, upload.initial, *impl_->retainedResourceStates);
    }
    return "RHI-ORDINARY-TEXTURE-UPLOAD-TRANSITION-DESTINATION";
}

bool VulkanRhiNativeCommandPayload::authorizesOrdinaryTextureTransition(
    const rhi::GpuTexture& object,
    const rhi::ResourceBarrierDesc& desc,
    const core::MutationJournal* journal) const noexcept
{
    if (hasOrdinaryAttachments())
        return journal && journal == impl_->retainedResourceStates.get()
            && impl_->ordinaryAttachmentClaimed && !validateOrdinaryAttachmentsPending()
            && !preflightOrdinaryAttachmentTransition(object,
                desc,
                static_cast<std::uint32_t>(impl_->ordinaryAttachmentMeter->commandCount()));
    if (!hasOrdinaryTextureUploads() || !impl_->recording || !journal
        || journal != impl_->retainedResourceStates.get()
        || desc.textureSubresources != rhi::defaultTextureSubresourceRange(object.desc()))
        return false;
    for (const auto& upload : impl_->ordinaryTextureUploads) {
        if (upload.packing->originalSource()->originalDestinationLease()->originalObject()
            != &object)
            continue;
        if (upload.pending)
            return !rhi::OriginalCommandStateTransaction::validateOrdinaryWholeUploadPending(
                object, *impl_->retainedResourceStates, *upload.pending);
        return !object.validateOriginalCommittedState(upload.initialState)
            && desc.before == rhi::ResourceState::Undefined
            && desc.after == rhi::ResourceState::CopyDst;
    }
    return false;
}

const char* VulkanRhiNativeCommandPayload::recordOrdinaryTextureUpload(
    std::uint32_t commandIndex, const rhi::UploadTextureDesc& desc) noexcept
{
    if (!hasOrdinaryTextureUploads() || !impl_->recording || !impl_->retainedResourceStates)
        return "RHI-ORDINARY-TEXTURE-UPLOAD-RECORDING-OWNER";
    for (auto& upload : impl_->ordinaryTextureUploads) {
        if (upload.commandIndex != commandIndex)
            continue;
        const auto& object =
            *upload.packing->originalSource()->originalDestinationLease()->originalObject();
        if (upload.pending || desc.destinationTexture != object.id()
            || !upload.packing->originalInput()->matches(desc))
            return "RHI-ORDINARY-TEXTURE-UPLOAD-INPUT-JOIN";
        if (vulkanRhiTextureStateCommitted(object)
            || !vulkanRhiTextureStateAvailable(
                object, desc.subresources, impl_->retainedResourceStates.get())
            || object.state() != rhi::ResourceState::CopyDst)
            return "RHI-ORDINARY-TEXTURE-UPLOAD-DESTINATION-STATE";
        if (const auto* id = rhi::OriginalCommandStateTransaction::preflightOrdinaryWholeUpload(
                object, upload.initial, *impl_->retainedResourceStates))
            return id;
        const auto native = vulkanRhiNativePayload(object);
        core::pmr::String diagnostic;
        if (!native)
            return "RHI-ORDINARY-TEXTURE-UPLOAD-DESTINATION-PAYLOAD";
        for (const auto& mip : upload.packing->packing()->mipLayouts()) {
            const auto packed = ordinaryPackedMip(mip, object.desc().format);
            if (!recordValidatedOriginalTextureCopy(upload.slice.buffer,
                    upload.slice.offsetBytes + mip.packedOffset,
                    *native,
                    packed,
                    diagnostic,
                    true,
                    false))
                return "RHI-ORDINARY-TEXTURE-UPLOAD-NATIVE-PREFLIGHT";
        }
        if (const auto* id = rhi::OriginalCommandStateTransaction::proposeOrdinaryWholeUpload(
                object, upload.initial, *impl_->retainedResourceStates, upload.pending))
            return id;
        retainResource(native);
        for (const auto& mip : upload.packing->packing()->mipLayouts()) {
            const auto packed = ordinaryPackedMip(mip, object.desc().format);
            if (!recordValidatedOriginalTextureCopy(upload.slice.buffer,
                    upload.slice.offsetBytes + mip.packedOffset,
                    *native,
                    packed,
                    diagnostic,
                    false,
                    true))
                return "RHI-ORDINARY-TEXTURE-UPLOAD-NATIVE-EMISSION";
        }
        return nullptr;
    }
    return "RHI-ORDINARY-TEXTURE-UPLOAD-ORDINAL";
}

bool VulkanRhiNativeCommandPayload::recordUploadStagingTextureCopy(VkBuffer stagingBuffer,
    std::uint64_t stagingOffsetBytes,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::UploadTextureDesc& desc,
    std::pmr::string& diagnostics)
{
    if (recordingStorageBound()) {
        rejectRecordingStorage(VulkanRhiRecordingStorageLimits::unsupportedStagingPoolDiagnostic());
        diagnostics.clear();
        return false;
    }
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
    core::pmr::String& diagnostics,
    bool coldPreflight,
    bool coldEmission)
{
    const auto rejectCopy = [&](const char* text) {
        if (coldPreflight || coldEmission) {
            rejectRecordingStorage("RHI-ORDINARY-TEXTURE-UPLOAD-NATIVE-COPY");
            diagnostics.clear();
        } else {
            diagnostics = text;
        }
        return false;
    };
    if (recordingStorageBound() && !coldPreflight && !coldEmission) {
        rejectRecordingStorage(VulkanRhiRecordingStorageLimits::unsupportedStagingPoolDiagnostic());
        diagnostics.clear();
        return false;
    }
    if (impl_ && impl_->conformanceClear) {
        if (coldPreflight || coldEmission)
            return rejectCopy("Original upload cannot use conformance-clear recording");
        return impl_->conformanceClear->reject(diagnostics);
    }
    if (!impl_ || !impl_->recording) {
        return rejectCopy("Vulkan RHI native command buffer is not recording");
    }
    if (desc.pixelDataSize == 0 || desc.extent.width == 0 || desc.extent.height == 0
        || desc.extent.depth == 0) {
        return rejectCopy("Vulkan RHI native texture upload requires non-zero data and extent");
    }
    if (desc.bytesPerRow == 0 || desc.rowsPerImage == 0) {
        return rejectCopy("Vulkan RHI native texture upload requires explicit row/slice pitch");
    }
    if ((desc.bytesPerRow % 256u) != 0u) {
        return rejectCopy("Vulkan RHI native texture upload row pitch must be 256-byte aligned");
    }
    if (impl_->device.syntheticRuntime()) {
        if (coldPreflight || coldEmission)
            return rejectCopy("Original upload requires a genuine native runtime");
        if (impl_->stats) {
            ++impl_->stats->nativeCommandBufferRecordCount;
            ++impl_->stats->nativeBufferToImageCopyRecordCount;
        }
        return true;
    }
    const auto destinationHandle = destination.backendVulkanImageHandle();
    if (!stagingBuffer || !destinationHandle) {
        return rejectCopy(
            "Vulkan RHI native texture upload requires staging buffer and destination texture");
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
            return rejectCopy(
                "Vulkan RHI native BC texture upload requires bytesPerRow aligned to block size");
        }
    } else {
        const auto texelBytes = bytesPerPixelForVulkanCopy(vkFormat);
        if (texelBytes == 0u) {
            return rejectCopy("Vulkan RHI native texture upload requires known texture format");
        }
        if (desc.bytesPerRow % texelBytes != 0u) {
            return rejectCopy(
                "Vulkan RHI native texture upload requires bytesPerRow aligned to texel size");
        }
        rowLengthTexels = desc.bytesPerRow / texelBytes;
    }
    auto* cmdCopyBufferToImage = functionFrom<PFN_vkCmdCopyBufferToImage>(
        impl_->device.getDeviceProcAddr("vkCmdCopyBufferToImage"));
    if (!cmdCopyBufferToImage) {
        return rejectCopy("Vulkan RHI native command buffer missing vkCmdCopyBufferToImage");
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
    if (coldPreflight)
        return true;
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
    return recordImageToBufferCopyImpl(source, destination, desc, diagnostics, false);
}

bool VulkanRhiNativeCommandPayload::recordImageToBufferCopyImpl(
    const VulkanRhiNativeResourcePayload& source,
    const VulkanRhiNativeResourcePayload& destination,
    const rhi::TextureToBufferCopyDesc& desc,
    core::pmr::String& diagnostics,
    bool validatedOriginal)
{
    if (impl_ && impl_->conformanceClear && !impl_->conformanceClear->internalCopy_)
        return impl_->conformanceClear->reject(diagnostics);
    if (source.backendVulkanOriginalContentExclusive() && !validatedOriginal) {
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
    if (recordingStorageBound()) {
        rejectRecordingStorage(VulkanRhiRecordingStorageLimits::unsupportedDebugLabelDiagnostic());
        diagnostics.clear();
        return false;
    }
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
    if (recordingStorageBound()) {
        rejectRecordingStorage(VulkanRhiRecordingStorageLimits::unsupportedDebugLabelDiagnostic());
        diagnostics.clear();
        return false;
    }
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
    if (recordingStorageBound()) {
        rejectRecordingStorage(VulkanRhiRecordingStorageLimits::unsupportedDebugLabelDiagnostic());
        diagnostics.clear();
        return false;
    }
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

bool VulkanRhiNativeCommandPayload::adoptOriginalStreamJournal(
    core::UniquePtr<core::MutationJournal>& journal,
    core::pmr::String& diagnostic,
    std::uint64_t identity)
{
    if (!impl_ || !impl_->recordingStorage || !impl_->recordingStorage->originalProgram
        || (impl_->recordingStorage->originalQueries
            && impl_->recordingStorage->originalQueries->recordingContractVersion != 2u)
        || !journal || !journal->empty() || impl_->retainedResourceStates
        || impl_->ordinaryJournalAliased) {
        diagnostic = "RHI-VULKAN-ORIGINAL-STREAM-JOURNAL";
        return false;
    }
    if (const auto* id = impl_->recordingStorage->diagnostic()) {
        diagnostic = id;
        return false;
    }
    if (!prepareOriginalSubmissionFence(diagnostic))
        return false;
    if (impl_->recordingStorage->originalQueries) {
        const auto* id = identity ? impl_->device.timestampQueryDiagnostic()
                                  : "RHI-MIXED-PASS-TIMING-RECORDING-IDENTITY";
        if (id) {
            diagnostic = id;
            return false;
        }
        impl_->originalQueryRecordingIdentity = identity;
        impl_->originalQueryFallback = rhi::FallbackMode::Native;
    }
    impl_->retainedResourceStates = std::move(journal);
    impl_->ordinaryJournalAliased = true;
    return true;
}

bool VulkanRhiNativeCommandPayload::adoptOriginalQueryJournal(
    core::UniquePtr<core::MutationJournal>& journal,
    std::uint64_t identity,
    const rhi::semantics::NativeCommandLoweringDecision& approved,
    core::pmr::String& diagnostic)
{
    if (!impl_ || !impl_->recordingStorage || !impl_->recordingStorage->originalQueries || !identity
        || !journal || !journal->empty() || impl_->retainedResourceStates
        || impl_->originalQueryRecordingIdentity) {
        diagnostic = "RHI-VULKAN-ORIGINAL-QUERY-JOURNAL";
        return false;
    }
    const auto& query = *impl_->recordingStorage->originalQueries;
    const auto use = rhi::semantics::validateNativeCommandLoweringUse(approved,
        rhi::semantics::NativeCommandSemantic::VulkanTimestampQuery,
        rhi::FallbackMode::Native,
        "original query journal");
    if (!use.success) {
        diagnostic = use.diagnostics;
        return false;
    }
    const auto lowering = rhi::semantics::QueryLowering::originalRecording(
        rhi::Backend::Vulkan, query.recordingContractVersion, query.recordingOptIn);
    if (!lowering.privateRecordingAvailable || lowering.diagnostic) {
        diagnostic = lowering.diagnostic;
        return false;
    }
    const char* id = nullptr;
    auto meter = rhi::CommandRecordingMeter::prepare(query.commandBudget, id);
    if (!meter) {
        diagnostic = id;
        return false;
    }
    if (const auto* error = originalQuerySlotDiagnostic()) {
        diagnostic = error;
        return false;
    }
    if (!prepareOriginalSubmissionFence(diagnostic))
        return false;
    impl_->originalQueryMeter = std::move(meter);
    impl_->retainedResourceStates = std::move(journal);
    impl_->ordinaryJournalAliased = true;
    impl_->originalQueryRecordingIdentity = identity;
    impl_->originalQueryFallback = approved.fallbackMode;
    return true;
}

const char* VulkanRhiNativeCommandPayload::recordOriginalTimestamp(const rhi::QueryPool& pool,
    std::uint32_t index,
    std::uint32_t ordinal,
    std::uint64_t identity) noexcept
{
    if (impl_ && !impl_->originalQueryRecordingIdentity && recordingStorageBound())
        return "RHI-VULKAN-COLD-QUERY-STORAGE-UNAVAILABLE";
    if (!impl_ || !impl_->recording || !identity
        || identity != impl_->originalQueryRecordingIdentity || !impl_->retainedResourceStates
        || !impl_->recordingRetention || (!impl_->originalQueryMeter && !hasOrdinaryMixedPasses())
        || impl_->originalQueryFallback != rhi::FallbackMode::Native)
        return "RHI-VULKAN-ORIGINAL-QUERY-JOURNAL";
    if (const auto* id = recordingStorageDiagnostic())
        return id;
    if (ordinal == UINT32_MAX)
        return "RHI-VULKAN-ORIGINAL-QUERY-ORDINAL";
    const auto& config = *impl_->recordingStorage->originalQueries;
    if (const auto* id = rhi::OriginalCommandInputs::validateOriginal(config.inputs))
        return id;
    if (impl_->recordingRetention->quota()->limitBytes() != config.budget.maxCpuMetadataBytes)
        return "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
    const auto* descriptor = config.inputs->descriptorAt(ordinal);
    const auto* binding =
        descriptor ? std::get_if<rhi::OriginalCommandQueryBinding>(descriptor) : nullptr;
    if (!binding || binding->kind() != rhi::CommandUsageKind::Timestamp
        || binding->original.scalar().get<rhi::CommandScalarArguments::Query>()->index != index)
        return "RHI-VULKAN-ORIGINAL-QUERY-ORDINAL";
    const auto& sources =
        config.inputs->originalScalars()->originalUsage()->originalSources()->sources();
    const auto& source = std::get<std::shared_ptr<const rhi::OriginalCommandQueryPoolSource>>(
        sources[binding->sourceIndex]);
    const auto* nativePool = asVulkanRhiQueryPool(pool);
    if (source->originalObject() != &pool || !nativePool || !nativePool->payload()
        || !nativePool->payload()->belongsTo(impl_->device))
        return "RHI-VULKAN-ORIGINAL-QUERY-POOL";
    if (const auto* id = nativePool->validateOriginalInput(binding->original))
        return id;
    auto* prepared = static_cast<Impl::RecordedTimestampQuery*>(nullptr);
    for (auto& query : impl_->recordedTimestampQueries)
        if (query.commandOrdinal == ordinal) {
            prepared = &query;
            break;
        }
    if (!prepared || prepared->recorded || !prepared->originalSlot
        || prepared->pool != nativePool->payload() || prepared->queryIndex != index)
        return "RHI-VULKAN-ORIGINAL-QUERY-RECORD";
    rhi::CommandRecordingUsage increment;
    increment.append(rhi::CommandUsageKind::Timestamp);
    increment.resourceReferences = 1u;
    const bool mixed = bool(impl_->recordingStorage->originalProgram);
    if (mixed) {
        if (!impl_->ordinaryMixedPasses
            || (!impl_->ordinaryComputeActive && !impl_->activeRenderPass.renderPass)
            || impl_->ordinaryTransferActive)
            return "RHI-MIXED-PASS-TIMING-PASS-STATE";
        if (const auto* id =
                preflightOrdinaryAttachmentOperation(rhi::CommandUsageKind::Timestamp, ordinal))
            return id;
        if (const auto* id = validateOrdinaryAttachmentsPending())
            return id;
    } else if (const auto* id = impl_->originalQueryMeter->preflight(increment))
        return id;
    const rhi::common::CommandRetainedQueryRecord record{prepared->pool, index};
    if (const auto* id = impl_->recordingRetention->preflightTimestamp(record))
        return id;
    bool retained = false;
    for (const auto& owner : impl_->retainedTimingPools)
        retained = retained || owner == prepared->pool;
    if (!retained && impl_->retainedTimingPools.size() == impl_->retainedTimingPools.capacity())
        return "RHI-VULKAN-COLD-QUERY-POOL-CAPACITY";
    if (const auto* id = impl_->device.timestampQueryDiagnostic())
        return id;
    const auto write = functionFrom<PFN_vkCmdWriteTimestamp>(
        impl_->device.getDeviceProcAddr("vkCmdWriteTimestamp"));
    if (!write)
        return "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS";
    // The slot rechecks its original epoch under its lock before the first
    // mutation. The common/native arrays are private to this serialized owner;
    // their successfully preflighted commits cannot allocate or change mapping.
    if (const auto* id = prepared->pool->declareRecorded(
            *prepared->originalSlot, *impl_->retainedResourceStates))
        return id;
    (void)impl_->recordingRetention->admitTimestamp(record);
    if (mixed)
        consumeOrdinaryAttachmentOperation();
    else
        (void)impl_->originalQueryMeter->consume(increment);
    if (!retained)
        impl_->retainedTimingPools.push_back(prepared->pool);
    prepared->recorded = true;
    write(impl_->commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        nativePool->nativeHandle(),
        index);
    if (impl_->stats)
        ++impl_->stats->nativeCommandBufferRecordCount;
    return nullptr;
}

const char* VulkanRhiNativeCommandPayload::sealOriginalQueries(std::uint32_t commandCount) noexcept
{
    if (!impl_ || !impl_->originalQueryRecordingIdentity)
        return nullptr;
    const auto& inputs = *impl_->recordingStorage->originalQueries->inputs;
    if (commandCount == UINT32_MAX
        || inputs.originalScalars()->originalUsage()->commands().size() != commandCount + 1u)
        return "RHI-VULKAN-ORIGINAL-QUERY-FINISH-ORDINAL";
    for (const auto& query : impl_->recordedTimestampQueries)
        if (!query.recorded)
            return "RHI-VULKAN-ORIGINAL-QUERY-INCOMPLETE";
    return impl_->recordingRetention->sealForSubmission()
        ? nullptr
        : "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
}

bool VulkanRhiNativeCommandPayload::recordTimestamp(
    const rhi::QueryPool& queryPool, const std::uint32_t queryIndex, std::pmr::string& diagnostics)
{
    if (recordingStorageBound()) {
        rejectRecordingStorage("RHI-VULKAN-COLD-QUERY-STORAGE-UNAVAILABLE");
        diagnostics = recordingStorageDiagnostic();
        return false;
    }
    if (impl_ && impl_->conformanceClear)
        return impl_->conformanceClear->reject(diagnostics);
    if (!impl_ || !impl_->recording) {
        diagnostics = "Vulkan timestamp write requires an active native command buffer";
        return false;
    }
    const auto* nativePool = asVulkanRhiQueryPool(queryPool);
    if (!nativePool || queryIndex >= nativePool->count() || !nativePool->payload()
        || !nativePool->payload()->belongsTo(impl_->device)
        || nativePool->nativeHandle() == VK_NULL_HANDLE) {
        diagnostics = "Vulkan timestamp write received an incompatible query pool or index";
        return false;
    }
    if (const auto* id = impl_->device.timestampQueryDiagnostic()) {
        diagnostics = id;
        return false;
    }
    const auto write = functionFrom<PFN_vkCmdWriteTimestamp>(
        impl_->device.getDeviceProcAddr("vkCmdWriteTimestamp"));
    if (!write) {
        diagnostics = "Vulkan timestamp write entrypoint is unavailable";
        return false;
    }
    // Ordinary metadata is still unbounded. Reserve before changing query
    // state; a failed allocation must not leave a Recorded slot without work.
    try {
        impl_->retainedTimingPools.reserve(impl_->retainedTimingPools.size() + 1u);
        impl_->recordedTimestampQueries.reserve(impl_->recordedTimestampQueries.size() + 1u);
    } catch (const std::bad_alloc&) {
        diagnostics = "RHI-VULKAN-TIMESTAMP-RECORD-ALLOCATION-FAILED";
        return false;
    } catch (const std::length_error&) {
        diagnostics = "RHI-VULKAN-TIMESTAMP-RECORD-LENGTH-INVALID";
        return false;
    }
    if (!nativePool->payload()->markRecorded(queryIndex, diagnostics)) {
        return false;
    }
    const auto nativeHandle = nativePool->nativeHandle();
    retainTimingQueryPool(nativePool->payload());
    impl_->recordedTimestampQueries.push_back({nativePool->payload(), queryIndex, {}, true});
    write(impl_->commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, nativeHandle, queryIndex);
    return true;
}

bool VulkanRhiNativeCommandPayload::end(std::pmr::string& diagnostics)
{
    const auto refuseProfiling = [&](const char* id) {
        if (impl_ && impl_->internalProfiling) {
            impl_->internalTimingFailure = id;
            rejectRecordingStorage(id);
        } else {
            diagnostics = id;
        }
        return false;
    };
    if (const auto* id = recordingStorageDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (!impl_) {
        diagnostics = "Vulkan RHI native command buffer payload is missing";
        return false;
    }
    if (impl_->ended) {
        return true;
    }
    if (hasOrdinaryAttachments() && !impl_->ordinaryAttachmentSealed) {
        rejectRecordingStorage("RHI-ORDINARY-ATTACHMENT-RECORDING-INCOMPLETE");
        return false;
    }
    for (const auto& upload : impl_->ordinaryTextureUploads) {
        if (!upload.pending) {
            rejectRecordingStorage("RHI-ORDINARY-TEXTURE-UPLOAD-INCOMPLETE");
            diagnostics.clear();
            return false;
        }
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
    auto* endCommandBuffer =
        functionFrom<PFN_vkEndCommandBuffer>(impl_->device.getDeviceProcAddr("vkEndCommandBuffer"));
    if (!endCommandBuffer) {
        if (impl_->internalProfiling)
            return refuseProfiling("RHI-VULKAN-COMMAND-END-ENTRYPOINT");
        diagnostics = "Vulkan RHI native command buffer missing vkEndCommandBuffer";
        return false;
    }
    if (impl_->timestampQueryPool) {
        if (impl_->internalProfiling) {
            if (const auto* id = impl_->device.timestampQueryDiagnostic()) {
                return refuseProfiling(id);
            }
        }
        if (auto* writeTimestamp = functionFrom<PFN_vkCmdWriteTimestamp>(
                impl_->device.getDeviceProcAddr("vkCmdWriteTimestamp"))) {
            writeTimestamp(impl_->commandBuffer,
                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                impl_->timestampQueryPool,
                1u);
        } else if (impl_->internalProfiling) {
            return refuseProfiling("RHI-VULKAN-TIMESTAMP-ENTRYPOINTS");
        }
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
        if (impl_->internalProfiling) {
            impl_->device.captureOriginalDeviceLoss(ended);
            return refuseProfiling(ended == VK_ERROR_DEVICE_LOST
                    ? "RHI-COMMAND-PROFILING-END-DEVICE-LOST"
                    : "RHI-COMMAND-PROFILING-END-FAILED");
        }
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
    if (acquireReusableFence(impl_->deviceOwner, impl_->submitFence, diagnostic))
        return true;
    if (!diagnostic.empty())
        return false;
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
    if (const auto* id = ordinaryTextureUploadThreadDiagnostic()) {
        if (acceptedThisAttempt)
            *acceptedThisAttempt = false;
        diagnostics = id;
        return false;
    }
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
    if ((impl_->internalProfiling
            || (impl_->recordingStorage && impl_->recordingStorage->originalQueries
                && impl_->recordingStorage->originalQueries->recordingContractVersion == 2u))
        && !impl_->frameSerial) {
        diagnostics = "RHI-COMMAND-PROFILING-FRAME-SERIAL";
        return false;
    }
    if (impl_->recordingRetention) {
        const auto& storage = *impl_->recordingRetention;
        if (storage.quota()->limitBytes() != storage.budget().maxCpuMetadataBytes) {
            diagnostics = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
            return false;
        }
        if (storage.state() != rhi::common::CommandRetentionStorage::State::Sealed) {
            diagnostics = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
            return false;
        }
        if (!impl_->retainedResourceStates || !impl_->retainedResourceStates->canInvalidate()) {
            diagnostics = "RHI-VULKAN-RETENTION-STATE-INVALIDATION-MISSING";
            return false;
        }
    }
    if (impl_->ordinaryJournalAliased
        && (!impl_->retainedResourceStates || !impl_->retainedResourceStates->canInvalidate())) {
        diagnostics = "RHI-VULKAN-RETENTION-STATE-INVALIDATION-MISSING";
        return false;
    }
    if (const auto* id = validateOrdinaryAttachmentsPending()) {
        diagnostics = id;
        return false;
    }
    for (const auto& upload : impl_->ordinaryTextureUploads) {
        const auto& object =
            *upload.packing->originalSource()->originalDestinationLease()->originalObject();
        const auto* id = upload.pending
            ? rhi::OriginalCommandStateTransaction::validateOrdinaryWholeUploadPending(
                  object, *impl_->retainedResourceStates, *upload.pending)
            : "RHI-ORDINARY-TEXTURE-UPLOAD-CONTENT-PENDING";
        if (id) {
            diagnostics = id;
            return false;
        }
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
    if (const auto* id = impl_->device.commandDomain().ownerDiagnostic()) {
        diagnostics = id;
        return false;
    }
    if (!asyncSubmittedCommandRetirementHasCapacity(impl_->device, diagnostics)) {
        return false;
    }
    if (impl_->device.syntheticRuntime()) {
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (!query.recorded)
                continue;
            if (!query.pool
                || !query.pool->markSubmitted(query.queryIndex,
                    diagnostics,
                    query.originalSlot ? impl_->retainedResourceStates.get() : nullptr)) {
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
        if (!retainSubmittedCommandForAsyncRetirement(
                impl_->device, shared_from_this(), diagnostics)) {
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

    if (!impl_->submitFence
        && !acquireReusableFence(impl_->deviceOwner, impl_->submitFence, diagnostics)) {
        if (!diagnostics.empty())
            return false;
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
    const auto cancelUninvokedQueries = [&]() {
        std::size_t remaining = submittedQueryCount;
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (!query.recorded)
                continue;
            if (!remaining)
                break;
            --remaining;
            if (query.pool)
                (void)query.pool->rollbackSubmitted(query.queryIndex,
                    diagnostics,
                    query.originalSlot ? impl_->retainedResourceStates.get() : nullptr);
        }
    };
    for (const auto& query : impl_->recordedTimestampQueries) {
        if (!query.recorded)
            continue;
        if (!query.pool
            || !query.pool->markSubmitted(query.queryIndex,
                diagnostics,
                query.originalSlot ? impl_->retainedResourceStates.get() : nullptr)) {
            cancelUninvokedQueries();
            return false;
        }
        ++submittedQueryCount;
    }
    if (impl_->originalBindlessCustody
        && !impl_->originalBindlessCustody->beforeSubmit(*this, diagnostics)) {
        cancelUninvokedQueries();
        return false;
    }
    // Reserve native retirement BEFORE queue invocation for canonical custody
    // and authored timestamp queries. An invoked error is unproven execution.
    const bool retainBeforeInvocation = impl_->originalBindlessCustody || impl_->recordingRetention
        || impl_->ordinaryJournalAliased || impl_->internalProfiling || hasTimelineWait
        || hasTimelineSignal || impl_->hasRecordedTimestampQueries();
    if (retainBeforeInvocation
        && !retainSubmittedCommandForAsyncRetirement(
            impl_->device, shared_from_this(), diagnostics)) {
        cancelUninvokedQueries();
        return false;
    }
    if (impl_->originalBindlessCustody) {
        if (const auto* id = impl_->originalBindlessCustody->maySubmit()) {
            diagnostics = id;
            cancelUninvokedQueries();
            return false;
        }
    }
    impl_->submittedTimeline = timeline;
    if (impl_->recordingRetention)
        (void)impl_->recordingRetention->markSubmitted();
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
        if (impl_->internalProfiling)
            impl_->internalTimingFailure = "RHI-COMMAND-PROFILING-SUBMISSION-UNPROVEN";
        for (const auto& query : impl_->recordedTimestampQueries) {
            if (query.recorded && query.pool) {
                if (query.originalSlot)
                    (void)query.pool->markSubmissionUnproven(
                        query.queryIndex, impl_->retainedResourceStates.get());
                else
                    (void)query.pool->markSubmissionUnproven(query.queryIndex);
            }
        }
        if (impl_->retainedResourceStates)
            (void)impl_->retainedResourceStates->invalidate();
        if (impl_->originalBindlessCustody)
            diagnostics = submitted == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-ROOT-FENCE-SUBMIT-DEVICE-LOST"
                : "RHI-VULKAN-ROOT-FENCE-SUBMIT-UNPROVEN";
        else if (impl_->recordingRetention)
            diagnostics = submitted == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-RETENTION-SUBMIT-DEVICE-LOST"
                : "RHI-VULKAN-RETENTION-SUBMIT-UNPROVEN";
        else if (impl_->recordingStorage && impl_->recordingStorage->originalProgram)
            diagnostics = submitted == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-ORIGINAL-STREAM-SUBMIT-DEVICE-LOST"
                : "RHI-VULKAN-ORIGINAL-STREAM-SUBMIT-UNPROVEN";
        else if (hasTimelineWait || hasTimelineSignal)
            diagnostics = submitted == VK_ERROR_DEVICE_LOST
                ? "RHI-VULKAN-TIMELINE-SUBMIT-DEVICE-LOST"
                : "RHI-VULKAN-TIMELINE-SUBMIT-UNPROVEN";
        else if (impl_->hasRecordedTimestampQueries())
            diagnostics = submitted == VK_ERROR_DEVICE_LOST ? "RHI-VULKAN-QUERY-SUBMIT-DEVICE-LOST"
                                                            : "RHI-VULKAN-QUERY-SUBMIT-UNPROVEN";
        else
            diagnostics = vkFailure("vkQueueSubmit", submitted);
        return false;
    }
    impl_->submitted = true;
    if (impl_->retainedResourceStates && impl_->ordinaryTextureUploads.empty()
        && !hasOrdinaryAttachments())
        impl_->retainedResourceStates->commit();
    if (acceptedThisAttempt)
        *acceptedThisAttempt = true;
    if (impl_->hasSwapchainFrameSync && impl_->swapchainFrameSyncState) {
        impl_->swapchainFrameSyncState->renderSubmitted_ = true;
        impl_->swapchainFrameSyncState->submittedCommand_ = shared_from_this();
    }
    if (!retainBeforeInvocation
        && !retainSubmittedCommandForAsyncRetirement(
            impl_->device, shared_from_this(), diagnostics)) {
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

VulkanRhiRetainedCommand::VulkanRhiRetainedCommand(ConstructionKey,
    std::shared_ptr<VulkanRhiNativeCommandPayload> command,
    std::shared_ptr<VulkanRhiResourceRegistry> registry)
    : command_(std::move(command)), registry_(std::move(registry))
{}

core::UniquePtr<VulkanRhiRetainedCommand> VulkanRhiRetainedCommand::prepare(
    const VulkanRhiNativeResourceAllocator& resources,
    std::shared_ptr<VulkanRhiResourceRegistry> registry,
    core::UniquePtr<rhi::common::CommandRetentionStorage> storage,
    core::pmr::String& diagnostic)
{
    using Storage = rhi::common::CommandRetentionStorage;
    if (!storage || storage->state() != Storage::State::Recording || storage->resourceOwnerCount()
        || storage->queryPoolOwnerCount() || !storage->timestamps().empty()
        || !storage->statistics().empty()) {
        diagnostic = "RHI-VULKAN-RETENTION-PREPARATION-STATE";
        return {};
    }
    if (storage->quota()->limitBytes() != storage->budget().maxCpuMetadataBytes) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return {};
    }
    if (!registry || !resources.connected() || !resources.runtime()
        || resources.runtime()->syntheticRuntime() || !resources.runtime()->valid()) {
        diagnostic = "RHI-VULKAN-RETENTION-NATIVE-RUNTIME-REQUIRED";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(storage->quota());
        auto command = createVulkanRhiNativeCommandPayload(resources, diagnostic);
        if (!command) {
            if (diagnostic == "RHI-VULKAN-BINDLESS-RETENTION-ALLOCATION-FAILED")
                diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
            return {};
        }
        // Exactly one explicit dependency is encoded and flushed per call.
        command->impl_->pendingMemoryBarriers.reserve(1u);
        command->impl_->pendingBufferBarriers.reserve(1u);
        command->impl_->pendingImageBarriers.reserve(1u);
        command->impl_->retainedResourceStates = core::makeUnique<core::MutationJournal>();
        if (!command->prepareOriginalSubmissionFence(diagnostic))
            return {};
        auto prepared = core::makeUnique<VulkanRhiRetainedCommand>(
            ConstructionKey{}, command, std::move(registry));
        VkFormatProperties depthProperties{};
        vkGetPhysicalDeviceFormatProperties(
            resources.runtime()->physicalDevice(), VK_FORMAT_D32_SFLOAT, &depthProperties);
        prepared->depthFormatFeatures_ = depthProperties.optimalTilingFeatures;
        prepared->sampledDepthLoadAvailable_ =
            (depthProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0u;
        VkFormatProperties colorProperties{};
        vkGetPhysicalDeviceFormatProperties(
            resources.runtime()->physicalDevice(), VK_FORMAT_R16G16B16A16_SFLOAT, &colorProperties);
        prepared->colorFormatFeatures_ = colorProperties.optimalTilingFeatures;
        command->impl_->recordingRetention = std::move(storage);
        diagnostic.clear();
        return prepared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-COMMAND-RETENTION-PREPARATION-ALLOCATION-FAILED";
        return {};
    }
}

const rhi::common::CommandRetentionStorage*
VulkanRhiRetainedCommand::retentionStorage() const noexcept
{
    return command_->impl_->recordingRetention.get();
}

bool VulkanRhiRetainedCommand::enableRecordingBudget(
    const rhi::CommandRecordingBudget& budget, core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    if (recordingMeter_ || retentionStorage()->resourceOwnerCount()
        || preparedPassTarget_.isValid()) {
        diagnostic = "RHI-COMMAND-RECORDING-BUDGET-PREPARATION-STATE";
        return false;
    }
    const auto decision =
        rhi::CommandRecordingBudgetContract::lowering(rhi::Backend::Vulkan, budget);
    if (!decision.privateRecordingAvailable) {
        diagnostic = decision.diagnostic;
        return false;
    }
    const char* id = nullptr;
    recordingMeter_ = rhi::CommandRecordingMeter::prepare(budget, id);
    if (!recordingMeter_) {
        diagnostic = id;
        return false;
    }
    diagnostic.clear();
    return true;
}

const rhi::CommandRecordingUsage* VulkanRhiRetainedCommand::recordingUsage() const noexcept
{
    return recordingMeter_ ? &recordingMeter_->used() : nullptr;
}

bool VulkanRhiRetainedCommand::preflightRecordingCharge(
    const rhi::CommandRecordingUsage& usage, core::pmr::String& diagnostic) const
{
    if (recordingMeter_)
        if (const auto* id = recordingMeter_->preflight(usage)) {
            diagnostic = id;
            return false;
        }
    return true;
}

bool VulkanRhiRetainedCommand::commitRecordingCharge(
    const rhi::CommandRecordingUsage& usage, core::pmr::String& diagnostic)
{
    if (recordingMeter_)
        if (const auto* id = recordingMeter_->consume(usage)) {
            failed_ = true;
            diagnostic = id;
            return false;
        }
    return true;
}

bool VulkanRhiRetainedCommand::validateRecording(core::pmr::String& diagnostic) const
{
    const auto* storage = retentionStorage();
    if (failed_ || !storage
        || storage->state() != rhi::common::CommandRetentionStorage::State::Recording) {
        diagnostic = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        return false;
    }
    if (storage->quota()->limitBytes() != storage->budget().maxCpuMetadataBytes) {
        diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
        return false;
    }
    return true;
}

std::shared_ptr<VulkanRhiNativeResourcePayload> VulkanRhiRetainedCommand::resolve(
    const rhi::GpuBuffer& buffer, core::pmr::String& diagnostic) const
{
    const auto original = vulkanRhiNativePayload(buffer);
    const auto registered = registry_->payload(buffer.id());
    if (!vulkanRhiBufferRegistryMatches(buffer, registry_)
        || !registry_->bufferObjectMatches(buffer) || !original || !registered
        || original != registered || !core::SharedOwnerProjection::sameOwner(original, registered)
        || original->backendVulkanDeviceHandle() != command_->impl_->device.logicalDevice()
        || !original->backendVulkanBufferHandle()
        || original->backendVulkanOriginalContentExclusive()) {
        diagnostic = "RHI-VULKAN-RETENTION-BUFFER-REGISTRATION";
        return {};
    }
    if (!vulkanRhiBufferStateAvailable(buffer, command_->impl_->retainedResourceStates.get())) {
        diagnostic = "RHI-VULKAN-RETENTION-BUFFER-STATE-UNCOMMITTED";
        return {};
    }
    return original;
}

bool VulkanRhiRetainedCommand::copy(const rhi::GpuBuffer& source,
    const rhi::GpuBuffer& destination,
    const rhi::BufferCopyDesc& desc,
    core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto src = resolve(source, diagnostic);
    if (!src)
        return false;
    const auto dst = resolve(destination, diagnostic);
    if (!dst)
        return false;
    if (desc.sourceBuffer != source.id() || desc.destinationBuffer != destination.id()
        || source.id() == destination.id()
        || src->backendVulkanBufferHandle() == dst->backendVulkanBufferHandle()
        || (source.desc().usage & rhi::ResourceUsage::CopySrc) == rhi::ResourceUsage::None
        || (destination.desc().usage & rhi::ResourceUsage::CopyDst) == rhi::ResourceUsage::None
        || source.state() != rhi::ResourceState::CopySrc
        || destination.state() != rhi::ResourceState::CopyDst || !desc.sizeBytes
        || (desc.sourceOffsetBytes % 4u) || (desc.destinationOffsetBytes % 4u)
        || (desc.sizeBytes % 4u) || desc.sourceOffsetBytes > source.desc().sizeBytes
        || desc.destinationOffsetBytes > destination.desc().sizeBytes
        || desc.sizeBytes > source.desc().sizeBytes - desc.sourceOffsetBytes
        || desc.sizeBytes > destination.desc().sizeBytes - desc.destinationOffsetBytes) {
        diagnostic = "RHI-VULKAN-RETENTION-BUFFER-COPY-DESCRIPTOR";
        return false;
    }
    if (destination.gpuTransferWritesSealed()
        || registry_->bufferGpuTransferWritesSealed(destination.id())) {
        diagnostic = "RHI-BUFFER-GPU-WRITES-SEALED";
        return false;
    }
    std::shared_ptr<const void> owners[]{src, dst};
    auto& storage = *command_->impl_->recordingRetention;
    if (const auto* id = storage.preflightResources(owners, 2u)) {
        diagnostic = id;
        return false;
    }
    if (!command_->impl_->device.getDeviceProcAddr("vkCmdCopyBuffer")) {
        diagnostic = "RHI-VULKAN-RETENTION-COPY-ENTRYPOINT";
        return false;
    }
    rhi::CommandRecordingUsage charge;
    charge.append(rhi::CommandUsageKind::BufferCopy);
    charge.resourceReferences = 2u;
    if (!preflightRecordingCharge(charge, diagnostic))
        return false;
    (void)storage.retainResources(owners, 2u);
    if (!commitRecordingCharge(charge, diagnostic))
        return false;
    if (!command_->recordBufferCopy(*src, *dst, desc, diagnostic)) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

bool VulkanRhiRetainedCommand::dependency(const rhi::GpuBuffer& buffer,
    const rhi::ResourceMemoryBarrierDesc& desc,
    core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    const auto owner = resolve(buffer, diagnostic);
    if (!owner)
        return false;
    const bool transfer = desc.sourceStages == rhi::PipelineStage::Transfer
        && desc.destinationStages == rhi::PipelineStage::Transfer;
    const bool compute = desc.sourceStages == rhi::PipelineStage::ComputeShader
        && desc.destinationStages == rhi::PipelineStage::ComputeShader;
    const auto allowed = transfer
        ? rhi::ResourceAccess::TransferRead | rhi::ResourceAccess::TransferWrite
        : rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite;
    const auto accesses = desc.sourceAccess | desc.destinationAccess;
    const auto read =
        transfer ? rhi::ResourceAccess::TransferRead : rhi::ResourceAccess::ShaderRead;
    const auto write =
        transfer ? rhi::ResourceAccess::TransferWrite : rhi::ResourceAccess::ShaderWrite;
    const bool reads = (accesses & read) != rhi::ResourceAccess::None;
    const bool writes = (accesses & write) != rhi::ResourceAccess::None;
    const bool stateValid = transfer
        ? (!reads
              || ((buffer.desc().usage & rhi::ResourceUsage::CopySrc) != rhi::ResourceUsage::None
                  && buffer.state() == rhi::ResourceState::CopySrc))
            && (!writes
                || ((buffer.desc().usage & rhi::ResourceUsage::CopyDst) != rhi::ResourceUsage::None
                    && buffer.state() == rhi::ResourceState::CopyDst))
        : ((buffer.desc().usage
               & (rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::StorageBuffer))
                  != rhi::ResourceUsage::None
              && (buffer.state() == rhi::ResourceState::ShaderRead
                  || buffer.state() == rhi::ResourceState::ShaderWrite)
              && (!writes
                  || ((buffer.desc().usage & rhi::ResourceUsage::StorageBuffer)
                          != rhi::ResourceUsage::None
                      && buffer.state() == rhi::ResourceState::ShaderWrite)));
    if (desc.resource != buffer.id() || (!transfer && !compute)
        || desc.sourceAccess == rhi::ResourceAccess::None
        || desc.destinationAccess == rhi::ResourceAccess::None
        || (desc.sourceAccess & allowed) != desc.sourceAccess
        || (desc.destinationAccess & allowed) != desc.destinationAccess || !stateValid) {
        diagnostic = "RHI-VULKAN-RETENTION-TRANSFER-DEPENDENCY";
        return false;
    }
    if (writes && (transfer ? buffer.gpuTransferWritesSealed() : buffer.gpuWritesSealed())) {
        diagnostic = "RHI-BUFFER-GPU-WRITES-SEALED";
        return false;
    }
    std::shared_ptr<const void> owners[]{owner};
    auto& storage = *command_->impl_->recordingRetention;
    if (const auto* id = storage.preflightResources(owners, 1u)) {
        diagnostic = id;
        return false;
    }
    if (!command_->impl_->device.getDeviceProcAddr("vkCmdPipelineBarrier")) {
        diagnostic = "RHI-VULKAN-RETENTION-BARRIER-ENTRYPOINT";
        return false;
    }
    rhi::CommandRecordingUsage charge;
    charge.append(rhi::CommandUsageKind::BufferMemoryBarrier);
    charge.resourceReferences = 1u;
    if (!preflightRecordingCharge(charge, diagnostic))
        return false;
    (void)storage.retainResources(owners, 1u);
    if (!commitRecordingCharge(charge, diagnostic))
        return false;
    if (!command_->recordResourceMemoryBarrier(desc, diagnostic)
        || !command_->flushPendingBarriers(diagnostic)) {
        failed_ = true;
        return false;
    }
    diagnostic.clear();
    return true;
}

bool VulkanRhiRetainedCommand::finish(core::pmr::String& diagnostic)
{
    if (!validateRecording(diagnostic))
        return false;
    rhi::CommandRecordingUsage charge;
    charge.append(rhi::CommandUsageKind::Finish);
    if (!preflightRecordingCharge(charge, diagnostic) || !commitRecordingCharge(charge, diagnostic))
        return false;
    if (!command_->end(diagnostic)) {
        failed_ = true;
        return false;
    }
    (void)command_->impl_->recordingRetention->sealForSubmission();
    if (recordingMeter_)
        recordingMeter_->seal();
    diagnostic.clear();
    return true;
}

bool VulkanRhiRetainedCommand::submit(
    const VulkanRhiTimelineSubmitInfo& timeline, core::pmr::String& diagnostic)
{
    if (failed_) {
        diagnostic = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
        return false;
    }
    return command_->submit(timeline, diagnostic);
}

bool VulkanRhiRetainedCommand::completed()
{
    return command_->submittedFenceCompleted();
}

bool VulkanRhiRetainedCommand::waitUntil(
    std::chrono::steady_clock::time_point deadline, core::pmr::String& diagnostic)
{
    return command_->waitSubmittedFenceUntil(deadline, diagnostic);
}

bool VulkanRhiNativeCommandPayload::prepareInternalProfiling(core::pmr::String& diagnostic)
{
    if (!impl_ || !impl_->recordingStorage || !impl_->recordingStorage->originalQueries
        || !impl_->recordingStorage->originalQueries->internalProfilingRequested)
        return true;
    const auto& budget = *impl_->recordingStorage->originalQueries->profilingBudget;
    const auto facts = impl_->device.originalQueryCreationFacts();
    const auto decision = rhi::CommandProfilingContract::lower(
        rhi::Backend::Vulkan, budget, facts.vulkanTimestamp, facts.vulkanRuntimeDiagnostic);
    if (!decision.executable()) {
        diagnostic = decision.diagnostic;
        return false;
    }
    impl_->internalProfiling = decision;
    // Publication storage was constructed with this runtime before preparation.
    const auto create =
        functionFrom<PFN_vkCreateQueryPool>(impl_->device.getDeviceProcAddr("vkCreateQueryPool"));
    if (!create) {
        diagnostic = "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS";
        return false;
    }
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = rhi::CommandProfilingContract::timestampWrites;
    const auto result =
        create(impl_->device.logicalDevice(), &info, nullptr, &impl_->timestampQueryPool);
    impl_->device.captureOriginalDeviceLoss(result);
    if (result != VK_SUCCESS || !impl_->timestampQueryPool) {
        diagnostic = result == VK_ERROR_DEVICE_LOST ? "RHI-COMMAND-PROFILING-CREATE-DEVICE-LOST"
                                                    : "RHI-COMMAND-PROFILING-CREATE-FAILED";
        return false;
    }
    if (const auto* id = impl_->device.timestampQueryDiagnostic()) {
        diagnostic = id;
        return false;
    }
    const auto reset =
        functionFrom<PFN_vkResetQueryPool>(impl_->device.getDeviceProcAddr("vkResetQueryPool"));
    if (!reset) {
        diagnostic = "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS";
        return false;
    }
    reset(impl_->device.logicalDevice(),
        impl_->timestampQueryPool,
        0u,
        rhi::CommandProfilingContract::timestampWrites);
    impl_->timestampPeriodNs = static_cast<float>(decision.timestampPeriodNanoseconds);
    return true;
}

std::shared_ptr<VulkanRhiNativeCommandPayload> createVulkanRhiNativeCommandPayload(
    const VulkanRhiNativeResourceAllocator& nativeResources,
    std::pmr::string& diagnostics,
    const rhi::CommandEncoderDesc& retention,
    const VulkanRhiRecordingStorageLimits* storage)
{
    if (storage) {
        if (const auto* id = storage->diagnostic()) {
            diagnostics = id;
            return {};
        }
    }
    if (retention.retentionBudget) {
        const auto decision = rhi::semantics::CommandRetentionBudgetLowering::plan(
            *retention.retentionBudget, rhi::CommandRetentionCapabilities{});
        if (!decision.success) {
            diagnostics = decision.diagnosticId;
            return {};
        }
    }
    if (!nativeResources.connected() || !nativeResources.runtime()) {
        diagnostics = "Vulkan RHI native command buffer requires a native device";
        return {};
    }
    auto runtime = nativeResources.runtime();
    const auto& device = *runtime;
    if (const auto* id = runtime->commandDomain().bindOwner()) {
        diagnostics = id;
        return {};
    }
    if (storage && device.syntheticRuntime()) {
        diagnostics = "RHI-VULKAN-COLD-RECORDING-SYNTHETIC-RUNTIME";
        return {};
    }
    // Reserve indexed ownership before creating any Vulkan object. Allocation
    // failure cannot leak a pool and can never trigger a recording-time growth.
    std::shared_ptr<VulkanRhiNativeCommandPayload> payload;
    std::optional<core::CpuMemoryQuotaScope> queryScope;
    try {
        core::UniquePtr<rhi::common::CommandRetentionStorage> queryRetention;
        if (storage && storage->originalQueries) {
            const auto lowering =
                rhi::OriginalQueryRecordingStorageContract::lowering(rhi::Backend::Vulkan);
            if (!lowering.nativeStorageImplemented) {
                diagnostics = lowering.diagnostic;
                return {};
            }
            const auto& query = *storage->originalQueries;
            // Revalidate genuine native owners before the first reservation or
            // command-pool creation. A Source cannot substitute an equal pool.
            const auto& sources =
                query.inputs->originalScalars()->originalUsage()->originalSources()->sources();
            for (const auto& source : sources) {
                const auto* original =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandQueryPoolSource>>(
                        &source);
                if (!original)
                    continue;
                const auto pool = std::dynamic_pointer_cast<const VulkanRhiQueryPool>(
                    (*original)->originalPool());
                if (const auto* id = VulkanRhiQueryPool::validateOriginal(pool, device)) {
                    diagnostics = id;
                    return {};
                }
            }
            queryScope.emplace(query.recordingQuota);
            const char* id = nullptr;
            queryRetention = rhi::common::CommandRetentionStorage::prepare(
                query.budget, query.recordingQuota, id);
            if (!queryRetention) {
                diagnostics = id;
                return {};
            }
        }
        payload = core::makeShared<VulkanRhiNativeCommandPayload>(
            core::makeUnique<VulkanRhiNativeCommandPayload::Impl>(runtime,
                nativeResources.allocator(),
                nativeResources.sharedStats(),
                VK_NULL_HANDLE,
                VK_NULL_HANDLE,
                retention,
                storage));
        payload->impl_->recordingRetention = std::move(queryRetention);
        if (storage && storage->originalQueries) {
            const auto& inputs = *storage->originalQueries->inputs;
            const auto& sources =
                inputs.originalScalars()->originalUsage()->originalSources()->sources();
            for (const auto& descriptor : inputs.descriptors()) {
                const auto* binding =
                    std::get_if<rhi::OriginalCommandQueryBinding>(&descriptor.arguments);
                if (!binding)
                    continue;
                const auto& source =
                    std::get<std::shared_ptr<const rhi::OriginalCommandQueryPoolSource>>(
                        sources[binding->sourceIndex]);
                const auto* pool = asVulkanRhiQueryPool(*source->originalPool());
                const auto index =
                    binding->original.scalar().get<rhi::CommandScalarArguments::Query>()->index;
                const auto observation = pool->payload()->observeSlot(index);
                if (!observation) {
                    diagnostics = "RHI-VULKAN-QUERY-SLOT-OBSERVATION";
                    return {};
                }
                if (const auto* id = pool->payload()->preflightRecord(*observation)) {
                    diagnostics = id;
                    return {};
                }
                // Existing cold-reserved record storage; no second owner graph
                // or command stream. This observation has recorded no GPU work.
                payload->impl_->recordedTimestampQueries.push_back(
                    {pool->payload(), index, *observation, false, descriptor.commandIndex});
            }
            if (const auto* id = payload->originalQuerySlotDiagnostic()) {
                diagnostics = id;
                return {};
            }
        }
    } catch (const std::bad_alloc&) {
        diagnostics = storage ? "RHI-VULKAN-COLD-RECORDING-STORAGE-ALLOCATION"
                              : "RHI-VULKAN-BINDLESS-RETENTION-ALLOCATION-FAILED";
        return {};
    } catch (const std::length_error&) {
        diagnostics = storage ? "RHI-VULKAN-COLD-RECORDING-STORAGE-ALLOCATION"
                              : "RHI-VULKAN-BINDLESS-RETENTION-ALLOCATION-FAILED";
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
    if (!payload->prepareInternalProfiling(diagnostics))
        return {};
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
        runtime, device.graphicsQueueFamilyIndex(), commandPool, commandBuffer, diagnostics);
    if (!reused && !diagnostics.empty())
        return {};
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
        destroyReusableCommandResources(device,
            {
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
    if (impl->internalProfiling) {
        if (const auto* id = device.timestampQueryDiagnostic()) {
            diagnostics = id;
            return {};
        }
        const auto write =
            functionFrom<PFN_vkCmdWriteTimestamp>(device.getDeviceProcAddr("vkCmdWriteTimestamp"));
        if (!write) {
            diagnostics = "RHI-VULKAN-TIMESTAMP-ENTRYPOINTS";
            return {};
        }
        write(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, impl->timestampQueryPool, 0u);
    }
    auto* createQueryPool =
        functionFrom<PFN_vkCreateQueryPool>(device.getDeviceProcAddr("vkCreateQueryPool"));
    auto* resetQueryPool =
        functionFrom<PFN_vkCmdResetQueryPool>(device.getDeviceProcAddr("vkCmdResetQueryPool"));
    auto* writeTimestamp =
        functionFrom<PFN_vkCmdWriteTimestamp>(device.getDeviceProcAddr("vkCmdWriteTimestamp"));
    // Explicit recording-storage admission never inherits legacy profiling.
    // prepareInternalProfiling is its sole budgeted creation path; original
    // Program storage must not add an unbudgeted two-timestamp interval.
    if (!storage && createQueryPool && resetQueryPool && writeTimestamp) {
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

rhi::GpuTimingSample latestVulkanRhiGpuTimingSample(const VulkanRhiRuntime& runtime)
{
    rhi::GpuTimingSample result;
    auto& timing = runtime.commandDomain().timing;
    const auto sequence = timing.sequence.load(std::memory_order_acquire);
    if (!(sequence & 1u)) {
        result.completedFrameSerial = timing.frameSerial.load(std::memory_order_relaxed);
        result.availability = timing.availability.load(std::memory_order_relaxed);
        result.diagnostic = timing.diagnostic.load(std::memory_order_relaxed);
        result.scopeCount = timing.scopeCount.load(std::memory_order_relaxed);
        if (result.scopeCount <= rhi::GpuTimingSample::kMaxScopes) {
            for (std::uint32_t i = 0u; i < result.scopeCount; ++i) {
                result.scopes[i].scopeId = timing.scopeIds[i].load(std::memory_order_relaxed);
                result.scopes[i].elapsedMs =
                    timing.scopeMilliseconds[i].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence == timing.sequence.load(std::memory_order_relaxed)
                && result.completedFrameSerial)
                return result;
        }
    }
    result = {};
    result.availability = rhi::GpuTimingAvailability::Pending;
    result.diagnostic = "RHI-VULKAN-TIMING-PUBLICATION-PENDING";
    return result;
}

} // namespace arp::rhi_vulkan
