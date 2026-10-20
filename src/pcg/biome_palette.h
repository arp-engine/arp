#pragma once

// BiomePalette - species palette that maps biome + placement context to a
// concrete species entry (template family, density, scale, age distribution,
// microhabitat preference).
//
// This is the data-driven replacement for the legacy hard-coded species
// selection (grass/weed/broadleaf + 3 tree families + 2 rock families).
// The legacy path picked species via 3-5 independent hash rolls inside
// buildChunkVegetationInstances; the new path queries a single palette
// that externalizes every species parameter to YAML.
//
// Design contract:
//   * The palette is pure data (POD-ish struct with std::vector storage).
//     No YAML parser lives here - arp::pcg must not depend on the engine
//     reflection layer. The wocc game layer loads the YAML and populates
//     the palette (matching the BiomeWeightSet pattern: pure data here,
//     loader at the game layer).
//   * Species selection is a pure function of (palette, biome sample,
//     placement context, hash). Deterministic given inputs.
//   * The palette is read-only after load; placement paths can cache
//     pointers into it safely.
//
// Plan reference: Track C P1 - Poisson cluster scatter + species palette.
// Status gate: Prototype Complete.

#include "pcg/biome_weights.h"
#include "pcg/foliage_instance_batch.h"
#include "pcg/vegetation_distribution.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace arp::pcg {

/// Identifies which vegetation layer a species belongs to. Matches the
/// three-layer model in computeLayerWeights + a fourth "accent" layer for
/// rare decorative species (flowers, mushrooms).
enum class SpeciesLayer : uint8_t {
    Canopy = 0,     // mature trees
    Understory = 1, // young trees, tall bushes
    Ground = 2,     // grass, broadleaf, fern, low bush
    Accent = 3,     // flowers, mushrooms (rare, biome-gated)
};

/// A single species entry in the palette. Each entry maps a biome context
/// to a template family + placement parameters.
struct SpeciesEntry {
    /// Stable species identifier (P0 InstanceData.speciesId field).
    /// Must fit in uint8 (0..255). The palette loader assigns these
    /// sequentially; the contract test asserts uniqueness.
    uint8_t speciesId = 0;

    /// Human-readable name for diagnostics (e.g. "mature_pine",
    /// "forest_fern", "meadow_flower_white").
    std::string name;

    /// Which layer this species occupies. Drives the scatter pass that
    /// picks it up (canopy/understory use clusterScatter; ground uses
    /// terrainScatter; accent uses a sparse hash-gated subset of ground).
    SpeciesLayer layer = SpeciesLayer::Ground;

    /// Template category the placement path maps this species to. The
    /// caller (buildChunkVegetationInstances) resolves this to a concrete
    /// templateId via the VegetationTemplateConfig + variant roll.
    TemplateCategory category = TemplateCategory::ProceduralGrass;

