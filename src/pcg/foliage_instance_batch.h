#pragma once

/// @file foliage_instance_batch.h
/// @brief P6: Instance data structures for instanced vegetation rendering.
///
/// Replaces the P5 CPU-baked merged-mesh vegetation path. Instead of
/// duplicating geometry per scatter point, a single template mesh is uploaded
/// once and instanced N times via drawIndexed(instanceCount=N). Each instance
/// carries only its transform (position, scale, yaw) and attributes (phase,
/// stable ID, LOD class) -- never baked into world-space vertices.
///
/// The instance vertex format (InstanceVertex) extends MeshVertex with three
/// wind-weight channels (primaryBend, flutter, stiffness) read from GLB
/// COLOR_0. This is the same data FoliageVertex was designed for, but in a
/// GPU-friendly layout that matches the shader's vertex input.

#include "assets/asset_pipeline.h" // assets::MeshAsset, MeshVertex
#include "core/cpu_allocator.h"
#include "pcg/lod_system.h" // P4: LodProfile for per-category LOD
#include "terrain/terrain_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace arp::pcg {

/// GPU vertex format for instanced rendering. Extends MeshVertex with wind
/// weights (COLOR_0 channel). Used for foliage cards, tree variants, and rock
/// variants. Rocks use default wind (0, 0, 1) = no bend, no flutter, max
/// stiffness.
///
/// Layout (60 bytes):
///   offset 0:  position  (float3, POSITION0)
///   offset 12: normal    (float3, NORMAL0)
///   offset 24: uv        (float2, TEXCOORD0)
///   offset 32: tangent   (float4, TANGENT0)
///   offset 48: wind      (float3, COLOR0) = (primaryBend, flutter, stiffness)
struct InstanceVertex {
    float position[3] = {0.0f, 0.0f, 0.0f};
    float normal[3] = {0.0f, 0.0f, 1.0f};
    float uv[2] = {0.0f, 0.0f};
    float tangent[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    float primaryBend = 0.0f; ///< COLOR_0.R: 0 = root pinned, 1 = tip
    float flutter = 0.0f;     ///< COLOR_0.G: 0 = wood, >0 = foliage
    float stiffness = 1.0f;   ///< COLOR_0.B: 0 = flexible, 1 = rigid
};

struct InstanceVertexLocalBounds {
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

inline bool computeInstanceVertexLocalBounds(const InstanceVertex* vertices,
    std::size_t vertexCount,
    InstanceVertexLocalBounds& result) noexcept
{
    if (vertices == nullptr || vertexCount == 0u) {
        result = {};
        return false;
    }
    float minValue[3]{vertices[0].position[0], vertices[0].position[1], vertices[0].position[2]};
    float maxValue[3]{minValue[0], minValue[1], minValue[2]};
    for (std::size_t index = 1u; index < vertexCount; ++index) {
        for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
            minValue[axis] = std::min(minValue[axis], vertices[index].position[axis]);
            maxValue[axis] = std::max(maxValue[axis], vertices[index].position[axis]);
        }
    }
    for (std::uint32_t axis = 0u; axis < 3u; ++axis) {
        result.center[axis] = 0.5f * (minValue[axis] + maxValue[axis]);
    }
    float radiusSquared = 0.0f;
    for (std::size_t index = 0u; index < vertexCount; ++index) {
        const float x = vertices[index].position[0] - result.center[0];
        const float y = vertices[index].position[1] - result.center[1];
        const float z = vertices[index].position[2] - result.center[2];
        radiusSquared = std::max(radiusSquared, x * x + y * y + z * z);
    }
    result.radius = std::sqrt(radiusSquared);
    return true;
}

/// GPU instance data: uploaded to slot 1 (instance-rate buffer). 64 bytes
/// (4 x float4 for safe std140 / uniform buffer alignment and to match the
/// RHI VertexFormat::Float32x4 attribute path shared by all four backends).
///
///   positionScale:      xyz = world position, w = uniform scale
///   yawPhaseIdLod:      x = yaw (radians), y = wind phase [0,1),
///                       z = stable instance ID (as float), w = LOD class (0-2)
///   extraTransform:     x = pitchX (radians, tilt around X),
///                       y = rollZ (radians, tilt around Z),
///                       z = scaleY (0 = use uniform scale, >0 = non-uniform Y),
///                       w = burialY (vertical offset, negative = sink into ground)
///   speciesLodState:    P0 (Track C) extension. Stored as float[4] so the
///                       vertex attribute travels the same Float32x4 path as
///                       the other three slots (the RHI does not expose a
///                       Uint32x4 VertexFormat). The float bits are the raw
///                       bit pattern of packed uint32 metadata; the shader
///                       decodes them via `asuint()`. Packed bit layout:
///                         speciesLodState[0] bits  0-7  : speciesId   (u8, P1)
///                         speciesLodState[0] bits  8-15 : ageClass    (u8, P1)
///                         speciesLodState[0] bits 16-23 : moisture    (u8, P2)
///                         speciesLodState[0] bits 24-31 : slopeClass  (u8, P2)
///                         speciesLodState[1] bits  0-7  : geometryLod (u8, P4)
///                         speciesLodState[1] bits  8-15 : shadowLod   (u8, P6)
///                       InstanceLodCrossfade/1 uses words 2/3 only when word3 bit31 is enabled:
///                       word2 = float start seconds; word3 = enable31, incoming30, seed16..29, durationMs0..15.
///                         speciesLodState[1] bits 16-31 : stateFlags  (u16,P8)
///                         speciesLodState[2..3] reserved (0)
///                       Default {0,0,0,0} = species 0, age 0, no moisture/slope
///                       bias, geometryLod 0, shadowLod 0, state intact. This is
///                       backward-compatible with the prior 48-byte layout: all
///                       existing placement paths leave the field zeroed, so
///                       the shader reads species 0 / lod 0 / intact and
///                       behaves identically to the pre-extension pipeline.
struct InstanceDataGpu {
    float positionScale[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float yawPhaseIdLod[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float extraTransform[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float speciesLodState[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// Bit-pack a uint32 into the float storage of speciesLodState[idx]. Uses
/// memcpy (not reinterpret_cast) to avoid strict-aliasing UB; the GPU reads
/// the same bits via `asuint()` in the shader. Bit-for-bit identical across
/// all four backends (Vulkan/Metal/WebGPU/Null) since Float32x4 is a raw
/// 16-byte attribute with no format conversion.
inline void packInstanceMetadataFloat(float& storage, uint32_t bits) noexcept
{
    std::memcpy(&storage, &bits, sizeof(float));
}

inline uint32_t unpackInstanceMetadataFloat(const float& storage) noexcept
{
    uint32_t bits = 0u;
    std::memcpy(&bits, &storage, sizeof(uint32_t));
    return bits;
}

/// Decode helpers for the packed speciesLodState field. These mirror the bit
/// layout documented on InstanceDataGpu and are the single source of truth
/// for both CPU placement (P1/P2/P4/P6/P8) and the contract tests. The shader
/// side decodes the same layout in `decodeInstanceMetadata` (foliage.slang /
/// pbr_scene.slang vertexMainInstanced) -- the two must stay byte-identical,
/// asserted by tests/contract/wocc_instance_layout_contract_tests.cpp.
inline uint8_t instanceSpeciesId(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>(unpackInstanceMetadataFloat(g.speciesLodState[0]) & 0xFFu);
}

inline uint8_t instanceAgeClass(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[0]) >> 8) & 0xFFu);
}

inline uint8_t instanceMoisture(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[0]) >> 16) & 0xFFu);
}

