#pragma once

// TerrainFieldSet - derived terrain analysis fields computed from a height
// grid. Provides slope, curvature, flow accumulation, wetness (TWI), shore
// distance, and river mask for use by road pathfinding, surface weight
// generation, biome classification, and buildable-space detection.
//
// All fields are computed on a regular NxN grid sampled from the
// TerrainHeightField. The grid is caller-owned; this module produces a
// TerrainFieldSet by value (movable via std::vector moves).
//
// Hydrology is derived from the D8 flow algorithm in river_system.h rather
// than from the hydraulic erosion simulation. This is the pragmatic path:
// the erosion pre-bake produces height displacement only, so we derive
// flow/wetness from the final (eroded + landformed) heightfield.
//
// Plan reference: complete_procedural_scene_implementation_plan.md P3.

#include "pcg/river_system.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <queue>
#include <vector>

namespace arp::pcg {

/// A set of derived terrain fields on a regular grid.
struct TerrainFieldSet {
    int width = 0;
    int height = 0;
    float cellSize = 0.0f;
    float originX = 0.0f;
    float originZ = 0.0f;
    float waterLevel = 0.0f;

    /// Input height grid (row-major: index = z * width + x).
    std::vector<float> heights;

    /// Max neighbor slope (rise/run, dimensionless). Diagonal neighbors
    /// are scaled by 1/sqrt(2).
    std::vector<float> slope;

    /// Laplacian curvature = (sum of 4 orthogonal neighbors - 4*center)
    /// / cellSize². Positive = concave (depression), negative = convex
    /// (ridge).
    std::vector<float> curvature;

    /// D8 flow accumulation (upstream cell count). Cells with no upstream
    /// = 1.0.
    std::vector<float> flowAccum;

    /// P3 fix: D8 flow direction (steepest descent neighbor index, 0-7).
    /// Encoded as: 0=E, 1=SE, 2=S, 3=SW, 4=W, 5=NW, 6=N, 7=NE.
    /// 255 = no descent (pit/flat).
    std::vector<uint8_t> flowDirection;

    /// P3 fix: Thermal erosion displacement (height change from rockfall).
    /// Positive = material removed (erosion), negative = material deposited.
    std::vector<float> erosion;

    /// P3 fix: Deposition field (accumulated sediment from hydraulic transport).
    /// Positive = sediment deposited. Derived from flow transport.
    std::vector<float> deposition;

    /// Topographic Wetness Index = ln(flowAccum / (slope + epsilon)).
    /// Clamped to [0, 12]. High values = wet depressions and valley
    /// bottoms.
    std::vector<float> wetness;

    /// Distance (meters) to the nearest cell below waterLevel.
    /// Computed via multi-source BFS from all water cells. 0.0 for water
    /// cells themselves.
    std::vector<float> shoreDistance;

    /// River mask: 1 where flowAccum exceeds the river threshold.
    std::vector<uint8_t> riverMask;

    /// Total river cell count (for diagnostics).
    int riverCellCount = 0;

    // ── Bilinear samplers ──────────────────────────────────────────────

    /// Convert world (wx, wz) to grid index. Returns false if out of bounds.
    bool worldToGrid(float wx, float wz, int& gx, int& gz) const noexcept
    {
        gx = static_cast<int>((wx - originX) / cellSize);
        gz = static_cast<int>((wz - originZ) / cellSize);
        return gx >= 0 && gx < width && gz >= 0 && gz < height;
    }

