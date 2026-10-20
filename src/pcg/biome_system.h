#pragma once

// Biome system — temperature and moisture maps with Whittaker classification.
//
// Generates two continuous maps from terrain data:
//   temperature = f(elevation, latitude, noise)
//   moisture    = f(noise, rainShadow from mountains)
//
// These are classified into 9 biomes via a Whittaker-style 2D lookup:
//
//              dry     mid     wet
//    cold    Tundra  Taiga  BorealForest
//    cool  Grassland Shrubland TemperateForest
//    warm   Desert  Savanna TropicalForest
//
// Dependencies: simplex_noise.h (for smooth temperature/moisture variation).

#include "pcg/simplex_noise.h"

#include <cstdint>
#include <vector>

namespace arp::pcg {

// ─── Biome enumeration ──────────────────────────────────────────────────────
enum class Biome : uint8_t {
    Tundra = 0,
    Taiga = 1,
    BorealForest = 2,
    Grassland = 3,
    Shrubland = 4,
    TemperateForest = 5,
    Desert = 6,
    Savanna = 7,
    TropicalForest = 8,

    COUNT = 9
};

inline constexpr const char* biomeName(Biome b) noexcept
{
    constexpr const char* names[] = {"Tundra",
        "Taiga",
        "BorealForest",
        "Grassland",
        "Shrubland",
        "TemperateForest",
        "Desert",
        "Savanna",
        "TropicalForest"};
    return names[static_cast<int>(b)];
}

// ─── Biome colour (for vertex colour / preview) ─────────────────────────────
struct BiomeColor {
    float r, g, b;
};

inline BiomeColor biomeBaseColor(Biome b) noexcept
{
    switch (b) {
    case Biome::Tundra:
        return {0.75f, 0.78f, 0.80f}; // grey-blue
    case Biome::Taiga:
        return {0.40f, 0.55f, 0.35f}; // dark green
    case Biome::BorealForest:
        return {0.30f, 0.50f, 0.30f}; // deep green
    case Biome::Grassland:
        return {0.60f, 0.70f, 0.35f}; // yellow-green
    case Biome::Shrubland:
        return {0.65f, 0.55f, 0.35f}; // brown-green
    case Biome::TemperateForest:
        return {0.35f, 0.60f, 0.30f}; // rich green
    case Biome::Desert:
        return {0.85f, 0.75f, 0.50f}; // sand
    case Biome::Savanna:
        return {0.75f, 0.70f, 0.40f}; // dry grass
    case Biome::TropicalForest:
        return {0.20f, 0.55f, 0.25f}; // vivid green
    default:
        return {0.5f, 0.5f, 0.5f};
    }
}

// ─── Biome config ───────────────────────────────────────────────────────────
struct BiomeConfig {
    std::uint32_t seed = 1337u;
    float tempNoiseScale = 0.003f;   // world-space frequency for temp variation
    float moistNoiseScale = 0.004f;  // world-space frequency for moisture variation
    float tempLatitudeDrop = 0.6f;   // temperature drop from equator to pole
    float tempElevationDrop = 0.5f;  // temperature drop at max elevation
    float rainShadowStrength = 0.4f; // moisture reduction behind mountains
    float maxElevation = 30.0f;      // reference max elevation for temp calc
};

// ─── Biome maps ─────────────────────────────────────────────────────────────
struct BiomeMaps {
    int width = 0, height = 0;
    std::vector<float> temperature; // [0, 1] cold→warm
    std::vector<float> moisture;    // [0, 1] dry→wet
    std::vector<Biome> biomes;      // classified biome per cell
};

/// Generate temperature, moisture, and biome maps for a terrain grid.
/// `heights` is a row-major height array of `width`×`height`.
/// `originX`, `originZ` are the world-space coordinates of cell (0,0).
/// `cellSize` is the world-space size of one grid cell.
inline BiomeMaps generateBiomeMaps(int width,
    int height,
    const float* heights,
    float originX,
    float originZ,
    float cellSize,
    const BiomeConfig& cfg = {}) noexcept
{
    BiomeMaps out;
    out.width = width;
    out.height = height;
    const size_t n = static_cast<size_t>(width) * height;
    out.temperature.resize(n);
    out.moisture.resize(n);
    out.biomes.resize(n);

    // Pass 1: temperature and raw moisture.
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const int ci = z * width + x;
            float wx = originX + x * cellSize;
            float wz = originZ + z * cellSize;

            // Temperature: decreases with elevation and latitude, plus noise.
            float elevRatio = std::min(heights[ci] / cfg.maxElevation, 1.0f);
            float latRatio = std::abs(wz) / (height * cellSize * 0.5f);
            float temp = 1.0f - elevRatio * cfg.tempElevationDrop - latRatio * cfg.tempLatitudeDrop
                + (simplex2D(wx * cfg.tempNoiseScale, wz * cfg.tempNoiseScale, cfg.seed) - 0.5f)
                    * 0.3f;
            out.temperature[ci] = std::max(0.0f, std::min(1.0f, temp));

            // Moisture: noise-driven base, rain shadow applied later.
            float moist = simplex2D(wx * cfg.moistNoiseScale + 0.5f,
                              wz * cfg.moistNoiseScale + 0.5f,
                              cfg.seed + 100u)
                    * 0.8f
                + 0.2f;
            out.moisture[ci] = std::max(0.0f, std::min(1.0f, moist));
        }
    }

    // Pass 2: rain shadow — reduce moisture on the leeward side of mountains.
    // Simple model: if a cell has high elevation neighbours to one side,
    // reduce moisture (moisture-bearing wind comes from one direction).
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const int ci = z * width + x;
            float maxUpwind = 0.0f;
            int count = 0;
            for (int dx = 1; dx <= 3; ++dx) { // look upwind (positive X direction)
                int nx = x + dx;
                if (nx < width) {
                    maxUpwind = std::max(maxUpwind, heights[z * width + nx]);
                    count++;
                }
            }
            if (count > 0 && maxUpwind > cfg.maxElevation * 0.3f) {
                float shadow = (maxUpwind / cfg.maxElevation) * cfg.rainShadowStrength;
                out.moisture[ci] = std::max(0.0f, out.moisture[ci] - shadow);
            }
        }
    }

    // Pass 3: Whittaker classification.
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            const int ci = z * width + x;
            float t = out.temperature[ci];
            float m = out.moisture[ci];

            // Whittaker thresholds.
            Biome b;
            if (t < 0.33f) { // cold
                if (m < 0.33f)
                    b = Biome::Tundra;
                else if (m < 0.66f)
                    b = Biome::Taiga;
                else
                    b = Biome::BorealForest;
            } else if (t < 0.66f) { // cool
                if (m < 0.33f)
                    b = Biome::Grassland;
                else if (m < 0.66f)
                    b = Biome::Shrubland;
                else
                    b = Biome::TemperateForest;
            } else { // warm
                if (m < 0.33f)
                    b = Biome::Desert;
                else if (m < 0.66f)
                    b = Biome::Savanna;
                else
                    b = Biome::TropicalForest;
            }
            out.biomes[ci] = b;
        }
    }

    return out;
}

