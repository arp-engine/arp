// Emscripten WebGPU RenderSystem backend: device creation, browser runtime
// probing, surface naming and native telemetry. The shared L2 frame-graph
// machinery lives in render_system_native_shared.cpp; the Metal (Apple)
// counterpart is render_system_metal.cpp.
#include "platform/render_system_native_shared.h"
#include "rhi/backends/webgpu/webgpu_device.h"
#include "rhi/semantics/webgpu_runtime_capability_bridge.h"
#include "rhi/semantics/webgpu_runtime_profile.h"

#include <cstdint>
#include <emscripten/emscripten.h>
#include <memory>
#include <string>
#include <utility>

namespace arp::platform {

namespace {

rhi::WebGpuRuntimeProfile makeBrowserRuntimeProfile()
{
    rhi::WebGpuRuntimeProbe probe;
    probe.browserRuntime = true;
    probe.nativeDevice = true; // emdawnwebgpu provides a native WebGPU device in web build
    probe.canvasSurface = true;
    probe.computeDispatch = rhi_webgpu::webGpuNativeDeviceSupportsComputeDispatch();
    probe.storageTextureBinding = rhi_webgpu::webGpuNativeDeviceSupportsStorageTextureBinding();
    probe.storageBuffer = rhi_webgpu::webGpuNativeDeviceSupportsStorageBuffer();
    probe.sceneGeometryRayQuery = rhi_webgpu::webGpuNativeDeviceSupportsSceneGeometryRayQuery();
    probe.timestampQuery = rhi_webgpu::webGpuNativeTimestampQuerySupported();
    // emdawnwebgpu does not expose a dedicated feature enum for the float
    // storage formats. Probe the actual device resource contract instead of
    // promoting SSR/DDGI from a compile-time assumption.
    // BGRA8Unorm is a swapchain format here, not a storage-texture format.
    // Do not issue an intentionally invalid createTexture call just to prove
    // a known-negative result; that would pollute the uncaptured-error stream.
    probe.bgra8UnormStorage = false;
    probe.rgba16FloatStorage =
        rhi_webgpu::webGpuNativeDeviceSupportsStorageTexture(rhi::TextureFormat::Rgba16Float);
    // Rg16Float is not guaranteed by WebGPU. The browser bootstrap performs
    // an async error-scoped storage-usage probe before RHI construction and
    // the backend exposes only that capability result here.
    probe.rg16FloatStorage =
        rhi_webgpu::webGpuNativeDeviceSupportsStorageTexture(rhi::TextureFormat::Rg16Float);
    probe.textureCompressionBc = rhi_webgpu::webGpuNativeDeviceSupportsTextureCompressionBc();
    // Texture creation does not validate blend state. Keep this separate
    // capability fail-closed until a render-pipeline probe exists.
    probe.rgba16FloatBlendable = false;
    const auto limits = rhi_webgpu::webGpuNativeDeviceLimits();
    probe.maxTexture2DSize = limits.maxTexture2DSize;
    probe.maxColorAttachments = limits.maxColorAttachments;
    probe.maxDescriptorBindings = limits.maxDescriptorBindings;
    probe.maxSampledTexturesPerShaderStage = limits.maxSampledTexturesPerShaderStage;
    probe.maxSamplersPerShaderStage = limits.maxSamplersPerShaderStage;
    probe.maxUniformBuffersPerShaderStage = limits.maxUniformBuffersPerShaderStage;
    probe.maxStorageTexturesPerShaderStage = limits.maxStorageTexturesPerShaderStage;
    probe.maxBindGroups = limits.maxBindGroups;
    probe.uniformBufferOffsetAlignment = limits.uniformBufferOffsetAlignment;
    probe.storageBufferOffsetAlignment = limits.storageBufferOffsetAlignment;
    probe.maxUniformBufferBindingSize = limits.maxUniformBufferBindingSize;
    probe.maxStorageBufferBindingSize = limits.maxStorageBufferBindingSize;
    probe.maxComputeInvocationsPerWorkgroup = limits.maxComputeInvocationsPerWorkgroup;
    probe.maxComputeWorkgroupSizeX = limits.maxComputeWorkgroupSizeX;
    probe.maxComputeWorkgroupSizeY = limits.maxComputeWorkgroupSizeY;
    probe.maxComputeWorkgroupStorageSize = limits.maxComputeWorkgroupStorageSize;
    probe.maxStorageBuffersPerShaderStage = limits.maxStorageBuffersPerShaderStage;
    EM_ASM(
        {
            // clang-format off
                document.body.dataset.woccWebGpuCapabilityProbe =
                    "compute=" + $0 + ",storageTexture=" + $1 + ",storageBuffer=" + $2 + ",sampledDepth=" + $3 +
                    ",workgroupInvocations=" + $4 + ",storageBindings=" + $5;
            // clang-format on
        },
        probe.computeDispatch ? 1 : 0,
        probe.storageTextureBinding ? 1 : 0,
        probe.storageBuffer ? 1 : 0,
        probe.canvasSurface && probe.nativeDevice ? 1 : 0,
        probe.maxComputeInvocationsPerWorkgroup,
        probe.maxStorageBuffersPerShaderStage);
    return rhi::buildWebGpuRuntimeProfile(probe);
}

arp::core::UniquePtr<rhi::GpuDevice> createRhiWebGpuDevice(
    const std::pmr::string& applicationName, std::pmr::string& diagnostics)
{
    const auto profile = makeBrowserRuntimeProfile();
    const auto capability = rhi::semantics::buildWebGpuRuntimeCapabilityContract(profile);
    if (!capability.success) {
        diagnostics = "WebGPU RHI capability contract failed: " + capability.diagnostics;
        return nullptr;
    }

    rhi_webgpu::WebGpuRhiDeviceDesc deviceDesc;
    deviceDesc.label = applicationName.empty() ? "RenderSystemRhiWebGpuDevice"
                                               : applicationName + "RhiWebGpuDevice";
    deviceDesc.runtimeProfile = profile;
    deviceDesc.capabilityContract = capability.contract;
    // Browser WebGPU does not expose a portable device-local heap budget. Use
    // a finite 1.5 GiB production baseline: the checked environment library,
    // meshes, and the full post/SSR frame graph reach just over 1 GiB while
    // one submitted frame is still in flight. This remains allocator-enforced
    // rather than a native-memory assumption, and unsupported allocations
    // still fail with diagnostics.
    constexpr std::uint64_t kWebGpuProductionMemoryBudgetBytes = 1536u * 1024u * 1024u;
    deviceDesc.memoryBudgetBytes = kWebGpuProductionMemoryBudgetBytes;
    deviceDesc.enableBackendDescriptorGroups = true;
    deviceDesc.enablePushConstantUploadRing = true;
    // This is a workload declaration, not a backend default.  The WoCC frame
    // contract bounds the number of atomic full-block updates per command
    // encoder; the WebGPU backend rounds the resulting byte budget using the
    // queried uniform-buffer alignment before allocating its ring.
    constexpr std::uint32_t kWoccMaxPushConstantUpdatesPerCommandEncoder = 4096u;
    deviceDesc.pushConstantDeclaredWorkloadByteBudget =
        kWoccMaxPushConstantUpdatesPerCommandEncoder * rhi::kL2AtomicPushConstantLimitBytes;
    // Transfer staging is separate: terrain and material uploads can contain
    // several MiB-sized mip levels before their command serials retire.
    deviceDesc.uploadStagingByteCapacity = 256u * 1024u * 1024u;
    deviceDesc.enableCanvasLifecycleDiagnostics = true;
    deviceDesc.enableResizeDiagnostics = true;
    deviceDesc.enableSurfaceLostDiagnostics = true;
    deviceDesc.enableHiddenTabFrameSkipping = true;
    deviceDesc.requireComputeStorageRuntimePromotion = false;
    deviceDesc.allowNativeHandleExposureToUpperLayers = false;
    deviceDesc.allowBlockingSurfaceAcquire = false;
    deviceDesc.allowPerDrawPipelineCreation = false;
    deviceDesc.allowPerFrameShaderCompilation = false;

    const auto report = rhi_webgpu::describeWebGpuRhiDevice(deviceDesc);
    if (!report.success) {
        diagnostics = "WebGPU RHI device contract failed: " + report.diagnostics;
        return nullptr;
    }

    auto device = rhi_webgpu::createWebGpuRhiDevice(deviceDesc);
    if (!device) {
        diagnostics = "WebGPU RHI device creation failed";
        return nullptr;
    }

#if ARP_WEBGPU_VALIDATION_ENABLED
    rhi_webgpu::setupWebGpuUncapturedErrorCallback();
#endif

    return device;
}

} // namespace

void applyNativeSceneRuntimeFrameStats(RenderSystemSceneRuntimeCustomGraphFrameReport& target,
    const rhi::GpuDevice& device,
    const rhi::Extent2D extent,
    const rhi::TextureFormat format)
{
    const auto commandStats = rhi_webgpu::inspectWebGpuRhiNativeCommandStats(device);
    const auto swapchainStats = rhi_webgpu::inspectWebGpuRhiNativeSwapchainStats(device);
    target.rhiQueueSubmitted = commandStats.nativeQueueSubmitCount > 0u;
    target.renderPassCount = commandStats.nativeRenderPassBeginRecordCount;
    target.drawCommandCount =
        commandStats.nativeIndexedDrawRecordCount + commandStats.nativeDrawRecordCount;
    target.framesSubmitted = swapchainStats.browserManagedPresentCount;
    // Native submit/present counters do not observe framebuffer pixels.
    target.hiddenWindowRenderOutputHashCaptured = false;
    target.deterministicRenderOutputHash = 0;
    target.nativeHandleExposedToUpperLayer = target.nativeHandleExposedToUpperLayer
        || commandStats.nativeHandleExposedToUpperLayer
        || swapchainStats.nativeHandleExposedToUpperLayer;
}

void applyNativeProductionFrameSubmitTelemetry(RenderSystemRhiProductionFrameSubmitReport& report,
    const rhi::GpuDevice& device,
    const rhi::Extent2D extent,
    const rhi::TextureFormat format)
{
    const auto commandStats = rhi_webgpu::inspectWebGpuRhiNativeCommandStats(device);
    const auto swapchainStats = rhi_webgpu::inspectWebGpuRhiNativeSwapchainStats(device);
    // Native submit/present counters do not observe framebuffer pixels.
    report.hiddenWindowRenderOutputHashCaptured = false;
    report.deterministicRenderOutputHash = 0;
    report.nativeHandleExposedToUpperLayer = report.nativeHandleExposedToUpperLayer
        || commandStats.nativeHandleExposedToUpperLayer
        || swapchainStats.nativeHandleExposedToUpperLayer;
}

RenderSystem createDefaultRenderSystem(WindowSystem&,
    const PlatformWindow& window,
    const std::pmr::string& applicationName,
    const rhi::PresentationMode presentationMode,
    const rhi::semantics::UploadStagingBudgetV1*,
    const rhi::semantics::DirectObjectBudget* directObjectBudgetOverride)
{
    auto impl = core::makeUnique<RenderSystem::Impl>();
    impl->backend = rhi::Backend::WebGPU;
    impl->extent = {
        static_cast<std::uint32_t>(window.pixelWidth()),
        static_cast<std::uint32_t>(window.pixelHeight()),
    };
    if (impl->extent.width == 0u || impl->extent.height == 0u) {
        impl->diagnostics = "native RHI RenderSystem requires a non-zero surface extent";
        return RenderSystem(std::move(impl));
    }

    if (directObjectBudgetOverride != nullptr) {
        impl->diagnostics = "RHI-DIRECT-OBJECT-BUDGET-WEBGPU-UNSUPPORTED";
        return RenderSystem(std::move(impl));
    }
    impl->device = createRhiWebGpuDevice(applicationName, impl->diagnostics);
    if (!impl->device) {
        return RenderSystem(std::move(impl));
    }
    impl->frameGraphResourcePool =
        arp::core::makeShared<render_graph::FrameGraphResourcePool>(*impl->device);
    impl->swapchainDesc = rhi::SwapchainDesc{
        "#canvas",
        impl->extent,
        impl->colorFormat,
        2u,
        presentationMode,
    };
    impl->swapchain = impl->device->createSwapchain(impl->swapchainDesc);
    if (!impl->swapchain) {
        impl->diagnostics = "native RHI swapchain creation failed";
        return RenderSystem(std::move(impl));
    }

    impl->surfaceBackedPresentation = true;
    impl->valid = true;
    return RenderSystem(std::move(impl));
}

RenderSystem createRenderSystem(const RenderSystemDesc& desc)
{
    const auto route =
        RenderSystemBackendSelection::forBuild(desc.backendPreference, rhi::Backend::WebGPU);
    if (!route.routeToFactory) {
        auto impl = core::makeUnique<RenderSystem::Impl>();
        impl->backend = route.requestedBackend;
        impl->diagnostics = route.diagnostic;
        return RenderSystem(std::move(impl));
    }
    if (desc.orderedGraphics) {
        auto impl = core::makeUnique<RenderSystem::Impl>();
        impl->diagnostics = "PLATFORM-ORDERED-PROFILE-BACKEND-UNAVAILABLE";
        return RenderSystem(std::move(impl));
    }
    if (!desc.surface.windowSystem || !desc.surface.window) {
        auto impl = core::makeUnique<RenderSystem::Impl>();
        impl->backend = rhi::Backend::WebGPU;
        impl->diagnostics = "RHI-PLATFORM-SURFACE-MISSING";
        return RenderSystem(std::move(impl));
    }
    return createDefaultRenderSystem(*desc.surface.windowSystem,
        *desc.surface.window,
        desc.applicationName,
        desc.presentationMode,
        nullptr,
        nullptr);
}

} // namespace arp::platform
