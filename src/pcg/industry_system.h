#pragma once
// Phase 12: Agriculture & Industry placement around settlements.
#include "pcg/simplex_noise.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace arp::pcg {

enum class IndustryType : uint8_t {
    Farmland = 0,
    Orchard = 1,
    Mine = 2,
    Quarry = 3,
    LumberCamp = 4,
    FishingDock = 5
};

struct IndustryPlacement {
    float x, z;
    IndustryType type;
    float extent;
    int settlementId;
};

/// Place agriculture and industry around settlement positions.
/// `heights` row-major, `settlementXs/Zs` are settlement centers.
inline std::vector<IndustryPlacement> placeIndustry(int width,
    int height,
    float cellSize,
    const float* heights,
    const float* settlementXs,
    const float* settlementZs,
    int numSettlements,
    std::uint32_t seed) noexcept
{
    std::vector<IndustryPlacement> result;
    for (int s = 0; s < numSettlements; ++s) {
        float sx = settlementXs[s], sz = settlementZs[s];

        // Farmland: 2-4 fields in a ring around settlement
        int farms = 2 + static_cast<int>(simplex2D(sx * 0.1f, sz * 0.1f, seed + s * 10u) * 3.0f);
        for (int f = 0; f < farms; ++f) {
            float angle = (float)f / farms * 6.283185307f
                + simplex2D(sx + f * 0.1f, sz + f * 0.1f, seed + 100u) * 0.3f;
            float dist = 15.0f + simplex2D(sx + f * 0.2f, sz + f * 0.2f, seed + 200u) * 10.0f;
            float fx = sx + std::cos(angle) * dist, fz = sz + std::sin(angle) * dist;
            int cx = static_cast<int>((fx + width * cellSize * 0.5f) / cellSize);
            int cz = static_cast<int>((fz + height * cellSize * 0.5f) / cellSize);
            if (cx < 0 || cx >= width || cz < 0 || cz >= height)
                continue;
            float h = heights[cz * width + cx];
            if (h < -0.5f || h > 8.0f)
                continue;
            // Check slope
            float slope = 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (cx + dx < 0 || cx + dx >= width || cz + dz < 0 || cz + dz >= height)
                        continue;
                    slope = std::max(
                        slope, std::abs(h - heights[(cz + dz) * width + (cx + dx)]) / cellSize);
                }
            if (slope < 0.15f)
                result.push_back({fx,
                    fz,
                    IndustryType::Farmland,
                    8.0f + simplex2D(fx, fz, seed + 300u) * 5.0f,
                    s});
        }

        // Mine: near elevated terrain
        float mx = sx + simplex2D(sx * 0.3f, sz, seed + 400u) * 30.0f;
        float mz = sz + simplex2D(sx, sz * 0.3f, seed + 500u) * 30.0f;
        int mcx = static_cast<int>((mx + width * cellSize * 0.5f) / cellSize);
        int mcz = static_cast<int>((mz + height * cellSize * 0.5f) / cellSize);
        if (mcx >= 0 && mcx < width && mcz >= 0 && mcz < height) {
            float mh = heights[mcz * width + mcx];
            if (mh > 3.0f)
                result.push_back({mx, mz, IndustryType::Mine, 5.0f, s});
        }
    }
    return result;
}

} // namespace arp::pcg
