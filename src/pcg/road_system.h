#pragma once
// Phase 7: Road network generation via A* on cost map.
// P3: Extended cost map with wetness/river/shore fields.
#include "pcg/simplex_noise.h"
#include "pcg/terrain_fields.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace arp::pcg {

struct Waypoint {
    float x, z;
};

struct RoadPath {
    std::vector<Waypoint> points;
    float width;
};

struct CostMap {
    int w, h;
    float cellSize, originX, originZ;
    std::vector<float> cost; // lower = easier to travel

    float sample(float wx, float wz) const noexcept
    {
        int cx = static_cast<int>((wx - originX) / cellSize);
        int cz = static_cast<int>((wz - originZ) / cellSize);
        if (cx < 0 || cx >= w || cz < 0 || cz >= h)
            return 1e9f;
        return cost[cz * w + cx];
    }
};

/// Build a cost map from terrain heights (row-major).  Slope penalty + water
/// avoidance (low elevation = water = high cost).
inline CostMap buildCostMap(int w,
    int h,
    const float* heights,
    float cellSize,
    float originX,
    float originZ,
    float waterLevel = -0.2f,
    float slopeWeight = 3.0f) noexcept
{
    CostMap cm{w, h, cellSize, originX, originZ, std::vector<float>(w * h, 1.0f)};
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            int ci = z * w + x;
            float hh = heights[ci];
            // Water penalty
            if (hh < waterLevel + 0.5f)
                cm.cost[ci] = 20.0f;
            // Slope penalty: sample neighbours
            float maxSlope = 0.0f;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0)
                        continue;
                    int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nx >= w || nz < 0 || nz >= h)
                        continue;
                    float slope = std::abs(heights[nz * w + nx] - hh) / cellSize;
                    if (slope > maxSlope)
                        maxSlope = slope;
                }
            cm.cost[ci] += maxSlope * slopeWeight;
        }
    return cm;
}

/// P3: Build a cost map enriched with terrain field data (wetness, river
/// crossings, shore distance). Produces higher cost for swamps, river
/// crossings, and beach cells, guiding roads along dry, gentle slopes.
inline CostMap buildCostMapWithFields(int w,
    int h,
    const float* heights,
    const TerrainFieldSet& fields,
    float cellSize,
    float originX,
    float originZ,
    float waterLevel,
    float slopeWeight = 3.0f,
    float wetnessWeight = 2.0f,
    float riverCrossingCost = 50.0f) noexcept
{
    CostMap cm{w, h, cellSize, originX, originZ, std::vector<float>(w * h, 1.0f)};
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            int ci = z * w + x;
            float hh = heights[ci];

            // Water penalty: hard avoid cells below water level.
            if (hh < waterLevel + 0.5f) {
                cm.cost[ci] = 100.0f;
            }

            // Slope penalty: sample neighbours.
            float maxSlope = 0.0f;
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dz == 0)
                        continue;
                    int nx = x + dx, nz = z + dz;
                    if (nx < 0 || nx >= w || nz < 0 || nz >= h)
                        continue;
                    float slope = std::abs(heights[nz * w + nx] - hh) / cellSize;
                    if (slope > maxSlope)
                        maxSlope = slope;
                }
            }
            cm.cost[ci] += maxSlope * slopeWeight;

            // Wetness penalty: soft avoid swamps and wet depressions.
            cm.cost[ci] += fields.wetness[ci] * wetnessWeight;

            // River crossing: crossable but expensive.
            if (fields.riverMask[ci]) {
                cm.cost[ci] += riverCrossingCost;
            }

            // Shore proximity: avoid building roads on narrow beaches.
            if (fields.shoreDistance[ci] < 5.0f && hh >= waterLevel + 0.5f) {
                cm.cost[ci] += 5.0f;
            }
        }
    }
    return cm;
}

/// A* pathfinding on cost map from (sx,sz) to (tx,tz).  Returns waypoints.
inline RoadPath findPath(const CostMap& cm, float sx, float sz, float tx, float tz) noexcept
{
    int si = static_cast<int>((sz - cm.originZ) / cm.cellSize) * cm.w
        + static_cast<int>((sx - cm.originX) / cm.cellSize);
    int ti = static_cast<int>((tz - cm.originZ) / cm.cellSize) * cm.w
        + static_cast<int>((tx - cm.originX) / cm.cellSize);
    int n = cm.w * cm.h;
    std::vector<float> g(n, 1e9f), f(n, 1e9f);
    std::vector<int> parent(n, -1);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    g[si] = 0;
    f[si] = std::sqrt((sx - tx) * (sx - tx) + (sz - tz) * (sz - tz));
    pq.push({f[si], si});

    while (!pq.empty()) {
        auto [_, ci] = pq.top();
        pq.pop();
        if (ci == ti)
            break;
        int cx = ci % cm.w, cz = ci / cm.w;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dz == 0)
                    continue;
                int nx = cx + dx, nz = cz + dz;
                if (nx < 0 || nx >= cm.w || nz < 0 || nz >= cm.h)
                    continue;
                int ni = nz * cm.w + nx;
                float stepCost = cm.cost[ni] * (dx != 0 && dz != 0 ? 1.414f : 1.0f);
                float ng = g[ci] + stepCost;
                if (ng < g[ni]) {
                    g[ni] = ng;
                    parent[ni] = ci;
                    float hx = cm.originX + nx * cm.cellSize, hz = cm.originZ + nz * cm.cellSize;
                    f[ni] = ng + std::sqrt((hx - tx) * (hx - tx) + (hz - tz) * (hz - tz));
                    pq.push({f[ni], ni});
                }
            }
    }
    RoadPath rp;
    rp.width = 1.5f;
    if (parent[ti] < 0 && ti != si)
        return rp;
    for (int c = ti; c >= 0; c = parent[c]) {
        int cx = c % cm.w, cz = c / cm.w;
        rp.points.push_back({cm.originX + cx * cm.cellSize, cm.originZ + cz * cm.cellSize});
        if (c == si)
            break;
    }
    std::reverse(rp.points.begin(), rp.points.end());
    return rp;
}

} // namespace arp::pcg
