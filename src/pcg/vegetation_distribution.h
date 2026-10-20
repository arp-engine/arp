#pragma once

// VegetationDistribution - ecological placement primitives that replace the
// legacy grid-jittered scatter with biologically-motivated distribution.
//
// This module provides three composable samplers that, together, produce
// vegetation placements matching the quality of commercial engines (UE
// Foliage Type / Unity GPU Instancer):
//
//   1. ClusterScatter  - Poisson-disk seed points + variable-radius Poisson
//                        cluster members. Trees cluster naturally; no grid.
//   2. EdgeGradient    - biome-weight derivative -> forest-meadow transition
//                        zone. Inserts edge species (fern / bush) at the
//                        ecotone, where the legacy scatter placed nothing.
//   3. AgeStructure    - J-curve age distribution (many saplings, few old
//                        trees). Drives scale + LOD class selection so a
//                        forest reads as a real age-structured stand.
//
// Design contract (must hold across all four RHI backends / all platforms):
//   * Determinism: every sampler is a pure function of (position, seed, desc).
//     Same inputs -> bit-identical outputs. No thread-local state, no IO.
//   * Composable: each sampler takes a heightfield + a callback
//     `void(VegetationPlacement)` so callers can fan placements into any
//     batch / buffer / accumulator.
//   * Bounded: per-chunk placement count is bounded by maxClusterSeeds *
//     maxClusterMembers + edgeStepCount^2 + ageResampleMax. No unbounded
//     growth; no allocation inside the sampler hot path beyond the Poisson
//     sampler's own grid (which is O(area / minDist^2)).
//   * Reusable: lives in arp::pcg (engine core), usable by any game / sample /
//     tool. No wocc-specific types; the caller maps VegetationPlacement to
//     its own InstanceData + templateId.
//
// Plan reference: Track C P1 - Poisson cluster scatter + species palette.
// Status gate: Prototype Complete (verified by vegetation_distribution_tests).

#include "core/deterministic_hash.h"
#include "core/math.h"
#include "pcg/biome_weights.h"
#include "pcg/poisson_scatter.h"
#include "pcg/terrain_fields.h"
#include "pcg/terrain_heightfield.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace arp::pcg {

/// A single placement produced by any vegetation distribution sampler.
///
/// Field semantics match the legacy ScatterPoint plus the new ecological
/// metadata needed by P1+ placement paths. Callers map this to
/// InstanceData (setting speciesId / ageClass / scale / lodClass etc.).
struct VegetationPlacement {
    float x = 0.0f;      ///< World X
    float z = 0.0f;      ///< World Z
    float y = 0.0f;      ///< Terrain height at (x, z)
    float jitter = 0.0f; ///< Primary hash r ∈ [0,1) for downstream use

    /// P1: Ecological context the placement path uses to pick a species /
    /// template. All fields are pure functions of (x, z, seed, desc) and
    /// therefore deterministic.
    float clusterWeight = 0.0f; ///< 0..1, strength of cluster membership
                                ///< (1.0 = cluster seed, <1 = cluster member)
    float edgeGradient = 0.0f;  ///< 0..1, |∂blackForest/∂x|+|∂meadow/∂x|
                                ///< at the placement; >edgeThreshold means
                                ///< the point sits on a forest-meadow
                                ///< ecotone and should pick an edge species.
    uint8_t ageClass = 0;       ///< 0 = sapling, 1 = mature, 2 = old
    uint8_t layerClass = 0;     ///< 0 = canopy, 1 = understory, 2 = ground
    uint8_t seedSlot = 0;       ///< Which ClusterScatter seed produced this
                                ///< member (for deterministic per-cluster
                                ///< species roll). 0 for non-cluster points.