    /// Per-biome density multiplier. The placement path multiplies the
    /// base scatter density by this factor when deciding whether to emit
    /// an instance of this species. Indexed by biome id:
    ///   [0] = coast, [1] = meadow, [2] = blackForest, [3] = mountain.
    /// A species with all zeros is never placed.
    float biomeDensity[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    /// Edge-gradient preference. Species with edgeAffinity > 0 get a
    /// density boost proportional to placement.edgeGradient (forest-meadow
    /// ecotone species). Reference: fern 0.8, bush 0.5, grass 0.0.
    float edgeAffinity = 0.0f;

    /// Cluster affinity. Species with high clusterAffinity get a density
    /// boost proportional to placement.clusterWeight (cluster members).
    /// Reference: pine 0.6 (clump-forming), fern 0.9 (colony-forming),
    /// grass 0.2 (uniform).
    float clusterAffinity = 0.0f;

    /// Scale range. The actual scale is rolled via ageClassScale() using
    /// the placement's ageClass.
    float minScale = 0.5f;
    float maxScale = 1.1f;

    /// Age distribution override. If null, uses the global J-curve. If
    /// non-null, the placement path uses these fractions instead. This
    /// lets old-growth species (pine) skew older than pioneer species
    /// (fir, weed).
    bool useCustomAgeDistribution = false;
    float saplingFraction = 0.70f;
    float matureFractionOfRemainder = 0.833f;

    /// Relative weight within the layer when multiple species are eligible.
    /// The placement path rolls a single species via weighted choice:
    ///   totalWeight = Σ(entry.weight * biomeDensity[biomeId]
    ///                            * (1 + edgeAffinity * edgeGradient)
    ///                            * (1 + clusterAffinity * clusterWeight))
    ///   speciesRoll = hash2(x, z, seed)
    ///   picked = first entry where cumulativeWeight >= speciesRoll * total
    float weight = 1.0f;

    /// Moisture preference (P2 microhabitat). 0..255; the placement path
    /// multiplies density by (1 + (moisturePref - 128)/255 * moistureField).
    /// 128 = neutral. Default neutral for P1; P2 wires the moisture field.
    uint8_t moisturePref = 128;

    /// Slope preference (P2 microhabitat). 0=flat, 1=gentle, 2=moderate,
    /// 3=steep. The placement path boosts density on matching slope classes.
    uint8_t slopePref = 0;
};

/// The full species palette. Loaded once at scenario start and queried per
/// placement. Index by SpeciesLayer to get the list of candidate species
/// for that layer.
struct SpeciesPalette {
    /// All species, indexed by speciesId. The contract test asserts
    /// speciesId == index for O(1) lookup.
    std::vector<SpeciesEntry> species;

    /// Per-layer indices into `species`, for fast iteration during
    /// placement. Built by buildLayerIndex() after load.
    std::vector<uint8_t> byLayer[4];

    /// Version tag from the YAML file (for cache invalidation).
    uint32_t version = 0;

    /// Build the byLayer[] index after the species vector is populated.
    /// Must be called once after load, before any placement query.
    void buildLayerIndex() noexcept
    {
        for (int i = 0; i < 4; ++i)
            byLayer[i].clear();
        for (uint8_t i = 0; i < species.size(); ++i) {
            const auto layer = static_cast<int>(species[i].layer);
            if (layer >= 0 && layer < 4) {
                byLayer[layer].push_back(i);
            }
        }
    }

    /// Number of species in the palette.
    std::size_t size() const noexcept { return species.size(); }

