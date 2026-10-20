#include "rhi/backends/webgl/webgl_canvas_internal.h"

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

namespace arp::rhi::webgl {

WebGlFramebuffer::WebGlFramebuffer(ConstructionKey, core::UniquePtr<Impl> impl)
    : impl_(std::move(impl))
{}

WebGlFramebuffer::~WebGlFramebuffer() = default;

std::shared_ptr<WebGlFramebuffer> WebGlResources::retainFramebuffer(
    GpuTexture& color, GpuTexture* depthStencil)
{
    if (const auto* image = dynamic_cast<WebGlCanvasImage*>(&color)) {
        if (depthStencil || image->payload->domain != domain_
            || image->payload != domain_->canvas.lock()
            || image->payload->phase != WebGlCanvasPayload::Phase::Acquired
            || (domain_->lastDiagnostic = image->payload->diagnostic())) {
            if (!domain_->lastDiagnostic)
                domain_->lastDiagnostic = "RHI-WEBGL1-CANVAS-FRAMEBUFFER-LEASE";
            return {};
        }
        return image->payload->framebuffer.lock();
    }
    return core::shareUnique(createFramebuffer(color, depthStencil));
}

GpuResourceId WebGlFramebuffer::id() const noexcept
{
    return impl_->object->id;
}

const char* WebGlFramebuffer::currentDiagnostic() const noexcept
{
    if (const auto* id = impl_->object->domain->currentDiagnostic())
        return id;
    if (impl_->canvas) {
        if (const auto* id = impl_->canvas->diagnostic())
            return id;
        return impl_->canvas->phase == WebGlCanvasPayload::Phase::Acquired
                || impl_->canvas->phase == WebGlCanvasPayload::Phase::Submitted
            ? nullptr
            : "RHI-WEBGL1-CANVAS-FRAMEBUFFER-LEASE";
    }
    return impl_->object->current() && impl_->color->current()
            && (!impl_->depthStencil || impl_->depthStencil->current())
        ? nullptr
        : "RHI-WEBGL1-FRAMEBUFFER-OWNER-RETIRED";
}

core::UniquePtr<WebGlFramebuffer> WebGlResources::createFramebuffer(
    GpuTexture& color, GpuTexture* depthStencil)
{
    auto& d = *domain_;
    if (d.preflight(0, 0))
        return {};
    const auto* c = dynamic_cast<WebGlTexture*>(&color);
    const auto* z = dynamic_cast<WebGlTexture*>(depthStencil);
    if (!c || c->payload->domain != domain_ || !c->payload->current()
        || (depthStencil && (!z || z->payload->domain != domain_ || !z->payload->current()))) {
        d.lastDiagnostic = "RHI-WEBGL1-FRAMEBUFFER-FOREIGN-ATTACHMENT";
        return {};
    }
    const auto& cd = c->desc();
    if (cd.format != TextureFormat::Rgba8Unorm || cd.mipLevelCount != 1
        || (cd.usage & TextureUsage::RenderAttachment) == TextureUsage::None
        || cd.extent.width > profile().limits().maxViewportWidth
        || cd.extent.height > profile().limits().maxViewportHeight
        || (z
            && (z->desc().format != TextureFormat::Depth24Stencil8
                || z->desc().extent != cd.extent))) {
        d.lastDiagnostic = "RHI-WEBGL1-FRAMEBUFFER-ATTACHMENT-LAYOUT";
        return {};
    }
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        auto impl = core::makeUnique<WebGlFramebuffer::Impl>();
        impl->object = core::makeShared<WebGlResourcePayload>(
            WebGlResourceFactoryKey{}, domain_, WebGlObjectKind::Framebuffer);
        impl->color = c->payload;
        if (z)
            impl->depthStencil = z->payload;
        auto result = core::makeUnique<WebGlFramebuffer>(
            WebGlFramebuffer::ConstructionKey{}, std::move(impl));
        auto& p = *result->impl_->object;
#ifdef __EMSCRIPTEN__
        if (d.nativeResult())
            return {};
        glGenFramebuffers(1, &p.native);
        glBindFramebuffer(GL_FRAMEBUFFER, p.native);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c->payload->native, 0);
        if (z) {
            if (z->payload->kind == WebGlObjectKind::PackedDepthStencil)
                glFramebufferRenderbuffer(
                    GL_FRAMEBUFFER, 0x821A, GL_RENDERBUFFER, z->payload->native);
            else
                glFramebufferTexture2D(
                    GL_FRAMEBUFFER, 0x821A, GL_TEXTURE_2D, z->payload->native, 0);
        }
        const auto complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (d.nativeResult())
            return {};
        if (!p.native || !complete) {
            d.lastDiagnostic = "RHI-WEBGL1-FRAMEBUFFER-INCOMPLETE";
            return {};
        }
#else
        d.lastDiagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
        return {};
#endif
        if ((d.lastDiagnostic = d.publish(result->impl_->object)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    }
    return {};
}
} // namespace arp::rhi::webgl
