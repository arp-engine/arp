#pragma once
#include "core/cpu_allocator.h"
#include "core/quota_bound_shared_identity.h"
#include "core/runtime/completion_signal.h"
#include "core/strided_image_statistics.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_commands.h"
#include "rhi/gpu_descriptors.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/gpu_resources.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/webgpu_timestamp_command_point.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace arp::rhi {
class BindlessAuthorization;
class BindlessShaderPackage;
class BindlessAccessDeclarations;
class OriginalSingleEntryPipelineInput;
} // namespace arp::rhi

namespace arp::rhi_webgpu {

struct WebGpuNativeBufferPayload;
struct WebGpuNativeTexturePayload;
struct WebGpuNativeTextureViewPayload;
struct WebGpuNativeSamplerPayload;
struct WebGpuNativeDescriptorGroupPayload;
struct WebGpuNativeShaderModulePayload;
struct WebGpuNativeRenderPipelinePayload;
struct WebGpuNativeComputePipelinePayload;
class WebGpuNativeZeroBaseIndirectProducer;
class WebGpuNativeIndirectProcessor;
struct WebGpuProducerCompletionOwner;
class WebGpuRhiResourceRegistry;
struct WebGpuOrdinaryTransferBudget;
struct WebGpuOrdinaryTransferCompletionBudget;
std::shared_ptr<WebGpuProducerCompletionOwner> prepareWebGpuOrdinaryTransferCompletionOwner(
    const WebGpuOrdinaryTransferCompletionBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    core::pmr::String& diagnostic);
std::shared_ptr<WebGpuProducerCompletionOwner> prepareWebGpuProducerCompletionOwner(
    std::uint32_t maxPendingSubmissions, core::pmr::String& diagnostic);
struct WebGpuNativeRenderBundlePayload;
struct WebGpuNativeCommandPayload;
rhi::semantics::WebGpuTimestampCommandPointFacts webGpuNativeTimestampCommandPointFacts() noexcept;

class WebGpuRhiBindlessFaultReadback final {
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

    void complete(std::uint32_t bits) noexcept
    {
        bits_.store(bits, std::memory_order_relaxed);
        auto pending = rhi::BindlessFaultValidationStatus::Pending;
        status_.compare_exchange_strong(pending,
            bits ? rhi::BindlessFaultValidationStatus::Faulted
                 : rhi::BindlessFaultValidationStatus::Clean,
            std::memory_order_release,
            std::memory_order_relaxed);
    }

    void fail() noexcept
    {
        auto pending = rhi::BindlessFaultValidationStatus::Pending;
        status_.compare_exchange_strong(pending,
            rhi::BindlessFaultValidationStatus::ReadbackFailed,
            std::memory_order_release,
            std::memory_order_relaxed);
    }

    void abandon() noexcept
    {
        auto pending = rhi::BindlessFaultValidationStatus::Pending;
        status_.compare_exchange_strong(pending,
            rhi::BindlessFaultValidationStatus::Abandoned,
            std::memory_order_release,
            std::memory_order_relaxed);
    }

private:
    std::atomic<rhi::BindlessFaultValidationStatus> status_{
        rhi::BindlessFaultValidationStatus::Pending};
    std::atomic<std::uint32_t> bits_{0u};
};
struct WebGpuNativeSwapchainPayload;

struct WebGpuNativeCommandStats {
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t nativeCommandEncoderCreateCount = 0;
    std::uint32_t nativeCommandBufferCreateCount = 0;
    std::uint32_t nativeQueueSubmitCount = 0;
    std::uint32_t nativeCommandRecordCount = 0;
    std::uint32_t nativeBufferBarrierRecordCount = 0;
    std::uint32_t nativeTextureBarrierRecordCount = 0;
    std::uint32_t nativeBufferCopyRecordCount = 0;
    std::uint32_t nativeBufferToTextureCopyRecordCount = 0;
    std::uint32_t nativeTextureToBufferCopyRecordCount = 0;
    std::uint32_t nativeTextureCopyRecordCount = 0;
    std::uint32_t nativeRenderPassBeginRecordCount = 0;
    std::uint32_t nativeRenderPassEndRecordCount = 0;
    std::uint32_t nativeGraphicsPipelineBindRecordCount = 0;
    std::uint32_t nativeIndexedDrawRecordCount = 0;
    std::uint32_t nativeDrawRecordCount = 0;
    std::uint32_t nativeComputePassBeginRecordCount = 0;
    std::uint32_t nativeComputePassEndRecordCount = 0;
    std::uint32_t nativeComputePipelineBindRecordCount = 0;
    std::uint32_t nativeDescriptorBindRecordCount = 0;
    std::uint32_t nativePushConstantUploadRingWriteCount = 0;
    std::uint32_t nativePushConstantUploadRingByteCount = 0;
    std::uint32_t pushConstantRequestedByteCount = 0;
    std::uint32_t pushConstantUsedByteCount = 0;
    std::uint32_t pushConstantBudgetByteCapacity = 0;
    std::uint32_t pushConstantRingExhaustionCount = 0;
    std::uint64_t pushConstantSubmissionSerial = 0;
    std::uint32_t nativeDispatchRecordCount = 0;
    std::uint32_t nativeDispatchIndirectRecordCount = 0;
    std::uint32_t nativeTimestampRecordCount = 0;
    std::uint32_t nativeTimestampResolveCount = 0;
    std::uint32_t nativeTimingReadbackScheduleCount = 0;
};

/// Backend-private completion state for a queue callback.  This is a CPU
/// completion notification only; it is deliberately not an L2 timeline
/// semaphore or a native WebGPU handle.
struct WebGpuOrdinaryCompletionSignalBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxStates = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

struct WebGpuNativeCompletionState : core::QuotaBoundSharedObjectIdentity {
    using SignalTicket = core::runtime::CompletionSignal::Ticket;
    static const char* validateBudget(const WebGpuOrdinaryCompletionSignalBudget&) noexcept;
    static std::shared_ptr<WebGpuNativeCompletionState> prepareOrdinarySignal(
        const WebGpuOrdinaryCompletionSignalBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*&);

