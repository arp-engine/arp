#pragma once
#include "core/atomic_capacity.h"
#include "core/bounded_retainer.h"
#include "core/cpu_allocator.h"
#include "core/external_object_slot.h"
#include "core/mutation_journal.h"
#include "rhi/backends/metal/metal_native_queries.h"
#include "rhi/backends/metal/metal_original_command_custody.h"
#include "rhi/frontend/bindless_binding_preparation.h"
#include "rhi/frontend/bindless_recording_validation.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_gpu_indirect_output_sequence.h"
#include "rhi/frontend/command_graphics_storage_write_sequence.h"
#include "rhi/frontend/command_indirect_recording_budget.h"
#include "rhi/frontend/command_retention_storage.h"
#include "rhi/frontend/descriptor_bound_descriptor_slots.h"
#include "rhi/frontend/memory_shutdown_drain.h"
#include "rhi/frontend/transfer_recorded_staging_upload.h"
#include "rhi/gpu_commands.h"
#include "rhi/semantics/metal_vertex_stream_lowering.h"
#include "rhi/semantics/push_constant_validation.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace arp::rhi_metal {
class MetalOriginalBindlessCommand;
struct MetalRhiNativeRenderPassPreparation;
struct MetalRhiNativePreparedComputeBindings;
class MetalRhiOrdinaryPreparedComputeBinding;
class MetalRhiOrdinaryPreparedComputePush;
class MetalRhiOrdinaryPreparedGraphicsPush;
class MetalRhiOrdinaryPreparedGraphicsBinding;
class MetalRhiOrdinaryPreparedVertexBinding;
class MetalRhiOrdinaryPreparedIndirectArguments;
class MetalRhiOrdinaryPreparedIndexedDraw;
class MetalRhiOrdinaryPreparedRenderPass;
class MetalRhiIndirectExecutionPreparation;
class MetalRhiCanonicalProducerRecordingPreparation;
class MetalRhiClosedConsumerRecordingReservation;
class MetalRhiClosedConsumerNativePreparation;
class MetalRhiOrdinaryPreparedGraphicsWrite;
class MetalRhiOrdinaryPreparedWriteReadback;
class MetalRhiOrdinaryPreparedGraphicsReadConsumer;
struct MetalRhiOrdinaryWritePrototypeIntent;
struct MetalRhiNativePreparedGraphicsBindings;

class MetalRhiResourceRegistry;
struct MetalRhiRegisteredResource;
enum class MetalReadbackCompletion { Pending, Completed, Failed, Abandoned };

class MetalRhiBindlessFaultReadback final {
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
    friend class MetalRhiNativeCommandPayload;

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
    std::atomic<std::uint32_t> bits_{0u};
};

struct MetalRhiNativePipelinePayload;
struct MetalRhiNativeDescriptorGroupPayload;
struct MetalRhiNativeDescriptorTablePayload;
struct MetalRhiNativeBufferPayload;
struct MetalRhiNativeTexturePayload;
struct MetalRhiNativeSharedEventPayload;
struct MetalRhiNativeSecondaryCommandPayload;

class MetalRhiCompletionState final {
public:
    enum class Outcome { Pending, Completed, Failed };

    void complete(bool success = true)
    {
        {
            std::scoped_lock lock(mutex_);
            outcome_.store(
                success ? Outcome::Completed : Outcome::Failed, std::memory_order_release);
        }
        condition_.notify_all();
    }

    bool isComplete() const { return outcome() != Outcome::Pending; }

    Outcome outcome() const noexcept { return outcome_.load(std::memory_order_acquire); }

    void wait()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] {
            return isComplete();
        });
    }

    void reset()
    {
        std::scoped_lock lock(mutex_);
        outcome_.store(Outcome::Pending, std::memory_order_release);
    }

private:
    std::atomic<Outcome> outcome_{Outcome::Pending};
    std::mutex mutex_;
    std::condition_variable condition_;
};

struct MetalRhiNativeCommandStats {
    std::uint32_t closedConsumerCommandCount = 0u;
    std::uint32_t canonicalProducerCommandCount = 0u;
    std::uint32_t ordinaryWriteCommandCount = 0u;
    std::uint32_t ordinaryIndirectCommandCount = 0u, ordinaryIndirectCallCount = 0u,
                  ordinaryIndirectRecordCount = 0u;
    std::uint32_t protectedIndirectCommandCount = 0u, protectedIndirectCallCount = 0u,
                  protectedIndirectRecordCount = 0u;
    bool backendPayloadPresent = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool nativeCommandBufferCreated = false;
    bool nativeRenderEncoderCreated = false;
    bool nativeComputeEncoderCreated = false;
    bool nativeBlitEncoderCreated = false;
    bool nativeGraphicsPipelineBound = false;
    bool nativeComputePipelineBound = false;
    bool nativeDescriptorArgumentBound = false;
    bool nativePushConstantUploadBufferWritten = false;
    bool nativePushConstantSetBytesWritten = false;
    std::uint32_t nativeArgumentBufferAllocationCount = 0;
    std::uint32_t nativeArgumentBufferReuseCount = 0;
    std::uint32_t nativeBindlessFaultOutcomePreparationCount = 0;
    std::uint32_t nativeBindlessFaultOutcomeConsumeCount = 0;
    bool nativeIndexedDrawEncoded = false;
    bool nativeDrawEncoded = false;
    bool nativeDispatchEncoded = false;
    bool nativeBlitCopyEncoded = false;
    bool nativeCommandBufferFinished = false;
    bool nativeAliasingBarrierEncoded = false;
    bool nativeAliasableTransitionEncoded = false;
    bool nativeResourceBarrierEncoded = false;
    bool nativeDepthStencilAttachmentRecorded = false;
    bool nativeDepthStencilStateBound = false;
    // Records that a graphics pipeline payload with depthBiasEnabled=true was
    // bound and applied through setDepthBias:slopeScale:clamp:.
    bool nativeDepthBiasSet = false;
    // Records dynamic raster state reaching the native render encoder.
    bool nativeViewportSet = false;
    bool nativeScissorSet = false;
    std::uint32_t nativeViewportSetCount = 0;
    std::uint32_t nativeScissorSetCount = 0;
    bool nativeColorClearValueRecorded = false;
    rhi::Color nativeColorClearValue;
    std::uint32_t nativePushConstantUploadBytes = 0;
};

