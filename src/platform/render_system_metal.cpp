// Apple Metal RenderSystem backend: device creation, surface naming and
// native telemetry. The shared L2 frame-graph machinery lives in
// render_system_native_shared.cpp; the WebGPU (Emscripten) counterpart is
// render_system_webgpu.cpp.
#include "platform/render_system_native_shared.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_swapchain.h"
#include "rhi/frontend/memory_upload_staging_budget.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace arp::platform {

namespace {

constexpr std::uint64_t kWoccObservedMaxSingleUploadBytes = 16u * 1024u * 1024u;
constexpr std::uint64_t kWoccUploadStagingPageSizeBytes = 16u * 1024u * 1024u;
constexpr std::uint32_t kWoccUploadStagingMaxPageCount = 16u;
constexpr rhi::semantics::UploadStagingBudgetV1 kWoccUploadStagingBudget{
    rhi::semantics::kUploadStagingBudgetVersion,
    kWoccObservedMaxSingleUploadBytes,
    kWoccUploadStagingPageSizeBytes,
    kWoccUploadStagingMaxPageCount,
    kWoccUploadStagingPageSizeBytes* kWoccUploadStagingMaxPageCount,
    256u,
};
static_assert(rhi::semantics::validateUploadStagingBudget(kWoccUploadStagingBudget).success,
    "WOCC Metal upload staging budget must satisfy the ARP L2 contract");

arp::core::UniquePtr<rhi::GpuDevice> createRhiMetalDevice(const std::pmr::string& applicationName,
    std::pmr::string& diagnostics,
    const rhi::semantics::UploadStagingBudgetV1* uploadStagingBudgetOverride,
    const rhi::semantics::DirectObjectBudget* directObjectBudgetOverride)
{
    auto capability = rhi::semantics::nativeBackendCapabilityContract(rhi::Backend::Metal);
    capability.queriedFromNativeDevice = true;
    capability.queriedExtendedFeatures = true;
    capability.maxPushConstantSizeBytes = rhi::kL2AtomicPushConstantLimitBytes;
    capability.depthBiasClampSupported = true;
    // Runtime features are intersected by the Metal device after probing the
    // active MTLDevice. Keeping the contract query flags explicit satisfies
    // the contract-first boundary without inventing platform defaults.
    rhi_metal::MetalRhiDeviceDesc deviceDesc;
    deviceDesc.label =
        applicationName.empty() ? "RenderSystemRhiMetalDevice" : applicationName + "RhiMetalDevice";
    deviceDesc.capabilityContract = std::move(capability);
    deviceDesc.memoryBudgetBytes = 1536ull * 1024ull * 1024ull;
    const auto& uploadStagingBudget = uploadStagingBudgetOverride != nullptr
        ? *uploadStagingBudgetOverride
        : kWoccUploadStagingBudget;
    // A scoped test may reduce in-flight capacity, but cannot raise the
    // production request/page contract or alter its alignment semantics.
    if (uploadStagingBudget.maxRequestBytes != kWoccUploadStagingBudget.maxRequestBytes
        || uploadStagingBudget.pageSizeBytes != kWoccUploadStagingBudget.pageSizeBytes
        || uploadStagingBudget.alignmentBytes != kWoccUploadStagingBudget.alignmentBytes
        || uploadStagingBudget.maxPageCount > kWoccUploadStagingBudget.maxPageCount) {
        diagnostics = "RHI-UPLOAD-STAGING-BUDGET-OVERRIDE-OUT-OF-SCOPE";
        return nullptr;
    }
    const auto budgetValidation = rhi::semantics::validateUploadStagingBudget(uploadStagingBudget);
    if (!budgetValidation.success) {
        diagnostics = "Metal RHI upload staging budget contract failed: ";
        diagnostics += budgetValidation.diagnostics;
        return nullptr;
    }
    deviceDesc.uploadStagingPageSizeBytes = uploadStagingBudget.pageSizeBytes;
    deviceDesc.uploadStagingMaxPageCount = uploadStagingBudget.maxPageCount;
    if (directObjectBudgetOverride != nullptr) {
        if (!directObjectBudgetOverride->valid()) {
            diagnostics = "RHI-DIRECT-OBJECT-BUDGET-INVALID";
            return nullptr;
        }
        deviceDesc.directObjectBudget = *directObjectBudgetOverride;
    }
    deviceDesc.enableArgumentBuffers = true;
    deviceDesc.enablePushConstantSetBytes = true;
    deviceDesc.enableMemorylessTransientTextures = true;
    deviceDesc.enableDrawableLifecycleGuards = true;
    deviceDesc.allowNativeHandleExposureToUpperLayers = false;
    deviceDesc.allowBlockingDrawableAcquire = false;
    deviceDesc.allowPerDrawPipelineCreation = false;
    deviceDesc.allowPerFrameShaderCompilation = false;

    const auto report = rhi_metal::describeMetalRhiDevice(deviceDesc);
    if (!report.success) {
        diagnostics = "Metal RHI device contract failed: " + report.diagnostics;
        return nullptr;
    }
    auto device = rhi_metal::createMetalRhiDevice(deviceDesc);
    if (!device) {
        diagnostics = "Metal RHI device creation failed";
    }
    return device;
}

} // namespace

