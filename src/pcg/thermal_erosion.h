#pragma once

// Thermal erosion — iterative material transport based on slope angle.
//
// For each cell, if the height difference to a neighbour exceeds the talus
// threshold (tan(talusAngle) × cellSize), excess material is moved from the
// higher cell to the lower one.  Repeated iterations produce natural talus
// slopes, sharp ridges, and scree fields.
//
// Based on the standard thermal erosion algorithm used in terrain generation
// (Musgrave et al., "The Synthesis and Rendering of Eroded Fractal Terrains").
//
// Usage:
//   std::vector<float> grid(w * h);
//   // ... fill grid with initial heights ...
//   thermalErode(w, h, grid.data(), iterations, cellSize, talusAngle);

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace arp::pcg {

struct ThermalErosionParams {
    int iterations = 16;      // erosion passes
    float cellSize = 1.0f;    // world-space size of one grid cell
    float talusAngle = 35.0f; // angle of repose (degrees); 35° is typical rock

    float talusThreshold() const noexcept
    {
        return std::tan(talusAngle * 3.14159265f / 180.0f) * cellSize;
    }
};

/// Thermal erosion on a 2D height grid.
/// `width` × `height` grid, stored row-major (y * width + x).
/// The grid is modified in place.
inline void thermalErode(
    int width, int height, float* grid, const ThermalErosionParams& p = {}) noexcept
{
    if (width < 3 || height < 3)
        return;
    const float threshold = p.talusThreshold();

    // 8-neighbour offsets.
    constexpr int dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr int dz[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    constexpr float diagScale = 0.707106781f; // 1/sqrt(2)

    for (int iter = 0; iter < p.iterations; ++iter) {
        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                const int ci = z * width + x;
                const float ch = grid[ci];

                for (int n = 0; n < 8; ++n) {
                    const int nx = x + dx[n];
                    const int nz = z + dz[n];
                    if (nx < 0 || nx >= width || nz < 0 || nz >= height)
                        continue;

                    const int ni = nz * width + nx;
                    const float nh = grid[ni];
                    const float diff = ch - nh;
                    // True distance for diagonals is √2 × cellSize.
                    const float dist =
                        (dx[n] != 0 && dz[n] != 0) ? p.cellSize * 1.414213562f : p.cellSize;
                    const float maxDiff = threshold * (dist / p.cellSize);

                    if (diff > maxDiff) {
                        // Move excess material downhill.
                        float excess = diff - maxDiff;
                        excess *= 0.5f; // damping
                        grid[ci] -= excess;
                        grid[ni] += excess;
                    }
                }
            }
        }
    }
}

} // namespace arp::pcg