struct MetalRhiNativeQueueStats {
    bool backendPayloadPresent = false;
    bool nativeRuntimeBacked = false;
    bool nativeRuntimeDiagnosticOnly = true;
    bool nativeHandleExposedToUpperLayer = false;
    bool boundedCompletionTelemetry = false;
    bool blockingWaitUsed = false;
    std::uint32_t nativeQueueSubmitCount = 0;
};

class MetalRhiNativeCommandPayload : public core::SharedObjectIdentity {
    friend class MetalOriginalBindlessCommand;
    friend class MetalOriginalTextureUploadCommand;
    friend class MetalOriginalBindlessExecutionView;
    friend class MetalOriginalBindlessGraphicsExecutionView;
    friend class MetalRhiIndirectOutputRecordingLease;
    friend class MetalRhiCanonicalProducerRecordingPreparation;
    friend class MetalRhiClosedConsumerRecordingPreparation;
    friend class MetalRhiClosedConsumerRecordingReservation;
    friend class MetalRhiClosedConsumerNativePreparation;
    friend class MetalRhiIndirectExecutionPreparation;

public:
    std::uint64_t recordingIdentity() const noexcept { return recordingIdentity_; }

    ~MetalRhiNativeCommandPayload()
    {
        if (originalCommandCustody_ || indirectOutputBudget_ || canonicalProducerBudget_
            || closedConsumerBudget_ || closedConsumerReservation_)
            abandonUnsubmitted();
    }

    // Private prototype evidence only; public CommandRetentionBudget stays closed.
    const rhi::common::CommandRetentionStorage* retentionStorage() const noexcept
    {
        return recordingRetention_.get();
    }

    MetalRhiNativeCommandPayload(std::shared_ptr<MetalRhiNativeCommandStats> stats,
        std::shared_ptr<void> nativeQueue,
        std::shared_ptr<void> nativeCommandBuffer,
        std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
        std::shared_ptr<core::AtomicCapacityLease> commandCapacity = {},
        std::uint32_t maxBindlessPipelines = 0u,
        std::uint32_t maxBindlessSnapshots = 0u,
        std::uint32_t maxBindlessGraphicsAccesses = 0u);

    bool recordBufferBarrier(std::pmr::string& diagnostics);
    bool recordTextureBarrier(std::pmr::string& diagnostics);
    bool recordAliasingBarrier(const rhi::AliasingBarrierDesc& desc,
        const std::shared_ptr<MetalRhiNativeTexturePayload>& beforeTexture,
        const std::shared_ptr<MetalRhiNativeTexturePayload>& afterTexture,
        std::pmr::string& diagnostics);
    bool recordRenderPassBegin(const rhi::RenderPassDesc& desc, std::pmr::string& diagnostics);
    bool recordGraphicsPipelineBind(const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline,
        std::pmr::string& diagnostics);
    bool recordSetViewport(const rhi::ViewportState& viewport, std::pmr::string& diagnostics);
    bool recordSetScissor(const rhi::ScissorState& scissor, std::pmr::string& diagnostics);
    bool recordVertexBufferBind(std::uint32_t slot,
        const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
        std::uint64_t offsetBytes,
        std::pmr::string& diagnostics);
    bool recordOrdinaryPreparedVertexBinding(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>&, core::pmr::String&);
    bool recordIndexBufferBind(const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
        rhi::IndexFormat format,
        std::uint64_t offsetBytes,
        std::pmr::string& diagnostics);
    bool recordTessellationFactorBufferBind(
        const std::shared_ptr<MetalRhiNativeBufferPayload>& buffer,
        std::uint64_t offsetBytes,
        std::uint32_t instanceStrideBytes,
        std::pmr::string& diagnostics);
    bool validateTessellationFactorBufferRange(
        std::uint32_t instanceCount, std::pmr::string& diagnostics);
    bool recordDescriptorSetBind(std::uint32_t set,
        const std::shared_ptr<MetalRhiNativeDescriptorGroupPayload>& descriptorSet,
        rhi::ShaderStage stages,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount,
        std::pmr::string& diagnostics);
    bool recordOrdinaryPreparedComputeBinding(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding>&,
        core::pmr::String&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush>& = {});
    bool recordOrdinaryPreparedRenderPassBegin(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        core::pmr::String&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& = {});
    bool recordCanonicalProducer(
        const std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation>&,
        core::pmr::String&);
    bool recordClosedConsumer(
        const std::shared_ptr<const MetalRhiClosedConsumerNativePreparation>&, core::pmr::String&);
    bool recordProtectedIndirectExecution(
        const std::shared_ptr<const MetalRhiIndirectExecutionPreparation>&,
        const rhi::BindlessResourceAccess& producer,
        const rhi::ResourceBarrierDesc& dependency,
        const rhi::BindlessResourceAccess& consumer,
        core::pmr::String&);
    bool recordOrdinaryPreparedWriteRenderPassBegin(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>&,
        core::pmr::String&);
    bool recordOrdinaryPreparedWriteDependency(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>&, core::pmr::String&);
    bool recordOrdinaryPreparedWriteReadback(
        const std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback>&, core::pmr::String&);
    bool recordOrdinaryPreparedReadConsumerBegin(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>&,
        core::pmr::String&);

