#pragma once

#include "rhi/semantics/render_target_coordinates.h"

namespace arp::renderer {

struct TaaSettings {
    enum class Quality : std::uint8_t { Low, Balanced, High };
    bool enabled = true;
    Quality quality = Quality::High;
    // Base history blend factor (alpha). Lower = more temporal accumulation
    // (smoother, more lag); higher = sharper, less ghosting but more aliasing.
    // Production integrations provide per-pixel motion vectors, depth and a
    // reactive mask. Keep the base alpha low enough to suppress sub-pixel
    // raster jitter; disocclusion and genuinely unstable pixels are handled
    // by those explicit rejection inputs.
    float blendFactor = 0.1f;
    // Alpha floor for fast-moving pixels. When velocity is high, the blend
    // factor is pushed toward this value so history contributes less and
    // ghosting on moving objects is reduced. 0.6 strongly trusts current.
    float velocityBlendFloor = 0.6f;
    // Velocity is converted from UV displacement to pixels before applying
    // this scale. At the default, eight pixels of motion reaches the history
    // rejection floor while sub-pixel motion remains temporally stable.
    float velocityScale = 0.125f;
    // Relative linear view-depth difference accepted by the disocclusion
    // test. Device depth is perspective-nonlinear and must not be compared
    // against a fixed threshold.
    float depthRejectionThreshold = 0.02f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float reactiveScale = 1.0f;
    // Sharpening the current jittered frame inside the temporal resolve feeds
    // high-frequency aliasing back into history. Sharpen only in a dedicated
    // spatial pass that consumes the resolved image.
    float sharpenStrength = 0.0f;
    // True on the first frame, after a camera cut, or when the history texture
    // was invalidated (resize / device loss). Forces the resolve to bypass
    // history blending and output the current color (historyMiss path).
    bool resetHistory = false;
    rhi::semantics::TextureUvYTransform inputUvYTransform =
        rhi::semantics::TextureUvYTransform::Identity;
};

} // namespace arp::renderer
