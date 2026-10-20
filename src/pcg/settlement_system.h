#pragma once
// Phase 8: Settlement placement and building layout.
#include "pcg/simplex_noise.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace arp::pcg {

struct BuildingPlacement {
    float x, z;
    float w, d;
    int type;
}; // type: 0=house,1=smith,2=inn,3=hall

struct Settlement {
    float cx, cz;
    float radius;
    int tier;
    std::vector<BuildingPlacement> buildings;
};

/// Score a location for settlement suitability (higher = better).
inline float settlementScore(
    float wx, float wz, float height, float slope, float waterDist, float roadDist) noexcept
{
    float s = 0.0f;
    // Flat terrain +1
    if (slope < 0.15f)
        s += 1.0f;
    else if (slope < 0.3f)
        s += 0.5f;
    // Near water +0.5
    if (waterDist < 20.0f && waterDist > 2.0f)
        s += 0.5f;
    // Near road +0.3
    if (roadDist < 30.0f)
        s += 0.3f;
    // Avoid extreme heights
    if (height > -1.0f && height < 15.0f)
        s += 0.5f;
    return s;
}

/// Place settlements on a grid of candidate sites, selecting top N.
inline std::vector<Settlement> placeSettlements(int worldW,
    int worldH,
    float cellSize,
    const float* heights,
    const float* waterMask,
    std::uint32_t seed,
    int maxSettlements = 8) noexcept
{
    std::vector<Settlement> result;
    const int step = 16; // check every 16 cells
    std::vector<std::pair<float, std::pair<float, float>>> candidates;

    for (int z = step; z < worldH - step; z += step) {
        for (int x = step; x < worldW - step; x += step) {
            int ci = z * worldW + x;
            float h = heights[ci];
            float wx = (x - worldW / 2.0f) * cellSize;
            float wz = (z - worldH / 2.0f) * cellSize;
            // Simple slope estimate
            float dx = std::abs(heights[ci + 1] - heights[ci - 1]) / cellSize;
            float dz = std::abs(heights[ci + worldW] - heights[ci - worldW]) / cellSize;
            float slope = std::max(dx, dz);
            float waterDist = waterMask ? waterMask[ci] : 100.0f;
            float score = settlementScore(wx, wz, h, slope, waterDist, 100.0f);
            if (score > 1.0f)
                candidates.push_back({score, {wx, wz}});
        }
    }
    std::sort(candidates.begin(), candidates.end(), std::greater<>());
    for (int i = 0; i < std::min(maxSettlements, (int)candidates.size()); ++i) {
        Settlement s;
        s.cx = candidates[i].second.first;
        s.cz = candidates[i].second.second;
        s.radius = 10.0f + simplex2D(s.cx * 0.1f, s.cz * 0.1f, seed + i) * 10.0f;
        s.tier = (i < 2) ? 2 : ((i < 5) ? 1 : 0);
        // Place buildings around center
        int bcount = 4 + s.tier * 3;
        for (int b = 0; b < bcount; ++b) {
            float angle = (float)b / bcount * 6.283185307f
                + simplex2D(s.cx + b * 0.1f, s.cz + b * 0.1f, seed + 100u) * 0.5f;
            float dist =
                s.radius * (0.3f + simplex2D(s.cx + b * 0.2f, s.cz + b * 0.2f, seed + 200u) * 0.5f);
            BuildingPlacement bp;
            bp.x = s.cx + std::cos(angle) * dist;
            bp.z = s.cz + std::sin(angle) * dist;
            bp.w = 2.0f + simplex2D(bp.x, bp.z, seed + 300u) * 3.0f;
            bp.d = 2.0f + simplex2D(bp.x + 0.5f, bp.z + 0.5f, seed + 400u) * 3.0f;
            bp.type = (b < 3) ? 0 : (b < s.tier + 4 ? (b % 3 + 1) : 0);
            s.buildings.push_back(bp);
        }
        result.push_back(s);
    }
    return result;
}

} // namespace arp::pcg