    bool ordinarySignalOwnerMatches(
        const std::shared_ptr<WebGpuNativeCompletionState>& owner) const noexcept
    {
        return ordinarySignalPrepared_ && hasQuotaBoundSharedOwner(owner);
    }

    void reset() noexcept;
    void complete() noexcept;
    bool isComplete() const noexcept;
    void wait();

    bool canReserveSignal() const noexcept { return signal_.canReserve(); }

    SignalTicket reserveSignal() noexcept { return signal_.reserve(); }

    bool signalMatches(SignalTicket ticket) const noexcept { return signal_.matches(ticket); }

    bool completeSignal(SignalTicket ticket) noexcept;

    bool releaseSignal(SignalTicket ticket) noexcept { return signal_.release(ticket); }

private:
    core::runtime::CompletionSignal signal_;
    bool ordinarySignalPrepared_ = false;
    mutable std::mutex mutex;
    std::condition_variable condition;
};

struct WebGpuNativeSwapchainStats {
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t nativeSurfaceCreateCount = 0;
    std::uint32_t nativeSurfaceConfigureCount = 0;
    std::uint32_t nativeCanvasAcquireCount = 0;
    std::uint32_t browserManagedPresentCount = 0;
    std::uint32_t nativeResizeCount = 0;
    std::uint32_t surfaceLossCount = 0;
    std::uint32_t surfaceRecreateCount = 0;
    std::uint32_t hiddenTabFrameSkipCount = 0;
    std::uint32_t boundedFrameSlotCount = 0;
};

struct WebGpuNativeDescriptorBindingPayload {
    rhi::DescriptorBindingDesc binding;
    rhi::DescriptorBindingWrite write;
    std::uint32_t arrayElement = 0u;
    std::shared_ptr<WebGpuNativeBufferPayload> buffer;
    std::shared_ptr<WebGpuNativeTextureViewPayload> textureView;
    std::shared_ptr<WebGpuNativeSamplerPayload> sampler;
};

struct WebGpuNativeRuntimeSmokeDesc {
    std::pmr::string canvasSelector = "#canvas";
    rhi::Extent2D extent{320u, 180u};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Bgra8Unorm;
    bool exerciseResize = true;
};

struct WebGpuNativeRuntimeReport {
    bool success = false;
    std::pmr::string diagnostics;
    bool nativeDeviceAvailable = false;
    bool nativeBufferCreated = false;
    bool nativeReadbackBufferCreated = false;
    bool nativeTextureCreated = false;
    bool nativeTextureViewCreated = false;
    bool nativeDepthTextureCreated = false;
    bool nativeDepthTextureViewCreated = false;
    bool nativeSamplerCreated = false;
    bool nativeDescriptorGroupCreated = false;
    bool nativeShaderModuleCreated = false;
    bool nativePipelineCreated = false;
    bool nativeCommandEncoderCreated = false;
    bool nativeCommandBufferCreated = false;
    bool queueSubmitted = false;
    bool textureReadbackScheduled = false;
    bool depthReadbackScheduled = false;
    bool canvasAcquired = false;
    bool canvasPresentedByBrowser = false;
    bool resizeHandled = false;
    bool perFramePipelineRebuild = false;
    bool nativeHandleExposedToUpperLayer = false;
    std::uint32_t pipelineCreateCount = 0;
    std::uint32_t queueSubmitCount = 0;
};

/// Browser-owned WebGPU device limits copied into backend-neutral scalar
/// facts. The struct contains no native handles and is zero when the browser
/// bootstrap has not completed or no native WebGPU runtime is available.
struct WebGpuNativeDeviceLimits {
    std::uint64_t maxBufferSize = 0u;
    std::uint32_t maxTexture3DSize = 0u, maxTextureArrayLayers = 0u;
    std::uint32_t maxTexture2DSize = 0;
    std::uint32_t maxColorAttachments = 0;
    std::uint32_t maxDescriptorBindings = 0;
    std::uint32_t maxSampledTexturesPerShaderStage = 0;
    std::uint32_t maxSamplersPerShaderStage = 0;
    std::uint32_t maxUniformBuffersPerShaderStage = 0;
    std::uint32_t maxStorageTexturesPerShaderStage = 0;
    std::uint32_t maxBindGroups = 0;
    std::uint32_t uniformBufferOffsetAlignment = 0;
    std::uint32_t storageBufferOffsetAlignment = 0;
    std::uint32_t maxUniformBufferBindingSize = 0;
    std::uint32_t maxStorageBufferBindingSize = 0;
    std::uint32_t maxComputeInvocationsPerWorkgroup = 0;
    std::uint32_t maxComputeWorkgroupSizeX = 0;
    std::uint32_t maxComputeWorkgroupSizeY = 0;
    std::uint32_t maxComputeWorkgroupStorageSize = 0;
    std::uint32_t maxStorageBuffersPerShaderStage = 0;
    std::uint32_t maxComputeWorkgroupSizeZ = 0;
    std::uint32_t maxComputeWorkgroupsPerDimension = 0;
    std::uint32_t maxVertexBuffers = 0;
    std::uint32_t maxVertexAttributes = 0;
    std::uint32_t maxVertexBufferArrayStride = 0;
};

/// The feature must be present on the created device, not just its adapter.
bool webGpuNativeDeviceSupportsIndirectFirstInstance() noexcept;

/// Sanitized result mailbox for production attachment readback.  The native
/// WebGPU buffer remains backend-owned; callers only receive numeric samples.
bool webGpuNativeDeviceAvailable();

// Registers the exact native device's loss promise. The caller preallocates
// userdata and transfers it only on true; callback delivery never allocates.
bool observeWebGpuNativeDeviceLoss(void* device, void* userdata, void (*complete)(void*)) noexcept;

// OriginalDeviceLossRegistration/1. Backend-private Emval state, no global
// registry. Native detach clears userdata before C++ may retire it. Release is
// legal only after native delivery/detach (or failed cold binding).
std::uintptr_t prepareWebGpuNativeDeviceLossRegistration(
    void* device, void* userdata, void (*complete)(void*)) noexcept;
bool detachWebGpuNativeDeviceLossRegistration(std::uintptr_t state) noexcept;
void releaseWebGpuNativeDeviceLossRegistration(std::uintptr_t state) noexcept;

template <class Owner>
bool observeWebGpuNativeOwnerDeviceLoss(
    const std::shared_ptr<Owner>& owner, void* device, void (*complete)(void*))
{
    auto observer = core::makeUnique<std::shared_ptr<Owner>>(owner);
    if (!observeWebGpuNativeDeviceLoss(device, observer.get(), complete))
        return false;
    (void)observer.release();
    return true;
}

/// Returns the effective limits of the browser-owned device after the
/// asynchronous bootstrap has requested it. Capability profile construction
/// must fail closed when any required limit is zero; hardcoded desktop limits
/// are not a valid substitute for browser device facts.
WebGpuNativeDeviceLimits webGpuNativeDeviceLimits() noexcept;

/// Checks whether the browser-owned device can execute a pipeline requiring
/// the given number of physical bind groups. The WebGPU minimum of four is a
/// specification guarantee; any larger count is accepted only from the
/// queried device limit and never from a desktop/default guess.
bool webGpuNativeBindGroupCountSupported(std::uint32_t requiredCount) noexcept;

/// Returns true only when the current browser device has the optional
/// timestamp-query feature enabled and accepts the complete backend payload:
/// query set plus query-resolve and CPU-readable readback buffers. This is a
/// capability probe, not a CPU-timer check.
bool webGpuNativeTimestampQuerySupported() noexcept;

/// Allocates a backend-owned serial for an asynchronously resolved timing
/// sample. The serial is opaque to callers and is never a native handle.
std::uint64_t webGpuNativeAllocateTimingFrameSerial() noexcept;

/// Starts mapping a backend-owned timestamp result buffer after its command
/// buffer has been submitted. Native WebGPU buffers remain private to this
/// backend and are released by the asynchronous callback.
void mapWebGpuNativeTimingReadbackBuffers(
    void* resolveBuffer, void* readbackBuffer, std::uint64_t frameSerial) noexcept;

/// Returns the most recently completed backend-owned timestamp sample.
rhi::GpuTimingSample webGpuNativeLatestGpuTimingSample() noexcept;

/// Reports the browser-owned device-loss state without exposing a native
/// handle.  Teardown may still release wrappers after loss, but must avoid
/// issuing per-resource destroy calls against an already invalid device.
bool webGpuNativeDeviceLost() noexcept;

// WebGenericQueueAdmission/1. Complete original general resource usage and
// cold completion ownership are mandatory; no public/general execution grant.
inline constexpr const char* webGpuGenericCompletionResourceGraphDiagnostic =
    "RHI-WEBGPU-ORDINARY-COMPLETION-RESOURCE-GRAPH-UNPROVEN";
// WebGenericRecordingAdmission/1. Ownership metadata alone never authorizes
// native recording, immediate queue writes or automatic resource creation.
inline constexpr const char* webGpuGenericRecordingUsageDiagnostic =
    "RHI-WEBGPU-COMMAND-RECORDING-USAGE-UNPROVEN";

/// Probes whether the current browser device accepts a texture format with
/// storage-texture usage. The probe is intentionally backend-owned: WebGPU
/// has no per-format feature enum for the float formats used by WoCC, so the
/// only truthful portable check is device-side resource validation. The
/// caller must treat a failed probe as DiagnosticFailure; the probe may be
/// unable to report a browser-side validation reason through the C API.
bool webGpuNativeDeviceSupportsStorageTexture(rhi::TextureFormat format);

/// Probes the backend-owned storage-buffer resource and bind-group contract.
/// Storage buffers are core WebGPU, but this result is kept independent from
/// storage-texture format probes so capability promotion cannot conflate the
/// two RHI features.
bool webGpuNativeDeviceSupportsStorageBuffer() noexcept;

/// Probes the complete software-BVH scene-ray-query substrate used by DDGI:
/// two read-only storage buffers with the node/leaf binding contract and a
/// compute pipeline that reads both layouts. This remains backend-owned; the
/// public RHI receives only the boolean capability result.
bool webGpuNativeDeviceSupportsSceneGeometryRayQuery() noexcept;

/// Returns the browser bootstrap's effective BC-family texture-compression
/// capability. The adapter feature is enabled before the RHI device is
/// imported, so this query never guesses from a format enum alone.
bool webGpuNativeDeviceSupportsTextureCompressionBc() noexcept;

/// Probes the backend-owned compute pipeline contract. This is intentionally
/// separate from storage-buffer support: a device may accept a compute
/// pipeline without accepting a particular resource binding layout.
bool webGpuNativeDeviceSupportsComputeDispatch() noexcept;

/// Probes a portable storage-texture binding and compute-pipeline contract
/// using the guaranteed Rgba8Unorm storage format. Float format support is
/// reported separately by webGpuNativeDeviceSupportsStorageTexture().
bool webGpuNativeDeviceSupportsStorageTextureBinding() noexcept;

// Optional synchronous cold initialization. The callback is borrowed only for
// creation and receives the complete mapped allocation; it is never retained.
struct WebGpuNativeBufferInitializer {
    const void* context = nullptr;
    bool (*initialize)(const void*, void*, std::uint64_t) noexcept = nullptr;
};

std::shared_ptr<WebGpuNativeBufferPayload> createWebGpuNativeBufferPayload(
    const rhi::BufferDesc& desc, const WebGpuNativeBufferInitializer& initializer = {});

/// Lowers the L2 host-to-buffer upload contract to WebGPU queue.writeBuffer.
/// Returns false when a native payload/queue is unavailable; callers must not
/// report successful GPU work in that case.
bool writeWebGpuNativeBufferPayload(const WebGpuNativeBufferPayload& payload,
    const void* data,
    std::uint64_t sizeBytes,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics);

std::shared_ptr<WebGpuNativeTexturePayload> createWebGpuNativeTexturePayload(
    const rhi::TextureDesc& desc);

std::shared_ptr<WebGpuNativeTextureViewPayload> createWebGpuNativeTextureViewPayload(
    const WebGpuNativeTexturePayload& texture, const rhi::TextureViewDesc& desc);

/// Return a backend-owned cached native view for a texture descriptor. The
/// cache excludes the descriptive label but preserves every field that
/// affects the native view or the L2 view identity.
std::shared_ptr<WebGpuNativeTextureViewPayload> getOrCreateWebGpuNativeTextureViewPayload(
    const WebGpuNativeTexturePayload& texture, const rhi::TextureViewDesc& desc);

std::shared_ptr<WebGpuNativeSamplerPayload> createWebGpuNativeSamplerPayload(
    const rhi::SamplerDesc& desc);

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> createWebGpuNativeDescriptorGroupPayload(
    const rhi::DescriptorLayoutDesc& layout,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>& bindings);

struct WebGpuOriginalBufferRangePublication {
    rhi::DescriptorBindingWrite write;
    std::shared_ptr<const rhi::OriginalCommandBufferLease> source;
};

struct WebGpuCompletedTexturePublication {
    // Tagged retained metadata; legacy completed consumers refuse cell identity.
    rhi::DescriptorBindingWrite write;
    std::shared_ptr<const rhi::OriginalCommandTextureViewSource> source;
    std::optional<rhi::GpuTexture::ContentExpectation> content;
    std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource> depthContent;
    std::optional<rhi::GpuTexture::ContentRangeExpectation> cellIdentity;
    rhi::OriginalTextureCellDescriptorRequest cellRequest;
    std::uint32_t colorPublicationVersion = 2u;

