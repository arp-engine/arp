#pragma once

// BiomeWeights - four continuous biome weight fields for the 600m world.
//
// Computes coast, meadow, blackForest, and mountain biome weights per grid
// cell from the TerrainFieldSet (height, slope, wetness, shore distance) and
// world position. The weights are continuous (not hard enum boundaries) so
// transitions between biomes blend smoothly.
//
// The biome weights drive P5 vegetation community selection:
//   - coast:     near shore, low elevation, exposed rock, sparse vegetation
//   - meadow:    mid-elevation gentle slopes, grass-dominant, buildable
//   - blackForest: concave depressions, mid-to-high elevation, dense canopy
//   - mountain:  high elevation, steep slopes, bare rock + snow
//
// Three vegetation layers are derived from the biome weights:
//   - canopy:     mature_pine_family (dense in blackForest, sparse in meadow)
//   - understory: young_fir_family + young_pine_family (forest edges, gaps)
//   - groundCover: grass (meadow) / broadleaf (forest edge) / none (mountain)
//
// Plan reference: complete_procedural_scene_implementation_plan.md P5.

#include "pcg/terrain_fields.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace arp::pcg {

/// Four continuous biome weights per grid cell. All weights sum to 1.0.
struct BiomeWeightSet {
    int width = 0;
    int height = 0;
    float cellSize = 0.0f;
    float originX = 0.0f;
    float originZ = 0.0f;

    std::vector<float> coast;       // shore/exposed rock
    std::vector<float> meadow;      // gentle grassland
    std::vector<float> blackForest; // dense forest
    std::vector<float> mountain;    // high elevation bare rock/snow

    /// Bilinear sample of 4 biome weights at world (wx, wz).
    struct BiomeSample {
        float coast = 0.0f;
        float meadow = 0.0f;
        float blackForest = 0.0f;
        float mountain = 0.0f;
    };

    BiomeSample sample(float wx, float wz) const noexcept
    {
        const float fx = (wx - originX) / cellSize;
        const float fz = (wz - originZ) / cellSize;
        int x0 = static_cast<int>(std::floor(fx));
        int z0 = static_cast<int>(std::floor(fz));
        if (x0 < 0)
            x0 = 0;
        if (z0 < 0)
            z0 = 0;
        if (x0 >= width - 1)
            x0 = width - 2;
        if (z0 >= height - 1)
            z0 = height - 2;
        if (x0 < 0 || z0 < 0)
            return {};

        const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
        const float tz = std::clamp(fz - static_cast<float>(z0), 0.0f, 1.0f);

        const auto idx = [&](int x, int z) -> std::size_t {
            return static_cast<std::size_t>(z) * width + x;
        };

        const auto sample4 = [&](const std::vector<float>& field) -> float {
            const float p00 = field[idx(x0, z0)];
            const float p10 = field[idx(x0 + 1, z0)];
            const float p01 = field[idx(x0, z0 + 1)];
            const float p11 = field[idx(x0 + 1, z0 + 1)];
            const float a = p00 * (1.0f - tx) + p10 * tx;
            const float b = p01 * (1.0f - tx) + p11 * tx;
            return a * (1.0f - tz) + b * tz;
        };

        BiomeSample s;
        s.coast = sample4(coast);
        s.meadow = sample4(meadow);
        s.blackForest = sample4(blackForest);
        s.mountain = sample4(mountain);
        return s;
    }
};

