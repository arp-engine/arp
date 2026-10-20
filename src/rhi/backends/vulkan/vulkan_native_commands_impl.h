#pragma once

#include "core/bounded_retainer.h"
#include "core/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_query_pool.h"
#include "rhi/backends/vulkan/vulkan_runtime.h"
#include "rhi/frontend/bindless_binding_preparation.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace arp::rhi_vulkan {

class VulkanRhiQueryPoolPayload;
class VulkanOriginalCommandCustody;
class VulkanConformanceClearFrame;

struct VulkanRhiNativeRenderPassResources {
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    /// Up to 8 color attachments + 1 depth attachment (matches the Null
    /// render-pass validation limit of 8 color attachments).
    static constexpr std::uint32_t kMaxImageViews = 9;
    std::array<VkImageView, kMaxImageViews> imageViews{};
    std::uint32_t imageViewCount = 0;
    /// Number of subpasses in this render pass (1 = single-subpass, the
    /// legacy behavior). Set from desc.subpasses.size() when non-empty.
    std::uint32_t subpassCount = 1;
    /// Current subpass index during recording (0-based). Advanced by
    /// recordNextSubpass; bounds-checked against subpassCount.
    std::uint32_t currentSubpassIndex = 0;
};

struct VulkanRhiNativeCommandPayload::Impl {
    Impl(std::shared_ptr<VulkanRhiRuntime> deviceOwner,
        VulkanRhiAllocator* allocator,
        std::shared_ptr<VulkanRhiNativeResourceStats> stats,
        VkCommandPool commandPool,
        VkCommandBuffer commandBuffer,
        const rhi::CommandEncoderDesc& retention)
        : deviceOwner(std::move(deviceOwner)), device(*this->deviceOwner), allocator(allocator),
          stats(std::move(stats)), commandPool(commandPool), commandBuffer(commandBuffer),
          indexedPipelines(retention.maxBindlessPipelines),
          preparedBindings(retention.maxBindlessPipelines),
          indexedSnapshots(retention.maxBindlessSnapshots),
          indexedFaultLatches(retention.maxBindlessSnapshots)
    {
        pendingBindlessFaults.reserve(retention.maxBindlessSnapshots);
        bindlessFaultOutcomes.reserve(retention.maxBindlessSnapshots);
        leasedBindlessFaultLatches.reserve(retention.maxBindlessSnapshots);
    }

    std::shared_ptr<VulkanRhiRuntime> deviceOwner;
    const VulkanRhiRuntime& device;
    VulkanRhiAllocator* allocator = nullptr;
    std::shared_ptr<VulkanRhiNativeResourceStats> stats;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBuffer bindlessFaultPreamble = VK_NULL_HANDLE;
    VkFence submitFence = VK_NULL_HANDLE;
    VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
    float timestampPeriodNs = 0.0f;
    VkSemaphore waitSemaphore = VK_NULL_HANDLE;
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::shared_ptr<VulkanRhiSwapchainFrameSyncState> swapchainFrameSyncState;
    // Fixed two owners; admitted before queue invocation, released by actual
    // original completion only. Failed/unknown queue invocation stays retained.
    VulkanRhiTimelineSubmitInfo submittedTimeline;
    VkPipelineLayout boundComputePipelineLayout = VK_NULL_HANDLE;
    VkShaderStageFlags boundComputePushConstantStages = 0;
    VkPipelineLayout boundGraphicsPipelineLayout = VK_NULL_HANDLE;
    VkShaderStageFlags boundGraphicsPushConstantStages = 0;
    VulkanRhiNativeRenderPassResources activeRenderPass;
    // Original Graphics custody owns these cold resources until real drain.
    // An interrupted recording must not destroy the same borrowed handles.
    bool originalRenderPassBorrowed = false;
    std::array<VulkanRhiNativeRenderPassResources, 64> retainedRenderPasses{};
    std::uint32_t retainedRenderPassCount = 0;
    core::pmr::Vector<VkImageMemoryBarrier> pendingImageBarriers;
    core::pmr::Vector<VkBufferMemoryBarrier> pendingBufferBarriers;
    std::vector<VkMemoryBarrier> pendingMemoryBarriers;
    VkPipelineStageFlags pendingSourceStages = 0;
    VkPipelineStageFlags pendingDestinationStages = 0;
    VkBuffer scratchIndexBuffer = VK_NULL_HANDLE;
    std::shared_ptr<void> scratchIndexAllocationPayload;
    std::uint32_t scratchIndexCapacity = 0;
    core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> retainedResources;
    core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads;
    // Populated only by indexed recording after semantic authorization. Opaque
    // objects own the typed snapshot/state, native storage and budget leases.
    core::BoundedRetainer<void> indexedPipelines;
    core::BoundedRetainer<rhi::BindlessPreparedBinding> preparedBindings;
    core::BoundedRetainer<void> indexedSnapshots;
    core::BoundedRetainer<void> indexedFaultLatches;