    /// Lookup by speciesId (which is also the vector index).
    const SpeciesEntry& at(uint8_t id) const noexcept { return species[id]; }
};

/// Result of a species selection query. The placement path uses this to
/// resolve a concrete templateId + InstanceData.
struct SpeciesSelection {
    /// Index into SpeciesPalette.species, or -1 if no species was eligible
    /// (the placement should be dropped).
    int speciesIndex = -1;
    /// The resolved density multiplier (post biome/edge/cluster weighting).
    /// Used by the caller to gate the placement against the scatter jitter.
    float density = 0.0f;
};

/// Biome id helper - mirrors BiomeWeightSet's 4-weight ordering.
///   0=coast, 1=meadow, 2=blackForest, 3=mountain
inline constexpr int kBiomeCoast = 0;
inline constexpr int kBiomeMeadow = 1;
inline constexpr int kBiomeBlackForest = 2;
inline constexpr int kBiomeMountain = 3;

/// Convert a BiomeSample to a density vector for palette lookup.
/// Returns the 4 biome weights in palette order.
inline void biomeSampleToDensityVector(const BiomeWeightSet::BiomeSample& bs, float out[4]) noexcept
{
    out[kBiomeCoast] = bs.coast;
    out[kBiomeMeadow] = bs.meadow;
    out[kBiomeBlackForest] = bs.blackForest;
    out[kBiomeMountain] = bs.mountain;
}

/// P2: Classify a continuous slope (rise/run, as produced by
/// TerrainFieldSet::sampleSlope) into one of four slope classes used by
/// SpeciesEntry::slopePref.
///   0 = flat      (slope < 0.15)
///   1 = gentle    (slope < 0.35)
///   2 = moderate  (slope < 0.55)
///   3 = steep     (slope >= 0.55)
/// The thresholds are consistent with existing hard gates: 0.55 is the
/// `rockSlope` bareRock cutoff in checkVegetationExclusion, and 0.15/0.35
/// bracket the settlement buildable slope band.
inline uint8_t classifySlope(const float slope) noexcept
{
    if (slope < 0.15f)
        return 0;
    if (slope < 0.35f)
        return 1;
    if (slope < 0.55f)
        return 2;
    return 3;
}

/// P2: Soft slope preference boost. Compares the placement's slope class
/// against the species' slope preference. A species whose slopePref is 0
/// (flat, the default) is treated as slope-agnostic and receives boost 1.0
/// everywhere - this is the key non-regression guarantee for P1 content
/// whose palette entries left slopePref at its default. Otherwise the boost
/// falls off with the class distance:
///   match     -> 1.0
///   diff 1    -> 0.6
///   diff 2    -> 0.3
///   diff 3    -> 0.15
inline float slopePreferenceBoost(const uint8_t slopePref, const uint8_t slopeClass) noexcept
{
    if (slopePref == 0u)
        return 1.0f; // flat default = slope-agnostic
    if (slopePref == slopeClass)
        return 1.0f;
    const int diff =
        static_cast<int>(slopePref > slopeClass ? slopePref - slopeClass : slopeClass - slopePref);
    switch (diff) {
    case 1:
        return 0.6f;
    case 2:
        return 0.3f;
    default:
        return 0.15f; // diff 3
    }
}

/// P2: Moisture preference boost. Implements the contract documented on
/// SpeciesEntry::moisturePref:
///   boost = 1 + (moisturePref - 128)/255 * (moistureField - 0.5) * 2
/// moisturePref 128 (neutral, the default) -> boost 1.0 regardless of field,
/// which is the non-regression guarantee for P1 content. Wet-preferring
/// species (moisturePref > 128) get boost > 1 on wet ground and < 1 on dry
/// ground; dry-preferring species the reverse. Clamped to [0.2, 1.8] so a
/// single extreme species cannot dominate or vanish entirely.
inline float moisturePreferenceBoost(const uint8_t moisturePref, const float moistureField) noexcept
{
    if (moisturePref == 128u)
        return 1.0f; // neutral default = no effect
    const float pref = (static_cast<float>(moisturePref) - 128.0f) / 255.0f;
    float boost = 1.0f + pref * (moistureField - 0.5f) * 2.0f;
    if (boost < 0.2f)
        boost = 0.2f;
    if (boost > 1.8f)
        boost = 1.8f;
    return boost;
}

/// Pick a species for a placement given the palette and biome context.
///
/// Algorithm:
///   1. Iterate all species in `layer`.
///   2. For each species, compute:
///        w = species.weight
///          * dot(species.biomeDensity, biomeVector)
///          * (1 + species.edgeAffinity    * placement.edgeGradient)
///          * (1 + species.clusterAffinity * placement.clusterWeight)
///          * slopePreferenceBoost(species.slopePref, placement.slopeClass)
///          * moisturePreferenceBoost(species.moisturePref, placement.moistureField)
///      If w <= 0, the species is ineligible in this context.
///   3. Roll speciesRoll = hash2(x, z, seed) in [0,1).
///   4. Walk the cumulative sum; pick the first species where
///      cumulative >= speciesRoll * total. Returns its index + density.
///   5. If no species is eligible (total == 0), returns {-1, 0}.
///
/// The density returned is the picked species' raw biomeDensity dot biome
/// (pre-edge/cluster/moisture/slope boost) - this is what the caller compares
/// against the scatter jitter to decide emit-vs-drop. The P2 micro-habitat
/// boosts (moisture/slope) only bias the *selection probability*, not the
/// density gate, so the P1 instance-count contract is preserved.
///
/// Non-regression: when a species leaves moisturePref at its default 128 and
/// slopePref at its default 0, both boosts are exactly 1.0, so P1 palette
/// content behaves identically to the pre-P2 path.
inline SpeciesSelection selectSpecies(const SpeciesPalette& palette,
    SpeciesLayer layer,
    const BiomeWeightSet::BiomeSample& biome,
    const VegetationPlacement& placement,
    uint32_t seed) noexcept
{
    SpeciesSelection sel;
    const auto& candidates = palette.byLayer[static_cast<int>(layer)];
    if (candidates.empty())
        return sel;

    float biomeVec[4];
    biomeSampleToDensityVector(biome, biomeVec);

    // First pass: compute total weight and stash per-species weights.
    // The candidate list is small (typically 3-8 species per layer), so
    // the double pass is cheaper than a heap allocation.
    float weights[16]; // bounded by typical palette size
    const std::size_t n = candidates.size();
    if (n > 16)
        return sel; // safety; palette contract enforces <=16/layer

    float total = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& sp = palette.species[candidates[i]];
        float dotB = 0.0f;
        for (int b = 0; b < 4; ++b)
            dotB += sp.biomeDensity[b] * biomeVec[b];
        if (dotB <= 0.0f) {
            weights[i] = 0.0f;
            continue;
        }
        const float edgeBoost = 1.0f + sp.edgeAffinity * placement.edgeGradient;
        const float clusterBoost = 1.0f + sp.clusterAffinity * placement.clusterWeight;
        // P2 micro-habitat soft preferences: bias selection probability only.
        const float slopeBoost = slopePreferenceBoost(sp.slopePref, placement.slopeClass);
        const float moistureBoost =
            moisturePreferenceBoost(sp.moisturePref, placement.moistureField);
        const float w = sp.weight * dotB * edgeBoost * clusterBoost * slopeBoost * moistureBoost;
        weights[i] = w;
        total += w;
    }
    if (total <= 0.0f)
        return sel;

