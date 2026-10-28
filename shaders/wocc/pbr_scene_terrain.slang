// World of ClaudeCraft Terrain Six-Layer PBR Shader (P4)
//
// Replaces the old 4-layer GPU-computed splat (grass/dirt/rock/sand derived
// from slope+height) with a 6-layer weight-page system. The six base weights
// (grassMoss, forestFloor, wetMud, rockyPath, mossyRock, dirtySnow) are
// computed CPU-side from TerrainFieldSet (slope, curvature, wetness, shore
// distance, height, river mask) and uploaded as two RGBA8 weight pages.
// The shader reads these pages instead of recomputing weights GPU-side.
//
// Six surface material sets (each with albedo/normal/roughness/height) are
// bound at descriptor set 0. The shader blends them by the sampled weights,
// with stochastic UV rotation to suppress tiling, tri-planar normal projection
// for mossyRock on cliff faces, and per-layer roughness.
//
// Overlay layers (snow, wetness) use a separate schema stored in weight page B
// channels B and A - they are applied additively on top of the base weights
// rather than competing with them in the normalized base.
//
// Push-constant block stays at 256 bytes (unchanged from the 4-layer shader).

// --- Ambient occlusion / contact shadow tuning knobs (shared with pbr_scene) ---
static const float kContactShadowHeight = 4.0;
static const float kContactShadowStrength = 0.55;
static const float kContactShadowBase = 0.18;
static const float kContactShadowNormalScale = 0.25;
static const float kPlayerContactShadowHeight = 1.8;
static const float kPlayerContactShadowLength = 1.75;
static const float kPlayerContactShadowWidth = 0.65;
static const float kPlayerContactShadowStrength = 0.85;
// Natural ground is a diffuse-dominant surface. Keep its dielectric lobe
// present for material response, but prevent it from becoming an exposed,
// bloom-amplified triangle under the high-intensity key light.
static const float kInvPi = 0.31830988618;
static const float kTerrainSpecularStrength = 0.04;

struct WoccPbrPushConstants
{
    float4x4 viewProjection;
    float4 cameraPositionExposure;
    float4 baseColorAlpha;
    float4 pbrParams;
    float4 pbrExtraParams;
    float4 environmentParams;
    float4 lightDirection;
    float4 lightColor;
    float4 shadowParams;
    float4x4 lightViewProjection;
};

[[vk::push_constant]]
WoccPbrPushConstants gScene;

// --- Set 0: Six-layer terrain environment material ----------------------------
// Bindings 0-23:  6 surface sets x 4 maps (albedo, normal, roughness, height)
//   0-3:   grassMoss     (wet_grass_moss_ground)
//   4-7:   forestFloor   (leaf_root_forest_ground)
//   8-11:  wetMud        (wet_mud_ground)
//   12-15: rockyPath     (rocky_forest_path)
//   16-19: mossyRock     (mossy_rock_cliff)
//   20-23: dirtySnow     (dirty_melting_snow)
// Binding 24: WeightPageA array (RGBA8: grassMoss, forestFloor, wetMud, rockyPath)
// Binding 25: WeightPageB array (RGBA8: mossyRock, dirtySnow, snowOverlay, wetnessOverlay)
// Binding 26: MacroNoiseTexture
// Binding 27: TerrainSplatSampler
// Binding 28: TerrainSplatParams UBO
// Binding 29: Terrain tile page table
// Binding 30: Terrain page-table count

[[vk::binding(0, 0)]]  Texture2D<float4> GrassMossAlbedo;
[[vk::binding(1, 0)]]  Texture2D<float4> GrassMossNormal;
[[vk::binding(2, 0)]]  Texture2D<float4> GrassMossRoughness;
[[vk::binding(3, 0)]]  Texture2D<float4> GrassMossHeight;

[[vk::binding(4, 0)]]  Texture2D<float4> ForestFloorAlbedo;
[[vk::binding(5, 0)]]  Texture2D<float4> ForestFloorNormal;
[[vk::binding(6, 0)]]  Texture2D<float4> ForestFloorRoughness;
[[vk::binding(7, 0)]]  Texture2D<float4> ForestFloorHeight;

[[vk::binding(8, 0)]]  Texture2D<float4> WetMudAlbedo;
[[vk::binding(9, 0)]]  Texture2D<float4> WetMudNormal;
[[vk::binding(10, 0)]] Texture2D<float4> WetMudRoughness;
[[vk::binding(11, 0)]] Texture2D<float4> WetMudHeight;

[[vk::binding(12, 0)]] Texture2D<float4> RockyPathAlbedo;
[[vk::binding(13, 0)]] Texture2D<float4> RockyPathNormal;
[[vk::binding(14, 0)]] Texture2D<float4> RockyPathRoughness;
[[vk::binding(15, 0)]] Texture2D<float4> RockyPathHeight;

[[vk::binding(16, 0)]] Texture2D<float4> MossyRockAlbedo;
[[vk::binding(17, 0)]] Texture2D<float4> MossyRockNormal;
[[vk::binding(18, 0)]] Texture2D<float4> MossyRockRoughness;
[[vk::binding(19, 0)]] Texture2D<float4> MossyRockHeight;

[[vk::binding(20, 0)]] Texture2D<float4> DirtySnowAlbedo;
[[vk::binding(21, 0)]] Texture2D<float4> DirtySnowNormal;
[[vk::binding(22, 0)]] Texture2D<float4> DirtySnowRoughness;
[[vk::binding(23, 0)]] Texture2D<float4> DirtySnowHeight;

[[vk::binding(24, 0)]] Texture2DArray<float4> WeightPageA;
[[vk::binding(25, 0)]] Texture2DArray<float4> WeightPageB;
[[vk::binding(26, 0)]] Texture2D<float4> MacroNoiseTexture;
[[vk::binding(27, 0)]] SamplerState TerrainSplatSampler;