    /// P2: Micro-habitat signals sampled at (x, z) and consumed by
    /// selectSpecies() to bias species selection. Both default to neutral so
    /// unfilled placements (e.g. legacy/non-palette callers, unit tests that
    /// do not populate them) do not regress P1 selection behavior.
    float moistureField = 0.5f; ///< [0,1] dry->wet, from BiomeMaps::moisture.
                                ///< 0.5 = neutral; selectSpecies applies
                                ///< (1 + (moisturePref-128)/255 * (m-0.5)*2).
    uint8_t slopeClass = 0;     ///< 0=flat,1=gentle,2=moderate,3=steep
                                ///< (see classifySlope). Compared against
                                ///< SpeciesEntry::slopePref as a soft
                                ///< preference (density-style boost, not a
                                ///< hard exclude).
};

// ---------------------------------------------------------------------------
// 1. ClusterScatter
// ---------------------------------------------------------------------------
//
// Replaces the grid-jittered tree scatter with Poisson-disk cluster seeds.
// Each seed becomes the center of a small Poisson cluster whose members are
// scattered at a variable radius (driven by hash2) around the seed. This
// reproduces the natural tendency of trees to grow in clumps (seed shadow,
// micro-site suitability, nurse-plant effect) rather than a uniform grid.
//
// Algorithm (all deterministic given seed):
//   1. Run PoissonDiskSampler over the chunk domain with minDist = seedSpacing.
//      -> N seed points (typically 3..15 for a 64m chunk).
//   2. For each seed (index = seedSlot), roll a cluster size in
//      [minMembers, maxMembers] via hash2(seedX, seedZ, seed+slotHash).
//   3. For each member i in [0, clusterSize):
//      a. Roll a radius in [minRadius, maxRadius] via hash2.
//      b. Roll an angle in [0, 2π) via hash2.
//      c. Compute member world position = seed + (cos θ * r, sin θ * r).
//      d. Sample terrain height; reject if below minY or slope > maxSlope.
//      e. Emit VegetationPlacement with clusterWeight = r/maxRadius
//         (members near the seed are "stronger" cluster members),
//         ageClass rolled by AgeStructure (caller composes), seedSlot set.
//   4. The seed itself is emitted with clusterWeight = 1.0.
//
// The sampler does NOT touch biome weights - that composition happens in
// EdgeGradient / the caller. This keeps the primitive pure and reusable
// for non-biome scenarios (e.g. rock clusters, prop clusters).

struct ClusterScatterDesc {
    /// World-space scatter region bounds (X/Z). Typically the chunk AABB.
    float minX = -32.0f;
    float maxX = 32.0f;
    float minZ = -32.0f;
    float maxZ = 32.0f;

    /// Minimum distance between any two cluster *seeds* (world units).
    /// Reference: 12m for trees (1-2 seeds per 64m chunk edge), 4m for
    /// understory, 1.5m for ground-cover clumps.
    float seedSpacing = 12.0f;

    /// Cluster member count range. The actual count per seed is
    /// minMembers + hash2 * (maxMembers - minMembers + 1), floored.
    int minMembers = 2;
    int maxMembers = 5;

    /// Cluster radius range (world units). Members are placed in
    /// [minRadius, maxRadius] from the seed. Reference: trees [1.5, 5.0],
    /// understory [0.8, 2.5], ground-cover clumps [0.3, 1.0].
    float minRadius = 1.5f;
    float maxRadius = 5.0f;

    /// Maximum terrain slope (rise/run) above which cluster members are
    /// rejected. Trees: 0.62, ground cover: 0.62, rocks: 1e30 (any).
    float maxSlope = 0.62f;

    /// Minimum terrain Y below which members are rejected (e.g. water).
    /// Pass -FLT_MAX to disable.
    float minY = -1e30f;

    /// Slope sample epsilon for finite-difference normal.
    float slopeEpsilon = 1.2f;

    /// Deterministic seed.
    uint32_t seed = 1u;

    /// Cap on total cluster seeds per call (safety valve for pathological
    /// minDist values). Default matches PoissonDiskSampler's internal cap.
    int maxClusterSeeds = 4096;
};