inline uint8_t instanceSlopeClass(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[0]) >> 24) & 0xFFu);
}

inline uint8_t instanceGeometryLod(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>(unpackInstanceMetadataFloat(g.speciesLodState[1]) & 0xFFu);
}

inline uint8_t instanceShadowLod(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint8_t>((unpackInstanceMetadataFloat(g.speciesLodState[1]) >> 8) & 0xFFu);
}

inline uint16_t instanceStateFlags(const InstanceDataGpu& g) noexcept
{
    return static_cast<uint16_t>(unpackInstanceMetadataFloat(g.speciesLodState[1]) >> 16);
}

inline void setInstanceMetadata(InstanceDataGpu& g,
    uint8_t speciesId,
    uint8_t ageClass,
    uint8_t moisture,
    uint8_t slopeClass,
    uint8_t geometryLod,
    uint8_t shadowLod,
    uint16_t stateFlags) noexcept
{
    const uint32_t word0 = (static_cast<uint32_t>(speciesId))
        | (static_cast<uint32_t>(ageClass) << 8) | (static_cast<uint32_t>(moisture) << 16)
        | (static_cast<uint32_t>(slopeClass) << 24);
    const uint32_t word1 = (static_cast<uint32_t>(geometryLod))
        | (static_cast<uint32_t>(shadowLod) << 8) | (static_cast<uint32_t>(stateFlags) << 16);
    packInstanceMetadataFloat(g.speciesLodState[0], word0);
    packInstanceMetadataFloat(g.speciesLodState[1], word1);
    packInstanceMetadataFloat(g.speciesLodState[2], 0u);
    packInstanceMetadataFloat(g.speciesLodState[3], 0u);
}

