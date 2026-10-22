#pragma once

// TerrainScatter — deterministic grid-jittered point scatter over a heightfield.
//
// Ported from world-of-claudecraft's decoration/dressing/grass scatter pattern
// (world.ts:496 generateDecorations, foliage.ts:1147 generateDressing). The
// algorithm is a grid walk with per-cell density gating and ±step/2 jitter,
// driven entirely by hash2() so results are reproducible for a given seed.
//
// Design:
//   - Pure utility: the caller owns the output buffer. No heap allocation
//     inside the scatter itself.
//   - Composable: takes any TerrainHeightField and any output iterator, so it
//     can scatter grass tufts, tree decorations, or critter spawn points with
//     the same primitive.
//   - Bounded: iterates [minX,maxX) × [minZ,maxZ) stepping by `step`, so the
//     cell count is (range/step)² and predictable.

#include "3d/terrain/terrain_surface.h"
#include "core/hash/deterministic_hash.h"
#include "core/math/math.h"
#include "pcg/terrain_heightfield.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace arp::core {

/// ScatterPoint — a single placement produced by terrainScatter().
struct ScatterPoint {
    float x = 0.0f;      // world X (grid + jitter)
    float z = 0.0f;      // world Z (grid + jitter)
    float y = 0.0f;      // terrain height at (x, z)
    float jitter = 0.0f; // the cell's density-gate hash r ∈ [0,1), for
                         // downstream kind/scale selection
};

/// ScatterDesc — parameters for terrainScatter().
struct ScatterDesc {
    /// World-space scatter region bounds (X/Z).
    float minX = -50.0f;
    float maxX = 50.0f;
    float minZ = -50.0f;
    float maxZ = 50.0f;

    /// Grid cell size (world units). Reference uses 10 for decorations, 1.8–2.05
    /// for grass. Must be > 0.
    float step = 10.0f;

    /// Density gate ∈ [0,1]: a cell is kept iff hash2(cellX, cellZ, seed+31) < density.
    /// Reference uses 0.48 (vale trees), 0.5 (lush grass), 0.38 (lean grass), etc.
    float density = 0.5f;

    /// Deterministic seed.
    std::uint32_t seed = 1u;

    /// P5: When true, uses blue-noise-style jitter that breaks the grid
    /// regularity. Instead of uniform ±step/2 jitter, uses a rotated offset
    /// with independent hash-driven displacement per axis, producing a
    /// distribution whose pair-correlation does not show grid peaks.
    /// Candidates remain world-cell-determined (hash of world cell coords),
    /// so different chunk generation orders produce identical results.
    bool blueNoise = false;

    /// Anchor grid cells to world origin instead of minX/minZ. This makes
    /// fractional-meter grids collision-free and stable across chunk bounds.
    /// The resulting integer grid-cell coordinates must fit in `int` because
    /// deterministic_hash uses signed integer coordinates.
    bool worldAlignedGrid = false;

    /// Optional: minimum terrain Y below which points are rejected (e.g. below
    /// water level). Pass -FLT_MAX to disable.
    float minY = -1e30f;

    /// Optional: maximum slope (rise/run) above which points are rejected.
    /// Reference uses 0.62 for grass/dressing. Pass a large value to disable.
    float maxSlope = 1e30f;

    /// Slope sample epsilon (world units) for finite-difference normal.
    float slopeEpsilon = 1.2f;
};

