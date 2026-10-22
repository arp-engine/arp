#pragma once
#include "core/memory/cpu_allocator.h"
#include "core/runtime/completion_retention_pool.h"
#include "rhi/backends/webgpu/webgpu_command_inputs.h"
#include "rhi/backends/webgpu/webgpu_native_indirect_processor.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_prototype_batch.h"
#include "rhi/backends/webgpu/webgpu_query_pool.h"
#include "rhi/backends/webgpu/webgpu_transfer_retention.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_bindless.h"

#include <atomic>
#include <memory>

namespace arp::rhi_webgpu {

struct WebGpuRetainedZeroBaseProducer {
    explicit WebGpuRetainedZeroBaseProducer(
        std::shared_ptr<const WebGpuNativeZeroBaseIndirectProducer> value)
        : producer(std::move(value))
    {}

    WebGpuRetainedZeroBaseProducer(const WebGpuRetainedZeroBaseProducer&) = delete;
    WebGpuRetainedZeroBaseProducer(WebGpuRetainedZeroBaseProducer&&) noexcept = default;

    ~WebGpuRetainedZeroBaseProducer()
    {
        if (producer)
            producer->recordings_.release(1u);
    }

    std::shared_ptr<const WebGpuNativeZeroBaseIndirectProducer> producer;
};

struct WebGpuZeroBaseProducerRetention {
    core::pmr::Vector<WebGpuRetainedZeroBaseProducer> producers{&core::defaultCpuMemoryResource()};

    struct Processor {
        std::shared_ptr<const WebGpuNativeIndirectProcessor> payload;
        rhi::BindlessIndirectProcessorRecordingLease lease;
        bool consumerRequired = false;
        bool consumerDrawRecorded = false;
    };

    core::pmr::Vector<Processor> processors{&core::defaultCpuMemoryResource()};
};

struct WebGpuProducerCompletionOwner {
    explicit WebGpuProducerCompletionOwner(
        std::uint32_t capacity, std::uint32_t transferCapacity = 0u)
        : retained(capacity), ordinaryTransfers(transferCapacity)
    {}

    ~WebGpuProducerCompletionOwner();
    core::runtime::CompletionRetentionPool<WebGpuZeroBaseProducerRetention> retained;
    core::runtime::CompletionRetentionPool<WebGpuOrdinaryTransferRetention> ordinaryTransfers;
    std::shared_ptr<core::CpuMemoryQuota> transferPreparationQuota;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUDevice device = nullptr;
#endif
};

struct WebGpuOrdinaryTransferCompletionCallback {
    ~WebGpuOrdinaryTransferCompletionCallback()
    {
        if (owner)
            owner->ordinaryTransfers.cancel(ticket);
    }

    std::shared_ptr<WebGpuProducerCompletionOwner> owner;
    core::runtime::CompletionRetentionPool<WebGpuOrdinaryTransferRetention>::Ticket ticket;
};

struct WebGpuProducerCompletionCallback {
    ~WebGpuProducerCompletionCallback()
    {
        if (owner)
            owner->retained.cancel(ticket);
    }