/// Compute four continuous biome weights from a TerrainFieldSet.
///
/// The biome classification uses the following factors:
///
/// coast:       shoreDistance < 40m, low elevation. Transition zone near water.
/// meadow:      gentle slopes (<0.25), low-to-mid elevation, away from shore.
/// blackForest: concave depressions, mid elevation, moderate slope. Dense canopy.
/// mountain:    high elevation (>40m) or steep slope (>0.5). Bare rock + snow.
///
/// The weights are computed with smooth transitions (smoothstep) and then
/// normalized to sum=1 per cell.
inline BiomeWeightSet computeBiomeWeights(const TerrainFieldSet& fields,
    float coastShoreDistance = 40.0f,
    float mountainHeight = 40.0f,
    float mountainSlope = 0.5f)
{
    BiomeWeightSet bs;
    bs.width = fields.width;
    bs.height = fields.height;
    bs.cellSize = fields.cellSize;
    bs.originX = fields.originX;
    bs.originZ = fields.originZ;

    const std::size_t N = static_cast<std::size_t>(fields.width) * fields.height;
    bs.coast.resize(N, 0.0f);
    bs.meadow.resize(N, 0.0f);
    bs.blackForest.resize(N, 0.0f);
    bs.mountain.resize(N, 0.0f);

    for (int iz = 0; iz < fields.height; ++iz) {
        for (int ix = 0; ix < fields.width; ++ix) {
            const auto i = static_cast<std::size_t>(iz) * fields.width + ix;
            const float slope = fields.slope[i];
            const float curvature = fields.curvature[i];
            const float wetness = fields.wetness[i];
            const float shoreDist = fields.shoreDistance[i];
            const float height = fields.heights[i];

            // ── Pre-normalization weights ────────────────────────────────
            float wCoast = 0.0f;
            float wMeadow = 0.0f;
            float wBlackForest = 0.0f;
            float wMountain = 0.0f;

            // coast: near-shore zone. Strongest right at shore, fades inland.
            const float shoreFactor = std::clamp(1.0f - shoreDist / coastShoreDistance, 0.0f, 1.0f);
            wCoast = shoreFactor * shoreFactor;

            // mountain: high elevation OR steep slope.
            const float heightFactor = std::clamp((height - mountainHeight) / 20.0f, 0.0f, 1.0f);
            const float slopeFactor = std::clamp((slope - mountainSlope) * 3.0f, 0.0f, 1.0f);
            wMountain = std::max(heightFactor, slopeFactor * 0.8f);

            // blackForest: concave depressions, mid elevation, moderate slope.
            // Suppressed by coast and mountain.
            const float concavity = std::clamp(curvature * 50.0f, 0.0f, 1.0f);
            const float forestHeight =
                std::clamp(1.0f - std::abs(height - 20.0f) / 25.0f, 0.0f, 1.0f);
            const float forestSlope = std::clamp(1.0f - slope * 2.5f, 0.0f, 1.0f);
            wBlackForest = (0.3f + 0.7f * concavity) * forestHeight * forestSlope * (1.0f - wCoast)
                * (1.0f - wMountain);

            // meadow: gentle slopes, low-to-mid elevation. Default when not
            // coast/forest/mountain.
            const float meadowSlope = std::clamp(1.0f - slope * 3.0f, 0.0f, 1.0f);
            const float meadowHeight = std::clamp(1.0f - height / 35.0f, 0.0f, 1.0f);
            wMeadow = meadowSlope * meadowHeight * (1.0f - wCoast * 0.8f)
                * (1.0f - wMountain * 0.9f) * (1.0f - wBlackForest * 0.7f);

            // ── Normalize ────────────────────────────────────────────────
            const float sum = wCoast + wMeadow + wBlackForest + wMountain;
            const float invSum = sum > 0.001f ? 1.0f / sum : 0.0f;
            bs.coast[i] = wCoast * invSum;
            bs.meadow[i] = wMeadow * invSum;
            bs.blackForest[i] = wBlackForest * invSum;
            bs.mountain[i] = wMountain * invSum;
        }
    }

    return bs;
}

/// Three vegetation community layers derived from biome weights.
/// Each layer has a density multiplier per biome, used to scale scatter
/// density at any world position.
struct VegetationLayerWeights {
    /// Canopy layer (mature trees): dense in blackForest, sparse in meadow,
    /// absent in coast/mountain.
    float canopy = 0.0f;
    /// Understory layer (young trees): present at forest edges and gaps.
    float understory = 0.0f;
    /// Ground cover layer (grass/broadleaf): dense in meadow, moderate at
    /// forest edge, absent in mountain.
    float groundCover = 0.0f;
};

/// Compute three-layer vegetation weights from a biome sample.
inline VegetationLayerWeights computeLayerWeights(const BiomeWeightSet::BiomeSample& biome)
{
    VegetationLayerWeights v;
    // Canopy: blackForest dominant, sparse meadow, absent coast/mountain.
    v.canopy = biome.blackForest * 0.9f + biome.meadow * 0.08f;
    // Understory: forest edges (where blackForest and meadow both present).
    const float forestEdge = biome.blackForest * biome.meadow * 4.0f;
    v.understory = std::min(biome.blackForest * 0.4f + forestEdge, 1.0f);
    // Ground cover is a continuous vegetation layer, not a rare decoration.
    // Keep it lush in meadows and present beneath an open forest canopy, with
    // a restrained contribution near coasts. Mountain/water exclusions are
    // still applied by the placement pass, so this does not put grass on
    // cliffs or beaches.
    v.groundCover = biome.meadow * 1.0f + biome.blackForest * 0.40f + biome.coast * 0.25f;
    v.groundCover = std::min(v.groundCover, 1.0f);
    return v;
}

/// Exclusion mask result for a world position. When any flag is true, the
/// position is excluded from vegetation placement.
struct VegetationExclusion {
    bool road = false;
    bool buildable = false;
    bool water = false;
    bool steepSlope = false;
    bool bareRock = false;
};

/// Check if a world position is excluded from vegetation placement.
/// Uses the TerrainFieldSet for slope/water/rock checks. Road and buildable
/// exclusion require external masks (not yet available - returns false).
inline VegetationExclusion checkVegetationExclusion(const TerrainFieldSet& fields,
    float wx,
    float wz,
    float waterLevel,
    float maxSlope = 0.65f,
    float rockSlope = 0.55f)
{
    VegetationExclusion ex;
    int gx, gz;
    if (!fields.worldToGrid(wx, wz, gx, gz)) {
        ex.water = true;
        return ex;
    }
    const auto i = static_cast<std::size_t>(gz) * fields.width + gx;
    const float height = fields.heights[i];
    const float slope = fields.slope[i];

    ex.water = height < waterLevel;
    ex.steepSlope = slope > maxSlope;
    ex.bareRock = slope > rockSlope && height > 15.0f;
    return ex;
}

} // namespace arp::pcg