/// CPU-side instance data (convenient field access, converted to
/// InstanceDataGpu at upload time).
struct InstanceData {
    float x = 0.0f;          ///< World X
    float y = 0.0f;          ///< World Y (terrain height)
    float z = 0.0f;          ///< World Z
    float scale = 1.0f;      ///< Uniform scale (XZ plane; Y uses scaleY if nonzero)
    float yaw = 0.0f;        ///< Y rotation (radians)
    float phase = 0.0f;      ///< Wind phase [0, 1)
    uint32_t instanceId = 0; ///< Stable deterministic ID
    uint32_t lodClass = 0;   ///< 0 = near (full), 1 = mid, 2 = far

    // P8: Terrain fusion transform - burial, tilt, non-uniform scale.
    // Default {0,0,0,0} = no tilt, uniform Y scale, no burial (backward compat).
    float pitchX = 0.0f;  ///< Tilt around X axis (radians) - rocks lean with slope
    float rollZ = 0.0f;   ///< Tilt around Z axis (radians) - asymmetric lean
    float scaleY = 0.0f;  ///< Y scale multiplier (0 = use uniform scale)
    float burialY = 0.0f; ///< Vertical offset (negative = sink into ground)

    // P0 (Track C): Packed metadata. Default values preserve the pre-extension
    // pipeline behavior (species 0, age 0, geometryLod 0, shadowLod 0, intact).
    // Populate via the explicit setters in P1+ placement paths; the legacy
    // grid-jittered scatter leaves these zeroed.
    uint8_t speciesId = 0;   ///< P1: species palette index
    uint8_t ageClass = 0;    ///< P1: 0=sapling, 1=mature, 2=old
    uint8_t moisture = 0;    ///< P2: 0..255 microhabitat moisture
    uint8_t slopeClass = 0;  ///< P2: 0=flat, 1=gentle, 2=moderate, 3=steep
    uint8_t geometryLod = 0; ///< P4: runtime geometry LOD (0/1/2), filled by unified LodProfile
    uint8_t shadowLod = 0;   ///< P6: shadow caster LOD (0=full, 1=proxy, 2=off)
    uint16_t stateFlags = 0; ///< P8: bit0=burning, bit1=chopped, bit2=frozen

