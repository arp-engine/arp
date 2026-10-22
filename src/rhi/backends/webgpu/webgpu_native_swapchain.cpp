#include "core/memory/cpu_allocator.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/backends/webgpu/webgpu_native_string_view.h"

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include <emscripten/emscripten.h>
#endif

#include <array>

namespace arp::rhi_webgpu {

namespace {

bool validExtent(const rhi::Extent2D& extent)
{
    return extent.width > 0 && extent.height > 0;
}

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
bool browserWebGpuEnvironmentAvailable()
{
    return emscripten_run_script_int("typeof navigator !== 'undefined' && !!navigator.gpu"
                                     "&& typeof Module !== 'undefined'"
                                     "&& !!Module.preinitializedWebGPUDevice"
                                     "&& !Module.webGpuDeviceLostInfo")
        != 0;
}

bool browserCanvasKeepAliveRequested()
{
    return emscripten_run_script_int("typeof window !== 'undefined'"
                                     "&& window.__arpRenderFeatureLabKeepCanvasAlive === true")
        != 0;
}

void releaseTexture(WGPUTexture& texture)
{
    if (texture) {
        wgpuTextureRelease(texture);
        texture = nullptr;
    }
}

void releaseSurface(WGPUSurface& surface)
{
    if (surface) {
        if (!webGpuNativeDeviceLost())
            wgpuSurfaceRelease(surface);
        surface = nullptr;
    }
}

void releaseInstance(WGPUInstance& instance)
{
    if (instance) {
        if (!webGpuNativeDeviceLost())
            wgpuInstanceRelease(instance);
        instance = nullptr;
    }
}

void releaseDevice(WGPUDevice& device)
{
    if (device) {
        if (!webGpuNativeDeviceLost())
            wgpuDeviceRelease(device);
        device = nullptr;
    }
}

void releaseTexturesBySlot(std::array<WGPUTexture, 2>& textures)
{
    for (auto& texture : textures) {
        releaseTexture(texture);
    }
}
#endif

} // namespace

struct WebGpuNativeSwapchainPayload::Impl {
    rhi::SwapchainDesc desc;
    std::shared_ptr<WebGpuNativeSwapchainStats> stats;
    bool surfaceAvailable = true;
    bool hidden = false;

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    WGPUDevice device = nullptr;
    WGPUInstance instance = nullptr;
    WGPUSurface surface = nullptr;
    std::array<WGPUTexture, 2> currentTexturesBySlot{};
    std::array<std::uint32_t, 2> acquiredTurns{};
    std::array<std::weak_ptr<WebGpuNativeTexturePayload>, 2> acquiredPayloads;
    // Bounded exact JS GPUTexture identity, not a caller acquisition counter.
    WGPUTexture lastIssuedCanvas = nullptr;
    bool surfaceConfigured = false;
#endif
};

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
void invalidateAcquiredImages(WebGpuNativeSwapchainPayload::Impl& impl) noexcept
{
    for (auto& weak : impl.acquiredPayloads)
        if (auto image = weak.lock())
            image->acquiredImageValid = false;
    impl.acquiredTurns.fill(0);
}

bool configureSurface(WebGpuNativeSwapchainPayload::Impl& impl, std::pmr::string& diagnostics)
{
    if (!impl.device || !impl.surface || !validExtent(impl.desc.extent)) {
        diagnostics = "WebGPU RHI native canvas configure requires device, surface, and extent";
        return false;
    }
    const auto format = nativeTextureFormat(impl.desc.format);
    if (format == WGPUTextureFormat_Undefined) {
        diagnostics = "WebGPU RHI native canvas format is unsupported";
        return false;
    }
    auto config = WGPU_SURFACE_CONFIGURATION_INIT;
    config.device = impl.device;
    config.format = format;
    config.usage =
        static_cast<WGPUTextureUsage>(WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);
    config.width = impl.desc.extent.width;
    config.height = impl.desc.extent.height;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(impl.surface, &config);
    impl.surfaceConfigured = true;
    if (impl.stats) {
        ++impl.stats->nativeSurfaceConfigureCount;
    }
    return true;
}
#endif

WebGpuNativeSwapchainPayload::WebGpuNativeSwapchainPayload(core::UniquePtr<Impl> impl)
    : impl_(std::move(impl))
{}

WebGpuNativeSwapchainPayload::~WebGpuNativeSwapchainPayload()
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        return;
    }
    invalidateAcquiredImages(*impl_);
    // The Render Feature Lab captures the canvas after main() returns. The
    // browser owns presentation of the configured surface, so retain its
    // configuration and native references until page teardown when that
    // fixture explicitly requests visual capture. Normal applications keep
    // the ordinary deterministic unconfigure/release lifecycle.
    if (browserCanvasKeepAliveRequested()) {
        return;
    }
    releaseTexturesBySlot(impl_->currentTexturesBySlot);
    releaseTexture(impl_->lastIssuedCanvas);
    if (impl_->surface && impl_->surfaceConfigured) {
        wgpuSurfaceUnconfigure(impl_->surface);
        impl_->surfaceConfigured = false;
    }
    releaseSurface(impl_->surface);
    releaseInstance(impl_->instance);
    releaseDevice(impl_->device);
