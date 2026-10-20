#pragma once

// TerrainSurfaceWeightPages - six-layer surface weight page generator.
//
// Produces two RGBA8 weight textures ("weight pages") from the TerrainFieldSet
// that the terrain fragment shader samples to determine the blend ratio of the
// six surface material layers: grassMoss, forestFloor, wetMud, rockyPath,
// mossyRock, dirtySnow.
//
// Weight inputs (all from TerrainFieldSet):
//   - slope (rise/run)
//   - curvature (Laplacian: +concave / -convex)
//   - wetness (TWI, clamped [0,12])
//   - shoreDistance (meters to water)
//   - height (elevation)
//   - riverMask
//
// The six base weights are explicitly normalized (sum == 1) on the CPU before
// packing into RGBA8. Two overlay channels (snow, wetness) use a separate
// schema so they do not silently compete with the base weights - the shader
// applies them additively on top of the sampled base.
//
// Page layout (RGBA8, 256x256, row-major):
//   pageA.R = grassMoss      pageB.R = mossyRock
//   pageA.G = forestFloor    pageB.G = dirtySnow
//   pageA.B = wetMud         pageB.B = snowOverlay
//   pageA.A = rockyPath      pageB.A = wetnessOverlay
//
// Plan reference: complete_procedural_scene_implementation_plan.md P4.

#include "pcg/terrain_fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace arp::pcg {

/// Six-layer surface weight pages (two RGBA8 textures).
struct TerrainSurfaceWeightPages {
    int width = 0;
    int height = 0;
    float cellSize = 0.0f;
    float originX = 0.0f;
    float originZ = 0.0f;

    /// Page A: grassMoss(R), forestFloor(G), wetMud(B), rockyPath(A).
    std::vector<std::uint8_t> pageA;

    /// Page B: mossyRock(R), dirtySnow(G), snowOverlay(B), wetnessOverlay(A).
    std::vector<std::uint8_t> pageB;

    /// Convert world (wx, wz) to grid index. Returns false if out of bounds.
    bool worldToGrid(float wx, float wz, int& gx, int& gz) const noexcept
    {
        gx = static_cast<int>((wx - originX) / cellSize);
        gz = static_cast<int>((wz - originZ) / cellSize);
        return gx >= 0 && gx < width && gz >= 0 && gz < height;
    }

    /// Bilinear sample of all 6 base weights + 2 overlays at world (wx, wz).
    /// Returns false if out of bounds (weights left at 0).
    struct WeightSample {
        float grassMoss = 0.0f;
        float forestFloor = 0.0f;
        float wetMud = 0.0f;
        float rockyPath = 0.0f;
        float mossyRock = 0.0f;
        float dirtySnow = 0.0f;
        float snowOverlay = 0.0f;
        float wetnessOverlay = 0.0f;
    };

    bool sampleWeights(float wx, float wz, WeightSample& out) const noexcept
    {
        const float fx = (wx - originX) / cellSize;
        const float fz = (wz - originZ) / cellSize;
        int x0 = static_cast<int>(std::floor(fx));
        int z0 = static_cast<int>(std::floor(fz));
        if (x0 < 0)
            x0 = 0;
        if (z0 < 0)
            z0 = 0;
        if (x0 >= width - 1)
            x0 = width - 2;
        if (z0 >= height - 1)
            z0 = height - 2;
        if (x0 < 0 || z0 < 0)
            return false;

        const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
        const float tz = std::clamp(fz - static_cast<float>(z0), 0.0f, 1.0f);

        const auto idx = [&](int x, int z) -> std::size_t {
            return static_cast<std::size_t>(z) * width + x;
        };

        const auto sample4 = [&](const std::vector<std::uint8_t>& page, int channel) -> float {
            const auto p00 = page[idx(x0, z0) * 4 + channel] / 255.0f;
            const auto p10 = page[idx(x0 + 1, z0) * 4 + channel] / 255.0f;
            const auto p01 = page[idx(x0, z0 + 1) * 4 + channel] / 255.0f;
            const auto p11 = page[idx(x0 + 1, z0 + 1) * 4 + channel] / 255.0f;
            const float a = p00 * (1.0f - tx) + p10 * tx;
            const float b = p01 * (1.0f - tx) + p11 * tx;
            return a * (1.0f - tz) + b * tz;
        };

        out.grassMoss = sample4(pageA, 0);
        out.forestFloor = sample4(pageA, 1);
        out.wetMud = sample4(pageA, 2);
        out.rockyPath = sample4(pageA, 3);
        out.mossyRock = sample4(pageB, 0);
        out.dirtySnow = sample4(pageB, 1);
        out.snowOverlay = sample4(pageB, 2);
        out.wetnessOverlay = sample4(pageB, 3);
        return true;
    }
};

