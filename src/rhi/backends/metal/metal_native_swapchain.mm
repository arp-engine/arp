#include "rhi/backends/metal/metal_native_swapchain.h"

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_native_commands.h"
#include "rhi/backends/metal/metal_native_format.h"
#include "rhi/backends/metal/metal_native_runtime.h"

#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <utility>

namespace arp::rhi_metal {

namespace {

bool validExtent(const rhi::Extent2D& extent)
{
    return extent.width > 0 && extent.height > 0;
}

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

std::shared_ptr<void> adoptNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    return std::shared_ptr<void>(reinterpret_cast<void*>(object), [](void* nativeObject) {
        if (nativeObject) {
            id object = reinterpret_cast<id>(nativeObject);
            [object release];
        }
    });
}

std::shared_ptr<void> retainBorrowedNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    [object retain];
    return adoptNativeObject(object);
}

// Swapchain-surface-valid pixel format mapping. Deliberately a subset of the
// full nativePixelFormat() in metal_native_resources.mm: only displayable
// color formats are accepted as CAMetalLayer surfaces. Depth / single-channel
// float formats (R32Float, D32Float, Depth24Stencil8) map to Invalid so the
// caller rejects them as swapchain surface formats. This is intentionally a
// separate function (not a call into the shared nativePixelFormat) because the
// shared mapping would return a valid MTLPixelFormat for R32Float, which is
// not a legal CAMetalLayer pixel format.
MTLPixelFormat swapchainSurfacePixelFormat(rhi::TextureFormat format)
{
    switch (format) {
    case rhi::TextureFormat::Bgra8Unorm:
        return MTLPixelFormatBGRA8Unorm;
    case rhi::TextureFormat::Rgba8Unorm:
        return MTLPixelFormatRGBA8Unorm;
    case rhi::TextureFormat::Rgba8UnormSrgb:
        return MTLPixelFormatRGBA8Unorm_sRGB;
    case rhi::TextureFormat::Rgba16Float:
        return MTLPixelFormatRGBA16Float;
    case rhi::TextureFormat::Unknown:
    case rhi::TextureFormat::R8Unorm:
    case rhi::TextureFormat::Rg16Float:
    case rhi::TextureFormat::R32Float:
    case rhi::TextureFormat::D32Float:
    case rhi::TextureFormat::Depth24Stencil8:
    case rhi::TextureFormat::Bc1Unorm:
    case rhi::TextureFormat::Bc3Unorm:
    case rhi::TextureFormat::Bc7Unorm:
    case rhi::TextureFormat::Bc7UnormSrgb:
        break;
    }
    return MTLPixelFormatInvalid;
}

void configureLayer(CAMetalLayer* layer, id<MTLDevice> device, const rhi::SwapchainDesc& desc)
{
    layer.device = device;
    layer.pixelFormat = swapchainSurfacePixelFormat(desc.format);
    layer.drawableSize = CGSizeMake(desc.extent.width, desc.extent.height);
    // The RHI swapchain image exposes CopySrc/CopyDst in addition to
    // RenderAttachment. A framebuffer-only drawable would make those
    // declared usages illegal on Metal.
    layer.framebufferOnly = NO;
    layer.opaque = YES;
    if (@available(macOS 10.13, iOS 11.0, *)) {
        layer.allowsNextDrawableTimeout = YES;
        layer.maximumDrawableCount = 2;
    }
}

} // namespace

MetalRhiNativeSwapchainPayload::MetalRhiNativeSwapchainPayload(rhi::SwapchainDesc desc,
    std::shared_ptr<MetalRhiNativeSwapchainStats> stats,
    std::shared_ptr<void> nativeLayer,
    std::shared_ptr<void> nativeQueue)
    : desc_(std::move(desc)), stats_(std::move(stats)), nativeLayer_(std::move(nativeLayer)),
      nativeQueue_(std::move(nativeQueue)), nativeDevice_(metalRhiNativeDeviceIdentity()),
      presentCompletion_(arp::core::makeShared<MetalRhiNativePresentCompletionState>())
{}