    std::shared_ptr<WebGpuProducerCompletionOwner> owner;
    core::runtime::CompletionRetentionPool<WebGpuZeroBaseProducerRetention>::Ticket ticket;
};

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
struct WebGpuBindlessFaultReadbackCallbackState {
    ~WebGpuBindlessFaultReadbackCallbackState();
    WGPUBuffer destination = nullptr;
    std::shared_ptr<WebGpuNativeBufferPayload> latch;
    std::shared_ptr<WebGpuRhiBindlessFaultReadback> result;
    rhi::BindlessFaultSubmissionLease lease;
};
#endif

struct WebGpuNativeCommandPayload::Impl {
    std::shared_ptr<WebGpuNativeCommandStats> stats;
    std::uint32_t pushConstantUploadRingByteCapacity = 0;
    std::uint32_t pushConstantMaxBindingSize = 0;
    std::uint32_t pushConstantOffsetAlignment = 0;
    std::uint32_t pushConstantDynamicBindingSize = 0;
    std::uint32_t pushConstantUploadRingOffset = 0;
    std::uint64_t pushConstantSubmissionSerial = 0;
    bool finished = false;
    bool submitted = false;
    bool ordinaryTransfer = false;
    bool ordinaryPrepared = false;
    std::shared_ptr<class WebGpuConformanceClearFrame> conformanceClear;
    core::UniquePtr<class WebGpuPrototypeBatch> prototypeBatch;
    // Lifetime/identity marker only. Distinct typed wrappers validate their own
    // authentic admission; this owner has no cast or execution interface.
    std::shared_ptr<const void> canonicalColdAdmission;
    std::shared_ptr<const class WebGpuOrdinaryScalarComputeRange> ordinaryScalarRange;
    std::shared_ptr<const class WebGpuOrdinaryGraphicsAttachments> ordinaryGraphicsAttachments;
    std::shared_ptr<const class WebGpuOriginalResourceStates> ordinaryOriginalStates;
    std::shared_ptr<class WebGpuOriginalStateTransaction> ordinaryOriginalStateTransaction;
    std::shared_ptr<class WebGpuOriginalTextureUploadTransaction>
        ordinaryOriginalTextureTransaction;
    std::shared_ptr<const class WebGpuOriginalTextureReadbackCopies> ordinaryOriginalReadbackCopies;
    std::shared_ptr<const class WebGpuOrdinaryGraphicsTriangleRange> ordinaryOriginalGraphicsRange;
    bool ordinaryUnsupported = false;
    bool ordinaryTransferDispatch = false;
    core::UniquePtr<WebGpuFrozenTransferArguments::Consumption> frozenTransferUsage;
    core::UniquePtr<WebGpuOrdinaryTransferCompletionCallback> transferCompletion;
    const std::uint64_t recordingIdentity = rhi::allocateGpuResourceId().value;
    std::uint64_t timingFrameSerial = 0u;
    bool timingResolved = false;
    core::pmr::Vector<WebGpuNativeDescriptorSetGroupLowering> activeDescriptorSetGroups;
    core::pmr::Vector<rhi::TextureFormat> activeRenderPassColorFormats;
    rhi::TextureFormat activeRenderPassDepthStencilFormat = rhi::TextureFormat::Unknown;
    std::uint32_t activeRenderPassSampleCount = 1u;
    bool activeInputAttachmentLoweringValidated = false;
    std::uint64_t activeShaderPackageHash = 0;
    const WebGpuNativeRenderPipelinePayload* activeRenderPipeline = nullptr;

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPUCommandBuffer commandBuffer = nullptr;
    WGPURenderPassEncoder renderPass = nullptr;
    WGPUComputePassEncoder computePass = nullptr;
    // Set by the logical RHI bindIndexBuffer command.  The previous scratch
    // buffer ignored the bound resource and made all uint16 indices read as
    // zero, so indexed PBR recorded successfully but rasterized no triangle.
    WGPUBuffer boundIndexBuffer = nullptr;
    WGPUIndexFormat boundIndexFormat = WGPUIndexFormat_Undefined;
    std::uint64_t boundIndexOffset = 0u;
    WGPUBuffer pushConstantUploadRing = nullptr;
    WGPUBindGroupLayout pushConstantBindGroupLayout = nullptr;
    WGPUBindGroup pushConstantBindGroup = nullptr;
    WGPUQuerySet timingQuerySet = nullptr;
    WGPUBuffer timingResolveBuffer = nullptr;
    WGPUBuffer timingReadbackBuffer = nullptr;
    // Supplied by the active pipeline's generated lowering artifact. This
    // must not be inferred from the historical group-3 convention.
    std::uint32_t pushConstantGroup = UINT32_MAX;
    // The physical binding is reflection-owned too; retaining only the group
    // would leave the native bind-group payload dependent on binding(0).
    std::uint32_t pushConstantBinding = UINT32_MAX;
    core::pmr::Vector<WGPUBindGroupLayout> activeDescriptorGroupLayouts;
    core::pmr::Vector<WebGpuNativeDescriptorBindingLowering> activeDescriptorBindings;
    core::pmr::Vector<WebGpuNativeDescriptorTableLowering> activeDescriptorTables;

