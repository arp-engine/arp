#pragma once

// Sub-pixel jitter utilities for Temporal Anti-Aliasing (TAA).
//
// TAA accumulates sub-pixel offsets across frames by perturbing the projection
// matrix, so each frame samples the scene at a slightly different sub-pixel
// position. The accumulation + neighborhood-clipping resolve pass in taa.slang
// reconstructs a higher-quality image from the per-frame history.
//
// Matrix convention: core::math is row-major (m[row*4+col] = M[row][col]),
// clip = M . v (column vector). applyProjectionJitter() perturbs the projection
// in this row-major storage, matching AGENTS.md "Matrix Layout" hard constraint
// #1. The resulting jittered VP is what the scene shader consumes via its
// default float4x4 member compiled with -matrix-layout-row-major, so no
// extra transposition is needed on the C++ side.
//
// Jitter sequence: Halton (2,3). The caller advances the frame index each frame
// (typically modulo a small period like 8 or 16) and calls halton23() to get
// the sub-pixel offset in [-1, 1]. The offset is scaled by texel size and
// folded into the projection via applyProjectionJitter().
//
// Engine / game boundary: the engine only provides these deterministic tools.
// The downstream game (e.g. World of ClaudeCraft) owns the camera and is
// responsible for:
//   1. Calling halton23(frameIndex) each frame.
//   2. Building the jittered projection via applyProjectionJitter().
//   3. Pushing the jittered VP into the scene pass (pbr_scene push-constant).
//   4. Tracking the previous-frame unjittered VP via TaaCameraHistory so the
//      velocity MRT represents scene/camera motion only. Projection jitter is
//      sample diversity, not motion of the canonical history texel grid.
// The engine does NOT hold or mutate camera state.

#include "core/math/math.h"

#include <cstdint>

namespace arp::core {

namespace detail {
template <std::uint32_t Base> inline float haltonRadicalInverse(std::uint32_t value)
{
    static_assert(Base >= 2u);
    float result = 0.0f;
    const float invBase = 1.0f / static_cast<float>(Base);
    float weight = invBase;
    while (value > 0u) {
        result += static_cast<float>(value % Base) * weight;
        value /= Base;
        weight *= invBase;
    }
    return result;
}
} // namespace detail

/// Compute the Halton (2,3) low-discrepancy sequence value for the given index.
// Returns a 2D point in [-1, 1]^2 suitable for sub-pixel jitter. The sequence
// is deterministic and frame-index-keyed, so two runs with the same index
// produce identical offsets (required for stable temporal accumulation).
//
// Base 2 covers the x axis, base 3 covers the y axis. The radical-inverse
// formulation produces a well-distributed set of points that avoids the
// regular grid patterns a naive round-robin would introduce.
inline Vec2 halton23(std::uint32_t index)
{
    // Map [0, 1) -> [-1, 1) so the jitter is symmetric about the pixel center.
    // The 2.0 * x - 1.0 transform preserves the low-discrepancy distribution.
    const float x = 2.0f * detail::haltonRadicalInverse<2u>(index) - 1.0f;
    const float y = 2.0f * detail::haltonRadicalInverse<3u>(index) - 1.0f;
    return Vec2{x, y};
}

/// Three-dimensional sub-cell samples in [0,1), with independent prime bases.
/// A caller wanting the centered, non-temporal sample uses (0.5,0.5,0.5).
inline Vec3 halton235(std::uint32_t index)
{
    return {detail::haltonRadicalInverse<2u>(index),
        detail::haltonRadicalInverse<3u>(index),
        detail::haltonRadicalInverse<5u>(index)};
}

/// Apply a sub-pixel jitter to a row-major perspective projection matrix.
//
// halton23() returns [-1, 1), while a TAA sample must stay within half a
// pixel of the pixel center. Since one pixel spans 2 / extent in NDC, the
// half-pixel sample radius is 1 / extent. For a perspective matrix clip.w is
// view-space z, so a depth-independent NDC offset requires
// clip.xy += jitter * (1 / extent) * clip.w. The vertex shader's
// mul(viewProjection, worldPos) then produces the jittered clip position
// directly, so no shader-side change is needed to consume the jitter.
//
// Row-major perturbation: in core::Mat4 (m[row*4+col]), the perspective
// projection produced by core::perspective() stores the x/y clip scaling in
// m[0] (xScale) and m[5] (-yScale), with clip.w = view.z. Adding the NDC
// offset to row-0/row-1's z coefficients (m[2], m[6]) therefore scales it by
// clip.w. A fixed translation in m[3]/m[7] would incorrectly shrink with depth.
//
// @param projection    Row-major projection matrix (e.g. core::perspective()).
// @param jitter        Sub-pixel offset in [-1, 1]^2 from halton23().
// @param extentWidth   Render target width in pixels (must be > 0).
// @param extentHeight  Render target height in pixels (must be > 0).
// @return A new row-major Mat4 with the jitter folded into the projection.
inline Mat4 applyProjectionJitter(
    const Mat4& projection, Vec2 jitter, float extentWidth, float extentHeight)
{
    Mat4 jittered = projection;
    if (extentWidth > 0.0f && extentHeight > 0.0f) {
        // One pixel spans 2 / extent in NDC. halton23() spans [-1, 1), so
        // dividing by extent keeps the raster excursion within half a pixel.
        const float ndcOffsetX = jitter.x / extentWidth;
        const float ndcOffsetY = jitter.y / extentHeight;
        // core::perspective() has clip.w = view.z, so z-column coefficients
        // produce a constant NDC offset at every depth.
        jittered.m[2] += ndcOffsetX;
        jittered.m[6] += ndcOffsetY;
    }
    return jittered;
}

} // namespace arp::core
