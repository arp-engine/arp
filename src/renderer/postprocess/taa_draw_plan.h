#pragma once

#include "renderer/postprocess/taa_settings.h"
#include "rhi/gpu_common.h"

namespace arp::renderer {

// Mirrors the TaaPushConstants struct in shaders/wocc/taa.slang. The struct
// body MUST contain NO inline // comments: shader_compiler.cpp's
// reflectedPushConstantSize parser does not skip // comments (AGENTS.md
// "Matrix Layout" hard constraint #4). Explanatory comments live above the
// struct, not inside it.
struct TaaPushConstants {
    float texelSize[4]{};   // xy = (1/w, 1/h), zw = (w, h)
    float blendParams[4]{}; // x = blendFactor, y = velocityBlendFloor,
                            // z = velocityScale, w = historyMiss flag
    // Keep this as float4 to exactly match taa.slang. zw carry the canonical
    // perspective depth coefficients used to reconstruct linear view depth.
    float inputUvScaleBias[4]{1.0f, 0.0f, 0.0f, 0.0f};
    float validationParams[4]{};
};

inline TaaPushConstants buildTaaPushConstants(
    const rhi::Extent2D& extent, const TaaSettings& settings = {})
{
    TaaPushConstants push;
    if (extent.width > 0 && extent.height > 0) {
        push.texelSize[0] = 1.0f / static_cast<float>(extent.width);
        push.texelSize[1] = 1.0f / static_cast<float>(extent.height);
        push.texelSize[2] = static_cast<float>(extent.width);
        push.texelSize[3] = static_cast<float>(extent.height);
    }
    push.blendParams[0] = settings.blendFactor;
    push.blendParams[1] = settings.velocityBlendFloor;
    push.blendParams[2] = settings.velocityScale;
    // historyMiss flag: 1.0 when the history is invalid (first frame / cut /
    // reset), forcing the resolve to output current color. 0.0 otherwise.
    push.blendParams[3] = settings.resetHistory ? 1.0f : 0.0f;
    if (settings.inputUvYTransform == rhi::semantics::TextureUvYTransform::Inverted) {
        push.inputUvScaleBias[0] = -1.0f;
        push.inputUvScaleBias[1] = 1.0f;
    }
    const float nearPlane = settings.nearPlane > 0.0f ? settings.nearPlane : 0.1f;
    const float farPlane = settings.farPlane > nearPlane ? settings.farPlane : nearPlane + 1.0f;
    const float depthRange = farPlane - nearPlane;
    push.inputUvScaleBias[2] = farPlane / depthRange;
    push.inputUvScaleBias[3] = -(nearPlane * farPlane) / depthRange;
    push.validationParams[0] = settings.depthRejectionThreshold;
    push.validationParams[1] = settings.reactiveScale;
    push.validationParams[2] = settings.sharpenStrength;
    push.validationParams[3] = static_cast<float>(settings.quality);
    return push;
}

} // namespace arp::renderer