// Splat configuration (mirrors WoccTerrainSplatParams in wocc_pbr_pipeline.h).
// std140: 6 x float4 = 96 bytes.
struct TerrainSplatParams
{
    float4 texParams;      // x = baseTexScale, y = macroScale, z = macroScale2, w = wallTexScale
    float4 weightParams;   // x = weightPageScale, y = snowLine, z = snowFeather, w = pad
    float4 layerRough012;  // x = grassMoss, y = forestFloor, z = wetMud, w = rockyPath
    float4 layerRough345;  // x = mossyRock, y = dirtySnow, z = weightOriginX, w = weightOriginZ
    float4 brushParams;    // xz = brushCenter, y = brushRadius, w = brushEnabled
    float4 brushColor;     // rgb = color, a = intensity
};
[[vk::binding(28, 0)]]
ConstantBuffer<TerrainSplatParams> gTerrain;

// Page-table metadata is authored by TerrainGpuRuntime from the active
// TerrainAsset generation. `weightArrayLayer` is an RHI array layer, never a
// native handle; page bounds deliberately make local UV calculation explicit.
struct TerrainTilePageTableEntry
{
    float4 worldBounds; // minX, maxX, minZ, maxZ
    uint weightArrayLayer;
    uint resourceGenerationLow;
    uint resourceGenerationHigh;
    uint reserved0;
};
[[vk::binding(29, 0)]]
StructuredBuffer<TerrainTilePageTableEntry> TerrainTilePageTable;

struct TerrainPageTableParams
{
    uint entryCount;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};
[[vk::binding(30, 0)]]
ConstantBuffer<TerrainPageTableParams> gTerrainPageTable;

// --- Set 2: Shadow + Dynamic DDGI irradiance (shared with pbr_scene.slang) ---
// Binding 0 carries the cascade VP array + split depths + per-cascade atlas UV
// transforms. Terrain must sample the same CSM atlas as pbr_scene.slang;
// otherwise player shadows are written into atlas strips but terrain receivers
// keep sampling the old single-matrix full-texture path.
struct WoccCsmShadow
{
    float4x4 lightViewProjection[3];
    float4   shadowParams;
    float4   cascadeSplits;
    float4   atlasUvTransforms[3];
    float4   playerPosition;           // reserved CPU-side cascade origin data
};
[[vk::binding(0, 2)]]
ConstantBuffer<WoccCsmShadow> gCsmShadow;

// TAA: previous-frame view-projection (un-jittered) for the velocity MRT.
// Lives in a dedicated UBO rather than the push-constant block because
// WoccPbrPushConstants is already at the 256B Vulkan push-constant limit.
// Uses the DEFAULT float4x4 layout (no row-major keyword) so SPIR-V emits a
// RowMajor decoration, matching the row-major bytes the CPU fills (core::Mat4)
// and the same layout as gScene.viewProjection - see AGENTS.md "Matrix Layout"
// hard constraint #2-3. Must stay in lockstep with pbr_scene.slang's
// WoccPrevCamera (same frame-globals set 1 binding 3, same default float4x4 layout).
struct WoccPrevCamera
{
    float4x4 prevViewProjection;
};
[[vk::binding(3, 1)]]
ConstantBuffer<WoccPrevCamera> gPrevCamera;

// Terrain is static, but it shares the frame-global bind group with dynamic
// mesh draws. Keep the two vertex-stage slots physically present so the
// WebGPU group layout remains identical across those draws.
[[vk::binding(4, 1)]]
ConstantBuffer<float4> gPreviousWindCompatibility;
[[vk::binding(5, 1)]]
ConstantBuffer<float4> gSkinningCompatibility;

[[vk::binding(4, 2)]]
Texture2D<float> ShadowMap;
[[vk::binding(5, 2)]]
SamplerState ShadowSampler;
[[vk::binding(6, 2)]]
Texture2D<float4> GiIrradiance;
[[vk::binding(7, 2)]]
SamplerState GiSampler;

// --- Set 4: Wind/player interaction UBO -------------------------------------
// The wind system already tracks the player's ground-plane interaction center.
// Terrain uses the same authored per-frame source to add a small near-field
// contact shadow that complements CSM for the player-sized caster footprint.
struct WoccWindField
{
    float4 directionSpeed;
    float4 timeGustTurb;
    float4 flutterParams;
    float4 interactionCenter;
};
[[vk::binding(1, 1)]]
ConstantBuffer<WoccWindField> gWind;

// --- Set 3: Lights (UBO, fixed array of 8) ------------------------------------
struct WoccGpuLight
{
    float4 positionType;
    float4 directionRange;
    float4 colorIntensity;
    float4 coneDecayShadow;
};
struct WoccLightArray
{
    WoccGpuLight lights[8];
    float4 countAndPadding;
};
[[vk::binding(0, 1)]]
ConstantBuffer<WoccLightArray> gLights;

// P9: Weather UBO (set 5, binding 0). Same struct as pbr_scene.slang.
struct WoccWeather
{
    float4 params;       // x = weatherMode, y = wetness, z = cloudCover, w = fogDensityScale
    float4 lightAdjust;  // x = sunIntensityScale, y = ambientScale, z = exposureAdjust, w = reserved
    float4 skyTint;      // rgb = sky tint, w = haze intensity
    float4 fogTint;      // rgb = fog color, a = fog density override
};
[[vk::binding(2, 1)]]
ConstantBuffer<WoccWeather> gWeather;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
};

struct VertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 shadowCoord : TEXCOORD4;
    float  contactShadow : TEXCOORD5;
    // TAA: previous-frame clip position (pre-perspective-divide) for the
    // velocity MRT. Paired with curClipPosition so the fragment shader can
    // compute the per-pixel motion vector without needing the render-target
    // extent (the push-constant block is full at 256B).
    float4 prevClipPosition : TEXCOORD6;
    // TAA: current-frame clip position (pre-perspective-divide), used as the
    // reference end of the motion vector. DEFAULT float4x4 layout (RowMajor).
    float4 curClipPosition : TEXCOORD7;
};

// TAA: MRT output. SV_Target0 is the scene color (HDR linear, as before).
// SV_Target1 is the per-pixel motion vector in [0,1] UV space; 0.5 = no motion.
// Must stay in lockstep with pbr_scene.slang's PbrSceneOutput.
struct PbrSceneOutput
{
    float4 color : SV_Target0;
    float2 velocity : SV_Target1;
};