/// Compute six-layer surface weight pages from a TerrainFieldSet.
///
/// The weight computation uses the following logic per cell:
///
/// grassMoss:    Dominant on gentle slopes (<0.35), moderate-to-low wetness,
///                low-to-mid elevation. The "default" surface.
/// forestFloor:  Concave depressions (curvature > 0.01), moderate slope,
///                away from immediate shore. Represents forest floor / duff.
/// wetMud:       High wetness (>5) or low shoreDistance (<15m) or riverMask.
///                Valleys, riverbanks, pond edges.
/// rockyPath:    Convex ridges (curvature < -0.01), moderate-high slope,
///                transitional between grass and bare rock. Path / trail.
/// mossyRock:    Steep slopes (>0.50), cliffs, high exposure. The rock
///                surface for cliff faces and mountain sides.
/// dirtySnow:    High elevation above snowLine, transitions from rock to snow.
///
/// Overlay channels (page B):
///   snowOverlay:     Elevation-based snow factor: smoothstep(snowLine,
///                    snowLine + snowFeather, height). Applied by the shader
///                    to boost dirtySnow weight.
///   wetnessOverlay:  Normalized wetness (wetness / 12). Applied by the shader
///                    to boost wetMud weight and darken albedo.
inline TerrainSurfaceWeightPages computeSurfaceWeightPages(
    const TerrainFieldSet& fields, float snowLine = 60.0f, float snowFeather = 20.0f)
{
    TerrainSurfaceWeightPages pages;
    pages.width = fields.width;
    pages.height = fields.height;
    pages.cellSize = fields.cellSize;
    pages.originX = fields.originX;
    pages.originZ = fields.originZ;

    const std::size_t N = static_cast<std::size_t>(fields.width) * fields.height;
    pages.pageA.resize(N * 4, 0);
    pages.pageB.resize(N * 4, 0);

    struct WeightFields {
        std::vector<float> grassMoss, forestFloor, wetMud, rockyPath;
        std::vector<float> mossyRock, dirtySnow, snowOverlay, wetnessOverlay;
    } wf;

    wf.grassMoss.resize(N, 0.0f);
    wf.forestFloor.resize(N, 0.0f);
    wf.wetMud.resize(N, 0.0f);
    wf.rockyPath.resize(N, 0.0f);
    wf.mossyRock.resize(N, 0.0f);
    wf.dirtySnow.resize(N, 0.0f);
    wf.snowOverlay.resize(N, 0.0f);
    wf.wetnessOverlay.resize(N, 0.0f);

    for (int iz = 0; iz < fields.height; ++iz) {
        for (int ix = 0; ix < fields.width; ++ix) {
            const auto i = static_cast<std::size_t>(iz) * fields.width + ix;
            const float slope = fields.slope[i];
            const float curvature = fields.curvature[i];
            const float wetness = fields.wetness[i];
            const float shoreDist = fields.shoreDistance[i];
            const float height = fields.heights[i];
            const float riverMask = static_cast<float>(fields.riverMask[i]);

            // ── Base weights (pre-normalization) ───────────────────────────
            float wGrassMoss = 1.0f;
            float wForestFloor = 0.0f;
            float wWetMud = 0.0f;
            float wRockyPath = 0.0f;
            float wMossyRock = 0.0f;
            float wDirtySnow = 0.0f;

            // mossyRock: steep slopes and cliffs
            const float rockSlope = std::clamp((slope - 0.45f) * 3.0f, 0.0f, 1.0f);
            wMossyRock = rockSlope;
            wGrassMoss *= (1.0f - rockSlope);

            // rockyPath: moderate slopes, convex terrain (ridges, trails)
            const float pathSlope =
                std::clamp((slope - 0.15f) * 2.5f, 0.0f, 1.0f) * (1.0f - rockSlope);
            const float convexity = std::clamp(-curvature * 50.0f, 0.0f, 1.0f);
            wRockyPath = pathSlope * (0.4f + 0.6f * convexity);
            wGrassMoss *= (1.0f - wRockyPath * 0.7f);

            // forestFloor: concave depressions, moderate slope, away from shore
            const float concavity = std::clamp(curvature * 50.0f, 0.0f, 1.0f);
            const float forestSlope = std::clamp(1.0f - slope * 2.0f, 0.0f, 1.0f);
            const float forestShore = std::clamp((shoreDist - 10.0f) / 30.0f, 0.0f, 1.0f);
            wForestFloor = concavity * forestSlope * forestShore * 0.8f;
            wGrassMoss *= (1.0f - wForestFloor * 0.5f);

            // wetMud: high wetness, near shore, river cells
            const float wetFactor = std::clamp((wetness - 4.0f) / 8.0f, 0.0f, 1.0f);
            const float shoreFactor = std::clamp(1.0f - shoreDist / 15.0f, 0.0f, 1.0f);
            wWetMud = std::max(wetFactor * 0.8f, shoreFactor * 0.9f) + riverMask * 0.5f;
            wWetMud = std::min(wWetMud, 1.0f);
            wGrassMoss *= (1.0f - wWetMud * 0.8f);

            // dirtySnow: high elevation above snow line
            const float snowFactor = std::clamp((height - snowLine) / snowFeather, 0.0f, 1.0f);
            wDirtySnow = snowFactor;
            wGrassMoss *= (1.0f - snowFactor * 0.9f);
            wMossyRock *= (1.0f - snowFactor * 0.7f);

            // ── Normalize base weights ────────────────────────────────────
            const float sum =
                wGrassMoss + wForestFloor + wWetMud + wRockyPath + wMossyRock + wDirtySnow;
            const float invSum = sum > 0.001f ? 1.0f / sum : 0.0f;
            wGrassMoss *= invSum;
            wForestFloor *= invSum;
            wWetMud *= invSum;
            wRockyPath *= invSum;
            wMossyRock *= invSum;
            wDirtySnow *= invSum;

            // ── Overlay channels (separate schema, not normalized) ────────
            const float snowOverlay = snowFactor;
            const float wetnessOverlay = std::clamp(wetness / 12.0f, 0.0f, 1.0f);

            // ── Pack into RGBA8 ───────────────────────────────────────────
            wf.grassMoss[i] = wGrassMoss;
            wf.forestFloor[i] = wForestFloor;
            wf.wetMud[i] = wWetMud;
            wf.rockyPath[i] = wRockyPath;
            wf.mossyRock[i] = wMossyRock;
            wf.dirtySnow[i] = wDirtySnow;
            wf.snowOverlay[i] = snowOverlay;
            wf.wetnessOverlay[i] = wetnessOverlay;
        }
    }

    const auto blurField = [&](std::vector<float>& values) {
        if (fields.width < 2 || fields.height < 2)
            return;
        std::vector<float> temp(values.size(), 0.0f);
        std::vector<float> out(values.size(), 0.0f);
        constexpr std::array<float, 5> k = {1.0f, 4.0f, 6.0f, 4.0f, 1.0f};
        constexpr float kInvNorm = 1.0f / 16.0f;

        auto sampleClamped = [&](const std::vector<float>& src, int x, int y) -> float {
            x = std::clamp(x, 0, fields.width - 1);
            y = std::clamp(y, 0, fields.height - 1);
            return src[static_cast<std::size_t>(y) * fields.width + x];
        };

        for (int y = 0; y < fields.height; ++y) {
            for (int x = 0; x < fields.width; ++x) {
                float acc = 0.0f;
                for (int dx = -2; dx <= 2; ++dx) {
                    acc += sampleClamped(values, x + dx, y) * k[static_cast<std::size_t>(dx + 2)];
                }
                temp[static_cast<std::size_t>(y) * fields.width + x] = acc * kInvNorm;
            }
        }
        for (int y = 0; y < fields.height; ++y) {
            for (int x = 0; x < fields.width; ++x) {
                float acc = 0.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    acc += sampleClamped(temp, x, y + dy) * k[static_cast<std::size_t>(dy + 2)];
                }
                out[static_cast<std::size_t>(y) * fields.width + x] = acc * kInvNorm;
            }
        }
        values.swap(out);
    };

    blurField(wf.grassMoss);
    blurField(wf.forestFloor);
    blurField(wf.wetMud);
    blurField(wf.rockyPath);
    blurField(wf.mossyRock);
    blurField(wf.dirtySnow);
    blurField(wf.snowOverlay);
    blurField(wf.wetnessOverlay);

    const auto to8 = [](float v) -> std::uint8_t {
        return static_cast<std::uint8_t>(std::clamp(v * 255.0f, 0.0f, 255.0f));
    };
    for (std::size_t i = 0; i < N; ++i) {
        float wGrassMoss = wf.grassMoss[i];
        float wForestFloor = wf.forestFloor[i];
        float wWetMud = wf.wetMud[i];
        float wRockyPath = wf.rockyPath[i];
        float wMossyRock = wf.mossyRock[i];
        float wDirtySnow = wf.dirtySnow[i];
        const float sum =
            wGrassMoss + wForestFloor + wWetMud + wRockyPath + wMossyRock + wDirtySnow;
        const float invSum = sum > 0.0001f ? 1.0f / sum : 0.0f;
        wGrassMoss *= invSum;
        wForestFloor *= invSum;
        wWetMud *= invSum;
        wRockyPath *= invSum;
        wMossyRock *= invSum;
        wDirtySnow *= invSum;

        pages.pageA[i * 4 + 0] = to8(wGrassMoss);
        pages.pageA[i * 4 + 1] = to8(wForestFloor);
        pages.pageA[i * 4 + 2] = to8(wWetMud);
        pages.pageA[i * 4 + 3] = to8(wRockyPath);
        pages.pageB[i * 4 + 0] = to8(wMossyRock);
        pages.pageB[i * 4 + 1] = to8(wDirtySnow);
        pages.pageB[i * 4 + 2] = to8(std::clamp(wf.snowOverlay[i], 0.0f, 1.0f));
        pages.pageB[i * 4 + 3] = to8(std::clamp(wf.wetnessOverlay[i], 0.0f, 1.0f));
    }

    return pages;
}

} // namespace arp::pcg
