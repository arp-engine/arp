#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/conformance/clear_execution.h"
#include "rhi/conformance/sampled_execution.h"
#include "rhi/conformance/storage_mrt_execution.h"
#include "rhi/frontend/descriptor_buffer_declaration_validation.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_general_write_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/frontend/memory_physical_budget_ledger.h"
#include "rhi/frontend/memory_shutdown_drain.h"
#include "rhi/frontend/memory_upload_staging_readiness.h"
#include "rhi/frontend/pipeline_creation_validation.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_graphics_input.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/frontend/pipeline_single_entry_input.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/frontend/resource_device_validation.h"
#include "rhi/frontend/resource_leases.h"
#include "rhi/frontend/resource_loss_validation.h"
#include "rhi/frontend/telemetry_direct_objects.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_common.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_frame_capture.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"
#include "rhi/gpu_swapchain.h"

#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

namespace arp::rhi {

class PipelineCache;
class DescriptorCache;

struct QueueSubmitResult {
    bool success = false;
    std::pmr::string diagnostics;
    std::uint32_t submittedCommandCount = 0;
    // Preserve irrevocable queue acceptance when a later bookkeeping step
    // fails. Success already implies acceptance; neither implies completion.
    bool workAccepted = false;

    bool hasAcceptedWork() const noexcept { return success || workAccepted; }
};

/// L2 synchronization primitive: a fence is a CPU-visible completion signal.
/// The upper layer can poll isSignaled() (non-blocking, hot-path) or wait()
/// (blocking, shutdown/drain only). The fence does not expose native handles;
/// backend implementations own the native object internally.
/// wait() returns normally only on successful completion. A failed/lost or
/// unsubmitted command-owned completion rejects with core::DiagnosticError;
/// reporting a diagnostic and returning as if drained is forbidden. Immutable
/// command-owned fences reject signal/reset; generic fence creation capability
/// is independent of this command-scoped completion contract.
class ARP_API GpuFence {
public:
    virtual ~GpuFence() = default;
    // Production completion fences reject host signal: only the original native
    // submission can establish completion. Null keeps explicit validator behavior.
    virtual void signal() = 0;
    virtual bool isSignaled() const = 0;
    virtual void wait() = 0;
    virtual void reset() = 0;
};

/// L2 synchronization primitive: a semaphore is a GPU-side ordering signal
/// used for queue-to-queue synchronization (e.g. async compute -> graphics).
/// The upper layer signals and waits through the L2 interface; no native
/// handle is exposed.
class ARP_API GpuSemaphore {
public:
    virtual ~GpuSemaphore() = default;
    virtual void signal() = 0;
    virtual void wait() = 0;
    virtual void reset() = 0;
};

/// L2 timeline synchronization primitive: a monotonic counter semaphore
/// for GPU-side ordering across queues and CPU-side host wait (fence
/// replacement). Supports signal/wait by uint64_t value; one object
/// replaces multiple binary semaphores across frames. Backends that lack
/// native timeline support must return null unless an explicit executable
/// fallback is part of the backend's published lowering contract.
class ARP_API GpuTimelineSemaphore {
public:
    virtual ~GpuTimelineSemaphore() = default;
    /// Explicit host signal; advances the counter, never proves queue completion.
    virtual void signal(uint64_t value) = 0;
    /// Explicit blocking host wait. GPU ordering uses GpuQueue::submit instead.
    virtual void wait(uint64_t value) = 0;
    /// CPU poll: returns the last signaled value visible to the host.
    virtual uint64_t getValue() const = 0;
    /// CPU blocking wait: waits until the semaphore reaches or exceeds value.
    /// timeoutNs = 0 means return immediately; UINT64_MAX means wait forever.
    virtual void hostWait(uint64_t value, uint64_t timeoutNs) = 0;
};

/// L2 memory-heap abstraction: a single GPU-visible memory allocation that
/// can host multiple placed resources (textures and buffers). Used for
/// transient resource aliasing where several pass-scope textures share the
/// same physical memory region without overlapping in time.
///
/// Backends that support aliasing (Vulkan VK_IMAGE_CREATE_ALIAS_BIT, Metal
/// heap aliasing) share one native allocation across members. Backends that
/// do not support a placed-resource primitive keep this object out of the
/// alias-plan path and use ordinary pool allocation instead. The capability
/// flag makes that degradation explicit; no silent fallback is permitted.
class ARP_API GpuMemoryHeap {
public:
    virtual ~GpuMemoryHeap() = default;
    virtual GpuResourceId id() const = 0;
    virtual std::uint64_t sizeBytes() const = 0;
    virtual std::uint64_t alignmentBytes() const = 0;
    virtual bool supportsAliasing() const = 0;

