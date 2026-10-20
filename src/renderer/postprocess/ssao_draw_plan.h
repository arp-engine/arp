#pragma once

#include "renderer/postprocess/ssao_settings.h"
#include "rhi/gpu_common.h"

#include <algorithm>

namespace arp::renderer {

struct SsaoPushConstants {
    float inverseScreenSize[2]{};
    float radiusPixels = 8.0f;
    float intensity = 0.45f;
    float depthBias = 0.002f;
    float depthFalloff = 48.0f;
    float inputUvScaleBias[2]{1.0f, 0.0f};
    float cameraNearFar[2]{0.1f, 1000.0f}; // {near, far} in view-space units
};

inline SsaoPushConstants buildSsaoPushConstants(
    const rhi::Extent2D& extent, const SsaoSettings& settings = {})
{
    SsaoPushConstants push;
    if (extent.width > 0 && extent.height > 0) {
        push.inverseScreenSize[0] = 1.0f / static_cast<float>(extent.width);
        push.inverseScreenSize[1] = 1.0f / static_cast<float>(extent.height);
    }
    push.radiusPixels = std::max(settings.radiusPixels, 0.0f);
    push.intensity = std::max(settings.intensity, 0.0f);
    push.depthBias = std::max(settings.depthBias, 0.0f);
    push.depthFalloff = std::max(settings.depthFalloff, 0.0f);
    // Clamp near/far to a sane, non-degenerate range. The shader divides by
    // (far - depth*(far-near)), so near must be > 0 and far > near.
    push.cameraNearFar[0] = std::max(settings.cameraNear, 1e-4f);
    push.cameraNearFar[1] = std::max(settings.cameraFar, settings.cameraNear + 1.0f);
    if (settings.inputUvYTransform == rhi::semantics::TextureUvYTransform::Inverted) {
        push.inputUvScaleBias[0] = -1.0f;
        push.inputUvScaleBias[1] = 1.0f;
    }
    return push;
}

} // namespace arp::renderer
