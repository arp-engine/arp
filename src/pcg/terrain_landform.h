#pragma once

// TerrainLandformStack - non-destructive landform composition from real DEM
// stamps.
//
// This module overlays real-world terrain height-field stamps (loaded as
// TerrainStampAsset resources) onto the analytic TerrainHeightField. Each
// stamp placement defines a crop, rotation, scale, vertical remap, smooth
// radial mask, and blend mode. The stack is sampled at runtime inside
// TerrainHeightField::sample(), after the analytic layers and erosion
// displacement, producing a final height that combines procedural base +
// real landform features.
//
// Design constraints:
//   - Pure CPU side. Never uploaded to GPU UBOs (unlike the fixed-size
//     wave/noise/feature layer arrays). The landform contribution is folded
//     into sample() which the TerrainStreamer calls per vertex.
//   - No dependency on arp::resource. The stamp grid data is copied out of
//     TerrainStampAsset into TerrainStampGrid at build time, keeping this
//     header in arp::core with no upward coupling.
//   - Deterministic: same placements + same stamp data => same heights.
//
// Plan reference: complete_procedural_scene_implementation_plan.md P2.

#include "core/memory/cpu_allocator.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace arp::core {

// ---------------------------------------------------------------------------
// TerrainStampGrid - raw height-field data copied from a TerrainStampAsset
// ---------------------------------------------------------------------------
/// A float32 height grid owned via shared_ptr (same pattern as the erosion
/// displacement grid in TerrainHeightField). Row-major layout:
/// index = iz * widthSamples + ix.
struct TerrainStampGrid {
    std::uint32_t widthSamples = 0;
    std::uint32_t heightSamples = 0;
    float minElevation = 0.0f;
    float maxElevation = 0.0f;
    /// Row-major float32 heights [widthSamples * heightSamples].
    /// Null when empty.
    std::shared_ptr<const float> heights;

    bool hasData() const noexcept
    {
        return heights != nullptr && widthSamples >= 2 && heightSamples >= 2;
    }

    /// Bilinearly sample at normalized UV [0,1]². Returns minElevation for
    /// out-of-bounds (clamped to edge). Returns minElevation when empty.
    float sampleNormalized(float u, float v) const noexcept
    {
        if (!hasData())
            return minElevation;
        u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        const float fx = u * static_cast<float>(widthSamples - 1u);
        const float fy = v * static_cast<float>(heightSamples - 1u);
        const std::uint32_t x0 = static_cast<std::uint32_t>(fx);
        const std::uint32_t y0 = static_cast<std::uint32_t>(fy);
        const std::uint32_t x1 = (x0 + 1u < widthSamples) ? x0 + 1u : x0;
        const std::uint32_t y1 = (y0 + 1u < heightSamples) ? y0 + 1u : y0;
        const float tx = fx - static_cast<float>(x0);
        const float ty = fy - static_cast<float>(y0);
        const float* data = heights.get();
        const float h00 = data[y0 * widthSamples + x0];
        const float h10 = data[y0 * widthSamples + x1];
        const float h01 = data[y1 * widthSamples + x0];
        const float h11 = data[y1 * widthSamples + x1];
        const float h0 = h00 * (1.0f - tx) + h10 * tx;
        const float h1 = h01 * (1.0f - tx) + h11 * tx;
        return h0 * (1.0f - ty) + h1 * ty;
    }
};

// ---------------------------------------------------------------------------
// Blend modes
// ---------------------------------------------------------------------------
enum class LandformBlendMode : std::uint8_t {
    /// result += stampHeight * maskWeight
    Additive,
    /// result = max(result, stampHeight * maskWeight + result * (1-maskWeight))
    Max,
    /// result = result * (1-maskWeight) + stampHeight * maskWeight
    HeightBlend,
};

// ---------------------------------------------------------------------------
// LandformStampPlacement - one stamp placement in world space
// ---------------------------------------------------------------------------
struct LandformStampPlacement {
    /// The stamp height grid data (copied from TerrainStampAsset at build time).
    std::shared_ptr<const TerrainStampGrid> grid;

    /// World-space center of the placement.
    float centerX = 0.0f;
    float centerZ = 0.0f;

    /// Rotation of the stamp around the placement center (degrees, CCW).
    float rotationDegrees = 0.0f;

    /// Horizontal scale: world meters covered per stamp unit.
    /// featureSizeX = stampHorizontalSizeMeters * scaleX.
    /// The stamp is mapped onto a featureSizeX × featureSizeZ rectangle
    /// centered at (centerX, centerZ).
    float scaleX = 1.0f;
    float scaleZ = 1.0f;

    /// Sub-window crop in stamp UV [0,1]². Only the region [cropU0,cropU1]×
    /// [cropV0,cropV1] of the stamp is used. This selects a local feature
    /// from the ~25km source DEM.
    float cropU0 = 0.0f;
    float cropV0 = 0.0f;
    float cropU1 = 1.0f;
    float cropV1 = 1.0f;

