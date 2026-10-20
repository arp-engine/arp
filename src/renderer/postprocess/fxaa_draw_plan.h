#pragma once

#include "renderer/postprocess/fxaa_settings.h"
#include "rhi/gpu_common.h"

namespace arp::renderer {

struct FxaaPushConstants {
    float inverseScreenSize[2]{};
    float edgeThreshold = 0.125f;
    float subpixelQuality = 0.75f;
    float inputUvScaleBias[2]{1.0f, 0.0f};
};

inline FxaaPushConstants buildFxaaPushConstants(
    const rhi::Extent2D& extent, const FxaaSettings& settings = {})
{
    FxaaPushConstants push;
    if (extent.width > 0 && extent.height > 0) {
        push.inverseScreenSize[0] = 1.0f / static_cast<float>(extent.width);
        push.inverseScreenSize[1] = 1.0f / static_cast<float>(extent.height);
    }
    push.edgeThreshold = settings.contrastThreshold;
    if (settings.inputUvYTransform == rhi::semantics::TextureUvYTransform::Inverted) {
        push.inputUvScaleBias[0] = -1.0f;
        push.inputUvScaleBias[1] = 1.0f;
    }
    return push;
}

} // namespace arp::renderer