    std::uint32_t ordinaryWritePrototypeVersion() const noexcept
    {
        return ordinaryWriteStates_ ? 1u : 0u;
    }

    bool recordOrdinaryPreparedGraphicsBinding(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        core::pmr::String&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>& = {});
    bool recordPreparedComputeBinding(
        const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>&,
        const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&,
        core::pmr::String& diagnostic);
    bool validatePreparedComputeBinding(
        const std::shared_ptr<const MetalRhiNativePreparedComputeBindings>&,
        const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>&,
        core::pmr::String& diagnostic) const;
    // Private recording prerequisite, not a public indexed draw authorization.
    bool recordPreparedGraphicsBinding(
        const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
        core::pmr::String& diagnostic);
    bool validatePreparedGraphicsBinding(
        const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
        core::pmr::String& diagnostic) const;
    bool prepareGraphicsPassScope(std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>,
        std::shared_ptr<const rhi::BindlessFaultDomain>,
        const rhi::BindlessAccessDeclarations&,
        core::pmr::String& diagnostic);
    bool prepareGraphicsPassScope(std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>,
        const rhi::BindlessAccessDeclarations&,
        core::pmr::String& diagnostic);
    bool recordScopedGraphicsBinding(
        const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
        const rhi::BindlessAccessDeclarations&,
        core::pmr::String& diagnostic);

    bool hasGraphicsPassScope() const noexcept
    {
        return static_cast<bool>(pendingGraphicsBinding_)
            || static_cast<bool>(activeGraphicsBinding_);
    }

