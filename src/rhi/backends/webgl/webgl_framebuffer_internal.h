#pragma once
#include "rhi/backends/webgl/webgl_resources_internal.h"

namespace arp::rhi::webgl {
class WebGlCanvasPayload;

struct WebGlFramebuffer::Impl {
    // Delete the original native FBO while its attachments remain retained.
    ~Impl() { object.reset(); }

    std::shared_ptr<WebGlResourcePayload> object;
    std::shared_ptr<WebGlTexturePayload> color, depthStencil;
    std::shared_ptr<WebGlCanvasPayload> canvas;
    GpuResourceId colorId() const noexcept;
    Extent3D extent() const noexcept;
};
} // namespace arp::rhi::webgl
