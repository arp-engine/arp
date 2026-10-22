#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_commands.h"
#include "rhi/backends/webgpu/webgpu_native_runtime.h"
#include "rhi/gpu_device.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <cstdint>
#include <memory>
#include <string>

namespace arp::rhi_webgpu {
class WebGpuTransferResourceObservation;

// Cold original geometry initialization reuses mapped-at-creation/unmap;
// no queue write, CopyDst usage, asynchronous upload or completion is inferred.
core::UniquePtr<rhi::GpuBuffer> createWebGpuRhiOriginalGeometryBuffer(
    rhi::GpuDevice&, const rhi::BufferDesc&, const WebGpuNativeBufferInitializer&, const char*&);
const char* captureWebGpuRhiOriginalGeometryBuffer(
    rhi::GpuDevice&, const rhi::GpuBuffer&, WebGpuTransferResourceObservation&) noexcept;

struct WebGpuPushConstantBudget {
    std::uint32_t declaredWorkloadByteBudget = 0;
    std::uint32_t ringByteCapacity = 0;
    std::uint32_t uniformOffsetAlignment = 0;
};

struct WebGpuRhiDeviceDesc {
    std::pmr::string label;
    rhi::WebGpuRuntimeProfile runtimeProfile;
    rhi::semantics::NativeBackendCapabilityContract capabilityContract;
    std::uint64_t memoryBudgetBytes = 0;
    bool enableBackendDescriptorGroups = true;
    bool enablePushConstantUploadRing = true;
    // Declared by the frame workload owner. The backend rounds this budget to
    // the queried uniform-buffer alignment and never invents a production
    // ring size from a backend default.
    std::uint32_t pushConstantDeclaredWorkloadByteBudget = 0;
    // Kept as an explicit compatibility input for existing contract fixtures;
    // production platform code must use pushConstantDeclaredWorkloadByteBudget.
    std::uint32_t pushConstantUploadRingByteCapacity = 0;
    // Transfer staging has a separate budget from push-constant uploads.
    std::uint64_t uploadStagingByteCapacity = 0;
    bool enableCanvasLifecycleDiagnostics = true;
    bool enableResizeDiagnostics = true;
    bool enableSurfaceLostDiagnostics = true;
    bool enableHiddenTabFrameSkipping = true;
    bool requireComputeStorageRuntimePromotion = false;
    bool allowNativeHandleExposureToUpperLayers = false;
    bool allowBlockingSurfaceAcquire = false;
    bool allowPerDrawPipelineCreation = false;
    bool allowPerFrameShaderCompilation = false;
};

struct WebGpuRhiDeviceReport {
    bool success = false;
    std::pmr::string diagnostics;
    rhi::Backend backend = rhi::Backend::WebGPU;
    bool runtimeCapabilityPromotedFromProbe = false;
    bool computePromotionValidated = false;
    bool storageTexturePromotionValidated = false;
    bool storageBufferPromotionValidated = false;
    bool sceneGeometryRayQueryPromotionValidated = false;
    bool backendDescriptorGroupsIntegrated = false;
    bool backendDescriptorVocabularyHiddenFromPublicApi = true;
    bool pushConstantUploadRingEmulated = false;
    std::uint32_t pushConstantDeclaredWorkloadByteBudget = 0;
    std::uint32_t pushConstantUploadRingByteCapacity = 0;
    std::uint32_t pushConstantUploadRingAlignmentBytes = 0;
    bool canvasLifecycleDiagnosticsSupported = false;
    bool resizeDiagnosticsSupported = false;
    bool surfaceLostDiagnosticsSupported = false;
    bool hiddenTabFrameSkippingSupported = false;
    bool memoryBudgetTelemetrySanitized = false;
    bool boundedSurfaceSynchronization = false;
    bool queueSubmitSupported = false;
    bool swapchainCanvasLifecycleSupported = false;
    bool nullDelegateUsed = false;
    bool backendOwnedResourceObjects = false;
    bool backendOwnedDescriptorObjects = false;
    bool backendOwnedPipelineObjects = false;
    bool backendOwnedCommandObjects = false;
    bool backendOwnedQueueSubmit = false;
    bool backendOwnedSwapchain = false;
    bool backendOwnedUploadStagingSupported = false;
    bool uploadStagingNativeHandlesHidden = true;
    bool uploadStagingBounded = false;
    std::uint64_t uploadStagingByteCapacity = 0;
    bool publicDeviceRuntimeBackingRequired = false;
    bool publicDeviceNativeDeviceAvailable = false;
    bool publicDeviceWgpuBackedObjects = false;
    bool publicDeviceSemanticOnlyFallback = true;
    bool publicDeviceDiagnosticOnlyFallback = false;
    bool nonEmscriptenDiagnosticOnly = false;
    bool nonEmscriptenNativeClaimRejected = false;
    bool nativeDeviceAvailable = false;
    bool nativeBufferCreated = false;
    bool nativeTextureCreated = false;
    bool nativeTextureViewCreated = false;
    bool nativeSamplerCreated = false;
    bool nativeDescriptorGroupCreated = false;
    bool nativeShaderModuleCreated = false;
    bool nativePipelineCreated = false;
    bool nativeCommandEncoderCreated = false;
    bool nativeCommandBufferCreated = false;
    bool queueSubmitted = false;
    bool canvasAcquired = false;
    bool canvasPresentedByBrowser = false;
    bool resizeHandled = false;
    bool surfaceLossHandled = false;
    bool hiddenTabHandled = false;
    bool perFramePipelineRebuild = false;
    bool nativeHandleExposedToUpperLayer = false;
    bool perDrawPipelineCreationAllowed = false;
    bool perFrameShaderCompilationAllowed = false;
};

enum class WebGpuRhiCanvasEvent { Resize, SurfaceLost, HiddenTab, VisibleTab };

struct WebGpuRhiCanvasLifecycleReport {
    bool success = false;
    std::pmr::string diagnostics;
    rhi::Backend backend = rhi::Backend::WebGPU;
    bool resizeDiagnosed = false;
    bool surfaceLostDiagnosed = false;
    bool hiddenTabFrameSkipped = false;
    bool acquireAllowed = false;
    bool presentAllowed = false;
    bool swapchainRebuildRequired = false;
    bool releaseSurfaceResources = false;
    bool nativeHandleExposedToUpperLayer = false;
};

WebGpuRhiDeviceReport describeWebGpuRhiDevice(const WebGpuRhiDeviceDesc& desc);

WebGpuRhiCanvasLifecycleReport describeWebGpuRhiCanvasLifecycle(
    const WebGpuRhiDeviceDesc& desc, WebGpuRhiCanvasEvent event);

arp::core::UniquePtr<rhi::GpuDevice> createWebGpuRhiDevice(const WebGpuRhiDeviceDesc& desc);

// HostDiagnosticDevice/1: explicit host contract fixture, not native execution.
// Rejected when a native device exists; no default version or implicit opt-in.
arp::core::UniquePtr<rhi::GpuDevice> createWebGpuRhiHostDiagnosticDevice(
    const WebGpuRhiDeviceDesc& desc, std::uint32_t contractVersion);

WebGpuNativeCommandStats inspectWebGpuRhiNativeCommandStats(const rhi::GpuDevice& device);

WebGpuNativeSwapchainStats inspectWebGpuRhiNativeSwapchainStats(const rhi::GpuDevice& device);

} // namespace arp::rhi_webgpu
