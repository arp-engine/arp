#pragma once

/// @file foliage_card_baker.h
/// @brief P6: Offline foliage card template generator.
///
/// Generates deterministic geometry templates from foliage atlas metadata.
/// Each atlas produces at least 3 card templates (grass_blade_atlas: short
/// grass + tall grass; wild_grass_weed_atlas: weed variants; broadleaf_atlas:
/// cluster variants). Each template is a crossed-quad card whose UVs map to
/// the atlas, with per-vertex wind weights (primaryBend=0 at root, 1 at tip;
/// flutter increasing toward tip; stiffness slightly decreasing).
///
/// Template variation comes from width, height, bend curve, and blade count
/// -- not just yaw/scale. This matches the P6 requirement: "变化来自宽高、
/// 弯曲、叶片数量和簇形，不只改变 yaw/scale".
///
/// The baker also converts tree variant meshes (from NamedVariant) and rock
/// variant meshes into InstanceVertex format for the shared template registry.

#include "pcg/foliage_instance_batch.h"

#include <cstdint>
#include <vector>

namespace arp::pcg {

/// Parameters for a single card template.
struct CardTemplateDesc {
    float width = 0.5f;            ///< Card width in world units
    float height = 1.0f;           ///< Card height
    float bendCurve = 0.0f;        ///< 0 = straight, 1 = max curve (bend tip forward)
    uint32_t bladeCount = 2;       ///< 2 = crossed quad, 3 = tri-blade, etc.
    float clusterRadius = 0.0f;    ///< Horizontal spread radius for tuft cards
    float uvScaleU = 1.0f;         ///< UV tile scale (for atlas sub-region)
    float uvOffsetU = 0.0f;        ///< UV offset (for atlas sub-region)
    float uvScaleV = 1.0f;         ///< UV tile scale on V (for atlas sub-region)
    float uvOffsetV = 0.0f;        ///< UV offset on V (for atlas sub-region)
    float tipBendWeight = 0.8f;    ///< Max primaryBend at tip
    float tipFlutterWeight = 0.7f; ///< Max flutter at tip
    float tipStiffness = 0.3f;     ///< Min stiffness at tip (more flexible)
    bool doubleSided = true;
};

/// Generate a single card template from a CardTemplateDesc. Produces
/// crossed quads (or tri-blade) with per-vertex wind weights.
/// The card is centered at origin, base at y=0, extending upward.
std::vector<InstanceVertex> bakeCardTemplate(const CardTemplateDesc& desc);

/// Generate N deterministic card templates for a foliage atlas.
/// Each template varies width, height, bend, and blade count.
/// Template index 0 is always the "primary" card; subsequent indices are
/// variations.
struct AtlasTemplateSet {
    std::vector<std::vector<InstanceVertex>> templates;
    std::vector<std::vector<uint32_t>> indices;
};

/// Bake a set of grass card templates from the grass_blade_atlas.
/// Produces at least 4 templates: short grass, medium grass, tall grass,
/// and a dense tuft (3 blades).
AtlasTemplateSet bakeGrassAtlasTemplates();

/// Bake a set of weed card templates from the wild_grass_weed_atlas.
/// Produces at least 3 templates: thin weed, broad weed, and seed-head.
AtlasTemplateSet bakeWeedAtlasTemplates();

/// Bake a set of broadleaf cluster card templates from the broadleaf_atlas.
/// Produces at least 3 templates: single-leaf, 3-leaf cluster, and 5-leaf
/// fan.
AtlasTemplateSet bakeBroadleafAtlasTemplates();

/// P1 (Track C): Bake a set of fern frond card templates for the
/// forest_fern_atlas. Ferns are the primary edge species - they colonize
/// the forest-meadow ecotone and the forest floor where light penetrates.
/// Produces 3 templates: single-frond, 3-frond cluster, fiddlehead (curled).
AtlasTemplateSet bakeFernAtlasTemplates();

/// P1 (Track C): Bake a set of bush / shrub card templates for the
/// bush_atlas. Bushes are cluster-forming low shrubs that fill the gap
/// between ground cover and understory. Produces 3 templates: small bush,
/// medium bush, berry bush (slightly taller, fruit-bearing silhouette).
AtlasTemplateSet bakeBushAtlasTemplates();

/// P1 (Track C): Bake a set of flower card templates for the
/// meadow_flower_atlas. Flowers are rare accent species (density-gated
/// by the palette) that add colour to meadow clearings. Produces 3
/// templates: single-stem flower, 3-stem cluster, tall wildflower.
AtlasTemplateSet bakeFlowerAtlasTemplates();

/// P1 (Track C): Bake a set of mushroom card templates for the
/// forest_mushroom_atlas. Mushrooms are rare accent species on the forest
/// floor. Produces 3 templates: single cap, cluster of 3, tall bracket.
AtlasTemplateSet bakeMushroomAtlasTemplates();

/// Bake a set of procedural grass templates (fallback when no atlas assets).
/// Same interface as bakeGrassAtlasTemplates but with plain green coloring.
AtlasTemplateSet bakeProceduralGrassTemplates();

/// Build indices for a crossed-quad (2 intersecting quads = 4 triangles).
/// vertexCountPerBlade = 4 (2 quads x 2 verts each). Actually for a
/// crossed quad we have 8 vertices (2 quads x 4 verts) and 12 indices.
inline std::vector<uint32_t> crossedQuadIndices(uint32_t baseVertex, uint32_t quadCount)
{
    std::vector<uint32_t> idx;
    idx.reserve(quadCount * 6);
    for (uint32_t q = 0; q < quadCount; ++q) {
        const uint32_t v0 = baseVertex + q * 4;
        idx.push_back(v0 + 0);
        idx.push_back(v0 + 1);
        idx.push_back(v0 + 2);
        idx.push_back(v0 + 0);
        idx.push_back(v0 + 2);
        idx.push_back(v0 + 3);
    }
    return idx;
}

/// Build indices for a triangle blade (single triangle from base to tip).
inline std::vector<uint32_t> triangleBladeIndices(uint32_t baseVertex, uint32_t bladeCount)
{
    std::vector<uint32_t> idx;
    idx.reserve(bladeCount * 3);
    for (uint32_t b = 0; b < bladeCount; ++b) {
        const uint32_t v0 = baseVertex + b * 3;
        idx.push_back(v0 + 0);
        idx.push_back(v0 + 1);
        idx.push_back(v0 + 2);
    }
    return idx;
}

} // namespace arp::pcg
