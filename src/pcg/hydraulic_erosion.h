#pragma once

// Hydraulic erosion — particle-based simulation of water flow and sediment
// transport across a height grid.
//
// Each "droplet" starts at a random position and travels downhill, eroding
// material (sediment capacity increases with slope × speed) and depositing
// sediment where the slope flattens or the droplet evaporates.
//
// Based on the algorithm by Hans Theobald Beyer ("Implementation of a method
// for hydraulic erosion", 2015), simplified for real-time terrain generation.
//
// Usage:
//   std::vector<float> grid(w * h);
//   // ... fill grid with initial heights ...
//   hydraulicErode(w, h, grid.data(), numDroplets, seed);

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace arp::pcg {

struct HydraulicErosionParams {
    int numDroplets = 10000;       // water droplets to simulate
    int maxSteps = 64;             // max steps per droplet before evaporation
    float inertia = 0.05f;         // velocity persistence (0 = no inertia)
    float sedimentCapacity = 4.0f; // how much sediment water can carry
    float erosionRate = 0.3f;      // how fast sediment erodes
    float depositRate = 0.3f;      // how fast sediment deposits
    float evaporationRate = 0.01f; // water volume lost per step
    float minSlope = 0.01f;        // stop threshold for flat terrain

    /// Simple deterministic PRNG for droplet placement.
    /// Mixes seed, droplet index, and step into a float in [0, 1).
    static float hash(std::uint32_t seed, std::uint32_t drop, std::uint32_t step) noexcept
    {
        auto imul = [](std::uint32_t a, std::uint32_t b) {
            return a * b;
        };
        std::uint32_t h = seed;
        h = imul(h ^ imul(drop, 374761393u), 668265263u);
        h = imul(h ^ imul(step, 1274126177u), 461845907u);
        h ^= h >> 13;
        h = imul(h, 1274126177u);
        h ^= h >> 16;
        return static_cast<float>(h & 0x7fffffff) / 2147483648.0f;
    }
};

/// Hydraulic erosion on a 2D height grid.
/// `width` × `height` grid, stored row-major.
/// `seed` deterministically controls droplet placement.
/// The grid is modified in place.
inline void hydraulicErode(int width,
    int height,
    float* grid,
    std::uint32_t seed = 1337u,
    const HydraulicErosionParams& p = {}) noexcept
{
    if (width < 3 || height < 3)
        return;

    for (int drop = 0; drop < p.numDroplets; ++drop) {
        // Random start position within grid (avoid border).
        float px = 1.0f + HydraulicErosionParams::hash(seed, drop, 0u) * (width - 3);
        float pz = 1.0f + HydraulicErosionParams::hash(seed, drop, 1u) * (height - 3);

        float dirX = 0.0f, dirZ = 0.0f; // flow direction (velocity)
        float speed = 1.0f;
        float water = 1.0f;
        float sediment = 0.0f;

        for (int step = 0; step < p.maxSteps; ++step) {
            // Compute gradient at current position via bilinear interpolation.
            int ix = static_cast<int>(px);
            int iz = static_cast<int>(pz);
            if (ix < 1 || ix >= width - 1 || iz < 1 || iz >= height - 1)
                break;

            float fx = px - ix;
            float fz = pz - iz;

            // Heights at the four corners.
            float h00 = grid[iz * width + ix];
            float h10 = grid[iz * width + (ix + 1)];
            float h01 = grid[(iz + 1) * width + ix];
            float h11 = grid[(iz + 1) * width + (ix + 1)];

            // Gradient via finite difference of bilinear height.
            float gradX = (h10 * (1.0f - fz) + h11 * fz) - (h00 * (1.0f - fz) + h01 * fz);
            float gradZ = (h01 * (1.0f - fx) + h11 * fx) - (h00 * (1.0f - fx) + h10 * fx);

            // Flow direction with inertia.
            dirX = dirX * p.inertia - gradX * (1.0f - p.inertia);
            dirZ = dirZ * p.inertia - gradZ * (1.0f - p.inertia);

            float len = std::sqrt(dirX * dirX + dirZ * dirZ);
            if (len < 0.0001f)
                break;
            dirX /= len;
            dirZ /= len;

            // Move the droplet.
            float oldX = px, oldZ = pz;
            px += dirX;
            pz += dirZ;

            // Bilinear height at new position.
            int nx = static_cast<int>(px);
            int nz = static_cast<int>(pz);
            if (nx < 0 || nx >= width - 1 || nz < 0 || nz >= height - 1)
                break;

            float nfx = px - nx;
            float nfz = pz - nz;
            float nh00 = grid[nz * width + nx];
            float nh10 = grid[nz * width + (nx + 1)];
            float nh01 = grid[(nz + 1) * width + nx];
            float nh11 = grid[(nz + 1) * width + (nx + 1)];
            float newH = nh00 * (1.0f - nfx) * (1.0f - nfz) + nh10 * nfx * (1.0f - nfz)
                + nh01 * (1.0f - nfx) * nfz + nh11 * nfx * nfz;

            // Bilinear height at old position.
            float oh00 = grid[iz * width + ix];
            float oh10 = grid[iz * width + (ix + 1)];
            float oh01 = grid[(iz + 1) * width + ix];
            float oh11 = grid[(iz + 1) * width + (ix + 1)];
            float oldH = oh00 * (1.0f - fx) * (1.0f - fz) + oh10 * fx * (1.0f - fz)
                + oh01 * (1.0f - fx) * fz + oh11 * fx * fz;

            float deltaH = oldH - newH;

            // Sediment capacity: higher at steeper slopes and higher speeds.
            float capacity = std::max(-deltaH, p.minSlope) * speed * water * p.sedimentCapacity;

            if (sediment > capacity) {
                // Deposit excess sediment.
                float deposit = (sediment - capacity) * p.depositRate;
                sediment -= deposit;
                // Distribute deposit to the four corners of old cell via bilinear weights.
                float w00 = (1.0f - fx) * (1.0f - fz);
                float w10 = fx * (1.0f - fz);
                float w01 = (1.0f - fx) * fz;
                float w11 = fx * fz;
                grid[iz * width + ix] += deposit * w00;
                grid[iz * width + (ix + 1)] += deposit * w10;
                grid[(iz + 1) * width + ix] += deposit * w01;
                grid[(iz + 1) * width + (ix + 1)] += deposit * w11;
            } else if (deltaH > 0.0f) {
                // Erode: water is flowing downhill, pick up sediment.
                float erode = std::min((capacity - sediment) * p.erosionRate, deltaH);
                sediment += erode;
                float w00 = (1.0f - fx) * (1.0f - fz);
                float w10 = fx * (1.0f - fz);
                float w01 = (1.0f - fx) * fz;
                float w11 = fx * fz;
                grid[iz * width + ix] -= erode * w00;
                grid[iz * width + (ix + 1)] -= erode * w10;
                grid[(iz + 1) * width + ix] -= erode * w01;
                grid[(iz + 1) * width + (ix + 1)] -= erode * w11;
            }

            // Update speed and water.
            speed = std::sqrt(speed * speed + std::abs(deltaH) * 0.5f);
            water *= (1.0f - p.evaporationRate);

            if (water < 0.01f)
                break;
        }
    }
}

} // namespace arp::pcg
