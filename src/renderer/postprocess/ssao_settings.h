#pragma once

#include "rhi/semantics/render_target_coordinates.h"

namespace arp::renderer {

struct SsaoSettings {
    bool enabled = true;
    float radiusPixels = 8.0f;
    float intensity = 0.45f;
    float depthBias = 0.002f;
    float depthFalloff = 48.0f;
    // Camera near/far in view-space units (meters). Required so the shader can
    // linearize the non-linear hardware depth buffer (z/w) into a view-space
    // distance before computing occlusion. Without these, depth deltas are
    // crushed against 1.0 by the perspective projection and AO vanishes.
    float cameraNear = 0.1f;
    float cameraFar = 1000.0f;
    rhi::semantics::TextureUvYTransform inputUvYTransform =
        rhi::semantics::TextureUvYTransform::Identity;
};

} // namespace arp::renderer
