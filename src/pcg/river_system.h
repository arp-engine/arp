#pragma once

// River system — D8 flow-direction, flow accumulation, river extraction,
// and lake-basin filling for procedural hydrology.
//
// Operates on a discrete height grid (row-major float array).  The D8
// algorithm assigns each cell a flow direction toward the steepest of its
// 8 neighbours; flow accumulation recursively sums upstream contributions;
// rivers are extracted where accumulation exceeds a threshold.
//
// Lake basins are identified by finding depressions (cells with no downhill
// neighbour) and flooding them to their spill elevation.
//
// All functions are deterministic and operate on caller-owned buffers.

#include <algorithm>
#include <cstdint>
#include <queue>
#include <vector>

namespace arp::pcg {

/// D8 flow direction per cell.  Values 0–7 index into the 8-neighbour
/// offsets below; 255 means flat / pit (no downhill neighbour).
///  0:N  1:NE  2:E  3:SE  4:S  5:SW  6:W  7:NW
struct FlowMap {
    int width = 0, height = 0;
    std::vector<uint8_t> directions; // row-major: y*width+x

    // 8-neighbour offsets (dx, dz).
    static constexpr int dx(int dir) noexcept
    {
        constexpr int v[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        return v[dir & 7];
    }

    static constexpr int dz(int dir) noexcept
    {
        constexpr int v[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        return v[dir & 7];
    }

    static constexpr float diagScale(int dir) noexcept
    {
        return (dir == 1 || dir == 3 || dir == 5 || dir == 7) ? 0.707106781f : 1.0f;
    }

    static constexpr uint8_t PIT = 255;
};

/// Compute D8 flow directions from a height grid.
/// `width` × `height` grid, row-major.
inline FlowMap computeFlowDirections(int width, int height, const float* heights) noexcept
{
    FlowMap fm;
    fm.width = width;
    fm.height = height;
    fm.directions.resize(static_cast<size_t>(width) * height, FlowMap::PIT);

    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const int ci = z * width + x;
            const float ch = heights[ci];

            int bestDir = -1;
            float bestSlope = 0.0f;

            for (int d = 0; d < 8; ++d) {
                const int nx = x + FlowMap::dx(d);
                const int nz = z + FlowMap::dz(d);
                if (nx < 0 || nx >= width || nz < 0 || nz >= height)
                    continue;

                const int ni = nz * width + nx;
                const float nh = heights[ni];
                const float slope = (ch - nh) / FlowMap::diagScale(d);

                if (slope > bestSlope) {
                    bestSlope = slope;
                    bestDir = d;
                }
            }

            if (bestDir >= 0 && bestSlope > 0.0f) {
                fm.directions[ci] = static_cast<uint8_t>(bestDir);
            }
        }
    }
    return fm;
}

/// Flow accumulation: each cell gets 1 + sum of all upstream contributions.
/// Returns a vector of accumulation values (row-major).
inline std::vector<float> computeFlowAccumulation(
    const FlowMap& fm, const float* /*heights*/) noexcept
{
    const int w = fm.width, h = fm.height;
    const size_t n = static_cast<size_t>(w) * h;
    std::vector<float> accum(n, 1.0f);

    // Build reverse adjacency: for each cell, add itself as upstream of its
    // downstream neighbour.  Then do a topological traversal.

    // Count in-degree (number of upstream neighbours per cell).
    std::vector<int> inDegree(n, 0);
    std::vector<std::vector<int>> upstream(n); // upstream cell indices per cell

    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            const int ci = z * w + x;
            const auto dir = fm.directions[ci];
            if (dir == FlowMap::PIT)
                continue;

            const int nx = x + FlowMap::dx(dir);
            const int nz = z + FlowMap::dz(dir);
            if (nx < 0 || nx >= w || nz < 0 || nz >= h)
                continue;

            const int ni = nz * w + nx;
            upstream[ni].push_back(ci);
            inDegree[ni]++;
        }
    }

    // Topological sort: process cells with no upstream first (leaf cells).
    std::queue<int> q;
    for (int i = 0; i < static_cast<int>(n); ++i) {
        if (inDegree[i] == 0)
            q.push(i);
    }

    int processed = 0;
    while (!q.empty() && processed < static_cast<int>(n)) {
        int ci = q.front();
        q.pop();
        processed++;

        const auto dir = fm.directions[ci];
        if (dir != FlowMap::PIT) {
            const int z = ci / w, x = ci % w;
            const int nx = x + FlowMap::dx(dir);
            const int nz = z + FlowMap::dz(dir);
            if (nx >= 0 && nx < w && nz >= 0 && nz < h) {
                const int ni = nz * w + nx;
                accum[ni] += accum[ci];
                inDegree[ni]--;
                if (inDegree[ni] == 0)
                    q.push(ni);
            }
        }

        // Drain upstream neighbours too.
        for (int up : upstream[ci]) {
            inDegree[up]--;
            if (inDegree[up] == 0)
                q.push(up);
        }
    }

    return accum;
}