// TAA: compute the per-pixel motion vector from the current and previous clip
// positions (both pre-perspective-divide). Returns [0,1] UV-space motion;
// 0.5 = no motion. Identical to pbr_scene.slang's computeTaaVelocity.
float2 computeTaaVelocity(float4 curClipPosition, float4 prevClipPosition)
{
    float curW = max(abs(curClipPosition.w), 1e-6);
    float prevW = max(abs(prevClipPosition.w), 1e-6);
    float2 curNdc = curClipPosition.xy / curW;
    float2 prevNdc = prevClipPosition.xy / prevW;
    return (curNdc - prevNdc) * 0.5 + 0.5;
}

// TAA: wrap a scene color in the MRT output, attaching the per-pixel velocity.
PbrSceneOutput taaSceneOutput(float4 color, VertexOutput input)
{
    PbrSceneOutput output;
    output.color = color;
    output.velocity = computeTaaVelocity(input.curClipPosition, input.prevClipPosition);
    return output;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    const float contractProbe = (gPreviousWindCompatibility.x + gSkinningCompatibility.x) * 0.0000001;
    const float3 worldPosition = input.position + float3(contractProbe, 0.0, 0.0);
    output.worldPosition = worldPosition;
    output.normal = normalize(input.normal);
    output.uv = input.uv;
    output.tangent = input.tangent;
    output.position = mul(gScene.viewProjection, float4(worldPosition, 1.0));
    // TAA: previous + current clip positions for the velocity MRT.
    output.prevClipPosition = mul(gPrevCamera.prevViewProjection, float4(worldPosition, 1.0));
    output.curClipPosition = output.position;
    output.shadowCoord = mul(gScene.lightViewProjection, float4(worldPosition, 1.0));
    output.contactShadow = 1.0 - smoothstep(0.0, kContactShadowHeight, input.position.y);
    return output;
}

// --- BRDF Functions (shared with pbr_scene.slang) ----------------------------
#include "renderer/pbr_brdf.slang"

float sampleShadow(float4 shadowCoord)
{
    if (gScene.shadowParams.x < 0.5) {
        return 1.0;
    }
    if (shadowCoord.w <= 0.0) {
        return 1.0;
    }
    float3 projected = shadowCoord.xyz / shadowCoord.w;
    // Outside the light frustum there is no valid shadow-map receiver.  Do
    // not saturate Z here: turning z>1 into 1 falsely compares terrain beyond
    // the shadow far plane against edge texels and creates chunk-sized black
    // patches that resemble a material/LOD seam.
    if (projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }
    float2 uv = projected.xy * float2(0.5, 0.5) + float2(0.5, 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    float receiver = projected.z;
    float bias = 0.0005;
    // A zero radius is a valid quality tier: retain the shadow-map test but
    // issue one depth comparison instead of a 3x3 PCF kernel.  This keeps
    // character contact shadows enabled on the terrain while avoiding nine
    // filtered samples for every visible terrain pixel.
    float pcfRadius = max(gScene.shadowParams.y, 0.0);
    float shadowStrength = saturate(gScene.shadowParams.z);
    float shadowedVisibility = 1.0 - shadowStrength;
    float texelSize = 1.0 / max(gScene.shadowParams.w, 1.0);
    float visibility = 0.0;
    float samples = 0.0;
    for (float x = -pcfRadius; x <= pcfRadius; x += 1.0) {
        for (float y = -pcfRadius; y <= pcfRadius; y += 1.0) {
            float2 offset = float2(x, y) * texelSize;
            float shadowDepth = ShadowMap.SampleLevel(ShadowSampler, uv + offset, 0.0).r;
            visibility += (receiver - bias) > shadowDepth ? shadowedVisibility : 1.0;
            samples += 1.0;
        }
    }
    return visibility / samples;
}

static const float2 kPoissonDisk[16] =
{
    float2(-0.94201624,  -0.39906216),
    float2( 0.94558609,  -0.76890725),
    float2(-0.09418410,  -0.92938870),
    float2( 0.34495938,   0.29387724),
    float2(-0.91588581,   0.45771432),
    float2(-0.81544232,  -0.87912464),
    float2(-0.38277543,   0.27604828),
    float2( 0.96754818,  -0.21455085),
    float2(-0.71185582,  -0.92661572),
    float2( 0.47942789,   0.81587590),
    float2(-0.10103717,  -0.94031335),
    float2( 0.65192364,  -0.17601420),
    float2(-0.55176430,   0.63284730),
    float2( 0.72261955,   0.71569090),
    float2( 0.45098465,   0.46884415),
    float2(-0.21401813,  -0.29867348),
};

float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float samplePoissonDiskPCF(float2 uv, float receiver, float bias,
                           float texelSize, float pcfRadius,
                           float shadowedVisibility, float2 pixelCoord)
{
    float angle = hash12(pixelCoord) * 6.2831853;
    float cs = cos(angle);
    float sn = sin(angle);
    float2x2 rot = float2x2(cs, -sn, sn, cs);
    float visibility = 0.0;
    [unroll]
    for (int i = 0; i < 16; ++i) {
        float2 rotOffset = mul(rot, kPoissonDisk[i]) * (pcfRadius * texelSize);
        float shadowDepth = ShadowMap.SampleLevel(ShadowSampler, uv + rotOffset, 0.0).r;
        visibility += (receiver - bias) > shadowDepth ? shadowedVisibility : 1.0;
    }
    return visibility / 16.0;
}

float sampleShadowCsm(float3 worldPos, float viewDepth, float2 pixelCoord)
{
    if (gCsmShadow.shadowParams.x < 0.5) {
        return 1.0;
    }
    uint cascadeIndex = viewDepth < gCsmShadow.cascadeSplits.y ? 0u
                     : viewDepth < gCsmShadow.cascadeSplits.z ? 1u : 2u;
    if (viewDepth > gCsmShadow.cascadeSplits.w) {
        return 1.0;
    }

    float4 shadowCoord = mul(gCsmShadow.lightViewProjection[cascadeIndex],
                             float4(worldPos, 1.0));
    if (shadowCoord.w <= 0.0) {
        return 1.0;
    }
    float3 projected = shadowCoord.xyz / shadowCoord.w;
    if (projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }

    float2 baseUv = projected.xy * float2(0.5, 0.5) + float2(0.5, 0.5);
    float2 uv = baseUv * gCsmShadow.atlasUvTransforms[cascadeIndex].xy
              + gCsmShadow.atlasUvTransforms[cascadeIndex].zw;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }

    float receiver = projected.z;
    float bias = 0.0005;
    float pcfRadius = max(gCsmShadow.shadowParams.y, 0.0);
    float shadowStrength = saturate(gCsmShadow.shadowParams.z);
    float shadowedVisibility = 1.0 - shadowStrength;
    float stripHeight = max(gCsmShadow.atlasUvTransforms[cascadeIndex].y
                            * gCsmShadow.shadowParams.w, 1.0);
    float texelSize = 1.0 / stripHeight;
    if (pcfRadius < 0.5) {
        float shadowDepth = ShadowMap.SampleLevel(ShadowSampler, uv, 0.0).r;
        return (receiver - bias) > shadowDepth ? shadowedVisibility : 1.0;
    }
    return samplePoissonDiskPCF(uv, receiver, bias, texelSize, pcfRadius,
                                shadowedVisibility, pixelCoord);
}

