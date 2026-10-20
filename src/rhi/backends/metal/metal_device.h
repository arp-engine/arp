#pragma once

#include "core/cpu_allocator.h"
#include "rhi/backends/metal/metal_commands.h"
#include "rhi/backends/metal/metal_native_swapchain.h"
#include "rhi/frontend/memory_direct_object_budget.h"
#include "rhi/frontend/telemetry_direct_objects.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::rhi_metal {

class MetalRhiResourceRegistry;
// Backend-private identity for preparation/recording; never exposed through L2.
std::shared_ptr<MetalRhiResourceRegistry> metalRhiIssuingResourceRegistry(
    const rhi::GpuDevice&) noexcept;

struct MetalRhiDeviceDesc {
    std::pmr::string label;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract;
    std::uint64_t memoryBudgetBytes = 0;
    /// Backend-owned transfer staging budget. The command lowering must use
    /// the declared device budget rather than inventing a pool size.
    // Zero means no workload-owned staging budget was supplied. Device
    // creation must fail closed instead of selecting a backend default.
    std::uint64_t uploadStagingPageSizeBytes = 0u;
    std::uint32_t uploadStagingMaxPageCount = 0u;
    // Optional during the observation phase. Once supplied, every direct L2
    // pipeline/layout/descriptor-arena reservation is made before native
    // creation and released by the concrete object's lifetime.
    rhi::semantics::DirectObjectBudget directObjectBudget;
    bool enableArgumentBuffers = true;
    // MetalComputeBuffers/1 optional extension; ordinary arguments never opt in.
    bool enableBindlessCompute = false;
    // MetalComputeImages/1 requires the base Compute opt-in and queried image
    // prerequisites; never enabled by ordinary argument buffers or Graphics.
    bool enableBindlessComputeImages = false;
    bool enablePushConstantSetBytes = true;
    // Deprecated compatibility field. Metal push constants are lowered with
    // setBytes, never with an upload buffer.
    bool enablePushConstantUploadBuffer = true;
    bool enableMemorylessTransientTextures = true;
    bool enableDrawableLifecycleGuards = true;
    bool allowNativeHandleExposureToUpperLayers = false;
    bool allowBlockingDrawableAcquire = false;
    bool allowPerDrawPipelineCreation = false;
    bool allowPerFrameShaderCompilation = false;
    // Public PreparedGraphicsBinding/1 scalar buffer scope, composed through
    // Original Root/2 for additional sets. Indexed Vertex/Fragment reads and
    // Fragment query/atomic only; sampled images require the independent opt-in.
    bool enableBindlessGraphics = false;
    // Independent completed RGBA8 Fragment 2D/2DArray sampling. Requires the
    // Graphics opt-in, queried image limits and the original Content9 executor.
    bool enableBindlessGraphicsImages = false;
};

