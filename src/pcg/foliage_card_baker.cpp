/// @file foliage_card_baker.cpp
/// @brief P6: Foliage card template generation implementation.

#include "pcg/foliage_card_baker.h"

#include <cmath>

namespace arp::pcg {

namespace {

/// Build a single quad (4 verts, 2 tris) centered at origin, base at y=0,
/// extending up to y=height. Width is along X. UVs map [0,1]x[0,1].
/// Wind weights: root (y=0) has primaryBend=0, flutter=0, stiffness=1;
/// tip (y=height) has primaryBend=tipBend, flutter=tipFlutter, stiffness=tipStiff.
/// The bend curve interpolates non-linearly: more bend near the tip.
void appendQuadBlade(std::vector<InstanceVertex>& verts,
    float width,
    float height,
    float rotY,
    float centerX,
    float centerZ,
    float topLeanX,
    float topLeanZ,
    float uvScaleU,
    float uvOffsetU,
    float uvScaleV,
    float uvOffsetV,
    float tipBendWeight,
    float tipFlutterWeight,
    float tipStiffness,
    float bendCurve)
{
    const float c = std::cos(rotY);
    const float s = std::sin(rotY);
    const float halfW = width * 0.5f;

    // 4 vertices: bottom-left, bottom-right, top-right, top-left (CCW from front)
    const float localPos[4][3] = {
        {-halfW, 0.0f, 0.0f},   // bottom-left
        {halfW, 0.0f, 0.0f},    // bottom-right
        {halfW, height, 0.0f},  // top-right
        {-halfW, height, 0.0f}, // top-left
    };
    const float uvs[4][2] = {
        {uvOffsetU, uvOffsetV + uvScaleV},            // bottom-left = atlas bottom
        {uvOffsetU + uvScaleU, uvOffsetV + uvScaleV}, // bottom-right
        {uvOffsetU + uvScaleU, uvOffsetV},            // top-right = atlas top
        {uvOffsetU, uvOffsetV},                       // top-left
    };

    for (int i = 0; i < 4; ++i) {
        InstanceVertex v;
        // Apply Y rotation.
        v.position[0] = localPos[i][0] * c + localPos[i][2] * s + centerX;
        v.position[1] = localPos[i][1];
        v.position[2] = -localPos[i][0] * s + localPos[i][2] * c + centerZ;

        // Apply bend curve: offset Z forward proportional to height^curve.
        const float heightFrac = localPos[i][1] / height;
        const float bendFactor = std::pow(heightFrac, 1.0f + bendCurve * 2.0f);
        v.position[0] += topLeanX * bendFactor;
        v.position[2] += bendFactor * height * bendCurve * 0.3f;
        v.position[2] += topLeanZ * bendFactor;

        // Normal: face outward from the blade plane (after rotation).
        v.normal[0] = s;
        v.normal[1] = 0.0f;
        v.normal[2] = c;

        v.uv[0] = uvs[i][0];
        v.uv[1] = uvs[i][1];

        // Tangent along blade width.
        v.tangent[0] = c;
        v.tangent[1] = 0.0f;
        v.tangent[2] = -s;
        v.tangent[3] = 1.0f;

        // Wind weights: interpolate root->tip.
        v.primaryBend = heightFrac * tipBendWeight;
        v.flutter = heightFrac * tipFlutterWeight;
        v.stiffness = 1.0f - heightFrac * (1.0f - tipStiffness);

        verts.push_back(v);
    }
}

} // anonymous namespace

std::vector<InstanceVertex> bakeCardTemplate(const CardTemplateDesc& desc)
{
    std::vector<InstanceVertex> verts;

    if (desc.bladeCount <= 1) {
        // Single blade.
        appendQuadBlade(verts,
            desc.width,
            desc.height,
            0.0f,
            0.0f,
            0.0f,
            desc.width * 0.08f,
            desc.height * 0.06f,
            desc.uvScaleU,
            desc.uvOffsetU,
            desc.uvScaleV,
            desc.uvOffsetV,
            desc.tipBendWeight,
            desc.tipFlutterWeight,
            desc.tipStiffness,
            desc.bendCurve);
    } else {
        // Multiple blades arranged in a radial tuft.
        for (uint32_t b = 0; b < desc.bladeCount; ++b) {
            const float angle = (static_cast<float>(b) / desc.bladeCount) * 6.2831853f;
            const float bladeT = desc.bladeCount > 1
                ? static_cast<float>(b) / static_cast<float>(desc.bladeCount - 1)
                : 0.5f;
            const float radial =
                desc.clusterRadius * (0.65f + 0.35f * std::sin(bladeT * 6.2831853f + 0.7f));
            const float centerX = std::cos(angle) * radial;
            const float centerZ = std::sin(angle) * radial;
            const float widthScale = 0.8f + 0.25f * std::sin(bladeT * 5.1f);
            const float heightScale = 0.78f + 0.26f * std::cos(bladeT * 4.3f);
            const float leanRadius =
                desc.height * (0.10f + 0.08f * std::sin(bladeT * 7.3f + 0.35f));
            const float topLeanX = std::cos(angle + 0.45f) * leanRadius;
            const float topLeanZ = std::sin(angle + 0.45f) * leanRadius;
            appendQuadBlade(verts,
                desc.width * widthScale,
                desc.height * heightScale,
                angle,
                centerX,
                centerZ,
                topLeanX,
                topLeanZ,
                desc.uvScaleU,
                desc.uvOffsetU,
                desc.uvScaleV,
                desc.uvOffsetV,
                desc.tipBendWeight,
                desc.tipFlutterWeight,
                desc.tipStiffness,
                desc.bendCurve);
        }
    }
    return verts;
}

AtlasTemplateSet bakeGrassAtlasTemplates()
{
    AtlasTemplateSet set;
    // 4 templates: short, medium, tall, dense tuft.
    // The source asset is a horizontal strip atlas of individual blades.
    // Each card must address ONE strip only; sampling the full width compresses
    // every blade into the same quad and creates the obvious barcode artefact.
    constexpr float kBladeStripWidth = 0.085f;

    // Template 0: short meadow tuft.  Blade count kept at 3 for GPU perf:
    // alpha-tested crossed-quad cards suffer heavy overdraw under the full
    // PBR fragment path, and 10-14 blades per tuft multiplied that cost.
    // clusterRadius is widened ~20% so the 3 blades spread wider, preserving
    // visible coverage at player height. Scatter density compensates the rest.
    {
        CardTemplateDesc d;
        d.width = 0.12f;
        d.height = 0.34f;
        d.bladeCount = 3;
        d.clusterRadius = 0.36f;
        d.bendCurve = 0.18f;
        d.tipBendWeight = 0.62f;
        d.tipFlutterWeight = 0.52f;
        d.tipStiffness = 0.48f;
        d.uvScaleU = kBladeStripWidth;
        d.uvOffsetU = 0.020f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: medium meadow tuft.
    {
        CardTemplateDesc d;
        d.width = 0.12f;
        d.height = 0.52f;
        d.bladeCount = 3;
        d.clusterRadius = 0.38f;
        d.bendCurve = 0.36f;
        d.tipBendWeight = 0.82f;
        d.tipFlutterWeight = 0.74f;
        d.tipStiffness = 0.32f;
        d.uvScaleU = kBladeStripWidth;
        d.uvOffsetU = 0.135f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: taller tuft, kept below a character's knee after instance
    // scaling so it does not read as isolated reeds.
    {
        CardTemplateDesc d;
        d.width = 0.11f;
        d.height = 0.68f;
        d.bladeCount = 3;
        d.clusterRadius = 0.41f;
        d.bendCurve = 0.58f;
        d.tipBendWeight = 1.0f;
        d.tipFlutterWeight = 0.95f;
        d.tipStiffness = 0.22f;
        d.uvScaleU = kBladeStripWidth;
        d.uvOffsetU = 0.335f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 3: dense foreground tuft.
    {
        CardTemplateDesc d;
        d.width = 0.12f;
        d.height = 0.44f;
        d.bladeCount = 3;
        d.clusterRadius = 0.43f;
        d.bendCurve = 0.28f;
        d.tipBendWeight = 0.74f;
        d.tipFlutterWeight = 0.68f;
        d.tipStiffness = 0.34f;
        d.uvScaleU = kBladeStripWidth;
        d.uvOffsetU = 0.640f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

AtlasTemplateSet bakeWeedAtlasTemplates()
{
    AtlasTemplateSet set;
    // 3 templates: thin weed, broad weed, seed-head.
    // The atlas is a horizontal strip of authored weeds. Each template must
    // pick a sub-region instead of shrinking the whole strip into one card.
    constexpr float kWeedStripWidth = 0.28f;

    // Each weed atlas cell already contains a complete plant, unlike the
    // grass atlas which contains individual blades.  It must therefore be a
    // single card; radial duplication of the same plant produced the curled,
    // tangled silhouettes visible in the near field.
    // Template 0: thin weed.
    {
        CardTemplateDesc d;
        d.width = 0.34f;
        d.height = 0.95f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.6f;
        d.tipBendWeight = 0.9f;
        d.tipFlutterWeight = 0.9f;
        d.tipStiffness = 0.2f;
        d.uvScaleU = kWeedStripWidth;
        d.uvOffsetU = 0.01f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: broad weed.
    {
        CardTemplateDesc d;
        d.width = 0.38f;
        d.height = 0.90f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.3f;
        d.tipBendWeight = 0.7f;
        d.tipFlutterWeight = 0.6f;
        d.tipStiffness = 0.4f;
        d.uvScaleU = kWeedStripWidth;
        d.uvOffsetU = 0.36f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: seed-head (3 blades, tall, thin).
    {
        CardTemplateDesc d;
        d.width = 0.30f;
        d.height = 1.15f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.4f;
        d.tipBendWeight = 0.8f;
        d.tipFlutterWeight = 0.85f;
        d.tipStiffness = 0.3f;
        d.uvScaleU = kWeedStripWidth;
        d.uvOffsetU = 0.69f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

AtlasTemplateSet bakeBroadleafAtlasTemplates()
{
    AtlasTemplateSet set;
    // 3 templates: single-leaf, 3-leaf cluster, 5-leaf fan.
    // The atlas is a 3x3 leaf grid. Choose one tile per template.
    constexpr float kLeafTile = 1.0f / 3.0f;

    // Template 0: single broad leaf.
    {
        CardTemplateDesc d;
        d.width = 0.18f;
        d.height = 0.34f;
        d.bladeCount = 3;
        d.clusterRadius = 0.08f;
        d.bendCurve = 0.2f;
        d.tipBendWeight = 0.6f;
        d.tipFlutterWeight = 0.8f;
        d.tipStiffness = 0.4f;
        d.uvScaleU = kLeafTile;
        d.uvOffsetU = 0.0f;
        d.uvScaleV = kLeafTile;
        d.uvOffsetV = 0.0f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: 3-leaf cluster.
    {
        CardTemplateDesc d;
        d.width = 0.16f;
        d.height = 0.42f;
        d.bladeCount = 4;
        d.clusterRadius = 0.10f;
        d.bendCurve = 0.3f;
        d.tipBendWeight = 0.7f;
        d.tipFlutterWeight = 0.9f;
        d.tipStiffness = 0.3f;
        d.uvScaleU = kLeafTile;
        d.uvOffsetU = kLeafTile;
        d.uvScaleV = kLeafTile;
        d.uvOffsetV = kLeafTile;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: 5-leaf fan (wider, more blades).
    {
        CardTemplateDesc d;
        d.width = 0.15f;
        d.height = 0.40f;
        d.bladeCount = 5;
        d.clusterRadius = 0.12f;
        d.bendCurve = 0.2f;
        d.tipBendWeight = 0.6f;
        d.tipFlutterWeight = 0.95f;
        d.tipStiffness = 0.2f;
        d.uvScaleU = kLeafTile;
        d.uvOffsetU = 2.0f * kLeafTile;
        d.uvScaleV = kLeafTile;
        d.uvOffsetV = 2.0f * kLeafTile;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

AtlasTemplateSet bakeProceduralGrassTemplates()
{
    // Same geometry as bakeGrassAtlasTemplates but with UVs that just tile.
    // The procedural fallback uses a plain green material (no atlas).
    return bakeGrassAtlasTemplates();
}

// ---------------------------------------------------------------------------
// P1 (Track C): new ground-cover species bakers.
//
// These follow the same pattern as the grass/weed/broadleaf bakers: each
// produces >=3 card templates that vary width/height/blade count/bend, with
// UVs addressing a sub-region of the corresponding atlas. The atlas assets
// themselves ship as horizontal-strip textures (same layout convention as
// the existing three atlases), so each baker picks a distinct strip via
// uvScaleU/uvOffsetU.
//
// The wind weights are tuned per species: ferns are stiff (low flutter,
// high stiffness - fronds don't flutter like grass blades), bushes are
// moderately stiff (whole-shrub sway, not leaf flutter), flowers have
// flexible stems (high flutter, low stiffness), mushrooms are rigid
// (near-zero wind response - they're small and dense).
// ---------------------------------------------------------------------------

AtlasTemplateSet bakeFernAtlasTemplates()
{
    AtlasTemplateSet set;
    // 3 templates: single-frond, 3-frond cluster, fiddlehead.
    // Atlas is a horizontal strip of fern fronds.
    constexpr float kFernStripWidth = 0.30f;

    // Template 0: single frond. Tall and narrow, slight forward bend.
    {
        CardTemplateDesc d;
        d.width = 0.40f;
        d.height = 0.85f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.25f;
        // Ferns are stiffer than grass - fronds resist wind.
        d.tipBendWeight = 0.45f;
        d.tipFlutterWeight = 0.30f;
        d.tipStiffness = 0.65f;
        d.uvScaleU = kFernStripWidth;
        d.uvOffsetU = 0.02f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: 3-frond cluster (radial spread).
    {
        CardTemplateDesc d;
        d.width = 0.38f;
        d.height = 0.78f;
        d.bladeCount = 3;
        d.clusterRadius = 0.22f;
        d.bendCurve = 0.30f;
        d.tipBendWeight = 0.50f;
        d.tipFlutterWeight = 0.35f;
        d.tipStiffness = 0.60f;
        d.uvScaleU = kFernStripWidth;
        d.uvOffsetU = 0.36f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: fiddlehead (curled, short, dense).
    {
        CardTemplateDesc d;
        d.width = 0.30f;
        d.height = 0.42f;
        d.bladeCount = 2;
        d.clusterRadius = 0.10f;
        d.bendCurve = 0.75f; // strongly curled
        d.tipBendWeight = 0.30f;
        d.tipFlutterWeight = 0.20f;
        d.tipStiffness = 0.75f;
        d.uvScaleU = kFernStripWidth;
        d.uvOffsetU = 0.70f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

AtlasTemplateSet bakeBushAtlasTemplates()
{
    AtlasTemplateSet set;
    // 3 templates: small bush, medium bush, berry bush.
    constexpr float kBushStripWidth = 0.30f;

    // Template 0: small bush (low, wide, crossed-quad for volume).
    {
        CardTemplateDesc d;
        d.width = 0.65f;
        d.height = 0.55f;
        d.bladeCount = 2;
        d.clusterRadius = 0.18f;
        d.bendCurve = 0.15f;
        // Whole-shrub sway: moderate bend, low leaf flutter.
        d.tipBendWeight = 0.40f;
        d.tipFlutterWeight = 0.25f;
        d.tipStiffness = 0.70f;
        d.uvScaleU = kBushStripWidth;
        d.uvOffsetU = 0.02f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: medium bush (taller, denser).
    {
        CardTemplateDesc d;
        d.width = 0.70f;
        d.height = 0.85f;
        d.bladeCount = 3;
        d.clusterRadius = 0.22f;
        d.bendCurve = 0.20f;
        d.tipBendWeight = 0.50f;
        d.tipFlutterWeight = 0.30f;
        d.tipStiffness = 0.65f;
        d.uvScaleU = kBushStripWidth;
        d.uvOffsetU = 0.36f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: berry bush (tallest, sparse - shows fruit silhouette).
    {
        CardTemplateDesc d;
        d.width = 0.60f;
        d.height = 1.00f;
        d.bladeCount = 2;
        d.clusterRadius = 0.25f;
        d.bendCurve = 0.25f;
        d.tipBendWeight = 0.55f;
        d.tipFlutterWeight = 0.35f;
        d.tipStiffness = 0.60f;
        d.uvScaleU = kBushStripWidth;
        d.uvOffsetU = 0.70f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

AtlasTemplateSet bakeFlowerAtlasTemplates()
{
    AtlasTemplateSet set;
    // 3 templates: single-stem, 3-stem cluster, tall wildflower.
    constexpr float kFlowerStripWidth = 0.30f;

    // Template 0: single-stem flower (thin, flexible).
    {
        CardTemplateDesc d;
        d.width = 0.18f;
        d.height = 0.70f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.45f;
        // Flexible stem - sways noticeably in wind.
        d.tipBendWeight = 0.85f;
        d.tipFlutterWeight = 0.90f;
        d.tipStiffness = 0.20f;
        d.uvScaleU = kFlowerStripWidth;
        d.uvOffsetU = 0.02f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: 3-stem cluster.
    {
        CardTemplateDesc d;
        d.width = 0.20f;
        d.height = 0.65f;
        d.bladeCount = 3;
        d.clusterRadius = 0.14f;
        d.bendCurve = 0.40f;
        d.tipBendWeight = 0.80f;
        d.tipFlutterWeight = 0.85f;
        d.tipStiffness = 0.25f;
        d.uvScaleU = kFlowerStripWidth;
        d.uvOffsetU = 0.36f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: tall wildflower (slender, very flexible).
    {
        CardTemplateDesc d;
        d.width = 0.16f;
        d.height = 0.95f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.55f;
        d.tipBendWeight = 0.95f;
        d.tipFlutterWeight = 1.0f;
        d.tipStiffness = 0.15f;
        d.uvScaleU = kFlowerStripWidth;
        d.uvOffsetU = 0.70f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

AtlasTemplateSet bakeMushroomAtlasTemplates()
{
    AtlasTemplateSet set;
    // 3 templates: single cap, cluster of 3, tall bracket.
    constexpr float kMushroomStripWidth = 0.30f;

    // Template 0: single cap (short, wide, rigid).
    {
        CardTemplateDesc d;
        d.width = 0.22f;
        d.height = 0.28f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.05f;
        // Mushrooms are rigid - negligible wind response.
        d.tipBendWeight = 0.10f;
        d.tipFlutterWeight = 0.05f;
        d.tipStiffness = 0.95f;
        d.uvScaleU = kMushroomStripWidth;
        d.uvOffsetU = 0.02f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 1: cluster of 3 caps (varying heights).
    {
        CardTemplateDesc d;
        d.width = 0.24f;
        d.height = 0.36f;
        d.bladeCount = 3;
        d.clusterRadius = 0.10f;
        d.bendCurve = 0.08f;
        d.tipBendWeight = 0.12f;
        d.tipFlutterWeight = 0.06f;
        d.tipStiffness = 0.92f;
        d.uvScaleU = kMushroomStripWidth;
        d.uvOffsetU = 0.36f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    // Template 2: tall bracket mushroom (slender stem, wider cap).
    {
        CardTemplateDesc d;
        d.width = 0.20f;
        d.height = 0.50f;
        d.bladeCount = 1;
        d.clusterRadius = 0.0f;
        d.bendCurve = 0.10f;
        d.tipBendWeight = 0.15f;
        d.tipFlutterWeight = 0.08f;
        d.tipStiffness = 0.90f;
        d.uvScaleU = kMushroomStripWidth;
        d.uvOffsetU = 0.70f;
        auto verts = bakeCardTemplate(d);
        auto idx = crossedQuadIndices(0, static_cast<uint32_t>(verts.size() / 4));
        set.templates.push_back(std::move(verts));
        set.indices.push_back(std::move(idx));
    }
    return set;
}

} // namespace arp::pcg
