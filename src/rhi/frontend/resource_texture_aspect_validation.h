#pragma once
#include "rhi/gpu_resource_types.h"

namespace arp::rhi::semantics {
class TextureAspectValidation final {
public:
    static bool textureAspectMatchesFormat(rhi::TextureFormat format, rhi::TextureAspect aspect)
    {
        if (format == rhi::TextureFormat::Depth24Stencil8) {
            return aspect == rhi::TextureAspect::Depth || aspect == rhi::TextureAspect::Stencil;
        }
        if (rhi::isDepthTextureFormat(format)) {
            return aspect == rhi::TextureAspect::Depth;
        }
        return aspect == rhi::TextureAspect::Color;
    }
};
} // namespace arp::rhi::semantics