/// River paths: cells whose flow accumulation exceeds a threshold.
/// Returns a mask (row-major, 0 or 1).
inline std::vector<uint8_t> extractRivers(
    const std::vector<float>& accum, int width, int height, float threshold) noexcept
{
    const size_t n = static_cast<size_t>(width) * height;
    std::vector<uint8_t> mask(n, 0);

    for (size_t i = 0; i < n; ++i) {
        if (accum[i] >= threshold)
            mask[i] = 1;
    }
    return mask;
}

/// Lake-basin detection: finds all depressions (pits) and fills them to
/// their spill elevation. Returns a mask of lake cells and the filled height.
struct LakeResult {
    std::vector<uint8_t> lakeMask;    // 1 = lake cell
    std::vector<float> filledHeights; // heights after filling
};

inline LakeResult fillLakeBasins(const float* heights,
    const FlowMap& fm,
    int width,
    int height,
    float maxLakeDepth = 20.0f) noexcept
{
    const size_t n = static_cast<size_t>(width) * height;
    LakeResult lr;
    lr.lakeMask.resize(n, 0);
    lr.filledHeights.assign(heights, heights + n);

    // Find pit cells.
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const int ci = z * width + x;
            if (fm.directions[ci] != FlowMap::PIT)
                continue;

            // Flood-fill the basin from this pit.
            // Find the spill elevation (lowest edge cell).
            float spillElev = heights[ci] + maxLakeDepth;
            std::vector<int> basinCells;
            std::vector<uint8_t> visited(n, 0);
            std::queue<int> floodQ;
            floodQ.push(ci);
            visited[ci] = 1;

            while (!floodQ.empty()) {
                int cell = floodQ.front();
                floodQ.pop();
                int cz = cell / width, cx = cell % width;
                basinCells.push_back(cell);

                for (int d = 0; d < 8; ++d) {
                    int nx = cx + FlowMap::dx(d);
                    int nz = cz + FlowMap::dz(d);
                    if (nx < 0 || nx >= width || nz < 0 || nz >= height)
                        continue;

                    int ni = nz * width + nx;
                    float nh = lr.filledHeights[ni];

                    // Check if this neighbour is a potential spill point.
                    if (nh > lr.filledHeights[cell]) {
                        spillElev = std::min(spillElev, nh);
                    }

                    if (!visited[ni] && nh <= lr.filledHeights[ci] + maxLakeDepth) {
                        visited[ni] = 1;
                        floodQ.push(ni);
                    }
                }
            }

            // Fill all basin cells to spill elevation (minus small margin).
            float fillHeight = spillElev - 0.01f;
            if (fillHeight > heights[ci]) {
                for (int cell : basinCells) {
                    if (lr.filledHeights[cell] < fillHeight) {
                        lr.filledHeights[cell] = fillHeight;
                        lr.lakeMask[cell] = 1;
                    }
                }
            }
        }
    }
    return lr;
}

} // namespace arp::pcg