float samplePlayerContactShadow(float3 worldPos, float3 normal)
{
    if (gCsmShadow.shadowParams.x < 0.5 || gWind.flutterParams.z <= 0.0) {
        return 1.0;
    }

    float3 surfaceToLight = normalize(gScene.lightDirection.xyz);
    // gScene.lightDirection is the surface-to-light vector. A cast shadow falls
    // away from the light, so the receiver footprint must extend along the
    // opposite ground-plane direction.
    float2 shadowDir = -surfaceToLight.xz;
    float horizontalLen = length(shadowDir);
    if (horizontalLen < 0.001) {
        shadowDir = float2(0.0, -1.0);
        horizontalLen = 1.0;
    }
    shadowDir /= horizontalLen;

    float2 acrossLight = float2(-shadowDir.y, shadowDir.x);
    float2 delta = worldPos.xz - gWind.interactionCenter.xz;
    float along = dot(delta, shadowDir);
    float across = dot(delta, acrossLight);

    float projectedHeight = kPlayerContactShadowHeight
        * horizontalLen / max(surfaceToLight.y, 0.35);
    float contactRadius = 0.82;
    float contactEllipse = dot(delta, delta)
        / max(contactRadius * contactRadius, 0.001);
    float contactCore = exp(-contactEllipse * 1.75);

    float shadowSide = smoothstep(-0.18, 0.28, along);
    float lobeAlong = max(along, 0.0);
    float lobeLength = max(kPlayerContactShadowLength, projectedHeight * 0.55);
    float lobeEllipse = (lobeAlong * lobeAlong)
        / max(lobeLength * lobeLength, 0.001)
        + (across * across)
        / max(kPlayerContactShadowWidth * kPlayerContactShadowWidth, 0.001);
    // Keep the footprint anchored at the feet. The compact core prevents the
    // shadow from detaching, while the one-sided lobe gives a soft tail on the
    // back-light side without drawing a circular rim.
    float lobe = exp(-lobeEllipse * 1.75) * shadowSide;
    float footprint = saturate(max(contactCore * 0.94, lobe * 0.88));
    float upness = saturate(normal.y * 0.5 + 0.5);
    float strength = kPlayerContactShadowStrength * upness;
    return 1.0 - footprint * strength;
}