#endif
}

bool WebGpuNativeSwapchainPayload::acquireCanvas(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || frameSlot >= impl_->currentTexturesBySlot.size()) {
        diagnostics = "WebGPU RHI native canvas acquire frame slot is invalid";
        return false;
    }
    if (!impl_ || !impl_->surfaceConfigured || !impl_->surfaceAvailable || impl_->hidden) {
        return hiddenTabFrameSkip(diagnostics);
    }
    auto& currentTexture = impl_->currentTexturesBySlot[frameSlot];
    if (auto previous = impl_->acquiredPayloads[frameSlot].lock())
        previous->acquiredImageValid = false;
    impl_->acquiredTurns[frameSlot] = webGpuNativeAcquireExecutionTurn();
    releaseTexture(currentTexture);
    auto surfaceTexture = WGPU_SURFACE_TEXTURE_INIT;
    wgpuSurfaceGetCurrentTexture(impl_->surface, &surfaceTexture);
    currentTexture = surfaceTexture.texture;
    const auto acquired = currentTexture != nullptr
        && (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal
            || surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal);
    if (!acquired) {
        diagnostics = "WebGPU RHI native canvas acquire failed";
        releaseTexture(currentTexture);
        return false;
    }
    if (impl_->lastIssuedCanvas
        && EM_ASM_INT(
            {
                // clang-format off
        return WebGPU.getJsObject($0) === WebGPU.getJsObject($1);
                // clang-format on
            },
            currentTexture,
            impl_->lastIssuedCanvas)) {
        diagnostics = "RHI-WEBGPU-CANVAS-ACQUISITION-REUSED";
        releaseTexture(currentTexture);
        return false;
    }
    if (impl_->stats) {
        ++impl_->stats->nativeCanvasAcquireCount;
    }
    return true;
#else
    (void)frameSlot;
    diagnostics = "WebGPU RHI native swapchain requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeSwapchainPayload::browserManagedPresent(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || frameSlot >= impl_->currentTexturesBySlot.size()) {
        diagnostics = "WebGPU RHI native browser-managed present frame slot is invalid";
        return false;
    }
    auto& currentTexture = impl_->currentTexturesBySlot[frameSlot];
    if (!impl_->surfaceAvailable || impl_->hidden) {
        diagnostics = "WebGPU RHI native browser-managed present requires an acquired frame";
        return false;
    }
    // Texture may have been transferred to acquiredImage via takeCanvasTexture() —
    // that is the expected path when the FrameGraph renders to the canvas texture.
    if (currentTexture) {
        releaseTexture(currentTexture);
    }
    if (impl_->stats) {
        ++impl_->stats->browserManagedPresentCount;
    }
    return true;