    WebGpuCompletedTexturePublication& operator=(const WebGpuCompletedTexturePublication& other)
    {
        if (this != &other) {
            write = other.write;
            source = other.source;
            content.reset();
            if (other.content)
                content.emplace(*other.content);
            depthContent = other.depthContent;
            cellIdentity.reset();
            if (other.cellIdentity)
                cellIdentity.emplace(*other.cellIdentity);
            cellRequest = other.cellRequest;
            colorPublicationVersion = other.colorPublicationVersion;
        }
        return *this;
    }
};

// Retained descriptor metadata only. Every finite binding must match an
// authentic typed publication; the generic factory remains whole-only.
std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
createWebGpuNativeOriginalRangeDescriptorPayload(const rhi::DescriptorLayoutDesc&,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>&,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>&);
std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
createWebGpuNativeOriginalCompletedTextureDescriptorPayload(const rhi::DescriptorLayoutDesc&,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>&,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>&,
    const core::pmr::Vector<WebGpuCompletedTexturePublication>&);

std::shared_ptr<WebGpuNativeDescriptorGroupPayload>
createWebGpuNativeOriginalTextureCellDescriptorPayload(const rhi::DescriptorLayoutDesc&,
    const core::pmr::Vector<WebGpuNativeDescriptorBindingPayload>&,
    const core::pmr::Vector<WebGpuOriginalBufferRangePublication>&,
    const core::pmr::Vector<WebGpuCompletedTexturePublication>&);

std::shared_ptr<WebGpuNativeShaderModulePayload> createWebGpuNativeShaderModulePayload(
    const rhi::ShaderPackageDesc& package);

std::shared_ptr<WebGpuNativeShaderModulePayload> createWebGpuNativeBindlessShaderModulePayload(
    const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    rhi::ShaderStage stage,
    core::pmr::String& diagnostic,
    bool explicitPush = false);

std::shared_ptr<WebGpuNativeComputePipelinePayload>
createWebGpuNativeBindlessComputePipelinePayload(const rhi::BindlessShaderPackage& package,
    std::string_view entry,
    core::pmr::String& diagnostic);

std::shared_ptr<WebGpuNativeZeroBaseIndirectProducer> prepareWebGpuNativeZeroBaseIndirectProducer(
    const rhi::BindlessShaderPackage&,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>&,
    core::pmr::String& diagnostic);

std::shared_ptr<WebGpuNativeRenderPipelinePayload> createWebGpuNativeBindlessRenderPipelinePayload(
    const rhi::BindlessShaderPackage& package,
    std::string_view vertexEntry,
    std::string_view fragmentEntry,
    const rhi::RenderPipelineDesc& state,
    core::pmr::String& diagnostic,
    const rhi::BindlessAuthorization* authorization = nullptr,
    const rhi::BindlessGraphicsPushRequest* push = nullptr);

std::shared_ptr<WebGpuNativeRenderPipelinePayload> createWebGpuNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc);