    OriginalResourceRegistrationTicket originalResourceRegistration() const noexcept
    {
        return originalRegistration_.ticket();
    }

    bool originalPoolAllocationCharged() const noexcept
    {
        return originalAllocationLifetime_ && originalAllocationLifetime_->hasPoolCharge();
    }

protected:
    void bindOriginalAllocationLifetime(OriginalAllocationLifetime& lifetime) noexcept
    {
        originalAllocationLifetime_ = originalAllocationLifetimeBound_ ? nullptr : &lifetime;
        originalAllocationLifetimeBound_ = true;
    }

private:
    friend class GpuDevice;
    OriginalResourceRegistrationBinding originalRegistration_;
    OriginalAllocationLifetime* originalAllocationLifetime_ = nullptr;
    bool originalAllocationLifetimeBound_ = false;
};

class ARP_API QueryPool {
public:
    virtual ~QueryPool() = default;
    virtual std::uint32_t count() const = 0;
    virtual QueryType type() const = 0;

    virtual QueryExecutionStatus executionStatus() const
    {
        QueryExecutionStatus status;
        status.type = type();
        status.count = count();
        status.diagnostics = "RHI query-pool execution status is not exposed by this backend";
        return status;
    }

    /// Clear backend-owned query state after all commands that referenced the
    /// pool have completed. A backend must reject reset while a recorded query
    /// is still in flight; callers must not infer completion from CPU submit.
    virtual CommandValidationResult reset()
    {
        return {
            false,
            "RHI query-pool reset is unavailable",
            0,
        };
    }
};

/// Queue family role for multi-queue submission. Used by GpuQueue::role()
/// and CompileFrameGraph queue assignment. Mirrors render_graph::QueueRole.
enum class GpuQueueRole { Graphics, Compute, Transfer };

class ARP_API GpuQueue {
public:
    virtual ~GpuQueue() = default;

    /// Which queue family this queue belongs to (Graphics/Compute/Transfer).
    virtual GpuQueueRole role() const = 0;