/// Scatter cluster seeds + members across a terrain heightfield.
///
/// `out` is called for each accepted placement (seed and members alike) in
/// deterministic order: seed 0 members, seed 1 members, ..., seed N-1
/// members. The seed itself is emitted first in each cluster with
/// clusterWeight = 1.0, followed by its members with clusterWeight < 1.0.
template <typename OutputFn>
void clusterScatter(
    const arp::core::TerrainHeightField& heightField, const ClusterScatterDesc& desc, OutputFn out)
{
    if (desc.seedSpacing <= 0.0f || desc.maxMembers < desc.minMembers)
        return;

    const float domainW = desc.maxX - desc.minX;
    const float domainH = desc.maxZ - desc.minZ;
    if (domainW <= 0.0f || domainH <= 0.0f)
        return;

    // 1. Poisson-disk seed points over the chunk domain. The sampler is
    //    deterministic given (domainW, domainH, seedSpacing, seed).
    PoissonDiskSampler sampler(domainW, domainH, desc.seedSpacing, desc.seed);
    const PoissonPoint* seedPt = nullptr;
    int seedSlot = 0;
    while ((seedPt = sampler.next()) != nullptr) {
        if (seedSlot >= desc.maxClusterSeeds)
            break;

        // Seed world position (sampler domain is [0, domainW] x [0, domainH];
        // shift into chunk-local coords).
        const float seedWorldX = desc.minX + seedPt->x;
        const float seedWorldZ = desc.minZ + seedPt->y;
        const float seedY = heightField.sample(seedWorldX, seedWorldZ);

        // Skip seeds below water / on cliffs - the whole cluster would be
        // rejected member-by-member anyway.
        if (seedY < desc.minY) {
            ++seedSlot;
            continue;
        }
        if (desc.maxSlope < 1e29f) {
            const float hx = heightField.sample(seedWorldX + desc.slopeEpsilon, seedWorldZ) - seedY;
            const float hz = heightField.sample(seedWorldX, seedWorldZ + desc.slopeEpsilon) - seedY;
            const float slope = std::sqrt(hx * hx + hz * hz) / desc.slopeEpsilon;
            if (slope > desc.maxSlope) {
                ++seedSlot;
                continue;
            }
        }

        // Emit the seed itself as a placement with full cluster weight.
        VegetationPlacement seedPlacement;
        seedPlacement.x = seedWorldX;
        seedPlacement.z = seedWorldZ;
        seedPlacement.y = seedY;
        // Use a deterministic hash for the seed's jitter so downstream
        // species / scale selection is reproducible.
        const int cellX = static_cast<int>(std::round(seedWorldX));
        const int cellZ = static_cast<int>(std::round(seedWorldZ));
        seedPlacement.jitter = arp::core::hash2(cellX, cellZ, desc.seed + 31u);
        seedPlacement.clusterWeight = 1.0f;
        seedPlacement.seedSlot = static_cast<uint8_t>(std::min(seedSlot, 255));
        seedPlacement.ageClass = 0; // AgeStructure composes this.
        seedPlacement.layerClass = 0;
        seedPlacement.edgeGradient = 0.0f; // EdgeGradient composes this.
        out(seedPlacement);

        // 2. Roll the cluster size for this seed.
        const float sizeRoll = arp::core::hash2(
            cellX, cellZ, desc.seed + 1000u + static_cast<uint32_t>(seedSlot) * 7u);
        int clusterSize = desc.minMembers
            + static_cast<int>(
                sizeRoll * static_cast<float>(desc.maxMembers - desc.minMembers + 1));
        clusterSize = std::clamp(clusterSize, desc.minMembers, desc.maxMembers);

        // 3. Scatter cluster members around the seed.
        for (int i = 0; i < clusterSize; ++i) {
            // Independent hash per member: angle, radius, jitter.
            const uint32_t memberSeedBase = desc.seed + 2000u
                + static_cast<uint32_t>(seedSlot) * 101u + static_cast<uint32_t>(i) * 13u;
            const float angle = arp::core::hash2(cellX, cellZ, memberSeedBase + 1u) * 6.283185307f;
            const float radiusRoll = arp::core::hash2(cellX, cellZ, memberSeedBase + 2u);
            const float radius = desc.minRadius + radiusRoll * (desc.maxRadius - desc.minRadius);

            const float memberX = seedWorldX + std::cos(angle) * radius;
            const float memberZ = seedWorldZ + std::sin(angle) * radius;

            // Reject members outside the chunk domain (the caller's
            // nearRoad / biome checks also apply downstream, but bounds
            // rejection here keeps the output set tight).
            if (memberX < desc.minX || memberX >= desc.maxX)
                continue;
            if (memberZ < desc.minZ || memberZ >= desc.maxZ)
                continue;

            const float memberY = heightField.sample(memberX, memberZ);
            if (memberY < desc.minY)
                continue;

            if (desc.maxSlope < 1e29f) {
                const float hx = heightField.sample(memberX + desc.slopeEpsilon, memberZ) - memberY;
                const float hz = heightField.sample(memberX, memberZ + desc.slopeEpsilon) - memberY;
                const float slope = std::sqrt(hx * hx + hz * hz) / desc.slopeEpsilon;
                if (slope > desc.maxSlope)
                    continue;
            }

            VegetationPlacement mp;
            mp.x = memberX;
            mp.z = memberZ;
            mp.y = memberY;
            mp.jitter = arp::core::hash2(static_cast<int>(std::round(memberX)),
                static_cast<int>(std::round(memberZ)),
                desc.seed + 31u);
            // Members near the seed (small radius) have higher weight.
            const float radiusNorm =
                (radius - desc.minRadius) / std::max(desc.maxRadius - desc.minRadius, 1e-4f);
            mp.clusterWeight = std::clamp(1.0f - radiusNorm, 0.0f, 1.0f);
            mp.seedSlot = static_cast<uint8_t>(std::min(seedSlot, 255));
            mp.ageClass = 0;
            mp.layerClass = 0;
            mp.edgeGradient = 0.0f;
            out(mp);
        }

        ++seedSlot;
    }
}