std::shared_ptr<WebGpuNativeComputePipelinePayload> createWebGpuNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc);

std::shared_ptr<WebGpuNativeRenderBundlePayload> createWebGpuNativeRenderBundlePayload(
    const rhi::RenderBundleDesc& desc, std::pmr::string& diagnostics);

bool recordWebGpuNativeRenderBundlePipeline(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    const WebGpuNativeRenderPipelinePayload& pipeline,
    std::pmr::string& diagnostics);
bool recordWebGpuNativeRenderBundleVertexBuffer(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t slot,
    const std::shared_ptr<WebGpuNativeBufferPayload>& buffer,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics);
bool recordWebGpuNativeRenderBundleIndexBuffer(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    const std::shared_ptr<WebGpuNativeBufferPayload>& buffer,
    rhi::IndexFormat format,
    std::uint64_t offsetBytes,
    std::pmr::string& diagnostics);
bool recordWebGpuNativeRenderBundleDescriptorSet(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t set,
    const WebGpuNativeRenderPipelinePayload& pipeline,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptorGroup,
    const std::uint32_t* dynamicOffsets,
    std::uint32_t dynamicOffsetCount,
    std::pmr::string& diagnostics);
bool recordWebGpuNativeRenderBundleDrawIndexed(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t indexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstIndex,
    std::int32_t vertexOffset,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics);
