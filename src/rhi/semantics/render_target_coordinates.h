#pragma once

#include "core/diagnostic_error.h"
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {

/// Y transform required when sampling a render target as a texture.
///
/// This is an RHI coordinate contract, not a shader-backend distinction. The
/// shader ABI always receives the explicit scale/bias pair; each backend
/// lowers its native render-target origin to that same sampled-texture space.
enum class TextureUvYTransform {
    Identity,
    Inverted,
};

/// Native render-target origin exposed through the canonical engine-space
/// contract. Backend-specific origin knowledge belongs here, at the RHI
/// boundary, rather than in a shader source or an application render pass.
enum class RenderTargetOrigin {
    BottomLeft,
    TopLeft,
};

constexpr RenderTargetOrigin renderTargetOriginForBackend(Backend backend)
{
    // RenderTargetCoordinates/2: native Metal and WebGPU map clip +Y to
    // the top row, while the canonical fullscreen UV increases with clip Y.
    // See docs/render-target-coordinate-contract.md for all four lowerings.
    switch (backend) {
    case Backend::Metal:
    case Backend::WebGPU:
        return RenderTargetOrigin::TopLeft;
    case Backend::Vulkan:
    case Backend::Null:
        return RenderTargetOrigin::BottomLeft;
    case Backend::WebGL:
        throw core::DiagnosticError("RHI-WEBGL1-COORDINATE-LOWERING-UNAVAILABLE");
    }
    throw core::DiagnosticError("RHI-COORDINATE-BACKEND-UNKNOWN");
}

constexpr bool projectionRequiresYFlip(Backend backend)
{
    return renderTargetOriginForBackend(backend) == RenderTargetOrigin::TopLeft;
}

/// UI layout coordinates are always top-left pixel coordinates. Their vertex
/// shader converts pixels directly to clip space, which is a distinct path
/// from the camera projection convention above. Native Metal and WebGPU map
/// that shader-space Y axis opposite to a top-left UI layout, so both require
/// the explicit rectangle-origin transform. Vulkan retains the canonical UI
/// mapping and Null only validates the L2 command sequence.
constexpr bool uiCoordinatesRequireYFlip(Backend backend)
{
    return projectionRequiresYFlip(backend);
}

/// Apply the selected render-target convention to a row-major projection
/// matrix. The engine math helpers use the Vulkan-style negative Y scale as
/// their canonical form; a top-left clip-space target requires the opposite
/// scale. Keeping this operation in RHI semantics makes camera, shadow-caster,
/// and shadow-receiver matrices agree without a shader-side backend branch or
/// a shadow-specific UV field in the shader ABI.
inline void applyProjectionYConvention(float* rowMajorMatrix16, Backend backend)
{
    const bool flip = projectionRequiresYFlip(backend);
    if (!rowMajorMatrix16 || !flip) {
        return;
    }
    rowMajorMatrix16[5] = -rowMajorMatrix16[5];
}

/// Apply the same render-target convention to an already-composed row-major
/// view-projection matrix.  A projection matrix is corrected by changing its
/// Y scale (m[5]); once view has been multiplied in, the equivalent operation
/// is to negate the complete clip-space Y row.  Keeping this distinct avoids
/// accidentally applying the projection-only operation to a VP matrix.
inline void applyViewProjectionYConvention(float* rowMajorViewProjection16, Backend backend)
{
    const bool flip = projectionRequiresYFlip(backend);
    if (!rowMajorViewProjection16 || !flip) {
        return;
    }
    for (int column = 0; column < 4; ++column) {
        rowMajorViewProjection16[4 + column] = -rowMajorViewProjection16[4 + column];
    }
}

/// Compose a raster VP with clip-to-texture Y for compute consumers that use
/// uv*2-1 directly. Apply before inversion for world reconstruction.
inline void applyRenderTargetSamplingYConvention(float* rowMajorViewProjection16, Backend backend)
{
    applyViewProjectionYConvention(rowMajorViewProjection16, backend);
}

/// Return the sampled render-target transform exposed by the backend-neutral
/// RHI contract. This applies only when a render-target output is later read
/// as a texture (post processing, SSR, and similar passes).
constexpr TextureUvYTransform textureUvYTransformForBackend(Backend backend)
{
    return renderTargetOriginForBackend(backend) == RenderTargetOrigin::TopLeft
        ? TextureUvYTransform::Inverted
        : TextureUvYTransform::Identity;
}

/// Return the sampled-texture transform for CPU-authored, top-left raster
/// data such as UI atlases and FreeType glyph pages. Uploading preserves the
/// source row order, so Metal and WebGPU require a V inversion at the sample
/// boundary. This is deliberately distinct from render-target sampling.
constexpr TextureUvYTransform cpuRasterTextureUvYTransformForBackend(Backend backend)
{
    return textureUvYTransformForBackend(backend);
}

/// Adjust a normalized texture rectangle when the UI quad has already been
/// converted from logical top-left coordinates to the backend render-target
/// origin. The quad vertices still run from logical top to bottom, so the
/// sampled V interval must run in the opposite direction for an inverted
/// render-target convention.
inline void applyTextureUvYConvention(float* originY, float* sizeY, Backend backend)
{
    const auto transform = textureUvYTransformForBackend(backend);
    if (!originY || !sizeY || transform == TextureUvYTransform::Identity) {
        return;
    }
    *originY += *sizeY;
    *sizeY = -*sizeY;
}

/// Apply the CPU-raster sampling convention without changing the render-target
/// sampling contract used by post-processing passes.
inline void applyCpuRasterTextureUvYConvention(float* originY, float* sizeY, Backend backend)
{
    const auto transform = cpuRasterTextureUvYTransformForBackend(backend);
    if (!originY || !sizeY || transform == TextureUvYTransform::Identity) {
        return;
    }
    *originY += *sizeY;
    *sizeY = -*sizeY;
}

/// Encode the semantic transform into the shader ABI's scale/bias lanes.
inline void writeTextureUvYScaleBias(TextureUvYTransform transform, float* scale, float* bias)
{
    if (!scale || !bias)
        return;
    *scale = transform == TextureUvYTransform::Inverted ? -1.0f : 1.0f;
    *bias = transform == TextureUvYTransform::Inverted ? 1.0f : 0.0f;
}

} // namespace arp::rhi::semantics