/// Count the number of placements a clusterScatter would produce, without
/// storing them. Useful for pre-sizing the output buffer.
template <typename ClusterScatterDescLike = ClusterScatterDesc>
inline std::size_t countClusterScatter(
    const arp::core::TerrainHeightField& heightField, const ClusterScatterDescLike& desc)
{
    std::size_t count = 0u;
    clusterScatter(heightField, desc, [&count](const VegetationPlacement&) {
        ++count;
    });
    return count;
}

// ---------------------------------------------------------------------------
// 2. EdgeGradient
// ---------------------------------------------------------------------------
//
// The legacy scatter placed the same species set everywhere a biome weight
// was nonzero. Real forests have an ecotone - a transition band at the
// forest-meadow boundary where edge-tolerant species (fern, bush, bramble)
// replace both the deep-forest canopy and the open-meadow grass. This
// sampler detects the ecotone by sampling the biome weights at the
// placement position and a small offset, then computing the gradient
// magnitude. Points with gradient above a threshold are marked as edge
// placements; the caller then picks an edge species.
//
// This is a *post-process* primitive: it does not generate new points. It
// takes an existing VegetationPlacement (from ClusterScatter or any other
// source) and enriches its edgeGradient field. The caller invokes it via
// sampleEdgeGradient() inside its placement loop.

struct EdgeGradientDesc {
    /// The biome field set. Required - without it, no gradient can be
    /// computed and the sampler returns edgeGradient = 0.
    const BiomeWeightSet* biomes = nullptr;