    terrain::TerrainSurfaceQueryStatus anchorToSurface(const terrain::TerrainSurface& surface)
    {
        if (!std::isfinite(x) || !std::isfinite(z))
            return terrain::TerrainSurfaceQueryStatus::InvalidData;
        const auto sampled = surface.sample(x, z);
        if (!sampled.ready())
            return sampled.status;
        if (!std::isfinite(sampled.sample.height))
            return terrain::TerrainSurfaceQueryStatus::InvalidData;
        y = sampled.sample.height;
        return terrain::TerrainSurfaceQueryStatus::Ready;
    }

    InstanceDataGpu toGpu() const
    {
        InstanceDataGpu g;
        g.positionScale[0] = x;
        g.positionScale[1] = y;
        g.positionScale[2] = z;
        g.positionScale[3] = scale;
        g.yawPhaseIdLod[0] = yaw;
        g.yawPhaseIdLod[1] = phase;
        g.yawPhaseIdLod[2] = static_cast<float>(instanceId);
        g.yawPhaseIdLod[3] = static_cast<float>(lodClass);
        g.extraTransform[0] = pitchX;
        g.extraTransform[1] = rollZ;
        g.extraTransform[2] = scaleY;
        g.extraTransform[3] = burialY;
        setInstanceMetadata(
            g, speciesId, ageClass, moisture, slopeClass, geometryLod, shadowLod, stateFlags);
        return g;
    }
};

/// A batch of instances sharing the same template. One batch = one
/// drawIndexed call with instanceCount = instances.size().
struct InstanceBatch {
    uint16_t templateId = 0; ///< Index into VegetationTemplateRegistry
    float baseColor[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    uint32_t materialIndex = UINT32_MAX; ///< P6: per-batch material (set by render session)
    arp::core::pmr::Vector<InstanceData> instances{&arp::core::defaultCpuMemoryResource()};

    void clear() { instances.clear(); }

    bool empty() const { return instances.empty(); }

    std::size_t size() const { return instances.size(); }
};

/// Describes which templates are available. Used by buildChunkVegetation
/// to select template IDs deterministically without needing GPU access.
struct VegetationTemplateConfig {
    bool hasEnvironmentAssets = false;

    // Foliage atlas template counts (at least 3 per atlas = 9 total).
    uint16_t grassCardCount = 0;
    uint16_t weedCardCount = 0;
    uint16_t broadleafCardCount = 0;

    // P1 (Track C): new ground-cover atlas counts. Each family is baked
    // by foliage_card_baker and registered as a contiguous template block
    // immediately after broadleaf. The family layout is:
    //   grass | weed | broadleaf | fern | bush | flower | mushroom | tree...
    // These are alpha-tested card families; isGroundCoverFoliage() returns
    // true for all of them, so they share the wind/alpha-test/no-shadow
    // rendering path with the existing three.
    uint16_t fernCardCount = 0;
    uint16_t bushCardCount = 0;
    uint16_t flowerCardCount = 0;
    uint16_t mushroomCardCount = 0;

    // Tree family variant counts.
    uint16_t maturePineVariantCount = 0;
    uint16_t youngFirVariantCount = 0;
    uint16_t youngPineVariantCount = 0;

    // Rock family variant counts.
    uint16_t mossyRock01VariantCount = 0;
    uint16_t mossyRock02VariantCount = 0;

    // P5: Geometry LOD counts for tree/rock GLB assets. Each (variant, LOD)
    // pair is registered as a separate template. Template layout per family:
    //   familyBase + variant + lod * familyVariantCount
    // So all LOD0 variants come first, then LOD1, then LOD2. When assets
    // only ship LOD0, these are 1 and the layout collapses to the legacy
    // variant-only layout.
    uint16_t treeLodCount = 1;
    uint16_t rockLodCount = 1;