    struct PendingBindlessFault {
        std::shared_ptr<VulkanRhiNativeResourcePayload> latch;
        std::shared_ptr<VulkanRhiNativeResourcePayload> destination;
        std::shared_ptr<VulkanRhiBindlessFaultReadback> result;
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot;
        rhi::BindlessFaultSubmissionLease lease;
    };

    core::pmr::Vector<PendingBindlessFault> pendingBindlessFaults;

    struct BindlessFaultOutcome {
        std::uint64_t snapshotGeneration = 0u;
        std::shared_ptr<VulkanRhiBindlessFaultReadback> result;
    };

    core::pmr::Vector<BindlessFaultOutcome> bindlessFaultOutcomes;

    struct LeasedBindlessFaultLatch {
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot;
        rhi::BindlessFaultSubmissionLease lease;
        std::shared_ptr<VulkanRhiNativeResourcePayload> latch;
    };

    core::pmr::Vector<LeasedBindlessFaultLatch> leasedBindlessFaultLatches;
    core::pmr::Vector<std::shared_ptr<VulkanRhiQueryPoolPayload>> retainedTimingPools;

    struct RecordedTimestampQuery {
        std::shared_ptr<VulkanRhiQueryPoolPayload> pool;
        std::uint32_t queryIndex = 0u;
    };

    core::pmr::Vector<RecordedTimestampQuery> recordedTimestampQueries;
    core::pmr::Vector<VkQueryPool> resetTimestampPools;
    std::uint64_t frameSerial = 0;
    bool indexBufferBound = false;
    bool recording = false;
    bool ended = false;
    bool submitted = false;
    bool submitFenceCompleted = false;
    bool timestampResolved = false;
    bool hasSwapchainFrameSync = false;
    std::shared_ptr<VulkanOriginalCommandCustody> originalBindlessCustody;
    std::shared_ptr<VulkanConformanceClearFrame> conformanceClear;
    // Unknown queue invocation is retained; an unsignalled fence is not a
    // completion proof. Explicit drain reports the quarantine until resolved.
    bool originalSubmissionUnproven = false;
};

void destroyVulkanRhiNativeRenderPassResources(
    const VulkanRhiRuntime& device, VulkanRhiNativeRenderPassResources& resources);

// Cold native mapping shared by authenticated Graphics and storage Clear/Store.
// Callers validate L2 intent, exact original views, format/extent and capacity;
// this factory neither records commands nor publishes content/completion.
class VulkanRhiNativeClearStorePassFactory final {
public:
    struct Attachment {
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageSubresourceRange range{};
    };

    static bool create(const VulkanRhiRuntime&,
        const Attachment*,
        std::uint32_t colorCount,
        bool depth,
        rhi::Extent3D,
        VulkanRhiNativeRenderPassResources&) noexcept;
};

} // namespace arp::rhi_vulkan
