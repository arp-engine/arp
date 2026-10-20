#pragma once
// Phase 15: Ocean & Coastline — world boundary and beach/slope transition.
#include <cmath>
#include <cstdint>
#include <vector>

namespace arp::pcg {

enum class CoastType : uint8_t {
    DeepWater = 0,
    ShallowWater = 1,
    Beach = 2,
    Cliff = 3,
    Inland = 4
};

struct CoastResult {
    std::vector<CoastType> types;
    float oceanLevel;
    float beachWidth;
};

/// Classify each cell as ocean/beach/cliff/inland based on height and slope.
/// `heights` row-major, `width`×`height`.
/// Returns a CoastType mask and the ocean water level.
inline CoastResult classifyCoast(
    int width, int height, const float* heights, float cellSize, float halfExtent) noexcept
{
    CoastResult r;
    r.types.resize(width * height, CoastType::Inland);
    r.oceanLevel = -1.5f;
    r.beachWidth = 15.0f;

    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            int ci = z * width + x;
            float h = heights[ci];
            float wx = (x - width / 2.0f) * cellSize;
            float wz = (z - height / 2.0f) * cellSize;
            float distFromCenter = std::sqrt(wx * wx + wz * wz);

            if (distFromCenter > halfExtent * 0.85f) {
                // Outer ring: force ocean
                if (h < r.oceanLevel + 3.0f)
                    r.types[ci] = CoastType::DeepWater;
                else if (h < r.oceanLevel + 6.0f)
                    r.types[ci] = CoastType::ShallowWater;
                else if (h < r.oceanLevel + 10.0f)
                    r.types[ci] = CoastType::Beach;
                else
                    r.types[ci] = CoastType::Cliff;
            } else {
                // Interior terrain — check natural water level
                if (h < r.oceanLevel)
                    r.types[ci] = CoastType::DeepWater;
                else if (h < r.oceanLevel + 1.0f)
                    r.types[ci] = CoastType::ShallowWater;
                else if (h < r.oceanLevel + 1.5f)
                    r.types[ci] = CoastType::Beach;
            }
        }
    }
    return r;
}

} // namespace arp::pcg