    /// Finite-difference offset (world units) for the gradient estimate.
    /// 4m matches the biome grid cell size and gives a stable derivative.
    float sampleOffset = 4.0f;

    /// Gradient magnitude above which a placement is considered an edge.
    /// Reference: 0.15 (15% weight change over sampleOffset metres).
    float edgeThreshold = 0.15f;

    /// Deterministic seed (currently unused - gradient is analytic from
    /// biomes - but reserved for future stochastic edge jitter).
    uint32_t seed = 1u;
};

/// Compute the biome-weight gradient magnitude at (x, z) and enrich the
/// placement's edgeGradient field. Returns true if the placement sits on
/// an ecotone (edgeGradient >= edgeThreshold).
///
/// The gradient is estimated by central differences on the blackForest and
/// meadow weights:
///   |∂w/∂x| ≈ |w(x+δ) - w(x-δ)| / (2δ)
///   |∂w/∂z| ≈ |w(z+δ) - w(z-δ)| / (2δ)
///   edgeGradient = sqrt(∂blackForest² + ∂meadow²) summed over X and Z.
inline bool sampleEdgeGradient(
    const EdgeGradientDesc& desc, VegetationPlacement& placement) noexcept
{
    placement.edgeGradient = 0.0f;
    if (desc.biomes == nullptr)
        return false;

    const auto bs0 = desc.biomes->sample(placement.x, placement.z);
    const auto bsXp = desc.biomes->sample(placement.x + desc.sampleOffset, placement.z);
    const auto bsXm = desc.biomes->sample(placement.x - desc.sampleOffset, placement.z);
    const auto bsZp = desc.biomes->sample(placement.x, placement.z + desc.sampleOffset);
    const auto bsZm = desc.biomes->sample(placement.x, placement.z - desc.sampleOffset);

    const float dx = desc.sampleOffset * 2.0f;
    const float dBfX = (bsXp.blackForest - bsXm.blackForest) / dx;
    const float dMeX = (bsXp.meadow - bsXm.meadow) / dx;
    const float dBfZ = (bsZp.blackForest - bsZm.blackForest) / dx;
    const float dMeZ = (bsZp.meadow - bsZm.meadow) / dx;

    const float mag = std::sqrt(dBfX * dBfX + dMeX * dMeX + dBfZ * dBfZ + dMeZ * dMeZ);
    placement.edgeGradient = std::clamp(mag, 0.0f, 1.0f);
    return placement.edgeGradient >= desc.edgeThreshold;
}

// ---------------------------------------------------------------------------
// 3. AgeStructure
// ---------------------------------------------------------------------------
//
// J-curve age distribution: real forests have many more saplings than
// mature trees, and very few old-growth specimens. The legacy scatter
// rolled scale uniformly in [minScale, maxScale], producing an even age
// distribution that reads as a tree farm, not a forest.
//
// This sampler rolls an ageClass from a J-curve CDF and stores it on the
// placement. The caller maps ageClass -> scale range and lodClass:
//   ageClass 0 (sapling): scale [0.25, 0.55], lodClass 0 (full wind)
//   ageClass 1 (mature):  scale [0.70, 1.10], lodClass 1 (gust)
//   ageClass 2 (old):     scale [1.10, 1.50], lodClass 2 (sway)
//
// The CDF is parameterized so the caller can tune the J-curve steepness
// per biome (e.g. old-growth blackForest has more ageClass 2 than a
// pioneer meadow). Default produces a 70/25/5 split.

struct AgeStructureDesc {
    /// Probability of ageClass 0 (sapling). Reference: 0.70.
    float saplingFraction = 0.70f;
    /// Probability of ageClass 1 (mature), drawn from the remainder after
    /// sapling. Reference: 0.83 (i.e. 25% of total, 5% left for old).
    float matureFractionOfRemainder = 0.833f;

    /// Deterministic seed base; the actual per-placement seed is
    /// (seedBase ^ hashOfPosition). Kept separate from the scatter seed
    /// so age rolls are independent of cluster rolls.
    uint32_t seedBase = 0x5EED5EEDu;
};

