#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/vulkan/vulkan_native_resources.h"
#include "rhi/backends/vulkan/vulkan_timeline_semaphore.h"
#include "rhi/frontend/transfer_upload_recording_validation.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_device.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace arp::rhi {
enum class CommandUsageKind : std::uint32_t;
class BindlessPreparedBinding;
class CommandScalarArguments;
class RecordedStagingUploadJournal;
struct OriginalRenderPassPreparationRequest;

namespace semantics {
struct NativeCommandLoweringDecision;
}

namespace common {
class CommandRetentionStorage;
}
} // namespace arp::rhi

namespace arp::rhi_vulkan {

class VulkanRhiStagingUploadPool;

class VulkanRhiQueryPoolPayload;
struct VulkanRhiRecordingStorageLimits;

class VulkanRhiBindlessFaultReadback final {
public:
    rhi::BindlessFaultValidationStatus status() const noexcept
    {
        return status_.load(std::memory_order_acquire);
    }

    std::uint32_t faultBits() const noexcept { return bits_.load(std::memory_order_acquire); }

    rhi::ValidationResult validate() const noexcept
    {
        return rhi::validateBindlessFaultStatus(status());
    }

private:
    friend class VulkanRhiNativeCommandPayload;

    void complete(std::uint32_t bits) noexcept
    {
        bits_.store(bits, std::memory_order_relaxed);
        status_.store(bits ? rhi::BindlessFaultValidationStatus::Faulted
                           : rhi::BindlessFaultValidationStatus::Clean,
            std::memory_order_release);
    }

    void fail() noexcept
    {
        status_.store(
            rhi::BindlessFaultValidationStatus::ReadbackFailed, std::memory_order_release);
    }

    void abandon() noexcept
    {
        status_.store(rhi::BindlessFaultValidationStatus::Abandoned, std::memory_order_release);
    }