#else
    (void)frameSlot;
    diagnostics = "WebGPU RHI native swapchain requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeSwapchainPayload::discardCanvasFrame(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || frameSlot >= impl_->currentTexturesBySlot.size()) {
        diagnostics = "WebGPU RHI native canvas discard frame slot is invalid";
        return false;
    }
    // If acquiredImage() transferred ownership, the temporary GpuTexture owns
    // the payload and its destructor releases it. Otherwise release the
    // surface texture still held by the native swapchain payload here.
    if (auto image = impl_->acquiredPayloads[frameSlot].lock())
        image->acquiredImageValid = false;
    impl_->acquiredTurns[frameSlot] = 0;
    releaseTexture(impl_->currentTexturesBySlot[frameSlot]);
    return true;
#else
    (void)frameSlot;
    diagnostics = "WebGPU RHI native swapchain requires Emscripten WebGPU";
    return false;
#endif
}

std::shared_ptr<WebGpuNativeTexturePayload> WebGpuNativeSwapchainPayload::takeCanvasTexture(
    std::uint32_t frameSlot)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || frameSlot >= impl_->currentTexturesBySlot.size()) {
        return nullptr;
    }
    auto& currentTexture = impl_->currentTexturesBySlot[frameSlot];
    if (!currentTexture) {
        return nullptr;
    }
    if (impl_->lastIssuedCanvas
        && EM_ASM_INT(
            {
                // clang-format off
        return WebGPU.getJsObject($0) === WebGPU.getJsObject($1);
                // clang-format on
            },
            currentTexture,
            impl_->lastIssuedCanvas))
        return nullptr;
    const auto stateDesc = WebGpuCanvasTextureContract::describe(impl_->desc);
    releaseTexture(impl_->lastIssuedCanvas);
    wgpuTextureAddRef(currentTexture);
    impl_->lastIssuedCanvas = currentTexture;
    auto payload = arp::core::makeShared<WebGpuNativeTexturePayload>(stateDesc);
    payload->texture = currentTexture;
    payload->ownerDevice = impl_->device;
    payload->browserSurfaceTexture = true;
    payload->acquiredExecutionTurn = impl_->acquiredTurns[frameSlot];
    payload->acquiredImageValid = payload->acquiredExecutionTurn != 0;
    impl_->acquiredPayloads[frameSlot] = payload;
    payload->format = nativeTextureFormat(impl_->desc.format);
    payload->dimension = WGPUTextureDimension_2D;
    payload->rhiFormat = impl_->desc.format;
    payload->extent = {impl_->desc.extent.width, impl_->desc.extent.height, 1u};
    payload->mipLevelCount = 1u;
    payload->arrayLayerCount = 1u;
    payload->sampleCount = 1u;
    currentTexture = nullptr; // ownership transferred
    return payload;
#else
    (void)frameSlot;
    return nullptr;
#endif
}

bool WebGpuNativeSwapchainPayload::resizeCanvas(
    const rhi::Extent2D& extent, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_ || !validExtent(extent)) {
        diagnostics = "WebGPU RHI native canvas resize extent is invalid";
        return false;
    }
    const auto previousDesc = impl_->desc;
    invalidateAcquiredImages(*impl_);
    impl_->desc.extent = extent;
    releaseTexturesBySlot(impl_->currentTexturesBySlot);
    if (impl_->surfaceAvailable && !configureSurface(*impl_, diagnostics)) {
        impl_->desc = previousDesc;
        return false;
    }
    if (impl_->stats) {
        ++impl_->stats->nativeResizeCount;
    }
    return true;