namespace detail {

template <typename PlacementHeightFn, typename SlopeHeightFn, typename OutputFn>
void terrainScatterSampled(PlacementHeightFn placementHeight,
    SlopeHeightFn slopeHeight,
    const ScatterDesc& desc,
    OutputFn out)
{
    if (!std::isfinite(desc.step) || desc.step <= 0.0f)
        return;
    const float halfStep = desc.step * 0.5f;

    const auto scatterCell = [&](const float gx, const float gz, const int cellX, const int cellZ) {
        const float r = hash2(cellX, cellZ, desc.seed + 31u);
        if (r >= desc.density)
            return;
        float ox, oz;
        if (desc.blueNoise) {
            const float jx = hash2(cellX, cellZ, desc.seed + 57u) - 0.5f;
            const float jz = hash2(cellX * 7 + 3, cellZ * 13 + 5, desc.seed + 91u) - 0.5f;
            const float angle = hash2(cellX * 3 + 1, cellZ * 5 + 7, desc.seed + 113u) * 6.2831853f;
            const float cosA = std::cos(angle);
            const float sinA = std::sin(angle);
            ox = (jx * cosA - jz * sinA) * desc.step;
            oz = (jx * sinA + jz * cosA) * desc.step;
        } else {
            ox = (hash2(cellX, cellZ, desc.seed + 57u) - 0.5f) * desc.step;
            oz = (hash2(cellX, cellZ, desc.seed + 91u) - 0.5f) * desc.step;
        }
        const float x = gx + ox;
        const float z = gz + oz;
        float y = 0.0f;
        if (!placementHeight(x, z, y) || y < desc.minY)
            return;
        if (desc.maxSlope < 1e29f) {
            float hx = 0.0f, hz = 0.0f;
            if (!slopeHeight(x + desc.slopeEpsilon, z, hx)
                || !slopeHeight(x, z + desc.slopeEpsilon, hz))
                return;
            const float slope =
                std::sqrt((hx - y) * (hx - y) + (hz - y) * (hz - y)) / desc.slopeEpsilon;
            if (slope > desc.maxSlope)
                return;
        }
        out(ScatterPoint{x, z, y, r});
    };

    if (desc.worldAlignedGrid) {
        if (!std::isfinite(desc.minX) || !std::isfinite(desc.maxX) || !std::isfinite(desc.minZ)
            || !std::isfinite(desc.maxZ)) {
            return;
        }
        const double step = static_cast<double>(desc.step);
        const double firstX = std::ceil(static_cast<double>(desc.minX) / step);
        const double endX = std::ceil(static_cast<double>(desc.maxX) / step);
        const double firstZ = std::ceil(static_cast<double>(desc.minZ) / step);
        const double endZ = std::ceil(static_cast<double>(desc.maxZ) / step);
        constexpr int kCellKeyLimit = std::numeric_limits<int>::max() / 13;
        const double intMin = -static_cast<double>(kCellKeyLimit);
        const double intMax = static_cast<double>(kCellKeyLimit);
        if (firstX < intMin || firstZ < intMin || endX > intMax || endZ > intMax || firstX > endX
            || firstZ > endZ) {
            return;
        }
        for (auto cellX = static_cast<std::int64_t>(firstX);
            cellX < static_cast<std::int64_t>(endX);
            ++cellX) {
            const float gx = static_cast<float>(static_cast<double>(cellX) * step);
            for (auto cellZ = static_cast<std::int64_t>(firstZ);
                cellZ < static_cast<std::int64_t>(endZ);
                ++cellZ) {
                const float gz = static_cast<float>(static_cast<double>(cellZ) * step);
                scatterCell(gx, gz, static_cast<int>(cellX), static_cast<int>(cellZ));
            }
        }
    } else {
        for (float gx = desc.minX; gx < desc.maxX; gx += desc.step) {
            for (float gz = desc.minZ; gz < desc.maxZ; gz += desc.step) {
                scatterCell(
                    gx, gz, static_cast<int>(std::round(gx)), static_cast<int>(std::round(gz)));
            }
        }
    }
}

} // namespace detail

/// Scatter points across a terrain heightfield using a deterministic
/// grid-jittered algorithm.
///
/// For each grid cell (gx, gz) in [minX,maxX) × [minZ,maxZ) stepping by `step`:
///   1. r = hash2(round(gx), round(gz), seed + 31)
///   2. If r >= density, skip (density gate).
///   3. Jitter: ox = (hash2(gx, gz, seed+57) - 0.5) * step
///               oz = (hash2(gx, gz, seed+91) - 0.5) * step
///   4. x = gx + ox, z = gz + oz
///   5. y = rendered heightfield mesh surface at (x, z)
///   6. Reject if y < minY (below water).
///   7. Reject if slope > maxSlope (finite-difference normal magnitude).
///   8. Emit ScatterPoint{x, z, y, jitter=r}.
///
/// `out` is called for each accepted point: out(ScatterPoint). Any functor
/// with signature void(ScatterPoint) works (std::back_inserter into a vector,
/// a lambda counter, etc.).
template <typename OutputFn>
void terrainScatter(const TerrainHeightField& heightField, const ScatterDesc& desc, OutputFn out)
{
    detail::terrainScatterSampled(
        [&heightField](float x, float z, float& outHeight) {
            outHeight = heightField.sampleMeshHeight(x, z);
            return true;
        },
        [&heightField](float x, float z, float& outHeight) {
            outHeight = heightField.sample(x, z);
            return true;
        },
        desc,
        out);
}

/// Baked terrain overload. Failed or unavailable surface queries reject the
/// candidate; PCG never fabricates a placement height.
template <typename OutputFn>
void terrainScatter(const terrain::TerrainSurface& surface, const ScatterDesc& desc, OutputFn out)
{
    const auto sample = [&surface](float x, float z, float& outHeight) {
        const auto query = surface.sample(x, z);
        if (!query.ready())
            return false;
        outHeight = query.sample.height;
        return true;
    };
    detail::terrainScatterSampled(sample, sample, desc, out);
}

/// Count the number of points a scatter would produce without storing them.
/// Useful for pre-sizing the output buffer.
inline std::size_t countScatter(const TerrainHeightField& heightField, const ScatterDesc& desc)
{
    std::size_t count = 0u;
    terrainScatter(heightField, desc, [&count](const ScatterPoint&) {
        ++count;
    });
    return count;
}

inline std::size_t countScatter(const terrain::TerrainSurface& surface, const ScatterDesc& desc)
{
    std::size_t count = 0u;
    terrainScatter(surface, desc, [&count](const ScatterPoint&) {
        ++count;
    });
    return count;
}

} // namespace arp::core