    virtual QueueSubmitResult submit(const CommandBuffer& commandBuffer) = 0;
    /// Submit with an optional fence to signal upon completion. When
    /// signalFence is null, behaves identically to the single-argument
    /// overload. A non-null fence requires a lowering that proves genuine GPU
    /// completion; unsupported backends must reject it, never signal at submit.
    virtual QueueSubmitResult submit(const CommandBuffer& commandBuffer, GpuFence* signalFence) = 0;
    /// Submit with timeline semaphore ordering. waitSemaphore/waitValue
    /// block execution start; signalSemaphore/signalValue fire on completion.
    /// signalFence is optional CPU fence (nullptr = no fence).
    virtual QueueSubmitResult submit(const CommandBuffer& commandBuffer,
        GpuTimelineSemaphore* waitSemaphore,
        uint64_t waitValue,
        GpuTimelineSemaphore* signalSemaphore,
        uint64_t signalValue,
        GpuFence* signalFence = nullptr) = 0;
    virtual std::uint32_t submitCount() const = 0;
};

class ARP_API GpuDevice {
public:
    GpuDevice() = default;
    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;
    GpuDevice(GpuDevice&&) = delete;
    GpuDevice& operator=(GpuDevice&&) = delete;
    virtual ~GpuDevice();
    /// Cold, explicit Prototype prerequisite. No resource registration, original
    /// state/control, native loss callback or GPU execution authority is issued.
    const char* prepareOriginalDeviceLifetime(
        const OriginalDeviceLifetimeBudget& budget, std::shared_ptr<core::CpuMemoryQuota> quota);
    OriginalDeviceLifetime::Ticket originalDeviceLifetimeTicket() const noexcept;
    const char* validateOriginalDeviceLifetime(
        const OriginalDeviceLifetime::Ticket& ticket) const noexcept;
    /// Permanently closes only this opt-in logical domain; it cannot be reopened
    /// on the same device. This is neither a GPU drain nor a native-loss report.
    void retireOriginalDeviceLifetime() noexcept;
    const char* prepareOriginalDeviceNativeLoss(
        const OriginalDeviceNativeLossBudget& budget, std::shared_ptr<core::CpuMemoryQuota> quota);
    const char* prepareOriginalDeviceNativeResultLoss(
        const OriginalDeviceNativeResultLossBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    /// Explicit separately admitted observer storage; no observer receiver or
    /// GPU proof can be issued through this preparation API.
    const char* prepareOriginalDeviceNativeLoss(const OriginalDeviceNativeLossBudget& budget,
        const OriginalDeviceNativeLossObserverRequest& request,
        std::shared_ptr<core::CpuMemoryQuota> quota);

private:
    template <class Bind>
    const char* prepareOriginalLossBinding(
        std::shared_ptr<core::CpuMemoryQuota> quota, Bind&& bind);

public:
    const char* originalDeviceNativeLossDiagnostic() const noexcept;
    std::optional<OriginalDeviceNativeLossObservation> observeOriginalDeviceNativeLoss(
        const char*& diagnostic) const noexcept;
    const char* validateOriginalDeviceNativeLoss(
        const OriginalDeviceNativeLossObservation& observation) const noexcept;
    const char* validateOriginalTypedNativeLossJoin(
        const OriginalDeviceNativeLossObservation& observation) const noexcept;
    const char* validateOriginalTypedNativeLossJoin(
        const OriginalDeviceNativeLossObservation& observation,
        const OriginalTypedNativeLossJoinRequest& request) const noexcept;
    const char* detachOriginalDeviceNativeLoss() noexcept;
    /// One-shot cold opt-in for factory-issued logical resource lifetimes only.
    const char* prepareOriginalResourceRegistration(
        const OriginalResourceRegistrationBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota);
    // /7 requires a separate request and the exact already-armed device-issued
    // observation. CPU parents remain metadata; native factories stay gated.
    const char* prepareOriginalTypedNativeLossRegistration(
        const OriginalTypedNativeLossJoinRequest& request,
        const OriginalResourceRegistrationBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const OriginalDeviceNativeLossObservation& loss);

private:
    const char* prepareOriginalResourceRegistrationImpl(
        const OriginalResourceRegistrationBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::weak_ptr<const OriginalDeviceNativeLossBinding> loss);
    template <class Pipeline>
    const char* validateOriginalTypedNativeLossGroupInputs(const DescriptorSet& object,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const noexcept;

public:
    const char* validateOriginalResourceRegistration(
        const OriginalResourceRegistrationTicket& ticket) const noexcept;
    template <class T>
    const char* validateOriginalResourceRegistration(const T& resource) const noexcept;
    std::string_view lastOriginalResourceRegistrationDiagnostic() const noexcept;
    // OriginalRuntimeLengthIntervalDiagnostic/1: exact original metadata join
    // only. A success cannot authorize a descriptor write or native command.
    template <class Pipeline>
    const char* diagnoseOriginalRuntimeLengthInterval(
        const OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest& request,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        OriginalDescriptorBufferBindingContract::IntervalDiagnosticObservation& output)
        const noexcept;
    template <class Pipeline>
    const char* diagnoseOriginalRuntimeProfileBufferInterval(
        const OriginalDescriptorBufferBindingContract::IntervalDiagnosticRequest& request,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        OriginalDescriptorBufferBindingContract::IntervalDiagnosticObservation& output)
        const noexcept;
    template <class Pipeline>
    const char* diagnoseOriginalTargetMemoryBufferInterval(
        const OriginalTargetMemoryIntervalContract::Request& request,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        OriginalTargetMemoryIntervalContract::Observation& output) const noexcept;

private:
    template <bool targetMemory, class Request, class Pipeline, class Observation>
    const char* diagnoseOriginalRuntimeProfileBufferIntervalImpl(const Request& request,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::shared_ptr<const OriginalCommandBufferLease>& buffer,
        std::uint32_t set,
        std::uint32_t binding,
        const DescriptorBufferBindingRange& range,
        std::uint32_t dynamicOffset,
        Observation& output,
        bool requireOperation) const noexcept;

public:
    template <class Pipeline, class Request = OriginalFiniteDescriptorSourceDiagnosticRequest>
    const char* diagnoseOriginalFiniteDescriptorSource(const Request& request,
        const DescriptorSet& object,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept;
    template <class Pipeline, class Request = OriginalFiniteDescriptorWriteRequest>
    const char* beginOriginalFiniteDescriptorWrite(DescriptorSet& object,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const Request& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const;
    template <class Pipeline>
    const char* diagnoseOriginalFiniteTargetMemoryDescriptorSource(
        const OriginalFiniteTargetMemorySourceDiagnosticRequest& request,
        const DescriptorSet& object,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept;
    template <class Pipeline>
    const char* beginOriginalFiniteTargetMemoryDescriptorWrite(DescriptorSet& object,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalFiniteDescriptorBufferWrite* writes,
        std::size_t count,
        const OriginalFiniteTargetMemoryWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const;
    template <class Pipeline>
    const char* beginOriginalWholeDescriptorWrite(DescriptorSet& object,
        const std::shared_ptr<Pipeline>& pipeline,
        std::uint32_t set,
        const OriginalWholeDescriptorBufferWrite* writes,
        std::size_t count,
        const OriginalWholeDescriptorWriteRequest& request,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas) const;
    const char* originalResourceRegistrationDiagnostic() const noexcept;
    const char* prepareOriginalDescriptorNativeLossRegistration(
        const OriginalTypedNativeLossJoinRequest&,
        const OriginalResourceRegistrationBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const OriginalDeviceNativeLossObservation&);
    template <class Pipeline>
    const char* beginOriginalGeneralDescriptorWrite(DescriptorSet&,
        const std::shared_ptr<Pipeline>&,
        const std::shared_ptr<const OriginalCommandResourceSources>&,
        std::uint32_t,
        const OriginalCommandDescriptorSetElement*,
        std::size_t,
        const OriginalGeneralDescriptorWriteRequest&,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>&) const;
    virtual Backend backend() const = 0;
    virtual DeviceCapabilities capabilities() const = 0;
    virtual MemoryStats memoryStats() const = 0;
    /// PhysicalBudgetLedger/1 reports only backend-owned scalar accounting.
    /// The default preserves correctness for a backend that has not published
    /// an observer: every category is Unknown, never a synthetic zero budget.
    virtual semantics::PhysicalBudgetLedgerReport physicalBudgetLedger() const;
    /// DirectObjectTelemetry/1 observes L2 objects created directly through
    /// this device. It is deliberately not a physical budget until a
    /// workload-owned capacity policy is configured and enforced.
    virtual semantics::DirectObjectTelemetryReport directObjectTelemetry() const;
    /// Most recently completed GPU timing sample. Backends return Pending
    /// while timestamp data is in flight and Unsupported when the API/device
    /// cannot expose it. This default keeps the contract safe for Null and
    /// partially implemented backends.
    virtual GpuTimingSample latestGpuTiming() const;
    /// Completion credential for work that returned a typed upload-staging
    /// Deferred result. It exposes no queue serial or native synchronization
    /// object. Consumers retry only after this epoch advances.
    virtual semantics::UploadStagingReadiness uploadStagingReadiness() const;
    /// Optional offline frame capture. The default is deterministic failure;
    /// a backend may only override it after publishing an executable lowering.
    virtual FrameCaptureCapability frameCaptureCapability(FrameCaptureTool tool) const;
    virtual FrameCaptureResult beginFrameCapture(const FrameCaptureRequest&);
    virtual FrameCaptureResult endFrameCapture();
    /// Most recently completed pipeline-statistics snapshot. Backends return
    /// Pending while the native counter result is in flight and Unsupported
    /// when the capability or query contract is unavailable.
    virtual GpuPipelineStatisticsSample latestPipelineStatistics() const;
    /// Read one explicit pipeline-statistics query result. The result is
    /// asynchronous: backends return NotRecorded, Pending, Available, or a
    /// deterministic unsupported/failure status and never synthesize CPU data.
    virtual GpuPipelineStatisticsSample readPipelineStatistics(
        const QueryPool& queryPool, std::uint32_t queryIndex) const;
    virtual GpuQueue& queue() = 0;
    /// Explicit teardown/diagnostic boundary.  It must never be called from a
    /// normal frame hot path.  Backends keep all native synchronization
    /// private while making the L2 lifetime boundary expressible.
    virtual void waitIdle();
    /// ShutdownDrain/1 is the bounded replacement for waitIdle at externally
    /// observable teardown boundaries.  Callers supply one absolute deadline
    /// shared with thread/readback/resource-retirement stages.  A backend that
    /// cannot prove completion before that deadline must fail closed.
    virtual semantics::ShutdownDrainReport drainForShutdown(
        const semantics::ShutdownDrainRequest& request);
    /// Async compute queue. Returns nullptr when the backend only has
    /// a single graphics queue. Callers must check for null before use.
    virtual GpuQueue* computeQueue();
    /// Native placement requirements for a transient texture. Backends with a
    /// native query (Vulkan vkGetImageMemoryRequirements2, Metal
    /// heapTextureSizeAndAlignWithDescriptor) override this to return precise
    /// size/alignment (row pitch, compression block size, backend alignment).
    /// The default returns an L2 estimate (mip chain + array + cube + MSAA);
    /// Null uses it. The FrameGraph compiler skips alias group generation when
    /// features.transientAliasing is false, producing an empty alias plan and
    /// taking the pool-allocation path. Metal publishes this capability only
    /// after its native MTLHeap placement/completion contract is available;
    /// WebGPU remains degraded. Backends that alias should override before
    /// claiming Backend Validated aliasing.
    virtual TexturePlacementRequirements queryTexturePlacementRequirements(const TextureDesc& desc);
    virtual arp::core::UniquePtr<GpuBuffer> createBuffer(const BufferDesc& desc) = 0;
    virtual CommandValidationResult writeBuffer(GpuBuffer& buffer,
        const void* data,
        std::uint64_t sizeBytes,
        std::uint64_t offsetBytes = 0) = 0;
    virtual arp::core::UniquePtr<GpuBufferView> createBufferView(
        const GpuBuffer& buffer, const BufferViewDesc& desc) = 0;
    virtual arp::core::UniquePtr<GpuTexture> createTexture(const TextureDesc& desc) = 0;
    // Explicit cold factory; ordinary TextureDesc never enables this intent.
    // Exact native exclusive creation/registration precede content issuance.
    // Exact native image-allocation requirement for ContentCreation3/4. Zero is
    // failure, never a logical-footprint or placed/alias-image fallback.
    std::uint64_t queryOriginalContentTextureAllocation(
        const TextureDesc& desc, const char*& diagnostic, std::uint32_t creationVersion = 3u);
    core::UniquePtr<GpuTexture> createOriginalContentTexture(const TextureDesc& desc,
        const OriginalTextureContentCreationBudget& budget,
        std::shared_ptr<core::CpuMemoryQuota> contentQuota,
        const char*& diagnostic);
    bool overlapsOriginalDeviceCpuOwner(
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept;
    const char* originalPoolAllocationRegistrationDiagnostic() const noexcept;
    const char* originalPoolHeapAllocationRegistrationDiagnostic() const noexcept;
    const char* validateOriginalResourceRegistration(const GpuMemoryHeap& heap) const noexcept;
    const char* originalMemoryHeapPlacementDiagnostic(const GpuMemoryHeap& heap) const noexcept;
    template <class T>
    const char* bindOriginalPoolAllocationReceipt(
        T& resource, std::shared_ptr<const OriginalPoolAllocationReceipt> receipt) const noexcept;
    template <class T>
    const OriginalPoolAllocationReceipt* originalPoolAllocationReceipt(
        const T& resource, const char*& diagnostic) const noexcept;
    /// Completion-gated pool reuse hook. The backend must discard prior
    /// contents and normalize the logical state to Undefined. Failure means
    /// the resource must be destroyed instead of cached.
    virtual ResourceBarrierResult prepareTextureForPoolReuse(GpuTexture& texture);
    virtual ResourceBarrierResult prepareBufferForPoolReuse(GpuBuffer& buffer);
    /// Create a memory heap suitable for placing transient aliased resources.
    /// Backends that support aliasing create a single native allocation (e.g.
    /// VkDeviceMemory, MTLHeap). Backends without a placed-resource primitive
    /// must return null here; the capability-gated caller uses ordinary pool
    /// allocations instead of treating a size-only wrapper as aliasing.
    virtual arp::core::UniquePtr<GpuMemoryHeap> createMemoryHeap(
        std::uint64_t sizeBytes, std::uint64_t alignmentBytes, MemoryUsage usage) = 0;
    /// Stable reason for the immediately preceding createMemoryHeap failure.
    /// The returned diagnostic describes the L2 creation boundary only; it
    /// never exposes a native allocation or backend-private handle.
    virtual std::string_view lastMemoryHeapCreationDiagnostic() const;
    /// Create a texture placed at the given offset within a memory heap. On
    /// backends with true aliasing, this reuses the heap's native allocation.
    /// On backends without aliasing, this operation is unsupported and must
    /// fail closed. Placed textures retain the allocation lease through native
    /// image destruction; releasing the public heap cannot release live memory.
    /// PlacedTextureResidency/1: Transient placement expresses heap lifetime
    /// and alias eligibility, not tile-memory residency. Usage remains intact.
    /// Vulkan uses device memory, Metal uses private placement heaps; Null is
    /// validator-only and WebGPU without placement support rejects this path.
    virtual arp::core::UniquePtr<GpuTexture> createPlacedTexture(
        const TextureDesc& desc, const GpuMemoryHeap& heap, std::uint64_t offsetBytes) = 0;
    virtual arp::core::UniquePtr<GpuTextureView> createTextureView(
        const GpuTexture& texture, const TextureViewDesc& desc) = 0;
    virtual arp::core::UniquePtr<GpuSampler> createSampler(const SamplerDesc& desc) = 0;
    virtual arp::core::UniquePtr<PipelineLayout> createPipelineLayout(
        const PipelineLayoutDesc& desc) = 0;
    virtual PipelineValidationReport validateRenderPipeline(const RenderPipelineDesc& desc) = 0;
    virtual PipelineValidationReport validateComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual arp::core::UniquePtr<RenderPipeline> createRenderPipeline(
        const RenderPipelineDesc& desc) = 0;
    virtual arp::core::UniquePtr<ComputePipeline> createComputePipeline(
        const ComputePipelineDesc& desc) = 0;
    // OriginalResourceRegistration/2: explicit cold lifetime factories. These
    // never adopt ordinary wrappers and grant no canonical typed execution.
    core::UniquePtr<PipelineLayout> createOriginalPipelineLayout(const PipelineLayoutDesc& desc);
    core::UniquePtr<RenderPipeline> createOriginalRenderPipeline(
        const RenderPipelineDesc& desc, const std::shared_ptr<PipelineLayout>& layout);
    core::UniquePtr<ComputePipeline> createOriginalComputePipeline(
        const ComputePipelineDesc& desc, const std::shared_ptr<PipelineLayout>& layout);
    core::UniquePtr<ComputePipeline> createOriginalArtifactComputePipeline(
        const std::shared_ptr<const OriginalComputePipelineCreation>& creation,
        OriginalAcceptedComputePipelineFactoryContract::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota);
    core::UniquePtr<RenderPipeline> createOriginalArtifactGraphicsPipeline(
        const std::shared_ptr<const OriginalGraphicsPipelineCreation>& creation,
        OriginalAcceptedGraphicsPipelineFactoryContract::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota);
    core::UniquePtr<ComputePipeline> createOriginalTableExpansionComputePipeline(
        const std::shared_ptr<const OriginalComputePipelineCreation>& creation,
        OriginalAcceptedTableExpansionPipelineContract::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota);
    core::UniquePtr<RenderPipeline> createOriginalTableExpansionGraphicsPipeline(
        const std::shared_ptr<const OriginalGraphicsPipelineCreation>& creation,
        OriginalAcceptedTableExpansionPipelineContract::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota);
    virtual arp::core::UniquePtr<DescriptorArena> createDescriptorArena(
        const DescriptorArenaDesc& desc) = 0;
    core::UniquePtr<DescriptorArena> createOriginalDescriptorArena(const DescriptorArenaDesc& desc);
    core::UniquePtr<DescriptorSet> allocateOriginalDescriptorSet(
        const std::shared_ptr<DescriptorArena>& arena, const DescriptorSetDesc& desc);
    core::UniquePtr<DescriptorTable> allocateOriginalDescriptorTable(
        const std::shared_ptr<DescriptorArena>& arena, const DescriptorTableDesc& desc);

    virtual core::UniquePtr<OriginalConformanceClearExecution> createConformanceClearExecution(
        core::UniquePtr<ConformanceClearWorkload>,
        const ConformanceClearExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic)
    {
        diagnostic = ConformanceClearExecutionContract::validate(request);
        if (!diagnostic)
            diagnostic = ConformanceClearExecutionContract::loweringDiagnostic(backend());
        if (!diagnostic)
            diagnostic = "RHI-CONFORMANCE-CLEAR-ORIGINAL-ISSUER-UNAVAILABLE";
        return {};
    }

    virtual core::UniquePtr<OriginalConformanceStorageMrtExecution>
    createConformanceStorageMrtExecution(core::UniquePtr<ConformanceStorageMrtWorkload>,
        const ConformanceStorageMrtExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic)
    {
        diagnostic = ConformanceStorageMrtExecutionContract::validate(request);
        if (!diagnostic)
            diagnostic = ConformanceStorageMrtExecutionContract::loweringDiagnostic(backend());
        if (!diagnostic)
            diagnostic = "RHI-CONFORMANCE-STORAGE-MRT-ORIGINAL-ISSUER-UNAVAILABLE";
        return {};
    }

    virtual core::UniquePtr<OriginalConformanceSampledExecution> createConformanceSampledExecution(
        core::UniquePtr<ConformanceSampledWorkload>,
        const ConformanceSampledExecutionRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic)
    {
        diagnostic = ConformanceSampledExecutionContract::validate(request);
        if (!diagnostic)
            diagnostic = ConformanceSampledExecutionContract::loweringDiagnostic(backend());
        if (!diagnostic)
            diagnostic = "RHI-CONFORMANCE-SAMPLED-ORIGINAL-ISSUER-UNAVAILABLE";
        return {};
    }

    virtual arp::core::UniquePtr<CommandEncoder> createCommandEncoder(
        const CommandEncoderDesc& desc) = 0;
    virtual arp::core::UniquePtr<SecondaryCommandEncoder> createSecondaryCommandEncoder(
        const SecondaryCommandEncoderDesc& desc) = 0;
    virtual arp::core::UniquePtr<RenderBundleEncoder> createRenderBundleEncoder(
        const RenderBundleDesc& desc) = 0;
    virtual arp::core::UniquePtr<Swapchain> createSwapchain(const SwapchainDesc& desc) = 0;
    // Standalone submission completion. NativeSync includes binary semaphore
    // ordering and is intentionally broader than this factory. Return a genuine
    // queue-bound native completion (Null: validator only), or null/diagnostic.
    // Availability is independent of CommandBuffer::ownedSubmissionFence().
    virtual arp::core::UniquePtr<GpuFence> createFence() = 0;
    virtual arp::core::UniquePtr<GpuSemaphore> createSemaphore() = 0;
    virtual arp::core::UniquePtr<GpuTimelineSemaphore> createTimelineSemaphore(
        uint64_t initialValue = 0) = 0;
    virtual PipelineCache& pipelineCache() = 0;
    virtual DescriptorCache& descriptorCache() = 0;
    virtual arp::core::UniquePtr<QueryPool> createQueryPool(const QueryPoolDesc& desc) = 0;

    /// Optional extension interface query. Returns nullptr when the backend
    /// does not support the requested extension. Core rendering paths must
    /// not depend on extensions; use capabilities() for the baseline.
    /// Extension IDs are defined in gpu_extensions.h.
    virtual void* queryInterface(std::string_view extensionId) noexcept;

protected:
    virtual core::UniquePtr<RenderPipeline> createAcceptedOriginalGraphicsPipeline(
        const RenderPipelineDesc&,
        const std::shared_ptr<const OriginalGraphicsPipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);
    virtual core::UniquePtr<ComputePipeline> createAcceptedOriginalComputePipeline(
        const ComputePipelineDesc&,
        const std::shared_ptr<const OriginalComputePipelineCreation>&,
        const std::shared_ptr<core::CpuMemoryQuota>&,
        const char*& diagnostic);
    virtual core::UniquePtr<GpuTexture> createOriginalContentTexturePayload(const TextureDesc&,
        const OriginalTextureContentCreationContract::Decision&,
        const OriginalTextureContentCreationBudget&,
        const char*& diagnostic);
    virtual std::uint64_t queryOriginalContentTextureAllocationPayload(
        const TextureDesc&, const char*& diagnostic);
    void closeOriginalDeviceLifetimeForShutdown() noexcept;
    virtual std::shared_ptr<OriginalDeviceNativeLossBinding> bindOriginalDeviceNativeLoss(
        const OriginalDeviceLifetime::NativeLossReceiver&,
        const OriginalDeviceNativeLossBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    virtual std::shared_ptr<OriginalDeviceNativeLossBinding> bindOriginalDeviceNativeResultLoss(
        const OriginalDeviceLifetime::NativeLossReceiver&,
        const OriginalDeviceNativeResultLossBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    virtual std::shared_ptr<OriginalDeviceNativeLossBinding> bindOriginalObservedNativeLoss(
        const OriginalDeviceLifetime::NativeLossReceiver&,
        const OriginalDeviceNativeLossBudget&,
        const OriginalDeviceNativeLossObserverRequest&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic);
    virtual const char* validateOriginalNativeLossResource(const GpuBuffer&) const noexcept;
    virtual const char* validateOriginalNativeLossResource(const GpuTexture&) const noexcept;
    virtual const char* validateOriginalNativeLossResource(const GpuTextureView&) const noexcept;
    virtual const char* validateOriginalNativeLossResource(const GpuSampler&) const noexcept;
    virtual const char* validateOriginalNativeLossPipeline(const ComputePipeline&) const noexcept;
    virtual const char* validateOriginalNativeLossPipeline(const RenderPipeline&) const noexcept;
    virtual const char* validateOriginalNativeLossDescriptor(const DescriptorArena&) const noexcept;
    virtual const char* validateOriginalNativeLossDescriptor(const DescriptorSet&) const noexcept;
    // Consume only a newly created unpublished factory candidate. Upper layers
    // cannot adopt borrowed objects or caller-created records into this issuer.
    template <class T>
    core::UniquePtr<T> completeOriginalResourceCreation(
        core::UniquePtr<T> resource, const GpuTexture* parent = nullptr);
    template <class Factory>
    core::UniquePtr<GpuMemoryHeap> createOriginalMemoryHeap(Factory&& factory);
    const char* originalMemoryHeapCreationDiagnostic() const noexcept;

private:
    // Reserve existing registry capacities and quota before any backend call.
    // A reserved record is unpublished and fails ticket validation until bound.
    std::shared_ptr<OriginalResourceRegistration> reserveOriginalPipelineRegistration(
        OriginalRegisteredResourceKind kind, const std::shared_ptr<PipelineLayout>& layout);
    // Shared cold issuance machinery for /1 resources and /2 typed objects.
    // Factory-specific identity/parent/domain checks stay at their boundaries.
    std::shared_ptr<OriginalResourceRegistration> reserveOriginalRegistrationRecord(
        OriginalRegisteredResourceKind kind,
        const std::shared_ptr<OriginalResourceRegistration>& parent,
        const void* object,
        std::uint64_t id,
        core::LifetimeEpoch::Ticket epoch = {});
    std::shared_ptr<OriginalResourceRegistration> reserveOriginalDescriptorRegistration(
        OriginalRegisteredResourceKind kind, const std::shared_ptr<DescriptorArena>& arena);
    template <class T>
    core::UniquePtr<T> publishOriginalDescriptorCandidate(
        core::UniquePtr<T> candidate, std::shared_ptr<OriginalResourceRegistration> record);
    template <class T, class Factory, class Validate>
    core::UniquePtr<T> allocateOriginalDescriptor(const std::shared_ptr<DescriptorArena>& arena,
        OriginalRegisteredResourceKind kind,
        Factory&& factory,
        Validate&& validate);
    // Accepted typed factories share the exact same registry/domain admission.
    template <class Pipeline,
        class Contract,
        bool TableExpansion = false,
        class Creation,
        class Factory>
    core::UniquePtr<Pipeline> createOriginalAcceptedArtifactPipeline(
        const std::shared_ptr<const Creation>& creation,
        typename Contract::Budget budget,
        const std::shared_ptr<core::CpuMemoryQuota>& wrapperQuota,
        std::uint32_t minimumRegistryVersion,
        const char* registryId,
        const char* quotaId,
        const char* domainId,
        OriginalRegisteredResourceKind kind,
        Factory&& factory);
    template <class Pipeline, class Desc, class Factory>
    core::UniquePtr<Pipeline> createOriginalPipeline(const Desc& desc,
        const std::shared_ptr<PipelineLayout>& layout,
        OriginalRegisteredResourceKind kind,
        Factory&& factory,
        std::shared_ptr<const OriginalComputePipelineCreation> creation = {},
        std::shared_ptr<const OriginalGraphicsPipelineCreation> graphicsCreation = {});
    template <class Control>
    static const char* bindOriginalControlDomain(
        const Control* control, const OriginalDeviceLifetime::Ticket& ticket) noexcept;
    static const char* bindOriginalStateDomain(
        GpuBuffer& object, const OriginalDeviceLifetime::Ticket& ticket) noexcept;
    static const char* bindOriginalStateDomain(
        GpuTexture& object, const OriginalDeviceLifetime::Ticket& ticket) noexcept;
    static const char* bindOriginalStateDomain(
        GpuTextureView&, const OriginalDeviceLifetime::Ticket&) noexcept;
    static const char* bindOriginalStateDomain(
        GpuSampler&, const OriginalDeviceLifetime::Ticket&) noexcept;
    std::shared_ptr<OriginalDeviceLifetime> originalLifetime_;
    std::shared_ptr<OriginalDeviceNativeLossBinding> originalLossBinding_;
    std::shared_ptr<OriginalResourceRegistrationRegistry> originalRegistry_;
    const char* lastOriginalRegistrationDiagnostic_ = nullptr;
};

// One accepted submission group per mutable input slot. Native storage remains
// backend-owned. See GpuInputSlotLease/1; no CPU wait or allocation here.
class GpuInputSlotLease final {
public:
    GpuInputSlotLease() = default;
    GpuInputSlotLease(const GpuInputSlotLease&) = delete;
    GpuInputSlotLease& operator=(const GpuInputSlotLease&) = delete;
    GpuInputSlotLease(GpuInputSlotLease&&) = default;
    GpuInputSlotLease& operator=(GpuInputSlotLease&&) = default;

    bool ready() const { return !completion_ || completion_->isSignaled(); }

    bool matches(const std::shared_ptr<const GpuFence>& completion) const noexcept
    {
        return completion && completion_ == completion;
    }

    bool canRetain(const std::shared_ptr<const GpuFence>& completion) const
    {
        return completion && (matches(completion) || ready());
    }

    bool retain(const std::shared_ptr<const GpuFence>& completion)
    {
        if (!canRetain(completion))
            return false;
        completion_ = completion;
        return true;
    }

private:
    std::shared_ptr<const GpuFence> completion_;
};

} // namespace arp::rhi

// Inline definitions follow the complete public declarations.
#include "rhi/frontend/device_device.inl"