    /// Bilinearly sample a field at world (wx, wz). Returns def if OOB.
    float sampleField(
        const std::vector<float>& field, float wx, float wz, float def = 0.0f) const noexcept
    {
        if (width < 2 || height < 2)
            return def;
        float fx = (wx - originX) / cellSize;
        float fz = (wz - originZ) / cellSize;
        if (fx < 0.0f)
            fx = 0.0f;
        if (fz < 0.0f)
            fz = 0.0f;
        if (fx > static_cast<float>(width - 1))
            fx = static_cast<float>(width - 1);
        if (fz > static_cast<float>(height - 1))
            fz = static_cast<float>(height - 1);
        const int x0 = static_cast<int>(fx);
        const int z0 = static_cast<int>(fz);
        const int x1 = (x0 + 1 < width) ? x0 + 1 : x0;
        const int z1 = (z0 + 1 < height) ? z0 + 1 : z0;
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);
        const float h00 = field[static_cast<std::size_t>(z0) * width + x0];
        const float h10 = field[static_cast<std::size_t>(z0) * width + x1];
        const float h01 = field[static_cast<std::size_t>(z1) * width + x0];
        const float h11 = field[static_cast<std::size_t>(z1) * width + x1];
        const float h0 = h00 * (1.0f - tx) + h10 * tx;
        const float h1 = h01 * (1.0f - tx) + h11 * tx;
        return h0 * (1.0f - tz) + h1 * tz;
    }

    float sampleHeight(float wx, float wz) const noexcept { return sampleField(heights, wx, wz); }

    float sampleSlope(float wx, float wz) const noexcept { return sampleField(slope, wx, wz); }

    float sampleWetness(float wx, float wz) const noexcept { return sampleField(wetness, wx, wz); }

    float sampleShoreDistance(float wx, float wz) const noexcept
    {
        return sampleField(shoreDistance, wx, wz);
    }

    float sampleCurvature(float wx, float wz) const noexcept
    {
        return sampleField(curvature, wx, wz);
    }

    bool isRiver(float wx, float wz) const noexcept
    {
        int gx, gz;
        if (!worldToGrid(wx, wz, gx, gz))
            return false;
        return riverMask[static_cast<std::size_t>(gz) * width + gx] != 0u;
    }
};

