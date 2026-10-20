#pragma once

// Voronoi / Worley cellular noise — deterministic, seed-driven.
//
// Voronoi noise divides space into cells around pseudo-random feature points.
// For any input coordinate, it returns the distance to the nearest feature
// point (F1), or the distance between nearest and second-nearest (F2 - F1,
// the "edge" distance — zero on cell boundaries, increasing toward centers).
//
// Uses a grid-based approach: each cell has one feature point placed by
// hashing the cell coordinate.  The search window covers the 3×3 (2D) or
// 3×3×3 (3D) neighbourhood of grid cells.
//
// All functions are deterministic given a seed.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace arp::pcg {

namespace voronoi_detail {

// Mix two 32-bit ints into one (same contract as imul32 in deterministic_hash.h).
inline std::uint32_t mix32(std::uint32_t a, std::uint32_t b) noexcept
{
    auto imul = [](std::uint32_t x, std::uint32_t y) -> std::uint32_t {
        return x * y;
    };
    std::uint32_t h = imul(a ^ imul(b, 374761393u), 668265263u);
    h ^= h >> 13;
    h = imul(h, 1274126177u);
    h ^= h >> 16;
    return h;
}

// Deterministic float in [0, 1) from a 32-bit hash.
inline float hashFloat(std::uint32_t h) noexcept
{
    return static_cast<float>(h & 0x7fffffff) / 2147483648.0f;
}

} // namespace voronoi_detail

/// 2D Voronoi: returns the distance to the nearest feature point (F1) in [0, 1].
/// `x`, `z` are the sample coordinates; `seed` deterministically places points.
/// The distance is normalised so typical maxima are ~0.5–0.8.
inline float voronoi2D(float x, float z, std::uint32_t seed = 0u) noexcept
{
    using namespace voronoi_detail;

    int cx = static_cast<int>(std::floor(x));
    int cz = static_cast<int>(std::floor(z));

    float minDist = std::numeric_limits<float>::max();

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int gx = cx + dx;
            int gz = cz + dz;

            std::uint32_t h = mix32(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gz));
            h = mix32(h, seed);

            // Feature point within this grid cell.
            float px = static_cast<float>(gx) + hashFloat(h);
            float pz = static_cast<float>(gz) + hashFloat(h + 1u);

            float dxv = x - px;
            float dzv = z - pz;
            float d = dxv * dxv + dzv * dzv;
            if (d < minDist)
                minDist = d;
        }
    }

    return std::sqrt(minDist) * 0.6f; // scale to roughly [0, 1]
}

/// 2D Voronoi edge distance (F2 - F1): zero on cell boundaries, positive inside.
/// Creates a network-like pattern useful for cracks, cell walls, and ridges.
inline float voronoiEdge2D(float x, float z, std::uint32_t seed = 0u) noexcept
{
    using namespace voronoi_detail;

    int cx = static_cast<int>(std::floor(x));
    int cz = static_cast<int>(std::floor(z));

    float f1 = std::numeric_limits<float>::max();
    float f2 = std::numeric_limits<float>::max();

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            int gx = cx + dx;
            int gz = cz + dz;

            std::uint32_t h = mix32(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gz));
            h = mix32(h, seed);

            float px = static_cast<float>(gx) + hashFloat(h);
            float pz = static_cast<float>(gz) + hashFloat(h + 1u);

            float dxv = x - px;
            float dzv = z - pz;
            float d = dxv * dxv + dzv * dzv;

            if (d < f1) {
                f2 = f1;
                f1 = d;
            } else if (d < f2) {
                f2 = d;
            }
        }
    }

    // Return clamped difference: ~0 at borders, ~1 near centers.
    float diff = std::sqrt(f2) - std::sqrt(f1);
    return std::min(diff * 2.0f, 1.0f);
}

/// 3D Voronoi: F1 distance in [0, 1].
inline float voronoi3D(float x, float y, float z, std::uint32_t seed = 0u) noexcept
{
    using namespace voronoi_detail;

    int cx = static_cast<int>(std::floor(x));
    int cy = static_cast<int>(std::floor(y));
    int cz = static_cast<int>(std::floor(z));

    float minDist = std::numeric_limits<float>::max();

    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int gx = cx + dx;
                int gy = cy + dy;
                int gz = cz + dz;

                std::uint32_t h =
                    mix32(static_cast<std::uint32_t>(gx), static_cast<std::uint32_t>(gz));
                h = mix32(h, static_cast<std::uint32_t>(gy));
                h = mix32(h, seed);

                float px = static_cast<float>(gx) + hashFloat(h);
                float py = static_cast<float>(gy) + hashFloat(h + 1u);
                float pz = static_cast<float>(gz) + hashFloat(h + 2u);

                float dxv = x - px;
                float dyv = y - py;
                float dzv = z - pz;
                float d = dxv * dxv + dyv * dyv + dzv * dzv;
                if (d < minDist)
                    minDist = d;
            }
        }
    }

    return std::sqrt(minDist) * 0.5f;
}

} // namespace arp::pcg
