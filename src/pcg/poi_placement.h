#pragma once
// Phase 16: POI Placement — deterministic point-of-interest site selection.
#include "pcg/simplex_noise.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace arp::pcg {

enum class PoiType : uint8_t {
    Trader = 0,
    Shrine = 1,
    Shipwreck = 2,
    AbandonedCamp = 3,
    DungeonEntrance = 4,
    Watchtower = 5
};

inline const char* poiName(PoiType t)
{
    constexpr const char* n[] = {"Trader", "Shrine", "Shipwreck", "Camp", "Dungeon", "Tower"};
    return n[(int)t];
}

struct PoiPlacement {
    float x, z;
    PoiType type;
};

struct PoiConfig {
    int traderCount = 3, shrineCount = 6, shipwreckCount = 5, campCount = 8, dungeonCount = 12,
        towerCount = 8;
    float minSpacing = 20.0f; // minimum distance between POIs
    std::uint32_t seed = 1337u;
};

/// Deterministic POI placement.  Places each type independently, avoiding
/// overlap and respecting minimum spacing.
inline std::vector<PoiPlacement> placePOIs(int worldW,
    int worldH,
    float cellSize,
    const float* heights,
    const uint8_t* coastMask, // CoastType
    const PoiConfig& cfg = {}) noexcept
{
    std::vector<PoiPlacement> result;
    auto tooClose = [&](float px, float pz) {
        for (auto& p : result) {
            float dx = p.x - px, dz = p.z - pz;
            if (dx * dx + dz * dz < cfg.minSpacing * cfg.minSpacing)
                return true;
        }
        return false;
    };

    struct TypeInfo {
        PoiType type;
        int count;
        std::uint32_t seedOff;
    };

    TypeInfo types[] = {
        {PoiType::Trader, cfg.traderCount, 10u},
        {PoiType::Shrine, cfg.shrineCount, 20u},
        {PoiType::Shipwreck, cfg.shipwreckCount, 30u},
        {PoiType::AbandonedCamp, cfg.campCount, 40u},
        {PoiType::DungeonEntrance, cfg.dungeonCount, 50u},
        {PoiType::Watchtower, cfg.towerCount, 60u},
    };

    for (auto& ti : types) {
        int placed = 0, attempts = 0;
        while (placed < ti.count && attempts < ti.count * 50) {
            float nx =
                simplex2D((float)attempts * 0.1f, 0.5f, cfg.seed + ti.seedOff) * worldW * cellSize
                - worldW * cellSize * 0.5f;
            float nz = simplex2D((float)attempts * 0.1f + 0.5f, 0.5f, cfg.seed + ti.seedOff + 1u)
                    * worldH * cellSize
                - worldH * cellSize * 0.5f;
            int cx = static_cast<int>((nx + worldW * cellSize * 0.5f) / cellSize);
            int cz = static_cast<int>((nz + worldH * cellSize * 0.5f) / cellSize);
            if (cx < 0 || cx >= worldW || cz < 0 || cz >= worldH) {
                attempts++;
                continue;
            }
            int ci = cz * worldW + cx;
            float h = heights[ci];

            // Type-specific constraints
            bool valid = true;
            switch (ti.type) {
            case PoiType::Trader:
                valid = h > -0.5f && h < 10.0f;
                break;
            case PoiType::Shipwreck:
                valid = coastMask && coastMask[ci] <= 2;
                break; // beach/shallow
            case PoiType::DungeonEntrance:
                valid = h > 2.0f;
                break; // elevated
            case PoiType::Watchtower:
                valid = h > 3.0f;
                break; // high ground
            default:
                break;
            }

            if (valid && !tooClose(nx, nz)) {
                result.push_back({nx, nz, ti.type});
                placed++;
            }
            attempts++;
        }
    }
    return result;
}

} // namespace arp::pcg
