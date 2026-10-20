#pragma once
// Phase 14: Natural landmark detection — waterfalls, arches, giant trees, etc.
#include "pcg/simplex_noise.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::pcg {

enum class LandmarkType : uint8_t {
    Waterfall = 0,
    NaturalArch = 1,
    GiantTree = 2,
    HotSpring = 3,
    Crater = 4
};

struct Landmark {
    float x, z;
    LandmarkType type;
    std::string name;
};

/// Detect natural landmarks from terrain height and river data.
/// `heights` row-major, `riverMask` same size (1=river from Phase 3).
inline std::vector<Landmark> detectLandmarks(int width,
    int height,
    float cellSize,
    const float* heights,
    const uint8_t* riverMask,
    std::uint32_t seed) noexcept
{
    std::vector<Landmark> result;

    for (int z = 2; z < height - 2; ++z) {
        for (int x = 2; x < width - 2; ++x) {
            int ci = z * width + x;
            float h = heights[ci];

            // Waterfall: river cell on steep slope
            if (riverMask && riverMask[ci]) {
                float dh = std::abs(heights[(z + 1) * width + x] - heights[(z - 1) * width + x])
                    / cellSize;
                if (dh > 0.3f) {
                    Landmark lm;
                    lm.x = (x - width / 2.0f) * cellSize;
                    lm.z = (z - height / 2.0f) * cellSize;
                    lm.type = LandmarkType::Waterfall;
                    lm.name = "Waterfall";
                    result.push_back(lm);
                }
            }

            // Crater: local depression
            int localMin = 1;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                    if (heights[(z + dz) * width + (x + dx)] < h)
                        localMin = 0;
            if (localMin && h > 2.0f && simplex2D(h * 0.1f, 0.5f, seed) < 0.02f) {
                Landmark lm;
                lm.x = (x - width / 2.0f) * cellSize;
                lm.z = (z - height / 2.0f) * cellSize;
                lm.type = LandmarkType::Crater;
                lm.name = "Crater";
                result.push_back(lm);
            }

            // Giant tree: flat forest area, random placement
            if (h > 0.5f && h < 8.0f && !(riverMask && riverMask[ci])) {
                float dh = std::abs(heights[(z + 1) * width + x] - heights[(z - 1) * width + x])
                    / cellSize;
                if (dh < 0.1f && simplex2D(h * 0.5f, x * 0.1f, seed + 100u) < 0.005f) {
                    Landmark lm;
                    lm.x = (x - width / 2.0f) * cellSize;
                    lm.z = (z - height / 2.0f) * cellSize;
                    lm.type = LandmarkType::GiantTree;
                    lm.name = "Giant Tree";
                    result.push_back(lm);
                }
            }

            // Hot spring: basin at moderate elevation
            if (localMin && h > 1.0f && h < 5.0f
                && simplex2D(h * 1.0f, z * 0.1f, seed + 200u) < 0.01f) {
                Landmark lm;
                lm.x = (x - width / 2.0f) * cellSize;
                lm.z = (z - height / 2.0f) * cellSize;
                lm.type = LandmarkType::HotSpring;
                lm.name = "Hot Spring";
                result.push_back(lm);
            }
        }
    }
    return result;
}

} // namespace arp::pcg