/// P2: Bilinearly sample the moisture field ([0,1] dry->wet) at world
/// coordinates. `originX`/`originZ`/`cellSize` must match the values passed
/// to generateBiomeMaps() so the world->grid mapping is consistent. Returns
/// 0.5 (neutral) when the maps are empty or the query falls outside the
/// grid, so placement paths that have no BiomeMaps wired degrade gracefully
/// to the neutral-moisture P1 behavior.
inline float sampleMoisture(const BiomeMaps& maps,
    float wx,
    float wz,
    float originX,
    float originZ,
    float cellSize) noexcept
{
    if (maps.width < 2 || maps.height < 2 || cellSize <= 0.0f)
        return 0.5f;
    float fx = (wx - originX) / cellSize;
    float fz = (wz - originZ) / cellSize;
    if (fx < 0.0f)
        fx = 0.0f;
    if (fz < 0.0f)
        fz = 0.0f;
    if (fx > static_cast<float>(maps.width - 1))
        fx = static_cast<float>(maps.width - 1);
    if (fz > static_cast<float>(maps.height - 1))
        fz = static_cast<float>(maps.height - 1);
    const int x0 = static_cast<int>(fx);
    const int z0 = static_cast<int>(fz);
    const int x1 = (x0 + 1 < maps.width) ? x0 + 1 : x0;
    const int z1 = (z0 + 1 < maps.height) ? z0 + 1 : z0;
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const float h00 = maps.moisture[static_cast<std::size_t>(z0) * maps.width + x0];
    const float h10 = maps.moisture[static_cast<std::size_t>(z0) * maps.width + x1];
    const float h01 = maps.moisture[static_cast<std::size_t>(z1) * maps.width + x0];
    const float h11 = maps.moisture[static_cast<std::size_t>(z1) * maps.width + x1];
    const float h0 = h00 * (1.0f - tx) + h10 * tx;
    const float h1 = h01 * (1.0f - tx) + h11 * tx;
    return h0 * (1.0f - tz) + h1 * tz;
}

} // namespace arp::pcg