    std::atomic<rhi::BindlessFaultValidationStatus> status_{
        rhi::BindlessFaultValidationStatus::Pending};
    std::atomic<std::uint32_t> bits_{0};
};

// Existing backend-owned retirement ceiling, exposed here for preparation and
// behavioral tests. It is not a public bindless capability or a frame count.
inline constexpr std::size_t kVulkanMaxAsyncSubmittedCommands = 64;

/// NativeTimelineOwnership/1: typed original owners, never borrowed raw handles.
struct VulkanRhiTimelineSubmitInfo {
    std::shared_ptr<VulkanRhiTimelineSemaphorePayload> waitSemaphore;
    std::uint64_t waitValue = 0u;
    std::shared_ptr<VulkanRhiTimelineSemaphorePayload> signalSemaphore;
    std::uint64_t signalValue = 0u;
};

class VulkanRhiNativeCommandPayload final
    : public VulkanRhiNativeResourcePayload,
      public std::enable_shared_from_this<VulkanRhiNativeCommandPayload> {
public:
    struct Impl;

    ~VulkanRhiNativeCommandPayload() override;

    explicit VulkanRhiNativeCommandPayload(core::UniquePtr<Impl> impl);

    bool record(std::pmr::string& diagnostics);
    bool rejectConformanceClearWork(core::pmr::String&);
    bool conformanceComputePass(bool begin, core::pmr::String&);
    bool retainRecordedStagingUploads(
        core::UniquePtr<rhi::RecordedStagingUploadJournal>& journal, std::pmr::string& diagnostics);
    bool flushPendingBarriers(std::pmr::string& diagnostics);
    bool bindSwapchainFrameSync(const VulkanRhiNativeResourcePayload& image,
        rhi::ResourceState firstUseState,
        std::pmr::string& diagnostics);
    bool waitSubmittedFence(std::pmr::string& diagnostics);
    // Explicit lifecycle wait only, never a resource-pressure fallback for
    // bindless. A timeout keeps all retained storage and budget leases alive.
    bool waitSubmittedFenceUntil(
        std::chrono::steady_clock::time_point deadline, std::pmr::string& diagnostics);
    bool recordBufferBarrier(const VulkanRhiNativeResourcePayload& buffer,
        const rhi::ResourceBarrierDesc& desc,
        std::pmr::string& diagnostics);
    bool recordAliasingBarrier(const rhi::AliasingBarrierDesc& desc, std::pmr::string& diagnostics);
    bool recordResourceMemoryBarrier(
        const rhi::ResourceMemoryBarrierDesc&, std::pmr::string& diagnostics);
    bool recordImageBarrier(const VulkanRhiNativeResourcePayload& image,
        const rhi::ResourceBarrierDesc& desc,
        std::pmr::string& diagnostics);
    bool recordImageBarrier(const VulkanRhiNativeResourcePayload& image,
        const rhi::TextureSubresourceRange& range,
        rhi::ResourceState before,
        rhi::ResourceState after,
        std::pmr::string& diagnostics);
    bool recordBufferCopy(const VulkanRhiNativeResourcePayload& source,
        const VulkanRhiNativeResourcePayload& destination,
        const rhi::BufferCopyDesc& desc,
        std::pmr::string& diagnostics);
    // Backend-private validation probe. Clear before shader use; after a
    // completed prior readback the latch's previous state is CopySrc.
    bool recordBindlessFaultLatchClear(
        const rhi::GpuBuffer& latch, rhi::ResourceState before, std::pmr::string& diagnostics);
    bool recordBindlessFaultLatchClear(std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        rhi::ResourceState before,
        std::pmr::string& diagnostics);
    // Records the shader-write barrier and a four-byte copy.
    // The returned result stays Pending until the submitted fence completes.
    std::shared_ptr<VulkanRhiBindlessFaultReadback> scheduleBindlessFaultReadback(
        const rhi::GpuBuffer& latch,
        const rhi::GpuBuffer& destination,
        std::pmr::string& diagnostics);
    std::shared_ptr<VulkanRhiBindlessFaultReadback> scheduleBindlessFaultReadback(
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot,
        const rhi::GpuBuffer& destination,
        std::pmr::string& diagnostics);
    std::shared_ptr<VulkanRhiBindlessFaultReadback> scheduleBindlessFaultReadback(
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot, std::pmr::string& diagnostics);
    rhi::BindlessFaultReport bindlessFaultReport(std::uint64_t snapshotGeneration) const noexcept;
    bool recordUploadStagingBufferCopy(VkBuffer stagingBuffer,
        std::uint64_t stagingOffsetBytes,
        const VulkanRhiNativeResourcePayload& destination,
        std::uint64_t sizeBytes,
        std::pmr::string& diagnostics);
    bool recordUploadStagingTextureCopy(VkBuffer stagingBuffer,
        std::uint64_t stagingOffsetBytes,
        const VulkanRhiNativeResourcePayload& destination,
        const rhi::UploadTextureDesc& desc,
        std::pmr::string& diagnostics);
    bool recordBufferToImageCopy(const VulkanRhiNativeResourcePayload& source,
        const VulkanRhiNativeResourcePayload& destination,
        const rhi::BufferToTextureCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool recordImageToBufferCopy(const VulkanRhiNativeResourcePayload& source,
        const VulkanRhiNativeResourcePayload& destination,
        const rhi::TextureToBufferCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool scheduleTextureReadback(const VulkanRhiNativeResourcePayload& source,
        std::shared_ptr<VulkanRhiNativeResourcePayload> destination,
        std::shared_ptr<void> allocationToken,
        void* mappedData,
        std::shared_ptr<VulkanRhiMappedBufferReadback> readback,
        const rhi::TextureDesc& textureDesc,
        std::string_view label,
        std::pmr::string& diagnostics);
    bool recordImageToImageCopy(const VulkanRhiNativeResourcePayload& source,
        const VulkanRhiNativeResourcePayload& destination,
        const rhi::TextureToTextureCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool recordRenderPassBegin(const rhi::RenderPassDesc& desc,
        const std::pmr::vector<std::shared_ptr<VulkanRhiNativeResourcePayload>>& colorTargets,
        const VulkanRhiNativeResourcePayload* depthTarget,
        std::pmr::string& diagnostics);
    /// Advance to the next subpass within the active render pass. Calls
    /// vkCmdNextSubpass. No-op if the render pass has only one subpass.
    bool recordNextSubpass(std::pmr::string& diagnostics);
    bool recordGraphicsPipelineBind(
        const VulkanRhiNativeResourcePayload& pipeline, std::pmr::string& diagnostics);
    /// Record vkCmdSetViewport for the active render pass. Vulkan 1.0 core;
    /// the pipeline must have been created with dynamicViewportScissor=true
    /// (validated at pipeline-state lowering). The viewport value is L1-
    /// validated by the encoder before this call.
    bool recordSetViewport(const rhi::ViewportState& viewport, std::pmr::string& diagnostics);
    /// Record vkCmdSetScissor for the active render pass. Vulkan 1.0 core;
    /// same lifecycle rules as recordSetViewport.
    bool recordSetScissor(const rhi::ScissorState& scissor, std::pmr::string& diagnostics);
    bool recordVertexBufferBind(std::uint32_t slot,
        const VulkanRhiNativeResourcePayload& buffer,
        std::uint64_t offsetBytes,
        std::pmr::string& diagnostics);
    bool recordIndexBufferBind(const VulkanRhiNativeResourcePayload& buffer,
        rhi::IndexFormat format,
        std::uint64_t offsetBytes,
        std::pmr::string& diagnostics);
    bool recordPushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data,
        std::pmr::string& diagnostics);
    bool recordDrawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance,
        std::pmr::string& diagnostics);
    bool recordDrawIndexedIndirect(const VulkanRhiNativeResourcePayload& argsBuffer,
        std::uint64_t argsOffset,
        std::uint32_t drawCount,
        std::uint32_t stride,
        std::pmr::string& diagnostics);
    bool recordDrawIndexedIndirectCount(const VulkanRhiNativeResourcePayload& argsBuffer,
        std::uint64_t argsOffset,
        const VulkanRhiNativeResourcePayload& countBuffer,
        std::uint64_t countOffset,
        std::uint32_t maxDrawCount,
        std::uint32_t stride,
        std::pmr::string& diagnostics);
    bool recordDraw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance,
        std::pmr::string& diagnostics);
    bool recordRenderPassEnd(std::pmr::string& diagnostics);
    bool recordComputePipelineBind(
        const VulkanRhiNativeResourcePayload& pipeline, std::pmr::string& diagnostics);
    bool recordComputePushConstants(std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data,
        std::pmr::string& diagnostics);
    bool recordDescriptorSetBind(const VulkanRhiNativeResourcePayload& descriptorSet,
        std::uint32_t setIndex,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount,
        std::pmr::string& diagnostics);
    bool recordDispatch(std::uint32_t groupCountX,
        std::uint32_t groupCountY,
        std::uint32_t groupCountZ,
        std::pmr::string& diagnostics);
    bool recordDispatchIndirect(const VulkanRhiNativeResourcePayload& indirectBuffer,
        std::uint64_t indirectOffset,
        std::pmr::string& diagnostics);
    bool recordTimestamp(
        const rhi::QueryPool& queryPool, std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool recordPushDebugGroup(std::string_view label, std::pmr::string& diagnostics);
    bool recordPopDebugGroup(std::pmr::string& diagnostics);
    bool recordInsertDebugMarker(std::string_view label, std::pmr::string& diagnostics);
    bool end(std::pmr::string& diagnostics);
    bool submit(std::pmr::string& diagnostics);
    bool submit(const VulkanRhiTimelineSubmitInfo& timeline,
        std::pmr::string& diagnostics,
        bool* acceptedThisAttempt = nullptr);
    void retainResource(std::shared_ptr<VulkanRhiNativeResourcePayload> payload);
    // Private original-derived storage preflight. No mutation/allocation.
    const char* recordingBarrierStorageDiagnostic(rhi::CommandUsageKind kind) const noexcept;
    const char* resourceRetentionStorageDiagnostic(std::size_t count) const noexcept;
    const char* recordingStorageDiagnostic() const noexcept;
    // Read-only authentication of prepared attachment observations. This cannot
    // propose/complete content or activate the private cold Graphics wrappers.
    const char* preparedOrdinaryRenderPassContentDiagnostic() const noexcept;
    const char* originalQuerySlotDiagnostic() const noexcept;
    bool recordingStorageBound() const noexcept;
    // Cancel entries without destroying the journal aliased by ordinary wrappers.
    void abandonOrdinaryRecording() noexcept;
    const char* internalTimingDiagnostic() const noexcept;
    const rhi::common::CommandRetentionStorage* queryRecordingStorage() const noexcept;
    void rejectRecordingStorage(const char* diagnostic) noexcept;
    const char* retainResources(
        const std::shared_ptr<VulkanRhiNativeResourcePayload>* payloads, std::size_t count);
    void retainTimingQueryPool(std::shared_ptr<VulkanRhiQueryPoolPayload> payload);
    void observeFrameSerial(std::uint64_t frameSerial);
    bool retainIndexedPipeline(const std::shared_ptr<const void>&, core::pmr::String& diagnostic);
    bool canRetainPreparedBinding(
        const std::shared_ptr<const rhi::BindlessPreparedBinding>&) const noexcept;
    bool retainPreparedBinding(
        const std::shared_ptr<const rhi::BindlessPreparedBinding>&, core::pmr::String& diagnostic);
    bool retainIndexedSnapshot(const std::shared_ptr<const void>&, core::pmr::String& diagnostic);
    std::uint64_t observedFrameSerial() const;
    bool submittedFenceCompleted();
    bool acceptOrdinaryRecordedOwnership() const noexcept;
    // Private original ordinary attachment profile; no public capability is
    // inferred from these backend implementation entrypoints.
    bool hasOrdinaryAttachments() const noexcept;
    bool hasOrdinaryMixedPasses() const noexcept;
    const char* recordOrdinaryRenderPassBegin(std::uint32_t, const rhi::RenderPassDesc&) noexcept;
    const char* recordOrdinaryRenderPassEnd(std::uint32_t) noexcept;
    const char* recordOrdinaryGraphicsCommand(std::uint32_t,
        const rhi::CommandScalarArguments&,
        const rhi::RenderPipeline* = nullptr,
        const rhi::DescriptorSet* = nullptr) noexcept;
    void resolveBindlessFaultReadbacks();

private:
    // Only sealed canonical issuers may use these shared native emitters for
    // exclusive original content, after validating the same pending journal.
    friend class VulkanRhiCommandEncoder;
    friend class VulkanRhiComputePassEncoder;
    friend class VulkanRhiTransferPassEncoder;
    bool prepareOrdinaryRenderPasses(const rhi::GpuDevice&,
        const rhi::OriginalRenderPassPreparationRequest&,
        core::pmr::String&);
    bool prepareOrdinaryTextureUploads(rhi::GpuDevice&,
        VulkanRhiStagingUploadPool&,
        rhi::RecordedStagingUploadJournal&,
        std::uint64_t,
        const rhi::OriginalOrdinaryTextureUploadContract::Budget&,
        const rhi::OriginalOrdinaryTextureUploadContract::Binding*,
        std::size_t,
        core::pmr::String&);
    bool hasOrdinaryTextureUploads() const noexcept;
    const char* preflightOrdinaryAttachmentOperation(
        rhi::CommandUsageKind, std::uint32_t) const noexcept;
    const char* preflightOrdinaryBufferTransition(
        const rhi::GpuBuffer&, const rhi::ResourceBarrierDesc&, std::uint32_t) const noexcept;
    const char* recordOrdinaryMixedCommand(std::uint32_t,
        const rhi::CommandScalarArguments&,
        const rhi::ComputePipeline* = nullptr,
        const rhi::DescriptorSet* = nullptr,
        const rhi::TextureToBufferCopyDesc* = nullptr) noexcept;
    bool recordImageToBufferCopyImpl(const VulkanRhiNativeResourcePayload&,
        const VulkanRhiNativeResourcePayload&,
        const rhi::TextureToBufferCopyDesc&,
        core::pmr::String&,
        bool);
    const char* claimOrdinaryAttachments() noexcept;
    const char* validateOrdinaryAttachmentsPending() const noexcept;
    const char* completeOrdinaryAttachments() noexcept;
    const char* sealOrdinaryAttachments(std::uint32_t) noexcept;
    void consumeOrdinaryAttachmentOperation() noexcept;
    const char* preflightOrdinaryAttachmentTransition(
        const rhi::GpuTexture&, const rhi::ResourceBarrierDesc&, std::uint32_t) const noexcept;
    const char* preflightOrdinaryTextureTransition(
        const rhi::GpuTexture&, const rhi::ResourceBarrierDesc&, std::uint32_t) const noexcept;
    bool authorizesOrdinaryTextureTransition(const rhi::GpuTexture&,
        const rhi::ResourceBarrierDesc&,
        const core::MutationJournal*) const noexcept;
    friend rhi::ResourceBarrierResult transitionVulkanRhiTexture(rhi::GpuTexture&,
        const rhi::ResourceBarrierDesc&,
        core::MutationJournal*,
        const VulkanRhiNativeCommandPayload*);
    const char* recordOrdinaryTextureUpload(std::uint32_t, const rhi::UploadTextureDesc&) noexcept;
    bool adoptOriginalStreamJournal(core::UniquePtr<core::MutationJournal>&, core::pmr::String&);
    bool adoptOriginalQueryJournal(core::UniquePtr<core::MutationJournal>&,
        std::uint64_t recordingIdentity,
        const rhi::semantics::NativeCommandLoweringDecision&,
        core::pmr::String&);
    const char* recordOriginalTimestamp(const rhi::QueryPool&,
        std::uint32_t queryIndex,
        std::uint32_t commandOrdinal,
        std::uint64_t recordingIdentity) noexcept;
    const char* sealOriginalQueries(std::uint32_t commandCount) noexcept;
    bool recordValidatedOriginalImageBarrier(
        const VulkanRhiNativeResourcePayload&, const rhi::ResourceBarrierDesc&, core::pmr::String&);
    bool recordValidatedOriginalTextureCopy(VkBuffer,
        std::uint64_t,
        const VulkanRhiNativeResourcePayload&,
        const rhi::UploadTextureDesc&,
        core::pmr::String&,
        bool coldPreflight = false,
        bool coldEmission = false);
    bool prepareOriginalSubmissionFence(core::pmr::String& diagnostic);
    bool recordBindlessFaultLatchClearPreamble(
        const std::shared_ptr<VulkanRhiNativeResourcePayload>& latch,
        rhi::ResourceState before,
        std::pmr::string& diagnostics);
    void resolveSubmittedCompletion();
    core::UniquePtr<Impl> impl_;
    friend class VulkanConformanceClearExecution;
    friend class VulkanConformanceClearFrame;
    friend class VulkanOriginalBindlessCommand;
    friend class VulkanOriginalBindlessGraphicsExecutionView;
    friend class VulkanOriginalBindlessComputeExecutionView;
    friend class VulkanOriginalTextureUploadCommand;
    friend class VulkanRhiRetainedCommand;
    friend void drainVulkanRhiSubmittedCommandsForShutdown();
    bool prepareInternalProfiling(core::pmr::String&);
    friend const char* waitIdleAndRetireVulkanRhiSubmittedCommands(
        const std::shared_ptr<VulkanRhiRuntime>&);

    friend std::shared_ptr<VulkanRhiNativeCommandPayload> createVulkanRhiNativeCommandPayload(
        const VulkanRhiNativeResourceAllocator& nativeResources,
        std::pmr::string& diagnostics,
        const rhi::CommandEncoderDesc& retention,
        const VulkanRhiRecordingStorageLimits* storage);
};

std::shared_ptr<VulkanRhiNativeCommandPayload> createVulkanRhiNativeCommandPayload(
    const VulkanRhiNativeResourceAllocator& nativeResources,
    std::pmr::string& diagnostics,
    const rhi::CommandEncoderDesc& retention = {},
    const VulkanRhiRecordingStorageLimits* storage = nullptr);

bool recordVulkanRhiNativeCommand(
    const std::shared_ptr<VulkanRhiNativeCommandPayload>& nativePayload,
    std::pmr::string& diagnostics);

void retireCompletedVulkanRhiSubmittedCommands();
// Explicit blocking GpuDevice::waitIdle path, never a per-frame fallback.
// Executes real vkDeviceWaitIdle and only then completes this exact runtime's
// submissions. No supplied result/flag can authorize retirement; failure retains.
// Returns a borrowed stable diagnostic, or nullptr on success.
const char* waitIdleAndRetireVulkanRhiSubmittedCommands(
    const std::shared_ptr<VulkanRhiRuntime>& runtime);
// Bounded resource pools call this only under allocation pressure. It waits
// for already-submitted work and retires its resource payloads before reuse.
void waitForVulkanRhiSubmittedCommandsForResourcePressure();
// Headless shutdown only: wait for every tracked Vulkan submission using one
// absolute deadline. A failure retains unfinished commands and their storage.
bool drainVulkanRhiSubmittedCommandsUntil(
    std::chrono::steady_clock::time_point deadline, std::pmr::string& diagnostics);
void drainVulkanRhiSubmittedCommandsForShutdown();
std::uint64_t completedVulkanRhiSubmittedFrameSerial();

/// Latest completed backend-owned timestamp result. This is intentionally an
/// internal Vulkan bridge; VulkanRhiDevice publishes it through the generic
/// GpuDevice::latestGpuTiming() contract.
rhi::GpuTimingSample latestVulkanRhiGpuTimingSample();

} // namespace arp::rhi_vulkan