    // Roll and pick.
    const float r = arp::core::hash2(static_cast<int>(std::round(placement.x * 8.0f)),
        static_cast<int>(std::round(placement.z * 8.0f)),
        seed);
    float cumulative = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        cumulative += weights[i];
        if (r * total <= cumulative) {
            sel.speciesIndex = candidates[i];
            // Density = raw biome dot product (pre-edge/cluster) so the
            // caller's jitter gate is independent of the boost factors.
            const auto& sp = palette.species[candidates[i]];
            float dotB = 0.0f;
            for (int b = 0; b < 4; ++b)
                dotB += sp.biomeDensity[b] * biomeVec[b];
            sel.density = dotB;
            return sel;
        }
    }
    // Floating-point round-off fallback: pick the last eligible species.
    for (std::size_t i = n; i > 0; --i) {
        if (weights[i - 1] > 0.0f) {
            sel.speciesIndex = candidates[i - 1];
            const auto& sp = palette.species[candidates[i - 1]];
            float dotB = 0.0f;
            for (int b = 0; b < 4; ++b)
                dotB += sp.biomeDensity[b] * biomeVec[b];
            sel.density = dotB;
            return sel;
        }
    }
    return sel;
}

/// Resolve a SpeciesEntry + biome sample + ageClass to a concrete
/// InstanceData scale, applying the species' custom age distribution if
/// present. Pure function of (entry, placement, seed).
inline float resolveScale(
    const SpeciesEntry& sp, const VegetationPlacement& placement, uint32_t seed) noexcept
{
    // The ageClass was already rolled by sampleAgeStructure using the
    // global J-curve. If the species overrides the curve, we re-roll
    // using the species-local curve. This is deterministic because the
    // re-roll uses a species-seeded hash.
    uint8_t ageClass = placement.ageClass;
    if (sp.useCustomAgeDistribution) {
        AgeStructureDesc localAge;
        localAge.saplingFraction = sp.saplingFraction;
        localAge.matureFractionOfRemainder = sp.matureFractionOfRemainder;
        localAge.seedBase = seed ^ 0x5EED5EEDu ^ (static_cast<uint32_t>(sp.speciesId) << 16);
        // Re-roll using the species-local curve. We make a mutable copy
        // of the placement so sampleAgeStructure can mutate it.
        VegetationPlacement tmp = placement;
        sampleAgeStructure(localAge, tmp);
        ageClass = tmp.ageClass;
    }
    return ageClassScale(ageClass, sp.minScale, sp.maxScale, placement.x, placement.z, seed);
}

} // namespace arp::pcg