    struct BoundDescriptorSet {
        std::uint32_t logicalSet = 0u;
        std::shared_ptr<WebGpuNativeDescriptorGroupPayload> payload;
        core::pmr::Vector<std::uint32_t> dynamicOffsets{&core::defaultCpuMemoryResource()};
    };

    core::pmr::Vector<BoundDescriptorSet> boundDescriptorSets;
    core::pmr::Vector<WGPUBindGroup> activeDescriptorGroups;
    core::pmr::Vector<std::shared_ptr<WebGpuRhiQueryPoolPayload>> retainedTimingPools;

    struct RecordedTimestampQuery {
        std::shared_ptr<WebGpuRhiQueryPoolPayload> pool;
        std::uint32_t queryIndex = 0u;
    };

    struct PendingTimestampReadback {
        WGPUBuffer resolveBuffer = nullptr;
        WGPUBuffer readbackBuffer = nullptr;
        std::shared_ptr<WebGpuRhiQueryPoolPayload> pool;
        std::uint32_t queryIndex = 0u;
    };

    core::pmr::Vector<RecordedTimestampQuery> recordedTimestampQueries;
    core::pmr::Vector<PendingTimestampReadback> pendingTimestampReadbacks;
    core::pmr::Vector<std::shared_ptr<WebGpuNativeBufferPayload>> retainedBindlessFaultLatches;

    struct RetainedBindlessPipeline {
        std::shared_ptr<const void> pipeline;
        std::shared_ptr<const rhi::BindlessAuthorization> authorization;
    };

    core::pmr::Vector<RetainedBindlessPipeline> retainedBindlessPipelines;
    core::pmr::Vector<std::shared_ptr<const void>> retainedBindlessSnapshots;
    core::UniquePtr<WebGpuProducerCompletionCallback> producerCompletion;

    struct LeasedBindlessFaultLatch {
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot;
        rhi::BindlessFaultSubmissionLease lease;
    };

    core::pmr::Vector<LeasedBindlessFaultLatch> leasedBindlessFaultLatches;

    struct PendingBindlessFaultReadback {
        std::shared_ptr<WebGpuNativeBufferPayload> latch;
        WGPUBuffer destination = nullptr;
        std::shared_ptr<WebGpuRhiBindlessFaultReadback> result;
        rhi::BindlessFaultSubmissionLease lease;
        std::uint64_t snapshotGeneration = 0u;
        core::UniquePtr<WebGpuBindlessFaultReadbackCallbackState> callback;
    };

    core::pmr::Vector<core::UniquePtr<WebGpuBindlessFaultReadbackCallbackState>>
        preparedBindlessFaultReadbacks;
    core::pmr::Vector<PendingBindlessFaultReadback> pendingBindlessFaultReadbacks;

    struct BindlessFaultOutcome {
        std::uint64_t snapshotGeneration = 0u;
        std::shared_ptr<WebGpuRhiBindlessFaultReadback> result;
    };

    core::pmr::Vector<BindlessFaultOutcome> bindlessFaultOutcomes;

    struct ProcessorFaultOutcome {
        std::uint64_t identity = 0u;
        std::uint64_t snapshotGeneration = 0u;
        std::shared_ptr<WebGpuRhiBindlessFaultReadback> result;
    };

    core::pmr::Vector<ProcessorFaultOutcome> processorFaultOutcomes;
    // Keep the shared native view payload alive until this command payload is
    // destroyed.  The encoder stores only the raw view handle in its native
    // attachment descriptor, so a transient frame resource must not release
    // the emdawn wrapper before submitted work has completed.
    core::pmr::Vector<std::shared_ptr<WebGpuNativeTextureViewPayload>> retainedTextureViews;
    // Transfer commands encode raw WGPUTexture handles. Retain the owning
    // payload alongside the command buffer so FrameGraph teardown cannot
    // destroy an attachment before queue completion.
    core::pmr::Vector<std::shared_ptr<WebGpuNativeTexturePayload>> retainedTextures;
    core::pmr::Vector<std::shared_ptr<WebGpuNativeRenderBundlePayload>> retainedRenderBundles;
#endif
};

} // namespace arp::rhi_webgpu