    // Fallback procedural template indices (set when no env assets).
    uint16_t proceduralGrassTemplate = 0;
    uint16_t proceduralTreeTemplate = 0;
    uint16_t proceduralUnderstoryTemplate = 0;
    uint16_t proceduralRockTemplate = 0;
    uint16_t proceduralSlabRockTemplate = 0;

    // P1 (Track C): family layout helpers are defined as free functions
    // below the TemplateCategory enum (which is declared after this struct).
    // See familyTemplateBase() / familyVariantCount() / familyLodStride().
};

/// Template type categories for material/draw selection.
enum class TemplateCategory : uint8_t {
    FoliageGrass,     // grass_blade_atlas
    FoliageWeed,      // wild_grass_weed_atlas
    FoliageBroadleaf, // broadleaf_atlas
    // P1 (Track C): new ground-cover categories for the species palette.
    // These are alpha-tested card families like the existing three, baked
    // by the foliage_card_baker and consumed by buildChunkVegetationInstances
    // via the SpeciesPalette edge/cluster/age selection.
    FoliageFern,     // forest_fern_atlas - edge species (forest ecotone)
    FoliageBush,     // bush_atlas         - cluster-forming low shrub
    FoliageFlower,   // meadow_flower_atlas - accent (rare, biome-gated)
    FoliageMushroom, // forest_mushroom_atlas - accent (rare, forest floor)
    TreeMaturePine,
    TreeYoungFir,
    TreeYoungPine,
    RockMossy01,
    RockMossy02,
    ProceduralGrass,
    ProceduralTree,
    ProceduralUnderstory,
    ProceduralRock,
    ProceduralSlabRock,
};

/// True for ground-cover foliage categories (grass/weed/broadleaf/procedural
/// grass) that use alpha-tested card rendering. These are the categories that:
///   - advance wind phase per-instance each frame (render_session wind loop),
///   - use alpha-tested rendering (useAlphaTest=true in WoccInstancedDraw),
///   - should NOT cast shadows (cards are too small/dense to produce useful
///     shadows, and their empty shadow_depth fragment writes transparent-pixel
///     depth, causing self-shadowing artifacts on the ground beneath them).
///
/// Centralized here to avoid the three-way duplication that existed when
/// render_session.cpp reimplemented this category check at the wind-phase
/// site, the useAlphaTest site, and the castsShadow site.
inline constexpr bool isGroundCoverFoliage(TemplateCategory category) noexcept
{
    return category == TemplateCategory::FoliageGrass || category == TemplateCategory::FoliageWeed
        || category == TemplateCategory::FoliageBroadleaf
        || category == TemplateCategory::FoliageFern || category == TemplateCategory::FoliageBush
        || category == TemplateCategory::FoliageFlower
        || category == TemplateCategory::FoliageMushroom
        || category == TemplateCategory::ProceduralGrass;
}

/// P1 (Track C): Resolve a category to its family's starting templateId in
/// the registry. The family layout is:
///   grass | weed | broadleaf | fern | bush | flower | mushroom | tree* | rock*
/// where tree/rock families are further stratified by (variant, LOD) pairs.
/// The caller adds `variant + geometryLod * familyLodStride()` to this base.
inline uint16_t familyTemplateBase(
    const VegetationTemplateConfig& cfg, TemplateCategory category) noexcept
{
    uint16_t base = 0;
    if (category == TemplateCategory::FoliageGrass)
        return base;
    base += cfg.grassCardCount;
    if (category == TemplateCategory::FoliageWeed)
        return base;
    base += cfg.weedCardCount;
    if (category == TemplateCategory::FoliageBroadleaf)
        return base;
    base += cfg.broadleafCardCount;
    if (category == TemplateCategory::FoliageFern)
        return base;
    base += cfg.fernCardCount;
    if (category == TemplateCategory::FoliageBush)
        return base;
    base += cfg.bushCardCount;
    if (category == TemplateCategory::FoliageFlower)
        return base;
    base += cfg.flowerCardCount;
    if (category == TemplateCategory::FoliageMushroom)
        return base;
    base += cfg.mushroomCardCount;
    if (category == TemplateCategory::TreeMaturePine)
        return base;
    base += static_cast<uint16_t>(cfg.maturePineVariantCount * cfg.treeLodCount);
    if (category == TemplateCategory::TreeYoungFir)
        return base;
    base += static_cast<uint16_t>(cfg.youngFirVariantCount * cfg.treeLodCount);
    if (category == TemplateCategory::TreeYoungPine)
        return base;
    base += static_cast<uint16_t>(cfg.youngPineVariantCount * cfg.treeLodCount);
    if (category == TemplateCategory::RockMossy01)
        return base;
    base += static_cast<uint16_t>(cfg.mossyRock01VariantCount * cfg.rockLodCount);
    if (category == TemplateCategory::RockMossy02)
        return base;
    return base;
}

/// Family variant count for a given category (ignoring LOD stride).
inline uint16_t familyVariantCount(
    const VegetationTemplateConfig& cfg, TemplateCategory category) noexcept
{
    switch (category) {
    case TemplateCategory::FoliageGrass:
        return cfg.grassCardCount;
    case TemplateCategory::FoliageWeed:
        return cfg.weedCardCount;
    case TemplateCategory::FoliageBroadleaf:
        return cfg.broadleafCardCount;
    case TemplateCategory::FoliageFern:
        return cfg.fernCardCount;
    case TemplateCategory::FoliageBush:
        return cfg.bushCardCount;
    case TemplateCategory::FoliageFlower:
        return cfg.flowerCardCount;
    case TemplateCategory::FoliageMushroom:
        return cfg.mushroomCardCount;
    case TemplateCategory::TreeMaturePine:
        return cfg.maturePineVariantCount;
    case TemplateCategory::TreeYoungFir:
        return cfg.youngFirVariantCount;
    case TemplateCategory::TreeYoungPine:
        return cfg.youngPineVariantCount;
    case TemplateCategory::RockMossy01:
        return cfg.mossyRock01VariantCount;
    case TemplateCategory::RockMossy02:
        return cfg.mossyRock02VariantCount;
    default:
        return 0;
    }
}

/// Number of variants in one LOD row.  Tree/rock templates are registered as
/// `[lod0 variants][lod1 variants][lod2 variants]`, so the caller advances
/// from one LOD row with `geometryLod * familyLodStride()`.  This is
/// intentionally the variant count, not the number of LODs.
inline uint16_t familyLodStride(
    const VegetationTemplateConfig& cfg, TemplateCategory category) noexcept
{
    switch (category) {
    case TemplateCategory::TreeMaturePine:
    case TemplateCategory::TreeYoungFir:
    case TemplateCategory::TreeYoungPine:
        return familyVariantCount(cfg, category);
    case TemplateCategory::RockMossy01:
    case TemplateCategory::RockMossy02:
        return familyVariantCount(cfg, category);
    default:
        return 1; // foliage cards have no LOD variants
    }
}

// ─── P4: Per-category vegetation LOD profiles ───────────────────────────────
// These replace the previously scattered LOD thresholds:
//   - S2 (geometry LOD): 3 duplicated lambdas with 10/20m, no hysteresis
//   - S3 (animation LOD): chunk-center proxy with 20/40/60m, no hysteresis
//   - S4 (distance cull): per-category constants 15/30/40/60m, no hysteresis
// Now one profile per family drives geometry LOD, animation LOD class, and
// cull -- all from the same tier resolution with uniform hysteresis.

/// Trees (MaturePine/YoungFir/YoungPine/ProceduralTree): 3 geometry tiers + cull.
/// Geometry LOD 0/1/2 maps to tiers 0/1/2. Wind class maps via tierToWindClass.
/// Hysteresis 3m prevents flicker at 10m/20m boundaries.
/// Cull at 40m (matches the legacy kTreeDistSq threshold).
inline LodProfile treeLodProfile() noexcept
{
    LodProfile p;
    p.tiers[0] = {10.0f, 3.0f}; // LOD0: within 10m (full detail mesh)
    p.tiers[1] = {20.0f, 3.0f}; // LOD1: 10-20m (half detail mesh)
    p.tiers[2] = {40.0f, 3.0f}; // LOD2: 20-40m (coarsest mesh, to cull)
    p.tierCount = 3;
    p.cullDistance = 40.0f;
    // P6: Trees cast shadows up to 80m (CSM cascade 1 far). Beyond that,
    // only cascade 2 (80-300m, 1024px) captures them -- too low-res to matter.
    p.shadowCullDistance = 80.0f;
    return p;
}

/// Rocks (Mossy01/Mossy02/Procedural/Slab): 3 tiers, cull at 60m.
/// Rocks are visible farther than trees (no canopy occlusion, high contrast).
inline LodProfile rockLodProfile() noexcept
{
    LodProfile p;
    p.tiers[0] = {10.0f, 3.0f};
    p.tiers[1] = {20.0f, 3.0f};
    p.tiers[2] = {60.0f, 5.0f}; // LOD2 extends to cull (wider hysteresis)
    p.tierCount = 3;
    p.cullDistance = 60.0f;
    // P6: Rocks cast shadows up to 80m (same rationale as trees).
    p.shadowCullDistance = 80.0f;
    return p;
}

/// Grass/ground cover (Grass/Weed/Broadleaf/Fern/Bush/Flower/Mushroom/
/// ProceduralGrass): single tier (no geometry LOD -- card meshes are cheap),
/// cull at 15m. Wind class is always 0 (full wind) for grass regardless of
/// tier, but the profile is still queried for cull consistency.
inline LodProfile grassLodProfile() noexcept
{
    LodProfile p;
    p.tiers[0] = {15.0f, 0.0f}; // no hysteresis for single-tier
    p.tierCount = 1;
    p.cullDistance = 15.0f;
    return p;
}

/// Understory (ProceduralUnderstory): single tier, cull at 30m.
inline LodProfile understoryLodProfile() noexcept
{
    LodProfile p;
    p.tiers[0] = {30.0f, 0.0f};
    p.tierCount = 1;
    p.cullDistance = 30.0f;
    return p;
}

/// Resolve the LodProfile for a given TemplateCategory.
/// Returns a const reference to a function-local static -- safe because the
/// profiles are pure-value (no mutable state).
inline const LodProfile& categoryLodProfile(TemplateCategory cat) noexcept
{
    switch (cat) {
    case TemplateCategory::FoliageGrass:
    case TemplateCategory::FoliageWeed:
    case TemplateCategory::FoliageBroadleaf:
    case TemplateCategory::FoliageFern:
    case TemplateCategory::FoliageBush:
    case TemplateCategory::FoliageFlower:
    case TemplateCategory::FoliageMushroom:
    case TemplateCategory::ProceduralGrass: {
        static const LodProfile p = grassLodProfile();
        return p;
    }
    case TemplateCategory::ProceduralUnderstory: {
        static const LodProfile p = understoryLodProfile();
        return p;
    }
    case TemplateCategory::TreeMaturePine:
    case TemplateCategory::TreeYoungFir:
    case TemplateCategory::TreeYoungPine:
    case TemplateCategory::ProceduralTree: {
        static const LodProfile p = treeLodProfile();
        return p;
    }
    case TemplateCategory::RockMossy01:
    case TemplateCategory::RockMossy02:
    case TemplateCategory::ProceduralRock:
    case TemplateCategory::ProceduralSlabRock: {
        static const LodProfile p = rockLodProfile();
        return p;
    }
    default: {
        static const LodProfile p = treeLodProfile(); // safe fallback
        return p;
    }
    }
}

/// A template entry in the registry. The GPU buffers are created once at
/// startup and shared across all chunks.
struct VegetationTemplate {
    struct Section {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t materialSlot = 0;
    };