#else
    (void)extent;
    diagnostics = "WebGPU RHI native swapchain requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeSwapchainPayload::surfaceLoss(bool available, std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!impl_) {
        diagnostics = "WebGPU RHI native surface lifecycle requires a payload";
        return false;
    }
    const bool previousSurfaceAvailable = impl_->surfaceAvailable;
    invalidateAcquiredImages(*impl_);
    impl_->surfaceAvailable = available;
    releaseTexturesBySlot(impl_->currentTexturesBySlot);
    if (!available) {
        if (impl_->surface && impl_->surfaceConfigured) {
            wgpuSurfaceUnconfigure(impl_->surface);
            impl_->surfaceConfigured = false;
        }
        if (impl_->stats) {
            ++impl_->stats->surfaceLossCount;
        }
        return true;
    }
    if (!configureSurface(*impl_, diagnostics)) {
        impl_->surfaceAvailable = previousSurfaceAvailable;
        return false;
    }
    if (impl_->stats) {
        ++impl_->stats->surfaceRecreateCount;
    }
    return true;
#else
    (void)available;
    diagnostics = "WebGPU RHI native swapchain requires Emscripten WebGPU";
    return false;
#endif
}

bool WebGpuNativeSwapchainPayload::hiddenTabFrameSkip(std::pmr::string& diagnostics)
{
    if (!impl_) {
        diagnostics = "WebGPU RHI native hidden-tab skip requires a payload";
        return false;
    }
    if (impl_->stats) {
        ++impl_->stats->hiddenTabFrameSkipCount;
    }
    diagnostics = "WebGPU RHI native canvas acquire skipped while hidden or unavailable";
    return false;
}

std::shared_ptr<WebGpuNativeSwapchainPayload> createWebGpuNativeSwapchainPayload(
    const rhi::SwapchainDesc& desc,
    std::shared_ptr<WebGpuNativeSwapchainStats> stats,
    std::pmr::string& diagnostics)
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    if (!browserWebGpuEnvironmentAvailable() || !validExtent(desc.extent)) {
        diagnostics = "WebGPU RHI native swapchain requires browser WebGPU and extent";
        return nullptr;
    }
    if (desc.surfaceName.empty() || desc.surfaceName[0] != '#') {
        diagnostics = "WebGPU RHI native swapchain requires an explicit canvas selector";
        return nullptr;
    }
    auto* device = emscripten_webgpu_get_device();
    if (!device) {
        diagnostics = "WebGPU RHI native swapchain requires an Emscripten WebGPU device";
        return nullptr;
    }
    auto instanceDesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    auto* instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        releaseDevice(device);
        diagnostics = "WebGPU RHI native swapchain instance creation failed";
        return nullptr;
    }

    auto canvasSource = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvasSource.selector = webGpuNativeStringView(desc.surfaceName);
    auto surfaceDesc = WGPU_SURFACE_DESCRIPTOR_INIT;
    surfaceDesc.label = webGpuNativeStringView(desc.surfaceName);
    surfaceDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvasSource);
    auto* surface = wgpuInstanceCreateSurface(instance, &surfaceDesc);
    if (!surface) {
        releaseInstance(instance);
        releaseDevice(device);
        diagnostics = "WebGPU RHI native swapchain surface creation failed";
        return nullptr;
    }

    auto impl = core::makeUnique<WebGpuNativeSwapchainPayload::Impl>();
    impl->desc = desc;
    impl->stats = std::move(stats);
    impl->device = device;
    impl->instance = instance;
    impl->surface = surface;
    if (impl->stats) {
        ++impl->stats->nativeSurfaceCreateCount;
        impl->stats->boundedFrameSlotCount = 2;
    }
    if (!configureSurface(*impl, diagnostics)) {
        releaseSurface(surface);
        releaseInstance(instance);
        releaseDevice(device);
        return nullptr;
    }
    return arp::core::makeShared<WebGpuNativeSwapchainPayload>(std::move(impl));
#else
    (void)desc;
    (void)stats;
    diagnostics = "WebGPU RHI native swapchain requires an Emscripten WebGPU build";
    return nullptr;
#endif
}

} // namespace arp::rhi_webgpu