void applyNativeSceneRuntimeFrameStats(RenderSystemSceneRuntimeCustomGraphFrameReport& target,
    const rhi::GpuDevice& device,
    const rhi::Extent2D extent,
    const rhi::TextureFormat format)
{
    const auto swapchainStats = rhi_metal::inspectMetalRhiNativeSwapchainStats(device);
    // Metal exposes command telemetry per command buffer. The generic
    // RenderSystem keeps this aggregate conservative and derives presentation
    // evidence from the backend-owned swapchain counters.
    target.rhiQueueSubmitted = swapchainStats.nativeDrawablePresentCount > 0u;
    target.framesSubmitted = swapchainStats.nativeDrawablePresentCount;
    // Native submit/present counters do not observe framebuffer pixels.
    target.hiddenWindowRenderOutputHashCaptured = false;
    target.deterministicRenderOutputHash = 0;
    target.nativeHandleExposedToUpperLayer =
        target.nativeHandleExposedToUpperLayer || swapchainStats.nativeHandleExposedToUpperLayer;
}

void applyNativeProductionFrameSubmitTelemetry(RenderSystemRhiProductionFrameSubmitReport& report,
    const rhi::GpuDevice& device,
    const rhi::Extent2D extent,
    const rhi::TextureFormat format)
{
    const auto swapchainStats = rhi_metal::inspectMetalRhiNativeSwapchainStats(device);
    // Native submit/present counters do not observe framebuffer pixels.
    report.hiddenWindowRenderOutputHashCaptured = false;
    report.deterministicRenderOutputHash = 0;
    report.nativeHandleExposedToUpperLayer =
        report.nativeHandleExposedToUpperLayer || swapchainStats.nativeHandleExposedToUpperLayer;
}

RenderSystem createDefaultRenderSystem(WindowSystem&,
    const PlatformWindow& window,
    const std::pmr::string& applicationName,
    const rhi::PresentationMode presentationMode,
    const rhi::semantics::UploadStagingBudgetV1* uploadStagingBudgetOverride,
    const rhi::semantics::DirectObjectBudget* directObjectBudgetOverride)
{
    auto impl = core::makeUnique<RenderSystem::Impl>();
    impl->backend = rhi::Backend::Metal;
    impl->extent = {
        static_cast<std::uint32_t>(window.pixelWidth()),
        static_cast<std::uint32_t>(window.pixelHeight()),
    };
    if (impl->extent.width == 0u || impl->extent.height == 0u) {
        impl->diagnostics = "native RHI RenderSystem requires a non-zero surface extent";
        return RenderSystem(std::move(impl));
    }

    impl->device = createRhiMetalDevice(applicationName,
        impl->diagnostics,
        uploadStagingBudgetOverride,
        directObjectBudgetOverride);
    if (!impl->device) {
        return RenderSystem(std::move(impl));
    }
    impl->frameGraphResourcePool =
        arp::core::makeShared<render_graph::FrameGraphResourcePool>(*impl->device);
    impl->swapchainDesc = rhi::SwapchainDesc{
        applicationName.empty() ? "RenderSystemRhiMetalSurface" : applicationName,
        impl->extent,
        impl->colorFormat,
        2u,
        presentationMode,
    };
    if (!window.surfaceToken()) {
        impl->diagnostics = "RHI-METAL-SURFACE-TOKEN-MISSING";
        return RenderSystem(std::move(impl));
    }
    impl->swapchainDesc.platformSurface = window.surfaceToken();
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
        RenderSystemBackendSelection::forBuild(desc.backendPreference, rhi::Backend::Metal);
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
        impl->backend = rhi::Backend::Metal;
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