// --- Stochastic UV rotation to suppress 2K texture tiling --------------------
// Rotates the UV around a per-cell pivot derived from the world position so
// adjacent tiles don't share the same orientation. The rotation angle is
// quantized to 8 discrete steps so the look stays deterministic.
float2 rotateUV(float2 uv, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

// Cell-stochastic orientation intentionally changes discretely from one
// world cell to the next. Never let implicit derivatives cross that
// discontinuity: doing so makes hardware infer a huge footprint at every
// cell boundary and selects unrelated mip levels per terrain block. The
// caller supplies derivatives from continuous world-space UVs, rotated by
// the current cell's rigid transform (which preserves their footprint).
float4 sampleTerrainGrad(
    Texture2D<float4> texture,
    float2 uv,
    float2 uvDx,
    float2 uvDy)
{
    // UV rotation is randomized per world cell. Use the caller's continuous
    // world-space derivatives so implicit derivatives never cross a cell
    // angle discontinuity and spuriously select the terminal mip levels.
    return texture.SampleGrad(TerrainSplatSampler, uv, uvDx, uvDy);
}

// MossyRockAlbedo's authored mip chain is not valid on the steep triplanar
// cliff footprint yet. This explicit base-level path is intentionally scoped
// to that albedo layer; all other terrain maps retain their declared LOD path.
float4 sampleTerrainBaseLevel(Texture2D<float4> texture, float2 uv)
{
    return texture.SampleLevel(TerrainSplatSampler, uv, 0.0);
}

float hash21(float2 p)
{
    float h = dot(p, float2(127.1, 311.7));
    return frac(sin(h) * 43758.5453);
}

bool terrainWeightPageAt(float2 worldXZ, out uint pageLayer, out float2 localUv)
{
    pageLayer = 0u;
    localUv = float2(0.0, 0.0);
    for (uint index = 0u; index < gTerrainPageTable.entryCount; ++index) {
        const TerrainTilePageTableEntry entry = TerrainTilePageTable[index];
        const float4 bounds = entry.worldBounds;
        if (worldXZ.x < bounds.x || worldXZ.x > bounds.y
            || worldXZ.y < bounds.z || worldXZ.y > bounds.w) {
            continue;
        }
        const float2 extent = float2(bounds.y - bounds.x, bounds.w - bounds.z);
        if (extent.x <= 0.0 || extent.y <= 0.0) {
            return false;
        }
        pageLayer = entry.weightArrayLayer;
        localUv = (worldXZ - float2(bounds.x, bounds.z)) / extent;
        localUv = clamp(localUv, float2(0.0005, 0.0005), float2(0.9995, 0.9995));
        return true;
    }
    return false;
}

[shader("fragment")]
PbrSceneOutput fragmentMain(VertexOutput input)
{
    // RenderDoc geometry A/B. This executes before texture sampling and
    // lighting, so any remaining black region did not rasterize as terrain.
    if (gScene.pbrExtraParams.y > 119.5 && gScene.pbrExtraParams.y < 120.5) {
        return taaSceneOutput(float4(1.0, 0.0, 1.0, 1.0), input);
    }

    float3 geometryNormal = normalize(input.normal);
    float slopeMetric = sqrt(max(0.0, 1.0 - geometryNormal.y * geometryNormal.y));
    float h = input.worldPosition.y;

    // ── Sample the active TerrainAsset tile's local weight pages ─────────
    uint weightPageLayer;
    float2 wuv;
    if (!terrainWeightPageAt(input.worldPosition.xz, weightPageLayer, wuv)) {
        // Missing residency is a diagnostic render failure, not a fabricated
        // default material. TerrainGpuRuntime must publish the page table and
        // array pages atomically before this draw is recorded.
        discard;
    }
    float4 pageA = WeightPageA.Sample(TerrainSplatSampler, float3(wuv, weightPageLayer));
    float4 pageB = WeightPageB.Sample(TerrainSplatSampler, float3(wuv, weightPageLayer));

    // Extract 6 base weights + 2 overlays.
    float wGrassMoss = pageA.r;
    float wForestFloor = pageA.g;
    float wWetMud = pageA.b;
    float wRockyPath = pageA.a;
    float wMossyRock = pageB.r;
    float wDirtySnow = pageB.g;
    float snowOverlay = pageB.b;
    float wetnessOverlay = pageB.a;

    // Raw base-weight sum. Black means no surface was assigned at that world
    // position; any non-black value rules out the weight page as the source.
    if (gScene.pbrExtraParams.y > 122.5 && gScene.pbrExtraParams.y < 123.5) {
        float weightSumDebug = saturate(wGrassMoss + wForestFloor + wWetMud
            + wRockyPath + wMossyRock + wDirtySnow);
        return taaSceneOutput(float4(weightSumDebug, weightSumDebug, weightSumDebug, 1.0), input);
    }

    // Renormalize base weights (texture quantization may drift).
    float wsum = wGrassMoss + wForestFloor + wWetMud
               + wRockyPath + wMossyRock + wDirtySnow;
    float invW = 1.0 / max(wsum, 0.001);
    wGrassMoss *= invW;
    wForestFloor *= invW;
    wWetMud *= invW;
    wRockyPath *= invW;
    wMossyRock *= invW;
    wDirtySnow *= invW;

    // ── Apply overlay layers (separate schema, additive) ──────────────────
    // Snow overlay: boost dirtySnow, pull from grassMoss/mossyRock.
    float snowFactor = snowOverlay * saturate((h - gTerrain.weightParams.y) /
        max(gTerrain.weightParams.z, 0.001));
    float snowPull = snowFactor * 0.7;
    wDirtySnow += snowPull;
    wGrassMoss *= (1.0 - snowPull);
    wMossyRock *= (1.0 - snowPull * 0.8);

    // Wetness overlay: boost wetMud slightly.
    float wetBoost = wetnessOverlay * 0.3;
    wWetMud += wetBoost;
    wGrassMoss *= (1.0 - wetBoost * 0.5);

    // Re-normalize after overlays.
    wsum = wGrassMoss + wForestFloor + wWetMud
         + wRockyPath + wMossyRock + wDirtySnow;
    invW = 1.0 / max(wsum, 0.001);
    wGrassMoss *= invW;
    wForestFloor *= invW;
    wWetMud *= invW;
    wRockyPath *= invW;
    wMossyRock *= invW;
    wDirtySnow *= invW;

    // ── Stochastic UV for tiling suppression ──────────────────────────────
    float2 tuv = input.worldPosition.xz * gTerrain.texParams.x;
    float cellAngle = hash21(floor(input.worldPosition.xz * 0.5)) * 6.2831853;
    float2 tuvRot = rotateUV(tuv, cellAngle);
    float2 tuvDx = ddx(tuv);
    float2 tuvDy = ddy(tuv);
    float2 tuvRotDx = rotateUV(tuvDx, cellAngle);
    float2 tuvRotDy = rotateUV(tuvDy, cellAngle);

    // ── Tri-planar projection for steep faces (mossyRock) ─────────────────
    float3 an = abs(geometryNormal);
    float wallW = clamp(1.0 - an.y * 1.45, 0.0, 1.0);
    float axisW = an.x / max(1e-4, an.x + an.z);

    // RenderDoc LOD heat map. The terrain source textures are normalized to
    // 1024px; show the largest derivative-selected LOD among the horizontal
    // and cliff triplanar projections without sampling any texture.
    if (gScene.pbrExtraParams.y > 123.5 && gScene.pbrExtraParams.y < 124.5) {
        float lodXZ = log2(max(1e-6, max(length(ddx(tuv)), length(ddy(tuv))) * 1024.0));
        float2 wallXY = input.worldPosition.xy * gTerrain.texParams.w;
        float2 wallZY = input.worldPosition.zy * gTerrain.texParams.w;
        float lodWall = max(
            log2(max(1e-6, max(length(ddx(wallXY)), length(ddy(wallXY))) * 1024.0)),
            log2(max(1e-6, max(length(ddx(wallZY)), length(ddy(wallZY))) * 1024.0)));
        float lod = clamp(max(lodXZ, lerp(lodXZ, lodWall, wallW)), 0.0, 10.0);
        return taaSceneOutput(float4(lod / 10.0, saturate((lod - 4.0) / 4.0), saturate((lod - 8.0) * 2.0), 1.0), input);
    }

    // ── Sample 6 albedo layers ────────────────────────────────────────────
    // Weight pages are sparse: sampling all six PBR sets for every terrain
    // pixel was the dominant GPU bandwidth cost. Skip zero-contribution layers
    // while retaining exactly the same blend near material transitions.
    const float kLayerSampleThreshold = 0.003;
    float3 grassAlb = float3(0.0);
    float3 forestAlb = float3(0.0);
    float3 mudAlb = float3(0.0);
    float3 pathAlb = float3(0.0);
    if (wGrassMoss > kLayerSampleThreshold) {
        grassAlb = lerp(sampleTerrainGrad(GrassMossAlbedo, tuv, tuvDx, tuvDy).rgb,
            sampleTerrainGrad(GrassMossAlbedo, tuv * 0.31, tuvDx * 0.31, tuvDy * 0.31).rgb, 0.42);
    }
    if (wForestFloor > kLayerSampleThreshold) forestAlb = sampleTerrainGrad(ForestFloorAlbedo, tuvRot * 0.9, tuvRotDx * 0.9, tuvRotDy * 0.9).rgb;
    if (wWetMud > kLayerSampleThreshold) mudAlb = sampleTerrainGrad(WetMudAlbedo, tuv * 0.8, tuvDx * 0.8, tuvDy * 0.8).rgb;
    if (wRockyPath > kLayerSampleThreshold) pathAlb = sampleTerrainGrad(RockyPathAlbedo, tuvRot * 0.7, tuvRotDx * 0.7, tuvRotDy * 0.7).rgb;

    // mossyRock: blend top-down with wall projection on steep faces.
    // The terrain capture isolates the black strips to the rock albedo's
    // derivative-selected mip levels on near-vertical triangles. Use the
    // authored base level for this triplanar cliff layer until its mip upload
    // path is corrected; this keeps the texture continuous across chunks.
    float3 rockAlb = float3(0.0);
    if (wMossyRock > kLayerSampleThreshold) {
        float3 rockFlat = sampleTerrainBaseLevel(MossyRockAlbedo, tuv * 0.6).rgb;
        if (wallW > 0.01) {
            float3 rockWall = lerp(sampleTerrainBaseLevel(MossyRockAlbedo, input.worldPosition.xy * gTerrain.texParams.w).rgb,
                sampleTerrainBaseLevel(MossyRockAlbedo, input.worldPosition.zy * gTerrain.texParams.w).rgb, axisW);
            rockAlb = lerp(rockFlat, rockWall, wallW);
        } else rockAlb = rockFlat;
    }
    float3 snowAlb = wDirtySnow > kLayerSampleThreshold
        ? sampleTerrainGrad(DirtySnowAlbedo, tuv * 0.5, tuvDx * 0.5, tuvDy * 0.5).rgb : float3(0.0);

    // ── Blend albedo by weights ───────────────────────────────────────────
    float3 alb = grassAlb * wGrassMoss
               + forestAlb * wForestFloor
               + mudAlb * wWetMud
               + pathAlb * wRockyPath
               + rockAlb * wMossyRock
               + snowAlb * wDirtySnow;

    // ── Wetness darkening (overlay) ───────────────────────────────────────
    // P9: Boost wetness by weather (rain increases dynamic wetness on top of
    // the baked terrain hydrology overlay).
    float effectiveWetness = wetnessOverlay + gWeather.params.y * (1.0 - wetnessOverlay);
    effectiveWetness = saturate(effectiveWetness);
    alb *= lerp(1.0, 0.65, effectiveWetness * 0.6);

    // ── Macro noise: brightness swing + grass hue drift breaks tiling ─────
    float macro = MacroNoiseTexture.Sample(TerrainSplatSampler, input.worldPosition.xz * gTerrain.texParams.y).r;
    alb *= lerp(0.92, 1.08, macro);
    float macro2 = MacroNoiseTexture.Sample(TerrainSplatSampler, input.worldPosition.xz * gTerrain.texParams.z + 0.37).r;
    alb = lerp(alb, alb * float3(1.07, 1.03, 0.86), (macro2 - 0.5) * 0.5 * wGrassMoss);

    float3 baseColor = alb;
    float alpha = 1.0;
    float3 emissive = float3(0.0, 0.0, 0.0);
    // Raw surface blend: isolates weight-page/texture inputs from normals,
    // direct lighting, ambient lighting, and post processing.
    if (gScene.pbrExtraParams.y > 120.5 && gScene.pbrExtraParams.y < 121.5) {
        return taaSceneOutput(float4(baseColor, 1.0), input);
    }

    // ── Weighted detail normals (tangent-space) ───────────────────────────
    float3 tangentCandidate = input.tangent.xyz
        - geometryNormal * dot(geometryNormal, input.tangent.xyz);
    float tangentLengthSq = dot(tangentCandidate, tangentCandidate);
    if (tangentLengthSq < 1e-8) {
        // A malformed or legacy terrain vertex can carry a tangent parallel to
        // its normal (most visible on steep X-facing slopes and old skirts).
        // Normalizing that zero projection produces NaNs which then explode in
        // the GGX term as bright triangle-shaped highlights. Pick a deterministic
        // axis that is not parallel to N so the TBN frame always remains finite.
        float3 fallbackAxis = abs(geometryNormal.y) < 0.999
            ? float3(0.0, 1.0, 0.0)
            : float3(0.0, 0.0, 1.0);
        tangentCandidate = cross(fallbackAxis, geometryNormal);
        tangentLengthSq = dot(tangentCandidate, tangentCandidate);
    }
    float3 tangent = tangentCandidate * rsqrt(max(tangentLengthSq, 1e-8));
    float3 bitangent = normalize(cross(geometryNormal, tangent) * input.tangent.w);

    float3 gN = wGrassMoss > kLayerSampleThreshold ? GrassMossNormal.Sample(TerrainSplatSampler, tuv).xyz * 2.0 - 1.0 : float3(0.0);
    float3 fN = wForestFloor > kLayerSampleThreshold ? sampleTerrainGrad(ForestFloorNormal, tuvRot * 0.9, tuvRotDx * 0.9, tuvRotDy * 0.9).xyz * 2.0 - 1.0 : float3(0.0);
    float3 mN = wWetMud > kLayerSampleThreshold ? WetMudNormal.Sample(TerrainSplatSampler, tuv * 0.8).xyz * 2.0 - 1.0 : float3(0.0);
    float3 pN = wRockyPath > kLayerSampleThreshold ? sampleTerrainGrad(RockyPathNormal, tuvRot * 0.7, tuvRotDx * 0.7, tuvRotDy * 0.7).xyz * 2.0 - 1.0 : float3(0.0);
    float3 rN = wMossyRock > kLayerSampleThreshold ? MossyRockNormal.Sample(TerrainSplatSampler, tuv * 0.6).xyz * 2.0 - 1.0 : float3(0.0);
    float3 sN = wDirtySnow > kLayerSampleThreshold ? DirtySnowNormal.Sample(TerrainSplatSampler, tuv * 0.5).xyz * 2.0 - 1.0 : float3(0.0);

    float2 detN = gN.xy * wGrassMoss * 0.65
                + fN.xy * wForestFloor * 0.7
                + mN.xy * wWetMud * 0.6
                + pN.xy * wRockyPath * 0.8
                + rN.xy * wMossyRock * 0.9 * (1.0 - wallW)
                + sN.xy * wDirtySnow * 0.5;
    float3 normal = normalize(geometryNormal + (tangent * detN.x + bitangent * detN.y));

    // Tri-planar normal for mossyRock on cliff faces.
    if (wMossyRock * wallW > 0.01) {
        float3 rNx = MossyRockNormal.Sample(TerrainSplatSampler, input.worldPosition.zy * gTerrain.texParams.w).xyz * 2.0 - 1.0;
        float3 rNz = MossyRockNormal.Sample(TerrainSplatSampler, input.worldPosition.xy * gTerrain.texParams.w).xyz * 2.0 - 1.0;
        float3 wallPerturb = lerp(float3(rNz.x, rNz.y, 0.0), float3(0.0, rNx.y, rNx.x), axisW);
        normal = normalize(normal + wallPerturb * (wMossyRock * wallW * 0.8));
    }
    // Final shading normal: distinguishes a bad normal basis from lighting.
    if (gScene.pbrExtraParams.y > 121.5 && gScene.pbrExtraParams.y < 122.5) {
        return taaSceneOutput(float4(normal * 0.5 + 0.5, 1.0), input);
    }

    // ── Brush ring emissive (editor/debug terrain brush) -------------------
    if (gTerrain.brushParams.w > 0.5) {
        float d = distance(input.worldPosition.xz, gTerrain.brushParams.xz);
        float rw = max(0.28, gTerrain.brushParams.y * 0.055);
        float ring = smoothstep(gTerrain.brushParams.y - rw, gTerrain.brushParams.y, d)
                   * (1.0 - smoothstep(gTerrain.brushParams.y, gTerrain.brushParams.y + rw, d));
        emissive += gTerrain.brushColor.rgb * ring * gTerrain.brushColor.a;
    }

    // ── Material params (per-layer roughness from UBO) ────────────────────
    // Keep all four authored maps per layer live in the WebGPU lowering.
    float grassRoughTex = GrassMossRoughness.Sample(TerrainSplatSampler, tuv).r;
    float forestRoughTex = ForestFloorRoughness.Sample(TerrainSplatSampler, tuvRot * 0.9).r;
    float mudRoughTex = WetMudRoughness.Sample(TerrainSplatSampler, tuv * 0.8).r;
    float pathRoughTex = RockyPathRoughness.Sample(TerrainSplatSampler, tuvRot * 0.7).r;
    float rockRoughTex = MossyRockRoughness.Sample(TerrainSplatSampler, tuv * 0.6).r;
    float snowRoughTex = DirtySnowRoughness.Sample(TerrainSplatSampler, tuv * 0.5).r;
    float grassHeightTex = GrassMossHeight.Sample(TerrainSplatSampler, tuv).r;
    float forestHeightTex = ForestFloorHeight.Sample(TerrainSplatSampler, tuvRot * 0.9).r;
    float mudHeightTex = WetMudHeight.Sample(TerrainSplatSampler, tuv * 0.8).r;
    float pathHeightTex = RockyPathHeight.Sample(TerrainSplatSampler, tuvRot * 0.7).r;
    float rockHeightTex = MossyRockHeight.Sample(TerrainSplatSampler, tuv * 0.6).r;
    float snowHeightTex = DirtySnowHeight.Sample(TerrainSplatSampler, tuv * 0.5).r;
    float metallic = 0.0;
    // RenderTerrainConfig stores the physical base roughness for each ground
    // material. Treat the texture as limited surface variation around that
    // base; multiplying the two made dark texels almost mirror-smooth and
    // produced isolated, bloom-amplified highlight patches on otherwise rough
    // grass and snow.
    float roughGrass = lerp(gTerrain.layerRough012.x, grassRoughTex, 0.2);
    float roughForest = lerp(gTerrain.layerRough012.y, forestRoughTex, 0.2);
    float roughMud = lerp(gTerrain.layerRough012.z, mudRoughTex, 0.2);
    float roughPath = lerp(gTerrain.layerRough012.w, pathRoughTex, 0.2);
    float roughRock = lerp(gTerrain.layerRough345.x, rockRoughTex, 0.2);
    float roughSnow = lerp(gTerrain.layerRough345.y, snowRoughTex, 0.2);

    float roughness = roughGrass * wGrassMoss
                    + roughForest * wForestFloor
                    + roughMud * wWetMud
                    + roughPath * wRockyPath
                    + roughRock * wMossyRock
                    + roughSnow * wDirtySnow;

    // Wetness increases roughness reduction (wetter = smoother).
    // P9: Use effective wetness (baked overlay + dynamic weather).
    roughness *= lerp(1.0, 0.7, effectiveWetness * 0.5);
    // Every terrain layer is soil, vegetation, rock or snow rather than a
    // polished dielectric. Keep a conservative floor after wetness so the GGX
    // lobe cannot collapse into a white texel-shaped glint.
    roughness = clamp(roughness, 0.45, 1.0);

    float heightDetail = grassHeightTex * wGrassMoss
                       + forestHeightTex * wForestFloor
                       + mudHeightTex * wWetMud
                       + pathHeightTex * wRockyPath
                       + rockHeightTex * wMossyRock
                       + snowHeightTex * wDirtySnow;
    float occlusion = lerp(0.78, 1.0, heightDetail);

    float3 viewDirection = normalize(gScene.cameraPositionExposure.xyz - input.worldPosition);
    float playerContactShadow = samplePlayerContactShadow(input.worldPosition, normal);

    // Shadow-map isolation diagnostic (shared with pbr_scene).
    if (gScene.pbrExtraParams.y > 105.5 && gScene.pbrExtraParams.y < 106.5) {
        float nDotLDbg = 0.0;
        uint activeCountDbg = (uint)gLights.countAndPadding.x;
        if (activeCountDbg > 0u) {
            WoccGpuLight LDbg = gLights.lights[0u];
            if ((uint)LDbg.positionType.w == 0u) {
                nDotLDbg = saturate(dot(normal, normalize(LDbg.directionRange.xyz)));
            }
        }
        return taaSceneOutput(float4(nDotLDbg, nDotLDbg, nDotLDbg, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 104.5 && gScene.pbrExtraParams.y < 105.5) {
        return taaSceneOutput(float4(playerContactShadow, playerContactShadow, playerContactShadow, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 102.0) {
        float3 cameraToFragmentDbg = gScene.cameraPositionExposure.xyz - input.worldPosition;
        float viewDepthDbg = length(cameraToFragmentDbg);
        float visibility = sampleShadowCsm(input.worldPosition, viewDepthDbg,
                                           input.position.xy);
        return taaSceneOutput(float4(visibility, visibility, visibility, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 100.0) {
        float3 projected = input.shadowCoord.xyz / max(input.shadowCoord.w, 0.0001);
        float2 uv = projected.xy * 0.5 + 0.5;
        float2 flippedUv = uv;
        flippedUv.y = 1.0 - flippedUv.y;
        float receiver = saturate(projected.z);
        float flippedDepth = ShadowMap.SampleLevel(ShadowSampler, flippedUv, 0.0).r;
        float unflippedDepth = ShadowMap.SampleLevel(ShadowSampler, uv, 0.0).r;
        return taaSceneOutput(float4(receiver, flippedDepth, unflippedDepth, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 99.0) {
        float visibility = sampleShadow(input.shadowCoord);
        return taaSceneOutput(float4(visibility, visibility, visibility, 1.0), input);
    }

    // Contact-shadow based ambient occlusion (shared with pbr_scene.slang).
    float upness = saturate(normal.y * 0.5 + 0.5);
    float ambientShadow = 1.0 - input.contactShadow * kContactShadowStrength *
        (kContactShadowBase + upness * kContactShadowNormalScale);
    float3 reflectionDirection = reflect(-viewDirection, normal);
    float nDotV = saturate(dot(normal, viewDirection));
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 fresnel = fresnelSchlickRoughness(nDotV, f0, roughness);
    float3 kD = (float3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic);

    // Ambient is provided entirely by DDGI (below) - the IBL cubemap ambient
    // path was removed. Start from zero and let the DDGI term fill it in.
    float3 ambientColor = baseColor * gScene.lightColor.rgb
        * saturate(gScene.lightColor.w) * ambientShadow * occlusion
        * lerp(1.0, playerContactShadow, 0.58);

    // Dynamic DDGI contribution (shared with pbr_scene.slang).
    if (gScene.environmentParams.y > 0.5) {
        float2 screenUv = input.position.xy * gScene.environmentParams.zw;
        float3 giIrradiance = GiIrradiance.Sample(GiSampler, screenUv).rgb;
        float giStrength = gScene.environmentParams.y;

        float3 indirectDiffuse = giIrradiance * baseColor * kD;

        float roughnessScale = lerp(0.5, 1.0, roughness);
        float3 indirectSpecular = giIrradiance * fresnel * roughnessScale
                                * kTerrainSpecularStrength;

        ambientColor += (indirectDiffuse + indirectSpecular)
                      * ambientShadow * occlusion * giStrength
                      * lerp(1.0, playerContactShadow, 0.65);
    }

    // Multi-light direct contribution.
    float3 directLight = float3(0.0, 0.0, 0.0);
    uint activeCount = (uint)gLights.countAndPadding.x;
    for (uint i = 0u; i < activeCount; ++i) {
        WoccGpuLight L = gLights.lights[i];
        uint ltype = (uint)L.positionType.w;
        float3 toLight;
        float atten = 1.0;
        if (ltype == 0u) {
            toLight = normalize(L.directionRange.xyz);
        } else {
            float3 d = L.positionType.xyz - input.worldPosition;
            float dist = length(d);
            toLight = d / max(dist, 0.001);
            float rangeAtten = (L.directionRange.w > 0.0)
                ? saturate(1.0 - dist / L.directionRange.w)
                : 1.0;
            float decay = L.coneDecayShadow.z;
            float distAtten = 1.0 / pow(max(dist, 0.001), decay);
            atten = rangeAtten * distAtten;
            if (ltype == 2u) {
                float3 spotDir = normalize(-L.directionRange.xyz);
                float cosAng = dot(toLight, spotDir);
                float cosOuter = cos(L.coneDecayShadow.x);
                float cosInner = cos(L.coneDecayShadow.x * (1.0 - L.coneDecayShadow.y));
                atten *= saturate((cosAng - cosOuter) / max(cosInner - cosOuter, 0.001));
            }
        }

        float nDotL = saturate(dot(normal, toLight));
        float3 halfVec = normalize(viewDirection + toLight);
        float3 lightFresnel = fresnelSchlick(saturate(dot(halfVec, viewDirection)), f0);
        float D = distributionGGX(normal, halfVec, roughness);
        float G = geometrySmith(normal, viewDirection, toLight, roughness);
        float3 specular = (D * G * lightFresnel) / max(4.0 * nDotV * nDotL, 0.001);
        specular *= kTerrainSpecularStrength;
        float3 diff = (1.0 - lightFresnel) * (1.0 - metallic) * baseColor
                    * kInvPi;

        float lightShadow = 1.0;
        if (ltype == 0u && L.coneDecayShadow.w > 0.5) {
            if (gCsmShadow.shadowParams.x > 0.5) {
                float viewDepth = length(gScene.cameraPositionExposure.xyz - input.worldPosition);
                lightShadow = sampleShadowCsm(input.worldPosition, viewDepth,
                                              input.position.xy);
            } else {
                lightShadow = sampleShadow(input.shadowCoord);
            }
        }

        directLight += (diff + specular) * nDotL * atten
            * L.colorIntensity.w * L.colorIntensity.rgb
            * lightShadow * lerp(1.0, playerContactShadow, 0.88);
    }

    float3 color = ambientColor + directLight + emissive;

    return taaSceneOutput(float4(color, alpha), input);
}
