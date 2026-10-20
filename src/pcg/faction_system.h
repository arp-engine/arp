#pragma once
// Phase 11: Faction territories — Voronoi-based political map.
#include "pcg/simplex_noise.h"
#include "pcg/voronoi_noise.h"

#include <cstdint>
#include <string>
#include <vector>

namespace arp::pcg {

struct Faction {
    std::string name;
    int id;
    float capitalX, capitalZ;
    float r, g, b; // faction color
};

struct FactionMap {
    std::vector<Faction> factions;
    std::vector<int> territoryMap; // cell → faction id, -1 = unclaimed
    int width, height;
};

/// Generate 3-5 factions with Voronoi territories on the world grid.
inline FactionMap generateFactions(
    int width, int height, float cellSize, std::uint32_t seed, int factionCount = 4) noexcept
{
    FactionMap fm;
    fm.width = width;
    fm.height = height;
    fm.territoryMap.resize(width * height, -1);

    const char* names[] = {
        "Iron Order", "Shadow Court", "Wild Clans", "Stoneward Accord", "Crimson Dawn"};
    for (int f = 0; f < factionCount; ++f) {
        float cx = simplex2D(f * 1.0f, 0.0f, seed) * width * cellSize - width * cellSize * 0.5f;
        float cz = simplex2D(f * 1.0f + 0.5f, 0.0f, seed + 100u) * height * cellSize
            - height * cellSize * 0.5f;
        Faction fc;
        fc.name = names[f];
        fc.id = f;
        fc.capitalX = cx;
        fc.capitalZ = cz;
        fc.r = simplex2D(f * 2.0f, 0.0f, seed + 200u);
        fc.g = simplex2D(f * 2.0f + 0.3f, 0.0f, seed + 300u);
        fc.b = simplex2D(f * 2.0f + 0.6f, 0.0f, seed + 400u);
        fm.factions.push_back(fc);
    }

    // Assign each cell to nearest faction capital
    for (int z = 0; z < height; ++z) {
        for (int x = 0; x < width; ++x) {
            float wx = (x - width / 2.0f) * cellSize;
            float wz = (z - height / 2.0f) * cellSize;
            // Add noise to create irregular borders
            float noiseOffX = simplex2D(wx * 0.02f, wz * 0.02f, seed + 500u) * 20.0f - 10.0f;
            float noiseOffZ =
                simplex2D(wx * 0.02f + 0.5f, wz * 0.02f + 0.5f, seed + 600u) * 20.0f - 10.0f;
            wx += noiseOffX;
            wz += noiseOffZ;

            int bestF = 0;
            float bestD = 1e9f;
            for (int f = 0; f < factionCount; ++f) {
                float dx = wx - fm.factions[f].capitalX;
                float dz = wz - fm.factions[f].capitalZ;
                float d = dx * dx + dz * dz;
                if (d < bestD) {
                    bestD = d;
                    bestF = f;
                }
            }
            fm.territoryMap[z * width + x] = bestF;
        }
    }
    return fm;
}

} // namespace arp::pcg
