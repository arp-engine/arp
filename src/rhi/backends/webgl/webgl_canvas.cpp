#include "rhi/backends/webgl/webgl_canvas_internal.h"
#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

namespace arp::rhi::webgl {
const char* WebGlCanvasPayload::diagnostic() const noexcept
{
    if (!ownerActive || !current() || !state.active())
        return "RHI-WEBGL1-CANVAS-OWNER-RETIRED";
    if (const auto* id = domain->currentDiagnostic())
        return id;
    if (!surfaceEpoch.isCurrent())
        return "RHI-WEBGL1-CANVAS-SURFACE-GENERATION-STALE";
    Extent2D actual;
    if (const auto* id = domain->runtime->drawingBufferExtent(actual))
        return id;
    if (actual.width != extent.width || actual.height != extent.height)
        return "RHI-WEBGL1-CANVAS-EXTENT-CHANGED";
    return nullptr;
}

GpuResourceId WebGlFramebuffer::Impl::colorId() const noexcept
{
    return canvas ? canvas->id : color->id;
}

Extent3D WebGlFramebuffer::Impl::extent() const noexcept
{
    return canvas ? Extent3D{canvas->extent.width, canvas->extent.height, 1} : color->desc.extent;
}

WebGlCanvas::WebGlCanvas(std::shared_ptr<WebGlCanvasPayload> payload,
    core::UniquePtr<GpuTexture> image,
    std::shared_ptr<WebGlFramebuffer> framebuffer)
    : payload_(std::move(payload)), image_(std::move(image)), framebuffer_(std::move(framebuffer))
{}

WebGlCanvas::~WebGlCanvas()
{
    payload_->ownerActive = false;
    payload_->state.invalidate();
}

const char* WebGlCanvas::current() const noexcept
{
    return payload_->diagnostic();
}

AcquiredFrame WebGlCanvas::acquireNextFrame()
{
    if (const auto* id = current())
        return makeAcquireFailureResult(id, 0, 0);
    if (payload_->phase != WebGlCanvasPayload::Phase::Prepared)
        return makeAcquireFailureResult("RHI-WEBGL1-CANVAS-SINGLE-USE-LEASE", 0, 0);
    payload_->phase = WebGlCanvasPayload::Phase::Acquired;
    return makeAcquireSuccessResult(payload_->serial, 0);
}

bool WebGlCanvas::isFrameAcquired(const AcquiredFrame& frame) const noexcept
{
    return !current() && frame.success && frame.status == AcquireStatus::Acquired
        && frame.frameSerial == payload_->serial && frame.frameIndex == 0
        && (payload_->phase == WebGlCanvasPayload::Phase::Acquired
            || payload_->phase == WebGlCanvasPayload::Phase::Submitted);
}

AcquiredSwapchainImage WebGlCanvas::acquiredImage(const AcquiredFrame& frame)
{
    AcquiredSwapchainImage result;
    if (!isFrameAcquired(frame) || !image_) {
        result.diagnostics = "RHI-WEBGL1-CANVAS-IMAGE-LEASE";
        return result;
    }
    result.success = true;
    result.ref = {payload_->id,
        0,
        payload_->serial,
        payload_->extent,
        TextureFormat::Rgba8Unorm,
        ResourceState::ColorAttachment};
    result.texture = std::move(image_);
    return result;
}

FrameDiscardResult WebGlCanvas::discardFrame(const AcquiredFrame& frame)
{
    if (!isFrameAcquired(frame))
        return makeFrameDiscardFailureResult(
            "RHI-WEBGL1-CANVAS-DISCARD-LEASE", frame.frameSerial, frame.frameIndex);
    if (payload_->phase != WebGlCanvasPayload::Phase::Acquired)
        return makeFrameDiscardFailureResult(
            "RHI-WEBGL1-CANVAS-DISCARD-AFTER-SUBMIT", frame.frameSerial, frame.frameIndex);
    if (payload_->maxLeases > 1) {
        if (const auto* id = payload_->state.writeWhole(ResourceState::ColorAttachment, nullptr))
            return makeFrameDiscardFailureResult(id, frame.frameSerial, frame.frameIndex);
    } else
        payload_->state.invalidate();
    payload_->phase = WebGlCanvasPayload::Phase::Discarded;
    return makeFrameDiscardSuccessResult(frame.frameSerial, frame.frameIndex);
}

const char* WebGlCanvas::enableReuse(const semantics::WebGlCanvasReuseRequest& request) noexcept
{
    if (const auto* id = semantics::WebGlCanvasReuseContract::validate(Backend::WebGL, request))
        return id;
    if (const auto* id = current())
        return id;
    if (payload_->phase != WebGlCanvasPayload::Phase::Prepared || payload_->maxLeases != 1)
        return "RHI-WEBGL1-CANVAS-REUSE-ALREADY-STARTED";
    payload_->maxLeases = request.maxLeases;
    return nullptr;
}

const char* WebGlCanvas::recycle(
    const AcquiredFrame& frame, core::UniquePtr<GpuTexture>& image) noexcept
{
    if (const auto* id = current())
        return id;
    if (payload_->maxLeases == 1)
        return "RHI-WEBGL1-CANVAS-REUSE-OPT-IN-REQUIRED";
    if (!frame.success || frame.status != AcquireStatus::Acquired || frame.frameIndex != 0
        || frame.frameSerial != payload_->serial)
        return "RHI-WEBGL1-CANVAS-REUSE-STALE-LEASE";
    if (payload_->phase != WebGlCanvasPayload::Phase::Presented
        && payload_->phase != WebGlCanvasPayload::Phase::Discarded)
        return "RHI-WEBGL1-CANVAS-REUSE-UNRELEASED-LEASE";
    if (payload_->leaseOrdinal >= payload_->maxLeases)
        return "RHI-WEBGL1-CANVAS-REUSE-BUDGET-EXHAUSTED";
    if (framebuffer_.use_count() != 1 || payload_->state.pending())
        return "RHI-WEBGL1-CANVAS-REUSE-RETAINED-COMMAND";
    const auto* original = dynamic_cast<WebGlCanvasImage*>(image.get());
    if (image_ ? bool(image) : (!original || original->payload != payload_))
        return "RHI-WEBGL1-CANVAS-REUSE-ORIGINAL-IMAGE-RETURN";
    if (const auto* id = payload_->state.writeWhole(ResourceState::ColorAttachment, nullptr))
        return id;
    if (!image_)
        image_ = std::move(image);
    payload_->serial = allocateGpuResourceId().value;
    ++payload_->leaseOrdinal;
    payload_->phase = WebGlCanvasPayload::Phase::Prepared;
    return nullptr;
}

PresentResult WebGlCanvas::present(
    const AcquiredFrame& frame, const FramePresentationCompletion& completion)
{
    WebGlNativeCallScope nativeScope(*payload_->domain);
    if (payload_->state.pending())
        return makePresentFailureResult(
            "RHI-WEBGL1-CANVAS-PRESENT-STATE-PENDING", frame.frameSerial, frame.frameIndex);
    if (completion.renderCompletion)
        return makePresentFailureResult(
            "RHI-WEBGL1-GPU-COMPLETION-UNAVAILABLE", frame.frameSerial, frame.frameIndex);
    if (!isFrameAcquired(frame) || payload_->phase != WebGlCanvasPayload::Phase::Submitted)
        return makePresentFailureResult(
            "RHI-WEBGL1-CANVAS-PRESENT-UNSUBMITTED", frame.frameSerial, frame.frameIndex);
#ifdef __EMSCRIPTEN__
    if (const auto* id = payload_->domain->nativeResult())
        return makePresentFailureResult(id, frame.frameSerial, frame.frameIndex);
    (*payload_->domain).nativeCall([&] {
        return glFlush();
    });
    if (const auto* id = payload_->domain->nativeResult()) {
        payload_->phase = WebGlCanvasPayload::Phase::Failed;
        payload_->state.invalidate();
        return makePresentFailureResult(id, frame.frameSerial, frame.frameIndex);
    }
    if (const auto* id = payload_->state.writeWhole(ResourceState::Present, nullptr)) {
        payload_->phase = WebGlCanvasPayload::Phase::Failed;
        payload_->state.invalidate();
        return makePresentFailureResult(id, frame.frameSerial, frame.frameIndex);
    }
    payload_->phase = WebGlCanvasPayload::Phase::Presented;
    return makePresentSuccessResult(frame.frameSerial, frame.frameIndex);
#else
    return makePresentFailureResult(
        "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE", frame.frameSerial, frame.frameIndex);
#endif
}

const char* WebGlCanvas::lifecycleOwnerDiagnostic() const noexcept
{
    const auto& domain = *payload_->domain;
    if (!payload_->ownerActive || !domain.active || domain.failed
        || !domain.contextEpoch.isCurrent())
        return "RHI-WEBGL1-SURFACE-LIFECYCLE-OWNER-RETIRED";
    return domain.runtime->currentDiagnostic();
}

SwapchainLifecycleResult WebGlCanvas::lifecycleResult(const char* diagnostic) const
{
    SwapchainLifecycleResult result;
    result.extent = payload_->extent;
    result.success = !diagnostic;
    result.diagnostics = diagnostic ? diagnostic : "";
    result.swapchainRebuildRequired = result.success && !payload_->surfaceEpoch.isCurrent();
    result.releaseSurfaceResources = result.swapchainRebuildRequired;
    result.acquireAllowed =
        result.success && !current() && payload_->phase == WebGlCanvasPayload::Phase::Prepared;
    result.presentAllowed =
        result.success && !current() && payload_->phase == WebGlCanvasPayload::Phase::Submitted;
    return result;
}

SwapchainLifecycleResult WebGlCanvas::setSurfaceAvailable(bool available)
{
    if (const auto* id = lifecycleOwnerDiagnostic())
        return lifecycleResult(id);
    return lifecycleResult(payload_->domain->runtime->setSurfaceAvailable(available));
}

SwapchainLifecycleResult WebGlCanvas::resize(const Extent2D& extent)
{
    if (const auto* id = lifecycleOwnerDiagnostic())
        return lifecycleResult(id);
    if (payload_->phase == WebGlCanvasPayload::Phase::Acquired
        || payload_->phase == WebGlCanvasPayload::Phase::Submitted || !image_
        || framebuffer_.use_count() != 1 || payload_->state.pending())
        return lifecycleResult("RHI-WEBGL1-SURFACE-RESIZE-RETAINED-LEASE");
    auto result = lifecycleResult(payload_->domain->runtime->resizeSurface(extent));
    if (result.success)
        result.extent = extent;
    return result;
}

SwapchainLifecycleResult WebGlCanvas::reconfigure(const SwapchainReconfigureRequest&)
{
    return lifecycleResult("RHI-WEBGL1-SURFACE-RECONFIGURE-UNSUPPORTED");
}

SwapchainPresentationStatus WebGlCanvas::presentationStatus() const
{
    SwapchainPresentationStatus result;
    result.selectedBackendMode = "BrowserScheduledCanvas/1";
    return result;
}

core::UniquePtr<Swapchain> WebGlResources::createCanvas(const SwapchainDesc& desc)
{
    auto& d = *domain_;
    if (d.preflight(0, 0))
        return {};
    Extent2D actual;
    if ((d.lastDiagnostic = d.runtime->drawingBufferExtent(actual))
        || (d.lastDiagnostic = semantics::WebGlCanvasLeaseContract::validate(Backend::WebGL,
                desc,
                actual,
                d.runtime->canvasSelector(),
                profile().contextAttributes())))
        return {};
    if (!d.canvas.expired()) {
        d.lastDiagnostic = "RHI-WEBGL1-CANVAS-OWNER-ALREADY-EXISTS";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto payload =
            core::makeShared<WebGlCanvasPayload>(WebGlResourceFactoryKey{}, domain_, actual);
        if ((d.lastDiagnostic = d.publish(payload)))
            return {};
        auto image = core::makeUnique<WebGlCanvasImage>(WebGlResourceFactoryKey{}, payload);
        auto impl = core::makeUnique<WebGlFramebuffer::Impl>();
        impl->object = payload;
        impl->canvas = payload;
        auto framebuffer = core::shareUnique(core::makeUnique<WebGlFramebuffer>(
            WebGlFramebuffer::ConstructionKey{}, std::move(impl)));
        payload->framebuffer = framebuffer;
        auto result =
            core::makeUnique<WebGlCanvas>(payload, std::move(image), std::move(framebuffer));
        d.canvas = payload;
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-CANVAS-CPU-ALLOCATION";
        return {};
    }
}

std::shared_ptr<WebGlFramebuffer> WebGlResources::canvasFramebuffer(
    Swapchain& swapchain, const AcquiredFrame& frame)
{
    auto* canvas = dynamic_cast<WebGlCanvas*>(&swapchain);
    if (!canvas || canvas->payload()->domain != domain_ || !canvas->isFrameAcquired(frame)) {
        domain_->lastDiagnostic = "RHI-WEBGL1-CANVAS-FRAMEBUFFER-LEASE";
        return {};
    }
    return canvas->framebuffer();
}

const char* WebGlResources::enableCanvasReuse(
    Swapchain& swapchain, const semantics::WebGlCanvasReuseRequest& request)
{
    auto* canvas = dynamic_cast<WebGlCanvas*>(&swapchain);
    if (!canvas || canvas->payload()->domain != domain_)
        return domain_->lastDiagnostic = "RHI-WEBGL1-CANVAS-REUSE-FOREIGN-CANVAS";
    return domain_->lastDiagnostic = canvas->enableReuse(request);
}

const char* WebGlResources::recycleCanvas(
    Swapchain& swapchain, const AcquiredFrame& frame, core::UniquePtr<GpuTexture>& image)
{
    auto* canvas = dynamic_cast<WebGlCanvas*>(&swapchain);
    if (!canvas || canvas->payload()->domain != domain_)
        return domain_->lastDiagnostic = "RHI-WEBGL1-CANVAS-REUSE-FOREIGN-CANVAS";
    return domain_->lastDiagnostic = canvas->recycle(frame, image);
}
} // namespace arp::rhi::webgl
