#pragma once
#include "3d/lod/lod_profile.h"
#include "3d/vegetation/animation_lod.h"
#include "3d/vegetation/instance.h"

namespace arp::vegetation {
using lod::LodProfile;

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
        static const LodProfile p = [] {
            LodProfile invalid;
            invalid.tierCount = 0;
            return invalid;
        }();
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

} // namespace arp::vegetation