bool recordWebGpuNativeRenderBundleDraw(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::uint32_t vertexCount,
    std::uint32_t instanceCount,
    std::uint32_t firstVertex,
    std::uint32_t firstInstance,
    std::pmr::string& diagnostics);
bool finishWebGpuNativeRenderBundle(
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle, std::pmr::string& diagnostics);
bool executeWebGpuNativeRenderBundle(
    const std::shared_ptr<WebGpuNativeCommandPayload>& commandPayload,
    const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
    std::pmr::string& diagnostics);

struct WebGpuNativeCommandPayload : core::SharedObjectIdentity {
    struct Impl;

    ~WebGpuNativeCommandPayload();

    explicit WebGpuNativeCommandPayload(core::UniquePtr<Impl> impl);

    /// Returns the opaque backend queue used by this payload. The handle is
    /// consumed only by WebGPU completion lowering and never crosses the RHI
    /// boundary.
    void* nativeQueue() const noexcept;
    bool completionDeviceMatches(void* nativeDevice) const noexcept;
    std::shared_ptr<class WebGpuOriginalStateTransaction> originalStateTransaction() const noexcept;
    std::shared_ptr<class WebGpuOriginalTextureUploadTransaction>
    originalTextureTransaction() const noexcept;
    std::shared_ptr<const class WebGpuOriginalTextureReadbackCopies>
    originalReadbackCopies() const noexcept;
    bool ordinaryTransferOnly() const noexcept;
    // Backend-private public wrappers preflight before allocating upload leases.
    const char* preparedRecordingDiagnostic() const noexcept;
    // Public transitions and Render/Compute entry share this raw admission gate.
    const char* nonTransferRecordingDiagnostic() const noexcept;
    const char* transitionRecordingDiagnostic() const noexcept;
    const char* sampledGeometryBarrierDiagnostic(const rhi::ResourceBarrierDesc&) const noexcept;
    const char* sampledGeometryBarrierDiagnostic(
        const rhi::ResourceMemoryBarrierDesc&) const noexcept;
    const char* memoryBarrierRecordingDiagnostic() const noexcept;
    bool recordRegisteredTransfer(const rhi::BufferCopyDesc&, core::pmr::String& diagnostic);
    bool recordRegisteredTransfer(
        const rhi::BufferToTextureCopyDesc&, core::pmr::String& diagnostic);
    bool recordRegisteredTransfer(
        const rhi::TextureToBufferCopyDesc&, core::pmr::String& diagnostic);
    bool recordRegisteredTransfer(
        const rhi::TextureToTextureCopyDesc&, core::pmr::String& diagnostic);