void MetalRhiNativeSwapchainPayload::clearNativeDrawables()
{
    for (auto& drawable : nativeDrawables_) {
        drawable.reset();
    }
}

bool MetalRhiNativeSwapchainPayload::ensureNativeLayer(std::pmr::string& diagnostics)
{
    if (nativeLayer_) {
        return true;
    }
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil || swapchainSurfacePixelFormat(desc_.format) == MTLPixelFormatInvalid
        || !validExtent(desc_.extent)) {
        diagnostics = "RHI-METAL-NATIVE-DRAWABLE-LAYER-RECREATE-INVALID";
        return false;
    }
    CAMetalLayer* layer = [[CAMetalLayer alloc] init];
    if (layer == nil) {
        diagnostics = "RHI-METAL-NATIVE-DRAWABLE-LAYER-RECREATE-FAILED";
        return false;
    }
    configureLayer(layer, device, desc_);
    nativeLayer_ = adoptNativeObject(layer);
    if (stats_) {
        ++stats_->nativeLayerCreateCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::acquireDrawable(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
    if (frameSlot >= nativeDrawables_.size()) {
        diagnostics = "Metal RHI native drawable acquire frame slot is invalid";
        return false;
    }
    if (!layerAvailable_) {
        diagnostics = "Metal RHI drawable acquire skipped while layer is unavailable";
        return false;
    }
    auto* layer = reinterpret_cast<CAMetalLayer*>(nativeLayer_.get());
    if (!layer) {
        diagnostics = "RHI-METAL-NATIVE-DRAWABLE-LAYER-MISSING";
        return false;
    }
    // The render thread is a long-lived std::thread, so it has no Cocoa run
    // loop to drain autoreleased objects returned by CAMetalLayer.
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil) {
            diagnostics = "Metal RHI native drawable acquire returned no drawable";
            return false;
        }
        nativeDrawables_[frameSlot] = retainBorrowedNativeObject(drawable);
        if (stats_) {
            ++stats_->nativeDrawableAcquireCount;
        }
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::presentDrawable(std::uint32_t frameSlot,
    std::shared_ptr<MetalRhiNativeFrameSlotCompletionState> frameCompletion,
    std::pmr::string& diagnostics)
{
    if (frameSlot >= nativeDrawables_.size()) {
        diagnostics = "Metal RHI native drawable present frame slot is invalid";
        return false;
    }
    auto drawable = reinterpret_cast<id<CAMetalDrawable>>(nativeDrawables_[frameSlot].get());
    auto queue = reinterpret_cast<id<MTLCommandQueue>>(nativeQueue_.get());
    if (!drawable || !queue || !frameCompletion) {
        diagnostics = "Metal RHI native drawable present requires an acquired drawable";
        return false;
    }
    const auto reservation = reserveSharedMetalRhiCommandCapacity(1u, diagnostics);
    if (!reservation)
        return false;
    @autoreleasepool {
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        if (commandBuffer == nil) {
            diagnostics = "Metal RHI native drawable present requires a command buffer";
            return false;
        }
        [commandBuffer presentDrawable:drawable];
        const auto completion = presentCompletion_;
        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
            const bool success = completedCommandBuffer.status == MTLCommandBufferStatusCompleted;
            const char* error = completedCommandBuffer.error.localizedDescription.UTF8String;
            reservation->release();
            completion->completed(success, error);
            // The present command is queued after the render command, so this
            // completion is also the exact retirement point for the logical
            // frame slot and every graph resource retained by that frame.
            frameCompletion->complete();
        }];
        presentCompletion_->submitted();
        if (stats_) {
            stats_->nativeDrawablePresentPendingCount = presentCompletion_->pendingCount();
        }
        [commandBuffer commit];
    }
    nativeDrawables_[frameSlot].reset();
    if (stats_) {
        ++stats_->nativeDrawablePresentCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::discardDrawable(
    std::uint32_t frameSlot, std::pmr::string& diagnostics)
{
    if (frameSlot >= nativeDrawables_.size()) {
        diagnostics = "Metal RHI native drawable discard frame slot is invalid";
        return false;
    }
    // Releasing an unsubmitted CAMetalDrawable returns it to the layer. No
    // command buffer is created because this path intentionally discards the
    // failed frame rather than presenting a partially recorded image.
    nativeDrawables_[frameSlot].reset();
    return true;
}

bool MetalRhiNativeSwapchainPayload::consumePresentFailure(std::pmr::string& diagnostics)
{
    if (!presentCompletion_) {
        return false;
    }
    const bool failed = presentCompletion_->consumeFailure(diagnostics);
    if (stats_) {
        stats_->nativeDrawablePresentPendingCount = presentCompletion_->pendingCount();
        stats_->nativeDrawablePresentCompletionCount = presentCompletion_->completionCount();
        stats_->nativeDrawablePresentFailureCount = presentCompletion_->failureCount();
        if (failed) {
            stats_->lastPresentDiagnostics = diagnostics;
        }
    }
    return failed;
}

std::shared_ptr<MetalRhiNativeTexturePayload>
MetalRhiNativeSwapchainPayload::acquiredDrawableTexture(
    std::uint32_t frameSlot, std::pmr::string& diagnostics) const
{
    if (frameSlot >= nativeDrawables_.size()) {
        diagnostics = "Metal RHI native drawable texture frame slot is invalid";
        return nullptr;
    }
    @autoreleasepool {
        auto drawable = reinterpret_cast<id<CAMetalDrawable>>(nativeDrawables_[frameSlot].get());
        id<MTLTexture> texture = drawable ? drawable.texture : nil;
        if (texture == nil) {
            diagnostics = "Metal RHI native drawable texture is unavailable";
            return nullptr;
        }
        auto payload = arp::core::makeShared<MetalRhiNativeTexturePayload>();
        payload->resource.nativeDevice = nativeDevice_;
        payload->resource.storageMode = MetalRhiNativeStorageMode::Private;
        payload->resource.nativeObject = retainBorrowedNativeObject(texture);
        payload->resource.nativeRuntimeBacked = true;
        payload->resource.nativeRuntimeDiagnosticOnly = false;
        payload->format = desc_.format;
        payload->extent = {desc_.extent.width, desc_.extent.height, 1u};
        payload->mipLevelCount = 1u;
        payload->arrayLayerCount = 1u;
        payload->sampleCount = 1u;
        return payload;
    }
}

bool MetalRhiNativeSwapchainPayload::resizeDrawableLayer(
    const rhi::Extent2D& extent, std::pmr::string& diagnostics)
{
    if (!validExtent(extent)) {
        diagnostics = "Metal RHI native drawable resize extent is invalid";
        return false;
    }
    desc_.extent = extent;
    clearNativeDrawables();
    auto* layer = reinterpret_cast<CAMetalLayer*>(nativeLayer_.get());
    if (layer) {
        layer.drawableSize = CGSizeMake(extent.width, extent.height);
        if (stats_) {
            ++stats_->nativeResizeCount;
        }
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::reconfigureDrawableLayer(
    const rhi::SwapchainDesc& desc, std::pmr::string& diagnostics)
{
    if (!validExtent(desc.extent)
        || swapchainSurfacePixelFormat(desc.format) == MTLPixelFormatInvalid) {
        diagnostics = "Metal RHI native drawable reconfigure descriptor is invalid";
        return false;
    }
    if (!nativeLayer_) {
        diagnostics = "RHI-METAL-NATIVE-DRAWABLE-LAYER-MISSING";
        return false;
    }
    desc_ = desc;
    clearNativeDrawables();
    auto* layer = reinterpret_cast<CAMetalLayer*>(nativeLayer_.get());
    if (layer) {
        layer.pixelFormat = swapchainSurfacePixelFormat(desc.format);
        layer.drawableSize = CGSizeMake(desc.extent.width, desc.extent.height);
        if (stats_) {
            ++stats_->nativeResizeCount;
        }
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::setDrawableLayerAvailable(
    bool available, std::pmr::string& diagnostics)
{
    if (available && !ensureNativeLayer(diagnostics)) {
        layerAvailable_ = false;
        return false;
    }
    layerAvailable_ = available;
    clearNativeDrawables();
    auto* layer = reinterpret_cast<CAMetalLayer*>(nativeLayer_.get());
    if (layer) {
        [layer removeAllAnimations];
    }
    if (!available) {
        nativeLayer_.reset();
    }
    if (stats_) {
        if (available) {
            ++stats_->layerRecreateCount;
        } else {
            ++stats_->layerLossCount;
        }
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::backgroundPause(std::pmr::string& diagnostics)
{
    (void)diagnostics;
    layerAvailable_ = false;
    clearNativeDrawables();
    auto* layer = reinterpret_cast<CAMetalLayer*>(nativeLayer_.get());
    if (layer) {
        [layer removeAllAnimations];
    }
    nativeLayer_.reset();
    if (stats_) {
        ++stats_->backgroundPauseCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::foregroundResume(std::pmr::string& diagnostics)
{
    if (!ensureNativeLayer(diagnostics)) {
        layerAvailable_ = false;
        return false;
    }
    layerAvailable_ = true;
    if (stats_) {
        ++stats_->foregroundResumeCount;
    }
    return true;
}

bool MetalRhiNativeSwapchainPayload::memoryWarning(std::pmr::string&)
{
    clearNativeDrawables();
    auto* layer = reinterpret_cast<CAMetalLayer*>(nativeLayer_.get());
    if (layer) {
        [layer removeAllAnimations];
    }
    if (stats_) {
        ++stats_->memoryWarningCount;
    }
    return true;
}

std::shared_ptr<MetalRhiNativeSwapchainStats> createMetalRhiNativeSwapchainStats()
{
    auto stats = arp::core::makeShared<MetalRhiNativeSwapchainStats>();
    stats->backendPayloadPresent = true;
    stats->boundedFrameSlotCount = 2;
    stats->nativeRuntimeBacked = defaultMetalDevice() != nil;
    stats->nativeRuntimeDiagnosticOnly = !stats->nativeRuntimeBacked;
    return stats;
}

std::shared_ptr<MetalRhiNativeSwapchainPayload> createMetalRhiNativeSwapchainPayload(
    const rhi::SwapchainDesc& desc, std::shared_ptr<MetalRhiNativeSwapchainStats> stats)
{
    if (!stats) {
        stats = createMetalRhiNativeSwapchainStats();
    }
    stats->backendPayloadPresent = true;
    stats->boundedFrameSlotCount = 2;
    id<MTLDevice> device = defaultMetalDevice();
    CAMetalLayer* layer = nil;
    std::shared_ptr<void> queuePayload;
    const bool nativeDescriptorValid = device != nil && validExtent(desc.extent)
        && swapchainSurfacePixelFormat(desc.format) != MTLPixelFormatInvalid;
    stats->nativeRuntimeBacked = false;
    stats->nativeRuntimeDiagnosticOnly = true;
    if (nativeDescriptorValid) {
        if (desc.platformSurface) {
            // The WindowSystem-owned token carries the CAMetalLayer attached
            // to the actual window. Retain it for the swapchain payload
            // lifetime; the opaque token itself remains backend-neutral above
            // this point.
            layer = reinterpret_cast<CAMetalLayer*>(desc.platformSurface.get());
            [layer retain];
        } else {
            layer = [[CAMetalLayer alloc] init];
        }
        configureLayer(layer, device, desc);
        queuePayload = sharedMetalRhiNativeCommandQueue();
        stats->nativeRuntimeBacked = layer != nil && queuePayload != nullptr;
        stats->nativeRuntimeDiagnosticOnly = !stats->nativeRuntimeBacked;
        if (layer != nil) {
            ++stats->nativeLayerCreateCount;
        }
    }
    return arp::core::makeShared<MetalRhiNativeSwapchainPayload>(
        desc, std::move(stats), adoptNativeObject(layer), std::move(queuePayload));
}

} // namespace arp::rhi_metal