    bool recordDescriptorTableBind(std::uint32_t set,
        const std::shared_ptr<MetalRhiNativeDescriptorTablePayload>& descriptorTable,
        rhi::ShaderStage stages,
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
    bool recordOrdinaryPreparedIndexedDraw(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedIndexedDraw>&, core::pmr::String&);
    bool recordOrdinaryPreparedIndirectDraw(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedIndirectArguments>&,
        const rhi::BindlessResourceAccess&,
        core::pmr::String&);
    bool recordDrawIndexedIndirect(
        const std::shared_ptr<MetalRhiNativeBufferPayload>& indirectBuffer,
        std::uint64_t indirectOffset,
        std::uint32_t drawCount,
        std::uint32_t stride,
        std::pmr::string& diagnostics);
    bool recordGpuCountedIndirectCountBuffer(
        const std::shared_ptr<MetalRhiNativeBufferPayload>& countBuffer,
        std::uint64_t countOffset,
        std::pmr::string& diagnostics);
    bool recordDraw(std::uint32_t vertexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstVertex,
        std::uint32_t firstInstance,
        std::pmr::string& diagnostics);
    bool recordRenderPassEnd(std::pmr::string& diagnostics);
    bool recordNextSubpass(std::pmr::string& diagnostics);
    bool recordComputePassBegin(std::pmr::string& diagnostics);
    bool recordComputePipelineBind(const std::shared_ptr<MetalRhiNativePipelinePayload>& pipeline,
        std::pmr::string& diagnostics);
    bool retainIndexedPipeline(const std::shared_ptr<const void>&, std::pmr::string& diagnostics);
    bool retainIndexedSnapshot(const std::shared_ptr<const void>&, std::pmr::string& diagnostics);
    bool recordBindlessFaultLatchClear(std::shared_ptr<const rhi::BindlessSnapshot>,
        rhi::ResourceState before,
        std::pmr::string& diagnostics);
    bool recordBindlessFaultLatchClearBeforePass(std::shared_ptr<const rhi::BindlessFaultDomain>,
        rhi::ResourceState before,
        core::pmr::String& diagnostics);
    std::shared_ptr<MetalRhiBindlessFaultReadback> scheduleBindlessFaultReadback(
        std::shared_ptr<const rhi::BindlessFaultDomain>, std::pmr::string& diagnostics);
    rhi::BindlessFaultReport bindlessFaultReport(std::uint64_t generation) const noexcept;
    bool recordDispatch(
        std::uint32_t x, std::uint32_t y, std::uint32_t z, std::pmr::string& diagnostics);
    bool recordDispatchIndirect(const std::shared_ptr<MetalRhiNativeBufferPayload>& indirectBuffer,
        std::uint64_t indirectOffset,
        std::pmr::string& diagnostics);
    bool recordDrawIndexedIndirectCount(
        const std::shared_ptr<MetalRhiNativeBufferPayload>& argsBuffer,
        std::uint64_t argsOffset,
        const std::shared_ptr<MetalRhiNativeBufferPayload>& countBuffer,
        std::uint64_t countOffset,
        std::uint32_t maxDrawCount,
        std::uint32_t stride,
        std::pmr::string& diagnostics);
    bool recordTimestamp(const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& queryPool,
        std::uint32_t queryIndex,
        std::pmr::string& diagnostics);
    bool recordPipelineStatistics(const std::shared_ptr<MetalRhiNativeQueryPoolPayload>& queryPool,
        std::uint32_t queryIndex,
        std::pmr::string& diagnostics);
    bool executeSecondary(const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& secondary,
        std::pmr::string& diagnostics);
    bool executeSecondaryCommand(
        const std::shared_ptr<MetalRhiNativeSecondaryCommandPayload>& secondary,
        std::uint32_t commandIndex,
        std::pmr::string& diagnostics);
    bool recordComputePassEnd(std::pmr::string& diagnostics);
    bool recordBlitPassBegin(std::pmr::string& diagnostics);
    bool recordBlitCopy(const rhi::BufferCopyDesc& desc, std::pmr::string& diagnostics);
    bool recordBlitBufferToTexture(
        const rhi::BufferToTextureCopyDesc& desc, std::pmr::string& diagnostics);
    bool recordUploadStagingBufferCopy(const std::shared_ptr<void>& stagingPayload,
        std::uint64_t stagingOffset,
        std::uint64_t sizeBytes,
        rhi::GpuResourceId destinationBuffer,
        std::pmr::string& diagnostics);
    bool recordUploadStagingTextureCopy(const std::shared_ptr<void>& stagingPayload,
        std::uint64_t stagingOffset,
        const rhi::UploadTextureDesc& desc,
        std::pmr::string& diagnostics);
    bool recordBlitTextureToBuffer(
        const rhi::TextureToBufferCopyDesc& desc, std::pmr::string& diagnostics);
    bool preflightBlitTextureToBuffer(
        const rhi::TextureToBufferCopyDesc& desc, std::pmr::string& diagnostics);
    bool recordBlitTextureCopy(
        const rhi::TextureToTextureCopyDesc& desc, std::pmr::string& diagnostics);
    bool recordBlitPassEnd(std::pmr::string& diagnostics);
    bool recordPushDebugGroup(std::string_view label, std::pmr::string& diagnostics);
    bool recordPopDebugGroup(std::pmr::string& diagnostics);
    bool recordInsertDebugMarker(std::string_view label, std::pmr::string& diagnostics);
    bool finish(std::pmr::string& diagnostics);
    // Native encoders cannot undo commands already submitted to an encoder.
    // Poisoning makes a partial recording non-submittable after a replay
    // mutation fails.
    void poisonRecording(std::string_view diagnostics);

    bool recordingPoisoned() const noexcept { return recordingPoisoned_; }

    bool submit(MetalRhiNativeQueueStats& queueStats,
        const std::shared_ptr<MetalRhiCompletionState>& completion,
        const std::shared_ptr<MetalRhiNativeSharedEventPayload>& waitEvent,
        std::uint64_t waitValue,
        const std::shared_ptr<MetalRhiNativeSharedEventPayload>& signalEvent,
        std::uint64_t signalValue,
        std::pmr::string& diagnostics);
    void markStagingUpload(std::shared_ptr<std::atomic<std::uint64_t>> completedEpoch);

    void adoptStagingUploads(core::UniquePtr<rhi::RecordedStagingUploadJournal> uploads)
    {
        stagingUploads_ = std::move(uploads);
    }

    std::shared_ptr<MetalRhiNativeCommandStats> stats() const { return stats_; }

    std::shared_ptr<MetalRhiCompletionState> preparedCompletionState() const;
    MetalReadbackCompletion readbackCompletionStatus() const;

    bool readbackWasSubmitted() const { return submitted_.load(std::memory_order_acquire); }

    // Public recording/submission ownership, independent of storage observers.
    // Called on the serialized owner thread; never abandons submitted GPU work.
    void abandonUnsubmitted();

    bool usesRegistry(const std::shared_ptr<MetalRhiResourceRegistry>& registry) const
    {
        return resourceRegistry_ == registry;
    }

private:
    struct VertexStreamBinding {
        std::shared_ptr<MetalRhiNativeBufferPayload> buffer;
        std::uint64_t offsetBytes = 0u;
    };

    struct CachedArgumentBuffer {
        std::shared_ptr<MetalRhiNativePipelinePayload> pipeline;
        std::shared_ptr<void> descriptorSnapshot;
        std::shared_ptr<void> argumentBuffer;
        std::uint32_t set = 0u;
        std::uint32_t binding = 0u;
        std::uint32_t argumentBufferSlot = 0u;
        rhi::ShaderStage stage = rhi::ShaderStage::None;
    };

    // VertexStreamBinding/1: logical state survives pipeline remapping, never
    // encodes into the shader buffer namespace before a mapping is available.
    std::array<VertexStreamBinding, 31> vertexStreams_;
    std::array<std::shared_ptr<const MetalRhiOrdinaryPreparedVertexBinding>,
        rhi::semantics::MetalVertexStreamMapping::capacity>
        ordinaryVertexBindings_{};
    bool validateVertexStreams(std::pmr::string& diagnostics) const;
    bool beginPreparedNativeRenderPass(const MetalRhiNativeRenderPassPreparation&,
        const std::shared_ptr<core::ExternalObjectSlot>&,
        core::pmr::String&);
    bool beginPreparedNativeComputePass(
        const std::shared_ptr<core::ExternalObjectSlot>&, core::pmr::String&);
    bool beginPreparedNativeBlitPass(
        const std::shared_ptr<core::ExternalObjectSlot>&, core::pmr::String&);
    std::shared_ptr<MetalRhiNativeCommandStats> stats_;
    std::shared_ptr<void> nativeDevice_;
    std::shared_ptr<void> nativeQueue_;
    // Declared before native buffers: discard destroys native ownership first.
    std::shared_ptr<core::AtomicCapacityLease> commandCapacity_;
    std::shared_ptr<void> nativeCommandBuffer_;
    // QueueSubmitStartOrder/1: a single same-queue prologue, never a retired list.
    std::shared_ptr<void> nativeSubmitWaitCommandBuffer_;
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry_;
    std::shared_ptr<void> nativeRenderEncoder_;
    std::shared_ptr<core::ExternalObjectSlot> preparedRenderEncoderSlot_;
    std::shared_ptr<void> nativeComputeEncoder_;
    std::shared_ptr<void> nativeBlitEncoder_;
    std::shared_ptr<MetalRhiNativePipelinePayload> renderPipeline_;
    std::shared_ptr<MetalRhiNativePipelinePayload> computePipeline_;
    rhi::BoundDescriptorSlots ordinaryComputeDescriptors_;
    std::array<std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding>,
        rhi::BoundDescriptorSlots::kMaxTrackedSlots>
        ordinaryComputeBindings_{};
    std::shared_ptr<const MetalRhiOrdinaryPreparedComputeBinding> ordinaryComputeContract_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedComputePush> ordinaryComputePush_;
    rhi::semantics::PushConstantConsumptionState ordinaryComputePushState_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass> ordinaryGraphicsPass_;
    std::shared_ptr<core::MutationJournal> ordinaryWriteStates_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite> ordinaryWrite_;
    std::shared_ptr<MetalRhiOrdinaryPreparedWriteReadback> ordinaryWriteReadback_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer> ordinaryReadConsumer_;
    std::shared_ptr<core::ExternalObjectSlot> preparedWriteBlitEncoderSlot_;
    rhi::GraphicsStorageWriteSequence ordinaryWriteSequence_;
    std::uint32_t maxOrdinaryWriteDraws_ = 0u, ordinaryWriteDraws_ = 0u;
    std::uint32_t maxOrdinaryConsumerDraws_ = 0u, ordinaryConsumerDraws_ = 0u;
    rhi::BoundDescriptorSlots ordinaryGraphicsDescriptors_;
    std::array<std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>,
        rhi::BoundDescriptorSlots::kMaxTrackedSlots>
        ordinaryGraphicsBindings_{};
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding> ordinaryGraphicsContract_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush> ordinaryGraphicsPush_;
    rhi::semantics::PushConstantConsumptionState ordinaryGraphicsPushState_;
    bool validateGraphicsPipelineBind(const std::shared_ptr<MetalRhiNativePipelinePayload>&,
        core::pmr::String&,
        bool requireEncoder = true) const;
    bool recordGraphicsPipelineBindUnchecked(
        const std::shared_ptr<MetalRhiNativePipelinePayload>&, core::pmr::String&);
    bool validateOrdinaryGraphicsBinding(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        core::pmr::String&,
        const MetalRhiOrdinaryPreparedGraphicsWrite* write = nullptr) const;
    bool recordOrdinaryPreparedRenderPassBeginAt(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        core::pmr::String&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsWrite>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsReadConsumer>& = {});
    bool validateOrdinaryGraphicsPush(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>&,
        core::pmr::String&) const;
    bool bindOrdinaryGraphicsUnchecked(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        core::pmr::String&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>&);
    bool validateOrdinaryGraphicsDraw(
        core::pmr::String&, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) const;
    bool validateOrdinaryGraphicsFixed(
        core::pmr::String&, const MetalRhiOrdinaryPreparedGraphicsWrite* = nullptr) const;
    // The registry is intentionally weak so resource destruction can remove
    // stale IDs. Once a command has encoded a resource, the command payload
    // owns the native payload until GPU completion.
    core::pmr::Vector<std::shared_ptr<void>> retainedResourcePayloads_;
    // This is intentionally a small contiguous list rather than a hash set.
    // Command payload storage is backed by the shared PMR; node allocations
    // from a hash set accumulate across command reuse even after clear().
    core::pmr::Vector<const void*> retainedResourcePayloadIdentities_;
    core::pmr::Vector<std::shared_ptr<void>> retainedStagingPayloads_;
    core::pmr::Vector<std::shared_ptr<void>> retainedArgumentBuffers_;
    core::BoundedRetainer<void> retainedBindlessPipelines_;
    core::BoundedRetainer<void> retainedBindlessSnapshots_;
    core::BoundedRetainer<void> retainedPreparedBindings_;
    class SubmissionRetention;
    std::shared_ptr<SubmissionRetention> submissionRetention_;
    core::UniquePtr<rhi::common::CommandRetentionStorage> recordingRetention_;
    bool usesRecordingRetention_ = false;
    std::optional<rhi::IndirectRecordingBudgetState> ordinaryIndirectRecording_;
    std::optional<rhi::GpuIndirectOutputRecordingBudget> indirectOutputBudget_;
    std::shared_ptr<core::MutationJournal> indirectOutputStates_;
    std::shared_ptr<core::ExternalObjectSlot> preparedIndirectComputeEncoderSlot_;
    std::shared_ptr<const MetalRhiIndirectExecutionPreparation> indirectExecution_;
    std::optional<rhi::GpuIndirectOutputSequence> indirectOutputSequence_;
    void cancelIndirectExecution(bool failure) noexcept;
    std::optional<rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Budget>
        canonicalProducerBudget_;
    std::optional<rhi::CanonicalZeroBaseIndirectProducerRecordingContract::PrivateLowering>
        canonicalProducerLowering_;
    std::shared_ptr<core::MutationJournal> canonicalProducerStates_;
    std::array<std::shared_ptr<core::ExternalObjectSlot>, 3u> canonicalProducerSlots_;
    std::shared_ptr<const MetalRhiCanonicalProducerRecordingPreparation> canonicalProducer_;
    std::optional<rhi::CanonicalZeroBaseIndirectProducerSequence> canonicalProducerSequence_;
    void cancelCanonicalProducer(bool failure) noexcept;
    std::shared_ptr<const MetalRhiClosedConsumerRecordingReservation> closedConsumerReservation_;
    std::optional<rhi::CanonicalClosedConsumerRecordingContract::Budget> closedConsumerBudget_;
    std::optional<rhi::CanonicalClosedConsumerRecordingContract::PrivateLowering>
        closedConsumerLowering_;
    bool closedConsumerIndirectNative_ = false;
    std::optional<rhi::CanonicalClosedConsumerSequence> closedConsumerSequence_;
    std::shared_ptr<const MetalRhiClosedConsumerNativePreparation> closedConsumerNative_;
    void cancelClosedConsumerReservation(bool failure) noexcept;
    bool encodeOrdinaryPreparedRenderPass(
        const std::shared_ptr<const MetalRhiOrdinaryPreparedRenderPass>&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsBinding>&,
        core::pmr::String&,
        const std::shared_ptr<const MetalRhiOrdinaryPreparedGraphicsPush>&);

    void publishOrdinaryIndirectCounters() noexcept
    {
        if (!ordinaryIndirectRecording_)
            return;
        stats_->ordinaryIndirectCommandCount = ordinaryIndirectRecording_->commands();
        stats_->ordinaryIndirectCallCount = ordinaryIndirectRecording_->calls();
        stats_->ordinaryIndirectRecordCount = ordinaryIndirectRecording_->records();
    }

    bool retainTransferResources(const MetalRhiRegisteredResource* source,
        const MetalRhiRegisteredResource& destination,
        const std::shared_ptr<void>& staging,
        core::pmr::String& diagnostic);

    bool retentionRecordingAllows(
        bool implemented, core::pmr::String& diagnostic, bool indirectAllowed = false) const
    {
        if (originalRecordingClosed_) {
            diagnostic = "RHI-METAL-ROOT-COMMAND-RECORDING-CLOSED";
            return false;
        }
        if (closedConsumerBudget_ || closedConsumerReservation_) {
            diagnostic = "RHI-METAL-CLOSED-CONSUMER-EXCLUSIVE-COMMAND";
            return false;
        }
        if (canonicalProducerBudget_) {
            diagnostic = "RHI-METAL-CANONICAL-PRODUCER-EXCLUSIVE-COMMAND";
            return false;
        }
        if (indirectOutputBudget_) {
            diagnostic = "RHI-METAL-INDIRECT-EXECUTION-PREPARATION-ONLY";
            return false;
        }
        if (ordinaryIndirectRecording_ && !indirectAllowed) {
            diagnostic = "RHI-METAL-ORDINARY-INDIRECT-OPERATION-DOMAIN";
            return false;
        }
        if (!usesRecordingRetention_)
            return true;
        if (!recordingRetention_
            || recordingRetention_->state()
                != rhi::common::CommandRetentionStorage::State::Recording) {
            diagnostic = "RHI-COMMAND-RETENTION-RECORDING-CLOSED";
            return false;
        }
        if (recordingRetention_->quota()->limitBytes()
            != recordingRetention_->budget().maxCpuMetadataBytes) {
            diagnostic = "RHI-COMMAND-RETENTION-CPU-QUOTA-MISMATCH";
            return false;
        }
        if (!implemented) {
            diagnostic = "RHI-METAL-COMMAND-RETENTION-OPERATION-UNIMPLEMENTED";
            return false;
        }
        return true;
    }

    friend std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiNativeCommandPayload(
        const std::pmr::string&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        std::pmr::string*,
        std::uint32_t,
        std::uint32_t,
        std::uint32_t,
        core::UniquePtr<rhi::common::CommandRetentionStorage>);
    friend std::shared_ptr<MetalRhiNativeCommandPayload>
    createMetalRhiOrdinaryWriteNativeCommandPayload(const MetalRhiOrdinaryWritePrototypeIntent&,
        const core::pmr::String&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        core::UniquePtr<rhi::common::CommandRetentionStorage>,
        std::shared_ptr<core::MutationJournal>,
        core::pmr::String&);
    friend std::shared_ptr<MetalRhiNativeCommandPayload>
    createMetalRhiOrdinaryIndirectNativeCommandPayload(const rhi::IndirectRecordingBudget&,
        const core::pmr::String&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        core::UniquePtr<rhi::common::CommandRetentionStorage>,
        core::pmr::String&);
    friend std::shared_ptr<MetalRhiNativeCommandPayload>
    createMetalRhiCanonicalProducerNativeCommandPayload(
        const rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Budget&,
        const core::pmr::String&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        core::UniquePtr<rhi::common::CommandRetentionStorage>,
        core::pmr::String&);
    friend std::shared_ptr<MetalRhiNativeCommandPayload>
    createMetalRhiClosedConsumerNativeCommandPayload(
        const rhi::CanonicalClosedConsumerRecordingContract::Budget&,
        const core::pmr::String&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        core::UniquePtr<rhi::common::CommandRetentionStorage>,
        core::pmr::String&);
    friend std::shared_ptr<MetalRhiNativeCommandPayload>
    createMetalRhiIndirectOutputNativeCommandPayload(const rhi::GpuIndirectOutputRecordingBudget&,
        const core::pmr::String&,
        std::shared_ptr<MetalRhiResourceRegistry>,
        core::UniquePtr<rhi::common::CommandRetentionStorage>,
        core::pmr::String&);
    rhi::BindlessRetainedDeclarations graphicsAccesses_;
    std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings> pendingGraphicsBinding_,
        activeGraphicsBinding_;
    std::shared_ptr<const rhi::BindlessFaultDomain> pendingGraphicsFaultDomain_,
        activeGraphicsFaultDomain_;
    bool graphicsScopeBound_ = false;
    bool prepareGraphicsPassScopeWithOwner(
        std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>,
        std::shared_ptr<const rhi::BindlessFaultDomain>,
        std::shared_ptr<const void>,
        const rhi::BindlessAccessDeclarations&,
        core::pmr::String& diagnostic);
    bool validatePreparedGraphicsStorage(
        const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
        core::pmr::String&) const;
    bool recordPreparedGraphicsBindingAt(
        const std::shared_ptr<const MetalRhiNativePreparedGraphicsBindings>&,
        bool scoped,
        core::pmr::String&);
    enum class BindlessFaultClearPlacement { BetweenEncoders, ResumeCompute };
    bool recordBindlessFaultLatchClearAt(std::shared_ptr<const rhi::BindlessFaultDomain>,
        rhi::ResourceState,
        BindlessFaultClearPlacement,
        core::pmr::String& diagnostics);

    struct LeasedBindlessFault {
        std::shared_ptr<const rhi::BindlessFaultDomain> snapshot;
        rhi::BindlessFaultSubmissionLease lease;
    };

    struct PendingBindlessFaultReadback {
        std::uint64_t generation = 0u;
        std::shared_ptr<const rhi::BindlessFaultDomain> snapshot;
        rhi::BindlessFaultSubmissionLease lease;
        std::shared_ptr<MetalRhiNativeBufferPayload> destination;
        std::shared_ptr<MetalRhiBindlessFaultReadback> result;
    };

    struct BindlessFaultOutcome {
        std::uint64_t generation = 0u;
        std::shared_ptr<MetalRhiBindlessFaultReadback> result;
    };

    core::pmr::Vector<LeasedBindlessFault> leasedBindlessFaults_;
    core::pmr::Vector<PendingBindlessFaultReadback> pendingBindlessFaultReadbacks_;
    core::pmr::Vector<BindlessFaultOutcome> bindlessFaultOutcomes_;
    core::pmr::Vector<std::shared_ptr<MetalRhiBindlessFaultReadback>> preparedFaultOutcomes_;
    std::uint32_t maxBindlessPipelines_ = 0u;
    std::uint32_t maxBindlessSnapshots_ = 0u;
    core::pmr::Vector<CachedArgumentBuffer> cachedArgumentBuffers_;
    core::pmr::Vector<std::shared_ptr<MetalRhiNativeQueryPoolPayload>> retainedQueryPools_;
    core::pmr::Vector<MetalRhiRecordedTimestampQuery> recordedTimestampQueries_;
    core::pmr::Vector<MetalRhiRecordedPipelineStatisticsQuery> recordedPipelineStatisticsQueries_;
    std::uint64_t timingFrameSerial_ = 0;
    core::UniquePtr<rhi::RecordedStagingUploadJournal> stagingUploads_;
    std::shared_ptr<std::atomic<std::uint64_t>> stagingCompletedEpoch_;

    std::shared_ptr<MetalRhiNativeBufferPayload> indexBuffer_;
    rhi::IndexFormat indexFormat_ = rhi::IndexFormat::Uint32;
    std::uint64_t indexBufferOffset_ = 0;
    std::shared_ptr<MetalRhiNativeBufferPayload> tessellationFactorBuffer_;
    std::uint64_t tessellationFactorBufferOffset_ = 0;
    std::uint32_t tessellationFactorInstanceStrideBytes_ = 0;
    std::uint32_t renderSubpassCount_ = 1;
    std::uint32_t renderSubpassIndex_ = 0;
    std::uint32_t renderPassWidth_ = 0;
    std::uint32_t renderPassHeight_ = 0;
    bool renderPassLayered_ = false;
    bool preflightOnly_ = false;
    std::shared_ptr<MetalOriginalCommandCustody> originalCommandCustody_;
    bool originalRecordingClosed_ = false;
    // Issued only by the native factory, never by the public constructor.
    std::uint64_t recordingIdentity_ = 0u;
    bool recordingPoisoned_ = false;
    bool abandoned_ = false;
    std::atomic<bool> submitted_ = false;
    std::atomic<bool> submissionFailed_ = false;
};

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiOrdinaryIndirectNativeCommandPayload(
    const rhi::IndirectRecordingBudget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String&);
std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiCanonicalProducerNativeCommandPayload(
    const rhi::CanonicalZeroBaseIndirectProducerRecordingContract::Budget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String&);
std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiClosedConsumerNativeCommandPayload(
    const rhi::CanonicalClosedConsumerRecordingContract::Budget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String&);

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiIndirectOutputNativeCommandPayload(
    const rhi::GpuIndirectOutputRecordingBudget&,
    const core::pmr::String&,
    std::shared_ptr<MetalRhiResourceRegistry>,
    core::UniquePtr<rhi::common::CommandRetentionStorage>,
    core::pmr::String&);

std::shared_ptr<MetalRhiNativeCommandPayload> createMetalRhiNativeCommandPayload(
    const std::pmr::string& label,
    std::shared_ptr<MetalRhiResourceRegistry> resourceRegistry,
    std::pmr::string* diagnostics = nullptr,
    std::uint32_t maxBindlessPipelines = 0u,
    std::uint32_t maxBindlessSnapshots = 0u,
    std::uint32_t maxBindlessGraphicsAccesses = 0u,
    core::UniquePtr<rhi::common::CommandRetentionStorage> preparedRetention = {});

// CommandCapacityReservation/1: existing native capacity, made explicit.
inline constexpr std::uint64_t kMetalNativeCommandBufferCapacity = 64u;
inline constexpr std::uint64_t kMetalPrimaryCommandReservation = 2u;

// Scalar observation for PhysicalBudgetLedger/1. This intentionally carries
// no queue, command buffer, completion object, or native API detail.
struct MetalRhiCommandCapacityStats {
    std::uint64_t capacity = 0u;
    std::uint64_t charged = 0u;
    std::uint64_t peakCharged = 0u;
    std::uint64_t rejectionCount = 0u;
};

std::shared_ptr<core::AtomicCapacityLease> reserveSharedMetalRhiCommandCapacity(
    std::uint64_t count, std::pmr::string& diagnostics);
MetalRhiCommandCapacityStats inspectSharedMetalRhiCommandCapacity() noexcept;

/// All primary command buffers and drawable-present command buffers for the
/// default Metal device share this queue.  Metal queue ordering is part of the
/// RHI present/submission contract; separate queues require an explicit event
/// dependency that the public swapchain API does not carry.
std::shared_ptr<void> sharedMetalRhiNativeCommandQueue();

std::shared_ptr<MetalRhiNativeQueueStats> createMetalRhiNativeQueueStats();

/// Drain the shared Metal command queue at an explicit device teardown
/// boundary. This is intentionally blocking and must not be used per frame.
bool waitMetalRhiNativeQueueIdle(std::pmr::string& diagnostics);

enum class MetalRhiNativeQueueDrainState : std::uint8_t {
    Completed,
    DeadlineExpired,
    DiagnosticFailure,
};

/// Queue-marker lowering for ShutdownDrain/1.  The marker's native completion
/// proves retirement of every earlier command on this serial Metal queue.
MetalRhiNativeQueueDrainState drainMetalRhiNativeQueueForShutdown(
    const rhi::semantics::ShutdownDrainRequest& request, std::pmr::string& diagnostics);

/// Runtime gate for completion handlers plus encoder-fence alias handoff.
/// Non-Apple diagnostic stubs return false.
bool metalRhiNativeCompletionAndAliasingCommandsAvailable();

} // namespace arp::rhi_metal