    bool recordBufferBarrier(const WebGpuNativeBufferPayload& buffer,
        const rhi::ResourceBarrierDesc& desc,
        std::pmr::string& diagnostics);
    bool recordTextureBarrier(const WebGpuNativeTexturePayload& texture,
        const rhi::ResourceBarrierDesc& desc,
        std::pmr::string& diagnostics);
    bool recordBufferMemoryBarrier(const WebGpuNativeBufferPayload& buffer,
        const rhi::ResourceMemoryBarrierDesc& desc,
        std::pmr::string& diagnostics);
    bool recordTextureMemoryBarrier(const WebGpuNativeTexturePayload& texture,
        const rhi::ResourceMemoryBarrierDesc& desc,
        std::pmr::string& diagnostics);
    bool recordBufferCopy(const WebGpuNativeBufferPayload& source,
        const WebGpuNativeBufferPayload& destination,
        const rhi::BufferCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool recordBindlessFaultLatchClear(
        std::shared_ptr<WebGpuNativeBufferPayload> latch, std::pmr::string& diagnostics);
    bool recordBindlessFaultLatchClear(
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot, std::pmr::string& diagnostics);
    bool retainIndexedPipeline(const std::shared_ptr<const void>&,
        std::shared_ptr<const rhi::BindlessAuthorization>,
        std::pmr::string& diagnostics);
    bool retainIndexedSnapshot(const std::shared_ptr<const void>&, std::pmr::string& diagnostics);
    bool recordPreparedZeroBaseIndirectProducer(
        std::shared_ptr<const WebGpuNativeZeroBaseIndirectProducer>, std::pmr::string& diagnostics);
    bool recordPreparedIndirectProcessor(std::shared_ptr<const WebGpuNativeIndirectProcessor>,
        const rhi::BindlessAccessDeclarations& producerAccess,
        const rhi::BindlessAccessDeclarations& consumerAccess,
        core::pmr::String& diagnostic,
        bool consumerRequired = false);
    bool recordIndirectProcessorDraw(const WebGpuNativeIndirectProcessor&,
        const rhi::BindlessSnapshot&,
        const rhi::BindlessAuthorization&,
        const rhi::BindlessAccessDeclarations&,
        core::pmr::String& diagnostic);
    std::uint64_t recordingIdentity() const noexcept;
    rhi::BindlessFaultReport processorFaultReport(std::uint64_t identity) const noexcept;
    std::shared_ptr<WebGpuRhiBindlessFaultReadback> scheduleIndirectProcessorFaultReadback(
        const WebGpuNativeIndirectProcessor&, core::pmr::String& diagnostic);
    rhi::BindlessFaultReport bindlessFaultReport(std::uint64_t snapshotGeneration) const noexcept;
    std::shared_ptr<WebGpuRhiBindlessFaultReadback> scheduleBindlessFaultReadback(
        std::shared_ptr<WebGpuNativeBufferPayload> latch, std::pmr::string& diagnostics);
    std::shared_ptr<WebGpuRhiBindlessFaultReadback> scheduleBindlessFaultReadback(
        std::shared_ptr<const rhi::BindlessSnapshot> snapshot, std::pmr::string& diagnostics);
    bool recordBufferToTextureCopy(const WebGpuNativeBufferPayload& source,
        const WebGpuNativeTexturePayload& destination,
        const rhi::BufferToTextureCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool recordTextureUpload(const WebGpuNativeTexturePayload& destination,
        const rhi::UploadTextureDesc& desc,
        std::pmr::string& diagnostics);
    bool recordTextureToBufferCopy(const WebGpuNativeTexturePayload& source,
        const WebGpuNativeBufferPayload& destination,
        const rhi::TextureToBufferCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool recordTextureToTextureCopy(const WebGpuNativeTexturePayload& source,
        const WebGpuNativeTexturePayload& destination,
        const rhi::TextureToTextureCopyDesc& desc,
        std::pmr::string& diagnostics);
    bool scheduleTextureReadback(std::shared_ptr<WebGpuNativeTexturePayload> source,
        rhi::TextureFormat format,
        const rhi::Extent3D& extent,
        std::string_view label,
        std::pmr::string& diagnostics);
    void retainTextureForSubmission(std::shared_ptr<WebGpuNativeTexturePayload> texture);
    bool recordRenderPassBegin(const rhi::RenderPassDesc& desc,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>& colorTargets,
        const core::pmr::Vector<const WebGpuNativeTexturePayload*>& resolveTargets,
        const WebGpuNativeTexturePayload* depthTarget,
        std::pmr::string& diagnostics);
    bool recordGraphicsPipelineBind(
        const WebGpuNativeRenderPipelinePayload& pipeline, std::pmr::string& diagnostics);
    bool recordRenderBundleExecute(const std::shared_ptr<WebGpuNativeRenderBundlePayload>& bundle,
        std::pmr::string& diagnostics);
    // WebGPU core: wgpuRenderPassEncoderSetViewport / SetScissorRect. No
    // extension or feature gate required. The pipeline does not bake
    // viewport/scissor on WebGPU (encoder-side dynamic state).
    bool recordSetViewport(const rhi::ViewportState& viewport, std::pmr::string& diagnostics);
    bool recordSetScissor(const rhi::ScissorState& scissor, std::pmr::string& diagnostics);
    bool recordRenderDescriptorSetBind(std::uint32_t set,
        const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptorGroup,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount,
        std::pmr::string& diagnostics);
    bool recordVertexBufferBind(std::uint32_t slot,
        const WebGpuNativeBufferPayload& buffer,
        std::uint64_t offsetBytes,
        std::pmr::string& diagnostics);
    bool recordIndexBufferBind(const WebGpuNativeBufferPayload& buffer,
        rhi::IndexFormat format,
        std::uint64_t offsetBytes,
        std::pmr::string& diagnostics);
    bool recordDrawIndexed(std::uint32_t indexCount,
        std::uint32_t instanceCount,
        std::uint32_t firstIndex,
        std::int32_t vertexOffset,
        std::uint32_t firstInstance,
        std::pmr::string& diagnostics);
    bool recordDrawIndexedIndirect(const WebGpuNativeBufferPayload& indirectBuffer,
        std::uint64_t indirectOffset,
        std::uint32_t drawCount,
        std::uint32_t stride,
        std::pmr::string& diagnostics);
    /// WebGPU core has no GPU-counted indexed-indirect draw command. This
    /// remains fail-closed until a lowering consumes countBuffer on the GPU;
    /// issuing maxDrawCount ordinary draws would not preserve the RHI
    /// contract.
    bool recordDrawIndexedIndirectCount(const WebGpuNativeBufferPayload& indirectBuffer,
        std::uint64_t indirectOffset,
        const WebGpuNativeBufferPayload& countBuffer,
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
    bool recordComputePassBegin(const rhi::ComputePassDesc& desc, std::pmr::string& diagnostics);
    bool recordComputePipelineBind(
        const WebGpuNativeComputePipelinePayload& pipeline, std::pmr::string& diagnostics);
    bool recordDescriptorSetBind(std::uint32_t set,
        const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptorGroup,
        const std::uint32_t* dynamicOffsets,
        std::uint32_t dynamicOffsetCount,
        std::pmr::string& diagnostics);
    bool recordPushConstants(rhi::ShaderStage stages,
        std::uint32_t offsetBytes,
        std::uint32_t sizeBytes,
        const void* data,
        std::pmr::string& diagnostics);
    bool recordDispatch(std::uint32_t groupCountX,
        std::uint32_t groupCountY,
        std::uint32_t groupCountZ,
        std::pmr::string& diagnostics);
    bool recordDispatchIndirect(const WebGpuNativeBufferPayload& indirectBuffer,
        std::uint64_t indirectOffset,
        std::pmr::string& diagnostics);
    bool recordTimestamp(
        const rhi::QueryPool& queryPool, std::uint32_t queryIndex, std::pmr::string& diagnostics);
    bool recordComputePassEnd(std::pmr::string& diagnostics);
    bool recordPushDebugGroup(std::string_view label, std::pmr::string& diagnostics);
    bool recordPopDebugGroup(std::pmr::string& diagnostics);
    bool recordInsertDebugMarker(std::string_view label, std::pmr::string& diagnostics);
    bool finish(std::pmr::string& diagnostics);
    bool submit(std::pmr::string& diagnostics);
    bool finished() const;

private:
    core::UniquePtr<Impl> impl_;
    friend class WebGpuNativeCommandPreparation;
    friend class WebGpuConformanceClearExecution;
    friend class WebGpuConformanceClearFrame;
    friend class WebGpuOrdinaryRecording;
    friend class WebGpuOrdinarySubmission;
    const char* restrictedRecordingDiagnostic() const noexcept;
    friend std::shared_ptr<WebGpuNativeCommandPayload>
    createWebGpuNativeOrdinaryTransferCommandPayload(const WebGpuRhiResourceRegistry&,
        const rhi::GpuResourceId*,
        std::size_t,
        const WebGpuOrdinaryTransferBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        std::shared_ptr<WebGpuProducerCompletionOwner>,
        std::shared_ptr<WebGpuNativeCommandStats>,
        core::pmr::String&,
        std::shared_ptr<const class WebGpuFrozenTransferArguments>);

    friend std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeCommandPayload(
        const std::pmr::string& label,
        std::uint32_t pushConstantUploadRingByteCapacity,
        std::uint32_t pushConstantMaxBindingSize,
        std::uint32_t pushConstantOffsetAlignment,
        std::uint64_t pushConstantSubmissionSerial,
        std::shared_ptr<WebGpuNativeCommandStats> stats,
        bool enableTimestampQueries,
        std::pmr::string& diagnostics,
        std::uint32_t maxBindlessFaultReadbacks,
        std::uint32_t maxBindlessPipelines,
        std::uint32_t maxPreparedZeroBaseProducers,
        std::shared_ptr<WebGpuProducerCompletionOwner> producerCompletionOwner,
        std::uint32_t maxRetainedBindlessSnapshots);
};

std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeOrdinaryTransferCommandPayload(
    const WebGpuRhiResourceRegistry&,
    const rhi::GpuResourceId*,
    std::size_t,
    const WebGpuOrdinaryTransferBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    std::shared_ptr<WebGpuProducerCompletionOwner>,
    std::shared_ptr<WebGpuNativeCommandStats>,
    core::pmr::String& diagnostic,
    std::shared_ptr<const class WebGpuFrozenTransferArguments> frozenArguments = {});

// Missing frozen input fails closed; never selects the existing dynamic-input
// Prototype transfer path. Both paths remain backend-private explicit opt-in.
std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeFrozenTransferCommandPayload(
    const WebGpuRhiResourceRegistry&,
    const rhi::GpuResourceId*,
    std::size_t,
    const WebGpuOrdinaryTransferBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    std::shared_ptr<WebGpuProducerCompletionOwner>,
    std::shared_ptr<WebGpuNativeCommandStats>,
    core::pmr::String& diagnostic,
    std::shared_ptr<const class WebGpuFrozenTransferArguments>);

std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeCommandPayload(
    const std::pmr::string& label,
    std::uint32_t pushConstantUploadRingByteCapacity,
    std::uint32_t pushConstantMaxBindingSize,
    std::uint32_t pushConstantOffsetAlignment,
    std::uint64_t pushConstantSubmissionSerial,
    std::shared_ptr<WebGpuNativeCommandStats> stats,
    bool enableTimestampQueries,
    std::pmr::string& diagnostics,
    std::uint32_t maxBindlessFaultReadbacks = 0u,
    std::uint32_t maxBindlessPipelines = 0u,
    std::uint32_t maxPreparedZeroBaseProducers = 0u,
    std::shared_ptr<WebGpuProducerCompletionOwner> producerCompletionOwner = {},
    std::uint32_t maxRetainedBindlessSnapshots = 0u);

struct WebGpuNativeSwapchainPayload {
    struct Impl;

    ~WebGpuNativeSwapchainPayload();

    explicit WebGpuNativeSwapchainPayload(core::UniquePtr<Impl> impl);

    bool acquireCanvas(std::uint32_t frameSlot, std::pmr::string& diagnostics);
    bool discardCanvasFrame(std::uint32_t frameSlot, std::pmr::string& diagnostics);
    bool browserManagedPresent(std::uint32_t frameSlot, std::pmr::string& diagnostics);
    /// Transfer ownership of the canvas texture for the given frame slot to a
    /// native texture payload. The slot is cleared; browserManagedPresent will
    /// skip releasing a transferred texture.
    std::shared_ptr<WebGpuNativeTexturePayload> takeCanvasTexture(std::uint32_t frameSlot);
    bool resizeCanvas(const rhi::Extent2D& extent, std::pmr::string& diagnostics);
    bool surfaceLoss(bool available, std::pmr::string& diagnostics);
    bool hiddenTabFrameSkip(std::pmr::string& diagnostics);

private:
    core::UniquePtr<Impl> impl_;

    friend std::shared_ptr<WebGpuNativeSwapchainPayload> createWebGpuNativeSwapchainPayload(
        const rhi::SwapchainDesc& desc,
        std::shared_ptr<WebGpuNativeSwapchainStats> stats,
        std::pmr::string& diagnostics);
};

std::shared_ptr<WebGpuNativeSwapchainPayload> createWebGpuNativeSwapchainPayload(
    const rhi::SwapchainDesc& desc,
    std::shared_ptr<WebGpuNativeSwapchainStats> stats,
    std::pmr::string& diagnostics);

WebGpuNativeRuntimeReport runWebGpuNativeRuntimeSmoke(const WebGpuNativeRuntimeSmokeDesc& desc);

#if ARP_WEBGPU_VALIDATION_ENABLED
// Registers an uncaptured error callback on the Emscripten WebGPU device so
// validation / out-of-memory / device-loss errors are logged to stderr instead
// of being silently dropped. Safe to call multiple times; only the first call
// takes effect.
void setupWebGpuUncapturedErrorCallback();
#endif

bool webGpuOrdinaryCompletionOwnerClosed(
    const std::weak_ptr<class WebGpuOrdinaryCompletionOwner>&) noexcept;

} // namespace arp::rhi_webgpu