    TemplateCategory category = TemplateCategory::ProceduralGrass;
    uint16_t id = 0;           ///< Index in the registry
    uint16_t variantIndex = 0; ///< Variant index within the category
    uint16_t lodIndex = 0;     ///< Geometry LOD index within the category
    arp::core::pmr::Vector<InstanceVertex> vertices{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<uint32_t> indices{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::Vector<Section> sections{&arp::core::defaultCpuMemoryResource()};
    float baseColor[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    uint32_t materialIndex = UINT32_MAX; ///< Set by render session
};

inline bool matchesFamilyTemplateAddress(
    const VegetationTemplateConfig& config, const VegetationTemplate& entry) noexcept
{
    const auto variants = familyVariantCount(config, entry.category);
    const auto lods = entry.category == TemplateCategory::TreeMaturePine
            || entry.category == TemplateCategory::TreeYoungFir
            || entry.category == TemplateCategory::TreeYoungPine
        ? config.treeLodCount
        : entry.category == TemplateCategory::RockMossy01
            || entry.category == TemplateCategory::RockMossy02
        ? config.rockLodCount
        : 1u;
    return entry.variantIndex < variants && entry.lodIndex < lods
        && entry.id
        == familyTemplateBase(config, entry.category) + entry.variantIndex
            + entry.lodIndex * familyLodStride(config, entry.category);
}

/// Result of per-chunk vegetation generation. Carries either instance batches
/// (P6 path) or a fallback merged mesh (prototype/A-B path).
struct ChunkVegetationResult {
    // Optional publisher-issued content identity. Zero means unpublished.
    // Consumers that cache generated content must compare this revision.
    std::uint64_t revision = 0u;
    arp::core::pmr::Vector<InstanceBatch> batches{&arp::core::defaultCpuMemoryResource()};
    bool useInstances = false;

    // Fallback merged mesh (prototype mode when useInstances = false).
    assets::MeshAsset fallbackMesh;

    std::size_t totalInstanceCount() const
    {
        std::size_t n = 0;
        for (const auto& b : batches)
            n += b.instances.size();
        return n;
    }
};

/// Convert a MeshAsset (MeshVertex) + optional wind weights to a vector of
/// InstanceVertex. Used for tree/rock variant meshes from GLBs.
inline arp::core::pmr::Vector<InstanceVertex> convertToInstanceVertices(
    const assets::MeshAsset& mesh,
    const float* windWeights4, // nullptr or float[vertices.size()*4]
    bool hasWindData)
{
    arp::core::pmr::Vector<InstanceVertex> out{&arp::core::defaultCpuMemoryResource()};
    out.reserve(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& mv = mesh.vertices[i];
        InstanceVertex iv;
        iv.position[0] = mv.position[0];
        iv.position[1] = mv.position[1];
        iv.position[2] = mv.position[2];
        iv.normal[0] = mv.normal[0];
        iv.normal[1] = mv.normal[1];
        iv.normal[2] = mv.normal[2];
        iv.uv[0] = mv.uv[0];
        iv.uv[1] = mv.uv[1];
        iv.tangent[0] = mv.tangent[0];
        iv.tangent[1] = mv.tangent[1];
        iv.tangent[2] = mv.tangent[2];
        iv.tangent[3] = mv.tangent[3];
        if (hasWindData && windWeights4) {
            iv.primaryBend = windWeights4[i * 4 + 0];
            iv.flutter = windWeights4[i * 4 + 1];
            iv.stiffness = windWeights4[i * 4 + 2];
        } else {
            iv.primaryBend = 0.0f;
            iv.flutter = 0.0f;
            iv.stiffness = 1.0f; // rigid (rocks, wood)
        }
        out.push_back(iv);
    }
    return out;
}

} // namespace arp::pcg