/// Roll an ageClass for a placement using a J-curve CDF. Mutates the
/// placement's ageClass field. The roll is a pure function of
/// (placement.x, placement.z, desc) -> deterministic.
inline void sampleAgeStructure(
    const AgeStructureDesc& desc, VegetationPlacement& placement) noexcept
{
    // Hash the placement position to get a deterministic roll. We use a
    // different seed offset from the scatter jitter (31u) so the age roll
    // is independent of the species roll.
    const uint32_t ageSeed = desc.seedBase ^ 0xA6E5u;
    const float r = arp::core::hash2(static_cast<int>(std::round(placement.x * 8.0f)),
        static_cast<int>(std::round(placement.z * 8.0f)),
        ageSeed);

    if (r < desc.saplingFraction) {
        placement.ageClass = 0;
    } else {
        const float remainder = 1.0f - desc.saplingFraction;
        const float r2 = (r - desc.saplingFraction) / remainder;
        if (r2 < desc.matureFractionOfRemainder) {
            placement.ageClass = 1;
        } else {
            placement.ageClass = 2;
        }
    }
}

/// Map an ageClass to a scale, given the species' min/max scale. The
/// mapping is deterministic (no extra hash) and produces:
///   ageClass 0 -> scale in [minScale*0.5, minScale*0.9]
///   ageClass 1 -> scale in [minScale,     maxScale]
///   ageClass 2 -> scale in [maxScale,     maxScale*1.3]
/// The actual scale within the band is rolled from hash2(x, z, seed).
inline float ageClassScale(
    uint8_t ageClass, float minScale, float maxScale, float x, float z, uint32_t seed) noexcept
{
    const float bandRoll = arp::core::hash2(static_cast<int>(std::round(x * 8.0f)),
        static_cast<int>(std::round(z * 8.0f)),
        seed ^ 0x5CA1Eu);
    if (ageClass == 0) {
        const float lo = minScale * 0.5f;
        const float hi = minScale * 0.9f;
        return lo + bandRoll * (hi - lo);
    }
    if (ageClass == 2) {
        const float lo = maxScale;
        const float hi = maxScale * 1.3f;
        return lo + bandRoll * (hi - lo);
    }
    return minScale + bandRoll * (maxScale - minScale);
}

/// Map an ageClass to an animation lodClass. Saplings always get full wind
/// (they're small and close to the camera in understory); mature trees
/// get gust; old trees get sway. The caller may override based on distance.
inline uint8_t ageClassToLodClass(uint8_t ageClass) noexcept
{
    switch (ageClass) {
    case 0:
        return 0; // sapling  -> full wind
    case 1:
        return 1; // mature   -> gust
    case 2:
        return 2; // old      -> sway
    default:
        return 3;
    }
}

// ---------------------------------------------------------------------------
// 4. Composed entry point: ecologicalScatter
// ---------------------------------------------------------------------------
//
// Convenience wrapper that runs ClusterScatter -> EdgeGradient ->
// AgeStructure in sequence. This is the typical entry point for tree /
// understory / bush placement. Ground-cover grass still uses the legacy
// terrainScatter (its density is too high for cluster scatter to add
// value, and the grid jitter is invisible at grass scale).

struct EcologicalScatterDesc {
    ClusterScatterDesc cluster;
    EdgeGradientDesc edge;
    AgeStructureDesc age;
};

template <typename OutputFn>
void ecologicalScatter(const arp::core::TerrainHeightField& heightField,
    const EcologicalScatterDesc& desc,
    OutputFn out)
{
    clusterScatter(heightField, desc.cluster, [&](VegetationPlacement p) {
        sampleEdgeGradient(desc.edge, p);
        sampleAgeStructure(desc.age, p);
        out(p);
    });
}

} // namespace arp::pcg