    /// Vertical remap: stamp elevation [stampMin, stampMax] is linearly
    /// remapped to [verticalRemapMin, verticalRemapMax] in world meters.
    float verticalRemapMin = 0.0f;
    float verticalRemapMax = 0.0f;

    /// Blend mode for combining with the running height.
    LandformBlendMode blendMode = LandformBlendMode::HeightBlend;

    /// Smooth radial mask: full weight inside (maskRadius - maskFalloff),
    /// zero weight outside maskRadius, smoothstep transition between.
    /// maskRadius = 0 means no mask (full weight everywhere within the
    /// stamp's feature extent).
    float maskRadius = 0.0f;
    float maskFalloff = 0.0f;

    /// Evaluate the placement's contribution at world (x, z), blending with
    /// the current running height. Returns the new running height.
    float evaluate(float x, float z, float currentHeight) const noexcept
    {
        if (!grid || !grid->hasData())
            return currentHeight;

        // 1. Transform world (x,z) to stamp-local coordinates.
        const float dx = x - centerX;
        const float dz = z - centerZ;
        const float rad = rotationDegrees * 3.14159265358979323846f / 180.0f;
        const float cosR = std::cos(rad);
        const float sinR = std::sin(rad);
        // Inverse rotation: rotate (dx, dz) by -rotation
        const float localX = dx * cosR + dz * sinR;
        const float localZ = -dx * sinR + dz * cosR;

        // 2. Map to stamp UV [0,1]². The stamp covers a featureSizeX ×
        //    featureSizeZ rectangle centered at the placement center.
        const float featureSizeX = scaleX;
        const float featureSizeZ = scaleZ;
        if (featureSizeX <= 0.0f || featureSizeZ <= 0.0f)
            return currentHeight;

        // Normalize local coords to [-0.5, 0.5] then to [0, 1].
        float u = (localX / featureSizeX) + 0.5f;
        float v = (localZ / featureSizeZ) + 0.5f;

        // 3. Crop: the placement rectangle always occupies the complete
        //    featureSizeX × featureSizeZ footprint. Map that [0,1] footprint
        //    into the selected source sub-window. Treating cropU/V as a
        //    second world-space clip leaves a hard, unmasked step halfway
        //    through the placement -- the source of the tall terrain walls.
        const float cropU = cropU1 - cropU0;
        const float cropV = cropV1 - cropV0;
        if (cropU <= 0.0f || cropV <= 0.0f)
            return currentHeight;
        if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) {
            return currentHeight;
        }
        const float cropUvU = cropU0 + u * cropU;
        const float cropUvV = cropV0 + v * cropV;

        // 4. Bilinear sample the stamp grid.
        const float stampHeight = grid->sampleNormalized(cropUvU, cropUvV);

        // 5. Vertical remap: [stampMin, stampMax] -> [remapMin, remapMax].
        const float stampRange = grid->maxElevation - grid->minElevation;
        float remapped;
        if (stampRange > 0.001f) {
            const float t = (stampHeight - grid->minElevation) / stampRange;
            remapped = verticalRemapMin + t * (verticalRemapMax - verticalRemapMin);
        } else {
            remapped = verticalRemapMin;
        }

        // 6. Smooth radial mask.
        float w = 1.0f;
        if (maskRadius > 0.0f) {
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= maskRadius) {
                w = 0.0f;
            } else if (maskFalloff > 0.0f && dist > maskRadius - maskFalloff) {
                // smoothstep from 1.0 at (maskRadius - maskFalloff) to 0.0 at maskRadius
                const float t = (maskRadius - dist) / maskFalloff;
                w = t * t * (3.0f - 2.0f * t);
            } else {
                w = 1.0f;
            }
        }

        if (w <= 0.0f)
            return currentHeight;

        // 7. Blend with current height.
        switch (blendMode) {
        case LandformBlendMode::Additive:
            return currentHeight + remapped * w;
        case LandformBlendMode::Max:
            return std::max(currentHeight, remapped * w + currentHeight * (1.0f - w));
        case LandformBlendMode::HeightBlend:
            return currentHeight * (1.0f - w) + remapped * w;
        }
        return currentHeight;
    }
};

// ---------------------------------------------------------------------------
// TerrainLandformStack - collection of stamp placements
// ---------------------------------------------------------------------------
class TerrainLandformStack {
public:
    TerrainLandformStack() = default;

    void addPlacement(LandformStampPlacement placement)
    {
        placements_.push_back(std::move(placement));
    }

    /// Sample the stack at world (x, z). Applies each placement in order,
    /// threading the running height through each evaluate() call.
    float sample(float x, float z, float baseHeight) const noexcept
    {
        float h = baseHeight;
        for (const auto& p : placements_) {
            h = p.evaluate(x, z, h);
        }
        return h;
    }

    std::size_t placementCount() const noexcept { return placements_.size(); }

    bool isEmpty() const noexcept { return placements_.empty(); }

    const std::vector<LandformStampPlacement>& placements() const noexcept { return placements_; }

private:
    std::vector<LandformStampPlacement> placements_;
};

} // namespace arp::core
