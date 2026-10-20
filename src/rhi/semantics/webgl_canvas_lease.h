#pragma once
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/webgl_context_ownership.h"

namespace arp::rhi::semantics {
struct WebGlCanvasReuseRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint32_t maxLeases = 0;
};

class WebGlCanvasReuseContract final {
public:
    static const char* validate(Backend backend, const WebGlCanvasReuseRequest& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-WEBGL1-CANVAS-REUSE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-WEBGL1-CANVAS-REUSE-PROTOTYPE-OPT-IN";
        if (r.maxLeases < 2)
            return "RHI-WEBGL1-CANVAS-REUSE-LEASE-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-CANVAS-REUSE-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-CANVAS-REUSE-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-CANVAS-REUSE-BACKEND-UNKNOWN";
    }
};

class WebGlCanvasLeaseContract final {
public:
    static const char* validate(Backend backend,
        const SwapchainDesc& d,
        Extent2D actual,
        std::string_view selector,
        const WebGlContextAttributes& a) noexcept
    {
        if (d.imageCount || d.presentationMode != PresentationMode::Auto || d.platformSurface)
            return "RHI-WEBGL1-CANVAS-PRESENTATION-POLICY";
        if (d.format != TextureFormat::Rgba8Unorm || !actual.width || !actual.height
            || d.extent.width != actual.width || d.extent.height != actual.height
            || std::string_view(d.surfaceName.data(), d.surfaceName.size()) != selector)
            return "RHI-WEBGL1-CANVAS-ORIGINAL-SURFACE";
        if (a.alpha || a.depth || a.stencil || a.antialias || a.premultipliedAlpha
            || a.preserveDrawingBuffer)
            return "RHI-WEBGL1-CANVAS-ATTRIBUTES-UNSUPPORTED";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-CANVAS-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-CANVAS-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-CANVAS-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