struct MetalRhiDeviceReport {
    bool success = false;
    std::pmr::string diagnostics;
    rhi::Backend backend = rhi::Backend::Metal;
    bool argumentBuffersIntegrated = false;
    bool argumentBindingsHiddenFromPublicApi = true;
    bool pushConstantUploadBufferEmulated = false;
    bool memoryIntentMapped = false;
    bool memoryBudgetTelemetrySanitized = false;
    bool drawableLifecycleGuarded = false;
    bool boundedDrawableSynchronization = false;
    bool queueSubmitSupported = false;
    bool swapchainDrawableLifecycleSupported = false;
    bool nullDelegateUsed = false;
    bool backendOwnedDevice = false;
    bool backendOwnedResourceObjects = false;
    bool backendOwnedDescriptorObjects = false;
    bool backendOwnedPipelineObjects = false;
    bool backendOwnedCommandObjects = false;
    bool backendOwnedQueueSubmit = false;
    bool backendOwnedSwapchain = false;
    bool backendOwnedUploadStagingSupported = false;
    bool uploadStagingNativeHandlesHidden = true;
    bool uploadStagingBounded = false;
    bool nativeResourcePayloadsPrivate = false;
    bool nativeBufferCreationPathPresent = false;
    bool nativeTextureCreationPathPresent = false;
    bool nativeTextureViewCreationPathPresent = false;
    bool nativeSamplerCreationPathPresent = false;
    bool privateStorageIntentMapped = false;
    bool sharedStorageIntentMapped = false;
    bool managedStorageIntentMapped = false;
    bool memorylessStorageIntentMapped = false;
    bool nonAppleNativeResourceRuntimeDiagnosticOnly = false;
    bool nativeArgumentBindingPayloadsPrivate = false;
    bool nativeArgumentBufferCreationPathPresent = false;
    bool uniformBufferArgumentBindingSupported = false;
    bool storageBufferArgumentBindingSupported = false;
    bool sampledTextureArgumentBindingSupported = false;
    bool storageTextureArgumentBindingSupported = false;
    bool samplerArgumentBindingSupported = false;
    bool nonAppleNativeDescriptorRuntimeDiagnosticOnly = false;
    bool nativeShaderModuleCreationPathPresent = false;
    bool nativeRenderPipelineCreationPathPresent = false;
    bool nativeComputePipelineCreationPathPresent = false;
    bool nonAppleNativePipelineRuntimeDiagnosticOnly = false;
    bool nativeCommandBufferCreationPathPresent = false;
    bool nativeRenderEncoderPathPresent = false;
    bool nativeComputeEncoderPathPresent = false;
    bool nativeBlitEncoderPathPresent = false;
    bool nativeDescriptorArgumentBindingRecordPathPresent = false;
    bool nativePushConstantUploadBufferPathPresent = false;
    bool nativePushConstantSetBytesPathPresent = false;
    bool nativeQueueSubmitPathPresent = false;
    bool boundedCompletionTelemetryPresent = false;
    bool nonAppleNativeCommandRuntimeDiagnosticOnly = false;
    bool nativeDrawableLayerCreationPathPresent = false;
    bool nativeDrawableAcquirePathPresent = false;
    bool nativeDrawablePresentPathPresent = false;
    bool nativeDrawableResizePathPresent = false;
    bool nativeDrawableLayerLossRecreatePathPresent = false;
    bool nativeDrawableBackgroundForegroundPathPresent = false;
    bool nativeDrawableMemoryWarningPathPresent = false;
    bool nativeDrawableTwoFrameSlotsPresent = false;
    bool nonAppleNativeSwapchainRuntimeDiagnosticOnly = false;
    bool windowsMingwNativeRuntimeDiagnosticOnly = false;
    bool windowsMingwNativeExecutionSuppressed = false;
    bool windowsMingwSourceGuardrailsEnforced = false;
    bool windowsMingwNoNullDelegateGuardrailEnforced = false;
    bool nativeHandleExposedToUpperLayer = false;
    bool perDrawPipelineCreationAllowed = false;
    bool perFrameShaderCompilationAllowed = false;
};

enum class MetalRhiDrawableLifecycleEvent {
    Resize,
    LayerLost,
    LayerRestored,
    BackgroundPause,
    ForegroundResume,
    MemoryWarning
};

struct MetalRhiDrawableLifecycleReport {
    bool success = false;
    std::pmr::string diagnostics;
    rhi::Backend backend = rhi::Backend::Metal;
    bool resizeHandled = false;
    bool layerLossHandled = false;
    bool layerRecreateHandled = false;
    bool backgroundPauseHandled = false;
    bool foregroundResumeHandled = false;
    bool memoryWarningHandled = false;
    bool acquireAllowed = false;
    bool presentAllowed = false;
    bool swapchainRebuildRequired = false;
    bool releaseDrawableResources = false;
    bool nativeHandleExposedToUpperLayer = false;
};

MetalRhiDeviceReport describeMetalRhiDevice(const MetalRhiDeviceDesc& desc);

MetalRhiDrawableLifecycleReport describeMetalRhiDrawableLifecycle(
    const MetalRhiDeviceDesc& desc, MetalRhiDrawableLifecycleEvent event);

arp::core::UniquePtr<rhi::GpuDevice> createMetalRhiDevice(const MetalRhiDeviceDesc& desc);

// HostDiagnosticDevice/1: explicit host contract fixture, not native execution.
// Rejected on native-capable hosts; no default version or implicit opt-in.
arp::core::UniquePtr<rhi::GpuDevice> createMetalRhiHostDiagnosticDevice(
    const MetalRhiDeviceDesc& desc, std::uint32_t contractVersion);

MetalRhiNativeSwapchainStats inspectMetalRhiNativeSwapchainStats(const rhi::GpuDevice& device);

} // namespace arp::rhi_metal