/// Compute all derived terrain fields from a height grid.
/// `heights` is row-major [width * height].
/// `waterLevel` defines the ocean/water surface for shore distance.
/// `riverThreshold` is the flow accumulation above which a cell is a river.
inline TerrainFieldSet computeTerrainFields(int width,
    int height,
    const float* heights,
    float cellSize,
    float originX,
    float originZ,
    float waterLevel,
    float riverThreshold = 50.0f) noexcept
{
    TerrainFieldSet fs;
    fs.width = width;
    fs.height = height;
    fs.cellSize = cellSize;
    fs.originX = originX;
    fs.originZ = originZ;
    fs.waterLevel = waterLevel;

    const std::size_t n = static_cast<std::size_t>(width) * height;
    fs.heights.resize(n);
    std::memcpy(fs.heights.data(), heights, n * sizeof(float));
    fs.slope.resize(n, 0.0f);
    fs.curvature.resize(n, 0.0f);
    fs.flowAccum.resize(n, 1.0f);
    fs.flowDirection.resize(n, 255u);
    fs.erosion.resize(n, 0.0f);
    fs.deposition.resize(n, 0.0f);
    fs.wetness.resize(n, 0.0f);
    fs.shoreDistance.resize(n, 0.0f);
    fs.riverMask.resize(n, 0u);

    // ── Slope + curvature ──────────────────────────────────────────────
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const int ci = z * width + x;
            const float ch = heights[ci];

            float maxSlope = 0.0f;
            float laplacian = 0.0f;
            int orthoCount = 0;
            float orthoSum = 0.0f;

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0)
                        continue;
                    const int nx = x + dx;
                    const int nz = z + dz;
                    if (nx < 0 || nx >= width || nz < 0 || nz >= height)
                        continue;
                    const int ni = nz * width + nx;
                    const float nh = heights[ni];

                    // Slope: max neighbor difference / effective distance
                    const bool diagonal = (dx != 0 && dz != 0);
                    const float dist = diagonal ? cellSize * 1.41421356f : cellSize;
                    const float s = std::abs(nh - ch) / dist;
                    if (s > maxSlope)
                        maxSlope = s;

                    // Curvature: accumulate orthogonal neighbors for Laplacian
                    if (!diagonal) {
                        orthoSum += nh;
                        ++orthoCount;
                    }
                }
            }
            fs.slope[ci] = maxSlope;
            if (orthoCount == 4) {
                fs.curvature[ci] = (orthoSum - 4.0f * ch) / (cellSize * cellSize);
            }
        }
    }

    // ── Flow accumulation (D8 from river_system.h) ─────────────────────
    auto flowMap = computeFlowDirections(width, height, heights);
    auto accum = computeFlowAccumulation(flowMap, heights);
    for (std::size_t i = 0; i < n; ++i) {
        fs.flowAccum[i] = accum[i];
        fs.flowDirection[i] = flowMap.directions[i];
    }

    // ── P3 fix: Thermal erosion + hydraulic deposition ─────────────────
    // Thermal erosion: on steep slopes, material moves downhill. The erosion
    // field records net height removed; deposition records where sediment
    // accumulates (in depressions and flat areas downstream).
    {
        // Thermal erosion: simple threshold-based transport.
        // Cells steeper than the repose angle lose material to downhill cells.
        const float kReposeSlope = 0.7f;  // ~35 degrees
        const float kErosionRate = 0.02f; // Small fraction per iteration
        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                const int ci = z * width + x;
                if (fs.slope[ci] < kReposeSlope)
                    continue;
                // Erode proportional to excess slope.
                const float excess = fs.slope[ci] - kReposeSlope;
                fs.erosion[ci] = excess * kErosionRate;
            }
        }
        // Hydraulic deposition: sediment from flow transport deposits in
        // low-slope, high-flow-accumulation cells (valley bottoms, plains).
        for (std::size_t i = 0; i < n; ++i) {
            const float s = fs.slope[i];
            const float a = fs.flowAccum[i];
            // Deposit where slope is low and flow is high.
            if (s < 0.1f && a > 5.0f) {
                fs.deposition[i] = std::min(a * 0.001f, 0.5f);
            }
        }
    }

    // ── River mask ─────────────────────────────────────────────────────
    auto rivers = extractRivers(accum, width, height, riverThreshold);
    fs.riverCellCount = 0;
    for (std::size_t i = 0; i < n; ++i) {
        fs.riverMask[i] = rivers[i];
        if (rivers[i])
            ++fs.riverCellCount;
    }

    // ── Wetness (TWI = ln(flowAccum / (slope + epsilon))) ──────────────
    for (std::size_t i = 0; i < n; ++i) {
        const float s = fs.slope[i];
        const float a = fs.flowAccum[i];
        if (s > 0.0001f && a > 0.0f) {
            float twi = std::log(a / (s + 0.001f));
            if (twi < 0.0f)
                twi = 0.0f;
            if (twi > 12.0f)
                twi = 12.0f;
            fs.wetness[i] = twi;
        } else {
            // Flat or pit cell -> maximum wetness
            fs.wetness[i] = 12.0f;
        }
    }

    // ── Shore distance (multi-source BFS from water cells) ─────────────
    // Water cells = height < waterLevel. BFS computes grid-cell distance,
    // then we multiply by cellSize for meters.
    {
        std::queue<int> q;
        // Initialize: water cells get distance 0, land cells get "infinity".
        constexpr float kInf = 1e30f;
        for (std::size_t i = 0; i < n; ++i) {
            if (heights[i] < waterLevel) {
                fs.shoreDistance[i] = 0.0f;
                q.push(static_cast<int>(i));
            } else {
                fs.shoreDistance[i] = kInf;
            }
        }
        // BFS with 8-neighbour (diagonal = 1.414 * cellSize)
        while (!q.empty()) {
            const int ci = q.front();
            q.pop();
            const int cx = ci % width;
            const int cz = ci / width;
            const float cd = fs.shoreDistance[ci];

            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0)
                        continue;
                    const int nx = cx + dx;
                    const int nz = cz + dz;
                    if (nx < 0 || nx >= width || nz < 0 || nz >= height)
                        continue;
                    const int ni = nz * width + nx;
                    const bool diagonal = (dx != 0 && dz != 0);
                    const float stepDist = diagonal ? cellSize * 1.41421356f : cellSize;
                    const float nd = cd + stepDist;
                    if (nd < fs.shoreDistance[ni]) {
                        fs.shoreDistance[ni] = nd;
                        q.push(ni);
                    }
                }
            }
        }
    }

    return fs;
}

} // namespace arp::pcg
