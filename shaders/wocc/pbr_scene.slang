// World of ClaudeCraft PBR Scene Shader
// Cook-Torrance BRDF with Directional Light + Shadow + DDGI Support
// Based on material_preview/pbr_preview.slang but extended for full scene rendering
//
// --- Ambient occlusion / contact shadow tuning knobs ---
// Match the stylized reference (stylized_scene.wgsl directionalShadow).
// Increase STRENGTH, BASE, or NORMAL_SCALE for darker crevices and ground shadows.
// Adjust HEIGHT to control how far up the model the contact shadow extends.
static const float kContactShadowHeight = 4.0;
static const float kContactShadowStrength = 0.30;
static const float kContactShadowBase = 0.12;
static const float kContactShadowNormalScale = 0.18;
static const float kDirectionalAmbientBacklight = 0.38;
static const float kDirectionalAmbientFrontScale = 0.62;

// WoCC PBR Scene push constants. The struct body MUST contain NO inline //
// comments: shader_compiler.cpp's reflectedPushConstantSize parser does not
// skip // comments, so an inline comment after a field's semicolon absorbs
// the next field's declaration into the comment, producing a wrong size.
// Explanatory comments live ABOVE the struct, not inside it.
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

// --- Set 1: Material Textures ---
[[vk::binding(0, 1)]]
Texture2D<float4> BaseColorTexture;
[[vk::binding(1, 1)]]
Texture2D<float4> NormalTexture;
[[vk::binding(2, 1)]]
Texture2D<float4> MetallicRoughnessTexture;
[[vk::binding(3, 1)]]
Texture2D<float4> EmissiveTexture;
[[vk::binding(4, 1)]]
Texture2D<float4> OcclusionTexture;
[[vk::binding(5, 1)]]
SamplerState LinearSampler;

// --- Set 2: Shadow + Dynamic DDGI irradiance ---
// ShadowMap (binding 4) is the persistent directional shadow depth; ShadowSampler
// (binding 5) samples it. GiIrradiance (binding 6) is the DDGI resolve output
// from the previous frame (1-frame lag to break the scene-pass-needs-depth /
// resolve-needs-depth dependency cycle); GiSampler (binding 7) samples it at the
// pixel's screen-space UV. The IBL cubemap ambient path was removed - DDGI
// provides the diffuse global illumination.
//
// Phase 1.4 (CSM): binding 0 carries the cascade VP array + split depths +
// per-cascade atlas UV transforms (WoccCsmShadow). The receiver picks a cascade
// by view depth (cascadeSplits) and rescales the shadow-map UV into that
// cascade's atlas sub-region (atlasUvTransforms). All 3 cascade VPs share the
// default float4x4 layout (SPIR-V RowMajor) - the caster (shadow_depth.slang)
// and this receiver read the same row-major bytes from the CPU fill, matching
// AGENTS.md "Matrix Layout" hard constraint #2-3.
struct WoccCsmShadow
{
    float4x4 lightViewProjection[3];   // 3 cascade VPs, row-major
    float4   shadowParams;             // x=enabled, y=pcfRadius, z=strength, w=atlasResolution
    float4   cascadeSplits;            // x=unused, y=near/mid split, z=mid/far split, w=far
    float4   atlasUvTransforms[3];     // per-cascade xy=uvScale, zw=uvOffset
    float4   playerPosition;           // reserved CPU-side cascade origin data
};
[[vk::binding(0, 2)]]
ConstantBuffer<WoccCsmShadow> gCsmShadow;

// TAA: previous-frame view-projection (un-jittered) for the velocity MRT.
// Lives in a dedicated UBO rather than the push-constant block because
// WoccPbrPushConstants is already at the 256B Vulkan push-constant limit and
// cannot absorb another float4x4 (64B). Uses the DEFAULT float4x4 layout (no
// row-major keyword) so SPIR-V emits a RowMajor decoration, matching the
// row-major bytes the CPU fills (core::Mat4, m[row*4+col]) and the same layout
// as gScene.viewProjection / lightViewProjection - see AGENTS.md "Matrix
// Layout" hard constraint #2-3. The caster (this shader) and the TAA resolve
// both read prevViewProjection row-major without transposition.
struct WoccPrevCamera
{
    float4x4 prevViewProjection;
};
[[vk::binding(1, 2)]]
ConstantBuffer<WoccPrevCamera> gPrevCamera;

[[vk::binding(4, 2)]]
Texture2D<float> ShadowMap;
[[vk::binding(5, 2)]]
SamplerState ShadowSampler;
[[vk::binding(6, 2)]]
Texture2D<float4> GiIrradiance;
[[vk::binding(7, 2)]]
SamplerState GiSampler;

// --- Set 3: Lights (UBO, fixed array of 8) ---
// The push-constant block is full (256B), so per-frame variable light data
// lives in this UBO. WoccGpuLight is 4 x float4 (64B, std140-safe); the layout
// is mirrored verbatim by the C++ WoccGpuLight struct in wocc_pbr_pipeline.h.
// Phase 1: only Directional lights (type=0) with castShadow>0.5 sample the
// existing single shadow map; Point/Spot never cast shadows.
struct WoccGpuLight
{
    float4 positionType;      // xyz = position, w = type (0=dir,1=point,2=spot)
    float4 directionRange;    // xyz = surface-to-light dir, w = range (0=infinite)
    float4 colorIntensity;    // rgb = color, w = intensity
    float4 coneDecayShadow;   // x = coneAngle, y = penumbra, z = decay, w = castShadow
};
struct WoccLightArray
{
    WoccGpuLight lights[8];
    float4 countAndPadding;   // x = activeCount, yzw = pad
};
[[vk::binding(0, 3)]]
ConstantBuffer<WoccLightArray> gLights;

// Set 6 is reserved for a skinned draw's double-buffered palette. Static and
// instanced entries do not access it, so their descriptor contracts remain
// unchanged. The host uploads engine column-major matrices transposed for the
// shader's float4x4 layout before binding this UBO.
// Must stay in lockstep with renderer::kMaxGpuSkinningJoints. The Viking
// berserker rig uses 219 joints, so a 128-entry palette would deform it by
// reading an incomplete pose.
static const uint kMaxGpuSkinningJoints = 256;
struct WoccGpuSkinningPalette
{
    float4x4 skinMatrices[kMaxGpuSkinningJoints];
    float4 worldTranslationAndScale;
    float4 worldRotation;
    uint jointCount;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::binding(0, 6)]]
ConstantBuffer<WoccGpuSkinningPalette> gSkinning;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
};

// A second vertex-rate stream. Joint indices are float lanes because the RHI
// currently guarantees Float32x4 across Vulkan/Metal/WebGPU; cast after range
// validation in the vertex stage.
struct SkinningInfluenceInput
{
    float4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
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
    // P8: Terrain contact Y for rock-ground blend. Set to the instance's
    // terrain height (pre-burial) by instanced vertex shader; -1 = disabled.
    float  terrainContactY : TEXCOORD6;
    // P0 (Track C): Packed instance metadata forwarded to the fragment stage.
    // Carries speciesId/ageClass/moisture/slopeClass/geometryLod/shadowLod/
    // stateFlags decoded from instanceData.speciesLodState. The fragment stage
    // does not consume these yet (P1+ will: species tint, P3 SSS thickness,
    // P8 state-based shading); forwarding them here is also what keeps the
    // Slang compiler from optimizing away the speciesLodState vertex input,
    // which would otherwise desync the C++ VertexBufferLayoutDesc (location 8)
    // from the SPV input list and trip Vulkan validation. Bits are reinterpreted
    // from the float4 storage via asuint(); see foliage_instance_batch.h
    // InstanceDataGpu::speciesLodState for the authoritative bit layout.
    uint4  instanceMetadata : TEXCOORD7;
    // TAA: previous-frame clip position (prevViewProjection * worldPos), BEFORE
    // perspective divide. The fragment shader divides this by .w and pairs it
    // with the current NDC (derived from SV_POSITION) to compute the per-pixel
    // motion vector written to the velocity MRT. Passing the un-divided clip
    // vector keeps the interpolation correct under perspective (dividing before
    // interpolation would produce affine-interpolated NDC, which is wrong for
    // perspective projection).
    float4 prevClipPosition : TEXCOORD8;
    // TAA: current-frame clip position (viewProjection * worldPos), BEFORE
    // perspective divide. Paired with prevClipPosition so the velocity MRT can
    // compute the motion vector from two perspective-divided NDC values without
    // needing the render-target extent (which is not available in the
    // push-constant - the block is full at 256B). Both clip vectors use the
    // same DEFAULT float4x4 layout (RowMajor) as gScene.viewProjection.
    float4 curClipPosition : TEXCOORD9;
};

// FragmentInput mirrors VertexOutput and adds SV_IsFrontFace, a fragment-only
// rasterizer builtin that cannot appear in a vertex-stage output (Slang
// rejects it there with E30702). The vertex shader returns VertexOutput; the
// rasterizer matches fields by semantic and supplies frontFacing from the
// triangle winding + FrontFace state. Slang resolves SV_IsFrontFace to
// gl_FrontFacing / [[front_facing]] / @builtin(front_facing) across backends.
struct FragmentInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 shadowCoord : TEXCOORD4;
    float  contactShadow : TEXCOORD5;
    float  terrainContactY : TEXCOORD6;
    bool   frontFacing : SV_IsFrontFace;
    float4 prevClipPosition : TEXCOORD8;
    float4 curClipPosition : TEXCOORD9;
};

// TAA: MRT output. SV_Target0 is the scene color (HDR linear, as before).
// SV_Target1 is the per-pixel motion vector in [0,1] UV space: a pixel that
// did not move maps to 0.5 (the neutral velocity). The TAA resolve shader
// samples the history color at (currentUV + (velocity - 0.5)) to reproject.
struct PbrSceneOutput
{
    float4 color : SV_Target0;
    float2 velocity : SV_Target1;
};

// TAA: compute the per-pixel motion vector from the current and previous
// clip positions (both pre-perspective-divide). The delta is scaled + biased
// to [0,1] UV space; the neutral (no-motion) value is 0.5. The TAA resolve
// shader reprojects history color to (currentUV + (velocity - 0.5)).
float2 computeTaaVelocity(float4 curClipPosition, float4 prevClipPosition)
{
    float curW = max(abs(curClipPosition.w), 1e-6);
    float prevW = max(abs(prevClipPosition.w), 1e-6);
    float2 curNdc = curClipPosition.xy / curW;
    float2 prevNdc = prevClipPosition.xy / prevW;
    return (curNdc - prevNdc) * 0.5 + 0.5;
}

// TAA: wrap a scene color in the MRT output, attaching the per-pixel velocity
// derived from the fragment's interpolated clip positions. Used by every
// return path in fragmentMain / fragmentMainFoliage so debug visualizations
// and early-outs still carry a correct motion vector.
PbrSceneOutput taaSceneOutput(float4 color, FragmentInput input)
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
    output.worldPosition = input.position;
    output.normal = normalize(input.normal);
    output.uv = input.uv;
    output.tangent = input.tangent;
    output.position = mul(gScene.viewProjection, float4(input.position, 1.0));
    // TAA: previous-frame clip position for the velocity MRT. Computed from the
    // same world position so per-pixel motion reflects camera/object movement.
    output.prevClipPosition = mul(gPrevCamera.prevViewProjection, float4(input.position, 1.0));
    // TAA: current-frame clip position (pre-perspective-divide) so the fragment
    // shader can compute NDC without needing the render-target extent.
    output.curClipPosition = output.position;
    // Project the world-space vertex into the light's clip space for shadow
    // lookup. The fragment shader divides by .w and remaps to [0,1] for the
    // shadow map texture coordinate.
    output.shadowCoord = mul(gScene.lightViewProjection, float4(input.position, 1.0));
    // Contact shadow: darken vertices near the ground (matching the stylized
    // reference's contactShadow in stylized_scene.wgsl). Uses the unscaled
    // world Y, which for this project places the terrain at y≈0.
    output.contactShadow = 1.0 - smoothstep(0.0, kContactShadowHeight, input.position.y);
    // P8: Non-instanced path has no terrain contact blend.
    output.terrainContactY = -1.0;
    return output;
}

float3 rotateByQuaternion(float3 value, float4 q)
{
    float3 axis = q.xyz;
    float3 twiceCross = 2.0 * cross(axis, value);
    return value + q.w * twiceCross + cross(axis, twiceCross);
}

[shader("vertex")]
VertexOutput vertexMainSkinned(VertexInput input, SkinningInfluenceInput influence)
{
    float4 blendedPosition = float4(0.0, 0.0, 0.0, 0.0);
    float3 blendedNormal = float3(0.0, 0.0, 0.0);
    float3 blendedTangent = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    [unroll]
    for (uint lane = 0; lane < 4; ++lane) {
        const float weight = influence.weights[lane];
        const uint joint = min((uint)max(influence.joints[lane], 0.0),
            gSkinning.jointCount > 0 ? gSkinning.jointCount - 1 : 0);
        if (weight > 0.0 && joint < kMaxGpuSkinningJoints) {
            const float4x4 skin = gSkinning.skinMatrices[joint];
            blendedPosition += mul(skin, float4(input.position, 1.0)) * weight;
            blendedNormal += mul((float3x3)skin, input.normal) * weight;
            blendedTangent += mul((float3x3)skin, input.tangent.xyz) * weight;
            weightSum += weight;
        }
    }
    const float invWeight = weightSum > 0.000001 ? rcp(weightSum) : 1.0;
    const float scale = gSkinning.worldTranslationAndScale.w;
    const float3 localPosition = blendedPosition.xyz * invWeight * scale;
    const float3 worldPosition = rotateByQuaternion(localPosition, gSkinning.worldRotation)
        + gSkinning.worldTranslationAndScale.xyz;
    const float3 worldNormal = normalize(rotateByQuaternion(blendedNormal, gSkinning.worldRotation));
    const float3 worldTangent = normalize(rotateByQuaternion(blendedTangent, gSkinning.worldRotation));

    VertexOutput output;
    output.worldPosition = worldPosition;
    output.normal = worldNormal;
    output.uv = input.uv;
    output.tangent = float4(worldTangent, input.tangent.w);
    output.position = mul(gScene.viewProjection, float4(worldPosition, 1.0));
    // TAA: previous-frame clip position for the velocity MRT.
    output.prevClipPosition = mul(gPrevCamera.prevViewProjection, float4(worldPosition, 1.0));
    output.curClipPosition = output.position;
    output.shadowCoord = mul(gScene.lightViewProjection, float4(worldPosition, 1.0));
    output.contactShadow = 1.0 - smoothstep(0.0, kContactShadowHeight, worldPosition.y);
    output.terrainContactY = -1.0;
    return output;
}

// P6: Instanced vegetation vertex shader.
// Slot 0 (vertex-rate): InstanceVertexInput extends MeshVertex with COLOR0
//   wind weights (primaryBend=R, flutter=G, stiffness=B).
// Slot 1 (instance-rate): InstanceDataInput carries per-instance transform
//   (position+scale, yaw+phase+id+lod). One drawIndexed call with
//   instanceCount=N renders N instances without baking transforms into
//   world-space vertices.
// Wind deformation is applied here (P6 initial pass: simple primary bend
// based on height). Full WindFieldSystem arrives in P7.

struct InstanceVertexInput
{
    float3 position   : POSITION0;
    float3 normal     : NORMAL0;
    float2 uv         : TEXCOORD0;
    float4 tangent    : TANGENT0;
    float3 windWeight : COLOR0;  // R=primaryBend, G=flutter, B=stiffness
};

struct InstanceDataInput
{
    float4 positionScale : POSITION1;  // xyz = world pos, w = uniform scale
    float4 yawPhaseIdLod : POSITION2;  // x = yaw, y = phase, z = id, w = lod
    // P8: Extra transform for terrain fusion (rocks: burial, tilt, non-uniform Y scale).
    float4 extraTransform : POSITION3; // x = pitchX, y = rollZ, z = scaleY (0=uniform), w = burialY
    // P0 (Track C): Packed metadata for P1 species palette / P4 geometry LOD
    // / P5 GPU cull / P8 gameplay state. Bits travel as float (the RHI has no
    // Uint32x4 VertexFormat); decode via asuint(). See foliage_instance_batch.h
    // InstanceDataGpu::speciesLodState for the authoritative bit layout.
    // Default (0,0,0,0) = species 0 / age 0 / geometryLod 0 / intact, which is
    // backward-compatible with the pre-extension 48-byte instance layout.
    float4 speciesLodState : POSITION4;
};

// P7: Wind field UBO (set 4, binding 0). Single source of truth for all
// vegetation motion. Mirrors the C++ WoccWindFieldUbo struct in
// wind_field_system.h (4 x float4 = 64 bytes, std140-safe).
struct WoccWindField
{
    float4 directionSpeed;    // xyz = normalized wind dir, w = speed [0,10]
    float4 timeGustTurb;      // x = time, y = gust amp, z = gust freq, w = turb amp
    float4 flutterParams;     // x = flutter freq, y = flutter amp, z = interact radius, w = interact strength
    float4 interactionCenter; // xyz = center (player pos), w = reserved
};
[[vk::binding(0, 4)]]
ConstantBuffer<WoccWindField> gWind;

// P9: Weather UBO (set 5, binding 0). Mirrors the C++ WoccWeatherUbo struct
// in weather_system.h (4 x float4 = 64 bytes, std140-safe).
struct WoccWeather
{
    float4 params;       // x = weatherMode (0=clear,1=overcast,2=rain), y = wetness, z = cloudCover, w = fogDensityScale
    float4 lightAdjust;  // x = sunIntensityScale, y = ambientScale, z = exposureAdjust, w = reserved
    float4 skyTint;      // rgb = sky color tint multiplier, w = horizon haze intensity
    float4 fogTint;      // rgb = fog color, a = fog density override
};
[[vk::binding(0, 5)]]
ConstantBuffer<WoccWeather> gWeather;

// P0 (Track C): Decode the packed speciesLodState float4 into a uint4 the
// fragment stage can consume. The float4 storage travels the RHI's Float32x4
// vertex-attribute path (the RHI has no Uint32x4 VertexFormat); asuint()
// reinterprets the bits. The bit layout MUST match the C++ side documented
// on arp::pcg::InstanceDataGpu::speciesLodState (single source of truth):
//   word0 bits  0-7  : speciesId    (u8, P1)
//   word0 bits  8-15 : ageClass     (u8, P1)
//   word0 bits 16-23 : moisture     (u8, P2)
//   word0 bits 24-31 : slopeClass   (u8, P2)
//   word1 bits  0-7  : geometryLod  (u8, P4)
//   word1 bits  8-15 : shadowLod    (u8, P6)
//   word1 bits 16-31 : stateFlags   (u16, P8)
//   word2, word3     : reserved (0)
// Returning the raw uint4 (rather than pre-decoded scalars) keeps the vertex
// stage cheap and lets the fragment stage pick the fields it needs. The
// function is declared no-op-safe: a default (0,0,0,0) input yields (0,0,0,0),
// which the fragment stage reads as species 0 / geometryLod 0 / intact.
uint4 decodeInstanceMetadata(float4 speciesLodState)
{
    return asuint(speciesLodState);
}

// P7: Foliage deformation function. Shared by color, shadow, and depth prepass
// passes to guarantee identical vertex positions across all passes.
//
// Layers:
//   1. Primary bend: wind direction * height^2 * (1 - stiffness)
//   2. Gust: low-frequency traveling wave (sin of position dot windDir + time)
//   3. Turbulence: crosswind noise modulated by height
//   4. Flutter: high-frequency per-instance oscillation (phase from instance data)
//
// Animation LOD (from instanceData.yawPhaseIdLod.w):
//   0 = near: full (bend + gust + turbulence + flutter)
//   1 = mid:  bend + gust (no flutter, no turbulence)
//   2 = far:  bend only (low-freq sway)
//   3+:       frozen
//
// Interaction: pushes grass away from player position within a radius.
float3 applyWindDeformation(
    float3 worldPos,
    float3 localPos,
    float3 windWeight,       // R=primaryBend, G=flutter, B=stiffness
    float2 instancePhase,    // x = phase, y = lodClass
    float3 instanceWorldPos)
{
    const float primaryBend = windWeight.x;
    const float flutterWeight = windWeight.y;
    const float stiffness = windWeight.b;

    const float lodClass = instancePhase.y;
    const float phase = instancePhase.x;

    // Frozen LOD: no deformation.
    if (lodClass >= 3.0) return worldPos;

    // A real UBO supplies these values.  The non-zero fallbacks keep foliage
    // animated on drivers that defer a vertex-stage UBO update for an
    // in-flight frame; the CPU also advances `phase` every frame.
    const float time = gWind.timeGustTurb.x + phase;
    const float windSpeed = max(gWind.directionSpeed.w, 3.0);
    const float3 configuredDir = gWind.directionSpeed.xyz;
    const float3 windDir = length(configuredDir.xz) > 0.001
        ? normalize(configuredDir)
        : float3(0.8, 0.0, 0.6);

    // Height fraction: 0 at root, 1 at tip. Using local Y (pre-transform).
    // The divisor is normalized by a small reference height so that short
    // vegetation (grass ~0.4m) still gets meaningful displacement. The
    // previous /2.0 made grass heightFrac ≈ 0.2, squelched by the square
    // below to ~0.04 - sub-millimeter and invisible. Using /0.5 gives
    // grass heightFrac ≈ 0.8 (nearly full bend at the tip).
    const float heightFrac = saturate(abs(localPos.y) / 0.5);

    // 1. Hierarchical clump sway.  The card baker supplies the resting lean;
    // wind must be centred around that pose, not added as a permanent world
    // offset.  The old model applied a large, static primary bend first and
    // then added a small gust on top, which made a moving grass clump read as
    // a static diagonally-leaning blade.  Phase comes from the instance buffer
    // and is advanced on the CPU every frame, so this remains animated even
    // when a uniform-buffer update is one frame behind.
    const float flex = (1.0 - stiffness);
    const float3 crossDir = float3(-windDir.z, 0.0, windDir.x);
    const float spatialPhase = dot(instanceWorldPos.xz, windDir.xz) * 0.70;
    const float clumpWave = sin(phase * 6.2831853
                                + spatialPhase
                                + gWind.timeGustTurb.x * 1.65);
    const float clumpAmplitude = primaryBend * flex * heightFrac
                               * (0.050 + windSpeed * 0.040);
    float3 displacement = windDir * (clumpWave * clumpAmplitude);
    // A smaller cross-axis component makes the clump trace an ellipse rather
    // than looking like a rigid card sliding left/right.
    displacement += crossDir * (cos(phase * 6.2831853
                                    + spatialPhase * 1.37
                                    + gWind.timeGustTurb.x * 1.18)
                                * clumpAmplitude * 0.28);

    // 2. Gust: low-frequency traveling wave.
    if (lodClass <= 1.0) {
        const float gustAmp = max(gWind.timeGustTurb.y, 0.25);
        const float gustFreq = gWind.timeGustTurb.z;
        const float gustWave = sin(dot(worldPos.xz, windDir.xz) * gustFreq
                                   + time * gustFreq * 2.0);
        const float gustStrength = gustAmp * heightFrac * windSpeed * 0.075;
        displacement += windDir * gustWave * gustStrength;
        displacement += crossDir * gustWave * gustStrength * (0.45 + 0.25 * primaryBend);
    }

    // 3. Turbulence: crosswind variation (near LOD only).
    if (lodClass <= 0.0) {
        const float turbAmp = gWind.timeGustTurb.w;
        const float turbNoise = sin(worldPos.x * 1.3 + time * 1.7)
                              * cos(worldPos.z * 1.1 + time * 1.3);
        displacement += crossDir * turbNoise * turbAmp * heightFrac * windSpeed * 0.08;
    }

    // 4. Flutter: high-frequency per-instance oscillation (near LOD only).
    if (lodClass <= 0.0 && flutterWeight > 0.001) {
        const float flutterFreq = max(gWind.flutterParams.x, 5.0);
        const float flutterAmp = max(gWind.flutterParams.y, 0.35);
        const float flutterWave = sin(time * flutterFreq + phase * 6.28318);
        displacement += crossDir * flutterWave * flutterWeight * flutterAmp
                      * heightFrac * 0.12;
        displacement.y += abs(flutterWave) * flutterWeight * heightFrac * 0.018;
    }

    // 5. Interaction: push grass away from player within radius.
    const float interactRadius = gWind.flutterParams.z;
    const float interactStrength = gWind.flutterParams.w;
    if (interactRadius > 0.001) {
        const float3 toCenter = worldPos - gWind.interactionCenter.xyz;
        const float dist = length(toCenter.xz);
        if (dist < interactRadius) {
            const float falloff = 1.0 - saturate(dist / interactRadius);
            const float3 pushDir = float3(toCenter.x, 0.0, toCenter.z);
            const float pushAmount = falloff * falloff * interactStrength
                                   * heightFrac * 0.3;
            // Clamp pushDir to avoid division by zero.
            const float pushLen = max(length(pushDir.xz), 0.001);
            displacement += pushDir / pushLen * pushAmount;
        }
    }

    return worldPos + displacement;
}

[shader("vertex")]
VertexOutput vertexMainInstanced(InstanceVertexInput input,
                                 InstanceDataInput instanceData)
{
    VertexOutput output;

    // Apply instance transform: scale -> Y-rotate -> tilt -> translate.
    // P8: Non-uniform Y scale (extraTransform.z > 0 overrides uniform scale for Y).
    float scale = instanceData.positionScale.w;
    float scaleY = instanceData.extraTransform.z > 0.0
        ? instanceData.extraTransform.z
        : scale;
    float3 scaledPos;
    scaledPos.x = input.position.x * scale;
    scaledPos.y = input.position.y * scaleY;
    scaledPos.z = input.position.z * scale;

    // Yaw rotation (Y axis).
    float yaw = instanceData.yawPhaseIdLod.x;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float3 yawedPos;
    yawedPos.x = scaledPos.x * cy + scaledPos.z * sy;
    yawedPos.y = scaledPos.y;
    yawedPos.z = -scaledPos.x * sy + scaledPos.z * cy;

    // P8: Tilt rotation (pitch around X, roll around Z) - aligns rocks to slope.
    // Applied after yaw, before translation, so the rock pivots around its origin.
    float pitchX = instanceData.extraTransform.x;
    float rollZ = instanceData.extraTransform.y;
    float3 tiltedPos = yawedPos;
    if (abs(pitchX) > 0.001 || abs(rollZ) > 0.001) {
        // Pitch around X: y' = y*cos - z*sin, z' = y*sin + z*cos
        float cp = cos(pitchX);
        float sp = sin(pitchX);
        float py = tiltedPos.y * cp - tiltedPos.z * sp;
        float pz = tiltedPos.y * sp + tiltedPos.z * cp;
        tiltedPos.y = py;
        tiltedPos.z = pz;
        // Roll around Z: x' = x*cos - y*sin, y' = x*sin + y*cos
        float cr = cos(rollZ);
        float sr = sin(rollZ);
        float rx = tiltedPos.x * cr - tiltedPos.y * sr;
        float ry = tiltedPos.x * sr + tiltedPos.y * cr;
        tiltedPos.x = rx;
        tiltedPos.y = ry;
    }

    // Translate to world position + P8 burial offset (sink into ground).
    float3 worldPos = tiltedPos + instanceData.positionScale.xyz;
    worldPos.y += instanceData.extraTransform.w;  // burialY

    // Rotate normal and tangent by yaw.
    float3 rotatedNormal;
    rotatedNormal.x = input.normal.x * cy + input.normal.z * sy;
    rotatedNormal.y = input.normal.y;
    rotatedNormal.z = -input.normal.x * sy + input.normal.z * cy;

    float3 rotatedTangent;
    rotatedTangent.x = input.tangent.x * cy + input.tangent.z * sy;
    rotatedTangent.y = input.tangent.y;
    rotatedTangent.z = -input.tangent.x * sy + input.tangent.z * cy;

    // P8: Also rotate normal/tangent by tilt.
    if (abs(pitchX) > 0.001 || abs(rollZ) > 0.001) {
        float cp = cos(pitchX);
        float sp = sin(pitchX);
        float ny = rotatedNormal.y * cp - rotatedNormal.z * sp;
        float nz = rotatedNormal.y * sp + rotatedNormal.z * cp;
        rotatedNormal.y = ny;
        rotatedNormal.z = nz;
        float cr = cos(rollZ);
        float sr = sin(rollZ);
        float nx = rotatedNormal.x * cr - rotatedNormal.y * sr;
        ny = rotatedNormal.x * sr + rotatedNormal.y * cr;
        rotatedNormal.x = nx;
        rotatedNormal.y = ny;

        float ty = rotatedTangent.y * cp - rotatedTangent.z * sp;
        float tz = rotatedTangent.y * sp + rotatedTangent.z * cp;
        rotatedTangent.y = ty;
        rotatedTangent.z = tz;
        float tx = rotatedTangent.x * cr - rotatedTangent.y * sr;
        ty = rotatedTangent.x * sr + rotatedTangent.y * cr;
        rotatedTangent.x = tx;
        rotatedTangent.y = ty;
    }

    // P7: Apply full wind field deformation.
    float3 deformedPos = applyWindDeformation(
        worldPos, input.position, input.windWeight,
        // yawPhaseIdLod = { yaw, phase, stableId, lodClass }.  Do not pass
        // `.yz`: that pairs phase with stableId, which made almost every
        // grass instance appear as LOD >= 3 and hit the frozen early-out.
        float2(instanceData.yawPhaseIdLod.y, instanceData.yawPhaseIdLod.w),
        instanceData.positionScale.xyz);

    output.worldPosition = deformedPos;
    output.normal = normalize(rotatedNormal);
    output.uv = input.uv;
    output.tangent = float4(rotatedTangent, input.tangent.w);
    output.position = mul(gScene.viewProjection, float4(deformedPos, 1.0));
    // TAA: previous-frame clip position for the velocity MRT.
    output.prevClipPosition = mul(gPrevCamera.prevViewProjection, float4(deformedPos, 1.0));
    output.curClipPosition = output.position;
    output.shadowCoord = mul(gScene.lightViewProjection, float4(deformedPos, 1.0));
    output.contactShadow = 1.0 - smoothstep(0.0, kContactShadowHeight, deformedPos.y);
    // P8: Terrain contact Y = instance's terrain height (pre-burial). Used by
    // the fragment shader to darken the rock base where it meets the ground.
    output.terrainContactY = instanceData.positionScale.y;
    // P0 (Track C): Forward packed instance metadata to the fragment stage.
    // This read is also what prevents the Slang compiler from optimizing
    // away the speciesLodState vertex input (location 8), which would
    // otherwise desync the C++ VertexBufferLayoutDesc from the SPV input
    // list and trip Vulkan validation. The fragment stage consumes these
    // in P1 (species tint) / P3 (SSS thickness) / P8 (state shading).
    output.instanceMetadata = decodeInstanceMetadata(instanceData.speciesLodState);
    return output;
}

// --- BRDF Functions ---

#include "renderer/pbr_brdf.slang"

// Double-sided lighting: the PBR pipeline renders procedural meshes (grass
// crossed-quads, cones, thin billboards) with CullMode::None so both faces of
// every triangle are drawn. When the rasterizer reports a back face
// (SV_IsFrontFace == false), flip the geometry normal AND the tangent's
// bitangent sign. Flipping only the normal would invert the TBN basis
// handedness, making normal-map detail mirror incorrectly; flipping the
// bitangent sign too keeps the TBN orthonormal and right-handed on the back
// face. This replaces the old per-generator "emit opposite winding + flip
// normals in geometry" hack (which was fragile: appendCone forgot to flip
// normals, so its interior rendered black).
float3 normalFromTexture(FragmentInput input)
{
    float3 geometryNormal = normalize(input.normal);
    float3 tangent = normalize(input.tangent.xyz);
    float bitangentSign = input.tangent.w;
    // pbrExtraParams.z is the per-submesh single-sided-lighting flag. The
    // shared pipeline remains CullMode::None for foliage, while terrain
    // overlays preserve their authored upward normal even if raster winding
    // varies along a procedural strip.
    if (!input.frontFacing && gScene.pbrExtraParams.z < 0.5) {
        geometryNormal = -geometryNormal;
        bitangentSign = -bitangentSign;
    }
    float3 bitangent = normalize(cross(geometryNormal, tangent) * bitangentSign);
    float3 sampledNormal = NormalTexture.Sample(LinearSampler, input.uv).xyz * 2.0 - 1.0;
    return normalize(
        tangent * sampledNormal.x
        + bitangent * sampledNormal.y
        + geometryNormal * sampledNormal.z);
}

float sampleShadow(float4 shadowCoord)
{
    if (gScene.shadowParams.x < 0.5) {
        return 1.0; // shadow disabled
    }
    if (shadowCoord.w <= 0.0) {
        return 1.0;
    }
    float3 projected = shadowCoord.xyz / shadowCoord.w;
    // Reject receivers outside the light depth range before sampling.  A
    // saturated out-of-range depth aliases to the shadow-map border and turns
    // otherwise lit terrain/foliage into large black regions.
    if (projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }
    float2 uv = projected.xy * float2(0.5, 0.5) + float2(0.5, 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    float receiver = projected.z;
    // Keep receiver bias comfortably below the character's actual light-space
    // depth span, otherwise the compare lifts the ground receiver in front of
    // the caster depths and the shadow disappears.
    // Phase 3: bias raised from 0.0005 to 0.0015 to compensate for larger PCF
    // radius (12 texels). Larger PCF kernels sample more aggressively, and a
    // higher bias reduces shadow acne / surface shimmer at the cost of slight
    // Peter Panning. Reference: UE5 default slope bias ~0.01-0.03 world units,
    // Unity normal bias ~0.005-0.02. Our 0.0015 is conservative in light-space
    // depth (which is ~500m range from near to far plane).
    float bias = 0.0015;
    float pcfRadius = max(gScene.shadowParams.y, 1.0);
    float shadowStrength = saturate(gScene.shadowParams.z);
    float shadowedVisibility = 1.0 - shadowStrength;
    // Texel size = 1.0 / shadowMapResolution (passed via shadowParams.w).
    // This replaces the hardcoded 1.0/2048.0 so the shader adapts to any
    // shadow map resolution without recompilation.
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

// Phase 3 (Poisson disk PCF): 16-tap rotated Poisson disk for soft shadows.
// The disk radius is normalized to [-1,1]; the receiver scales it by pcfRadius
// (in texels) and rotates it by a per-pixel random angle to break up the
// repeating-sample pattern that fixed Poisson disks show on long shadows.
// Sample layout follows the canonical OpenGL shadow-mapping convention (16
// vectors roughly distributed by a Vogel/Halton-like spiral). Do NOT reorder
// these - the contract test in shader_tests.cpp asserts the array exists with
// exactly 16 entries and the shader relies on [unroll] having a fixed count.
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

// Per-pixel hash -> [0,1). Used to derive a rotation angle in [0,2*pi) so the
// 16-sample Poisson disk is rotated differently for each pixel,打散 the static
// sample pattern into a stochastic field. Implementation is the canonical
// Inigo Quilez hash12 (xor + frac, no sin/cos); cheap enough for a per-pixel
// call in the fragment shader.
float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// Phase 3 Poisson disk PCF helper. Samples the shadow map at 16 rotated disk
// positions around the receiver UV and averages the (receiver > depth) test.
// pcfRadius is in texels of THIS cascade's strip; texelSize is 1/stripHeight.
float samplePoissonDiskPCF(float2 uv, float receiver, float bias,
                           float texelSize, float pcfRadius,
                           float shadowedVisibility, float2 pixelCoord)
{
    // Per-pixel rotation angle in [0, 2*pi). The seed is the framebuffer
    // pixel coordinate (input.position.xy), which is stable across frames
    // for static geometry - this avoids temporal flicker while still
    // decorrelating neighboring pixels.
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

// Phase 1.4 (CSM): cascade-aware shadow sampling. The receiver picks one of 3
// cascades by view depth (computed from the camera-to-fragment distance), then
// projects the fragment with that cascade's VP and rescales the resulting UV
// into the cascade's atlas sub-region (atlasUvTransforms). Until Phase 1.5
// partitions the atlas, all 3 cascades share the full UV range [0,1] - the
// per-cascade UV transform is identity, so each cascade samples the same
// shadow map. Phase 1.5 will partition the 4096px atlas into 3 sub-regions
// and set the UV transforms accordingly.
//
// This function is the receiver-side pair of shadow_depth.slang's
// buildShadowVertex: both read gCsmShadow.lightViewProjection[] (or the
// push-constant equivalent for the caster) with the DEFAULT float4x4 layout,
// so caster and receiver project into the same clip space. See AGENTS.md
// "Matrix Layout" §2-3.
//
// Phase 3: when pcfRadius >= 0.5, sampleShadowCsm now uses a 16-tap rotated
// Poisson disk (samplePoissonDiskPCF) instead of the old square PCF kernel.
// pcfRadius is the disk radius in texels of the selected cascade's strip.
float sampleShadowCsm(float3 worldPos, float viewDepth, float2 pixelCoord)
{
    if (gCsmShadow.shadowParams.x < 0.5) {
        return 1.0; // shadow disabled
    }
    // Select cascade by view depth. cascadeSplits.y = near/mid boundary,
    // .z = mid/far boundary, .w = far plane (cascade 2 covers everything
    // beyond .z). A fragment past .w is outside the shadow frustum.
    uint cascadeIndex = viewDepth < gCsmShadow.cascadeSplits.y ? 0u
                     : viewDepth < gCsmShadow.cascadeSplits.z ? 1u : 2u;
    if (viewDepth > gCsmShadow.cascadeSplits.w) {
        return 1.0; // beyond the farthest cascade - no shadow
    }

    // Project the fragment into the selected cascade's light clip space.
    float4 shadowCoord = mul(gCsmShadow.lightViewProjection[cascadeIndex],
                             float4(worldPos, 1.0));
    if (shadowCoord.w <= 0.0) {
        return 1.0;
    }
    float3 projected = shadowCoord.xyz / shadowCoord.w;
    // Reject receivers outside the light depth range before sampling. A
    // saturated out-of-range depth aliases to the shadow-map border and turns
    // otherwise lit terrain/foliage into large black regions.
    if (projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }
    // NDC -> UV uses the backend-corrected RHI projection, then rescales into
    // the cascade's atlas sub-region.
    float2 baseUv = projected.xy * float2(0.5, 0.5) + float2(0.5, 0.5);
    float2 uvScale = gCsmShadow.atlasUvTransforms[cascadeIndex].xy;
    float2 uvOffset = gCsmShadow.atlasUvTransforms[cascadeIndex].zw;
    float2 uv = baseUv * uvScale + uvOffset;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }
    float receiver = projected.z;
    // Phase 3: bias raised from 0.0005 to 0.0015 (see sampleShadow for rationale).
    float bias = 0.0015;
    // pcfRadius is per-cascade in principle; for now we use the shared
    // shadowParams.y. Phase 3 semantics: < 0.5 = single-tap fast path,
    // >= 0.5 = Poisson disk radius in texels (16-tap + per-pixel rotation).
    float pcfRadius = max(gCsmShadow.shadowParams.y, 0.0);
    float shadowStrength = saturate(gCsmShadow.shadowParams.z);
    float shadowedVisibility = 1.0 - shadowStrength;
    // Phase 1.5b: per-cascade texel size. The atlas is 4096x4096 but each
    // cascade covers a horizontal strip of varying height (2048/1024/1024).
    // uvScale.y = stripHeight / 4096, so stripHeight = uvScale.y * 4096,
    // and texelSize = 1.0 / stripHeight = 1.0 / (uvScale.y * shadowParams.w).
    // The Poisson disk uses the cascade's own texel size so a 1-texel offset
    // is one texel of THIS cascade's strip, not one texel of the full atlas.
    float stripHeight = max(gCsmShadow.atlasUvTransforms[cascadeIndex].y
                            * gCsmShadow.shadowParams.w, 1.0);
    float texelSize = 1.0 / stripHeight;
    if (pcfRadius < 0.5) {
        // Single-tap path: 1 sample at the receiver UV.
        float shadowDepth = ShadowMap.SampleLevel(ShadowSampler, uv, 0.0).r;
        return (receiver - bias) > shadowDepth ? shadowedVisibility : 1.0;
    }
    // Phase 3: 16-tap Poisson disk + per-pixel rotation. Replaces the old
    // square (2N+1)^2 kernel which was dormant (pcfRadius was 0.0 in C++)
    // and produced visible axis-aligned banding when enabled.
    return samplePoissonDiskPCF(uv, receiver, bias, texelSize, pcfRadius,
                                shadowedVisibility, pixelCoord);
}

[shader("fragment")]
PbrSceneOutput fragmentMain(FragmentInput input)
{
    float4 sampledBase = BaseColorTexture.Sample(LinearSampler, input.uv);
    float3 baseColor = sampledBase.rgb * gScene.baseColorAlpha.rgb;
    float alpha = sampledBase.a * gScene.baseColorAlpha.a;
    float alphaMode = gScene.pbrParams.w;
    if (alphaMode > 0.5 && alphaMode < 1.5 && alpha < gScene.pbrParams.z) {
        discard;
    }

    // P8: Rock-terrain blend. Darken the base of rocks where they meet the
    // ground, creating a natural wet/dark contact gradient. Only active when
    // terrainContactY >= 0 (instanced rocks set it; non-instanced = -1).
    if (input.terrainContactY >= 0.0) {
        float heightAboveGround = input.worldPosition.y - input.terrainContactY;
        float groundBlend = 1.0 - smoothstep(0.0, 0.4, heightAboveGround);
        baseColor *= 1.0 - groundBlend * 0.35;
    }

    float4 mr = MetallicRoughnessTexture.Sample(LinearSampler, input.uv);
    float metallic = saturate(mr.b * gScene.pbrParams.x);
    float roughness = clamp(mr.g * gScene.pbrParams.y, 0.045, 1.0);
    float occlusion = lerp(1.0, OcclusionTexture.Sample(LinearSampler, input.uv).r, saturate(gScene.pbrExtraParams.x));
    float3 emissive = EmissiveTexture.Sample(LinearSampler, input.uv).rgb * gScene.pbrExtraParams.y;

    // P9: Weather response. Wetness darkens base color and smooths roughness
    // (rain wets surfaces, making them darker and more reflective).
    {
        float wetness = gWeather.params.y;
        baseColor *= lerp(1.0, 0.75, wetness * 0.5);
        roughness *= lerp(1.0, 0.55, wetness);
        roughness = clamp(roughness, 0.045, 1.0);
    }

    float3 cameraToFragment = gScene.cameraPositionExposure.xyz - input.worldPosition;
    float3 viewDirection = normalize(cameraToFragment);
    // Phase 1.4 (CSM): view depth = camera-to-fragment distance (meters),
    // used by sampleShadowCsm to pick the cascade. cascadeSplits are in the
    // same unit (meters) as the CsmShadowConfig farDepth values.
    float viewDepth = length(cameraToFragment);
    float3 normal = normalFromTexture(input);

    // RenderDoc road-material diagnostics. These modes are enabled only for
    // selected submeshes by the application and deliberately bypass lighting
    // so a black road can be attributed to texture sampling versus normals.
    if (gScene.pbrExtraParams.y > 110.5 && gScene.pbrExtraParams.y < 111.5) {
        return taaSceneOutput(float4(baseColor, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 111.5 && gScene.pbrExtraParams.y < 112.5) {
        return taaSceneOutput(float4(normalize(input.normal) * 0.5 + 0.5, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 112.5 && gScene.pbrExtraParams.y < 113.5) {
        return taaSceneOutput(float4(normal * 0.5 + 0.5, 1.0), input);
    }

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

    // WOCC_SHADOW_DEBUG routes this sentinel through pbrExtraParams.y. Display
    // the raw PCF visibility before material, direct light, AO, or post-light
    // composition can obscure the actual shadow-map footprint.
    if (gScene.pbrExtraParams.y > 103.5) {
        // Phase 3 debug: visualize the per-pixel rotation angle (R) and the
        // Poisson disk visibility (G). Useful for confirming the rotation
        // hash is decorrelating neighboring pixels (R should look like static,
        // not a smooth gradient) and that the 16-tap disk is sampling the
        // expected region (G should match the > 102 grayscale mode but with
        // softer edges).
        float3 cameraToFragmentDbg = gScene.cameraPositionExposure.xyz - input.worldPosition;
        float viewDepthDbg = length(cameraToFragmentDbg);
        float visibility = sampleShadowCsm(input.worldPosition, viewDepthDbg,
                                           input.position.xy);
        float angle = hash12(input.position.xy);
        return taaSceneOutput(float4(angle, visibility, 0.0, 1.0), input);
    }
    if (gScene.pbrExtraParams.y > 102.0) {
        // Phase 1.7 CSM debug: visualize sampleShadowCsm result (the actual
        // receiver-side path used in production). 1.0 = lit (white), 0.0 =
        // fully shadowed (black). Lets us see exactly which pixels the CSM
        // path darkens, independent of material/lighting.
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

    // Contact-shadow based ambient occlusion (matching the stylized reference
    // directionalShadow in stylized_scene.wgsl). Darkens vertices near the
    // ground and downward-facing surfaces in a unified factor.
    float upness = saturate(normal.y * 0.5 + 0.5);
    float ambientShadow = 1.0 - input.contactShadow * kContactShadowStrength *
        (kContactShadowBase + upness * kContactShadowNormalScale);
    float3 reflectionDirection = reflect(-viewDirection, normal);
    float nDotV = saturate(dot(normal, viewDirection));
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 fresnel = fresnelSchlickRoughness(nDotV, f0, roughness);
    float3 kD = (float3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic);
    float primaryNDotL = 0.0;
    uint primaryLightCount = (uint)gLights.countAndPadding.x;
    if (primaryLightCount > 0u) {
        WoccGpuLight primaryLight = gLights.lights[0u];
        if ((uint)primaryLight.positionType.w == 0u) {
            primaryNDotL = saturate(dot(normal, normalize(primaryLight.directionRange.xyz)));
        }
    }
    float directionalAmbient = kDirectionalAmbientBacklight
        + kDirectionalAmbientFrontScale * sqrt(primaryNDotL);

    // Ambient is provided entirely by DDGI (below) — the IBL cubemap ambient
    // path was removed. Start from zero and let the DDGI term fill it in.
    // SceneLighting::ambientFactor is packed in lightColor.w. It is the
    // deterministic base environment term for every material; DDGI below is
    // additive indirect lighting, not a replacement for it.
    float3 ambientColor = baseColor * gScene.lightColor.rgb
        * saturate(gScene.lightColor.w) * ambientShadow * occlusion
        * directionalAmbient;

    // --- Dynamic DDGI contribution -----------------------------------------
    // Sample the previous frame's half-res GI irradiance at this pixel's
    // screen position. The DDGI resolve pass reconstructs world position from
    // depth and trilinear-blends the 8 surrounding probes' octahedral
    // irradiance maps, producing a smooth diffuse GI field that captures
    // terrain bounce light, sky occlusion, and color bleeding. GI parameters
    // are packed into environmentParams.yzw to keep the push-constant block
    // at 256 bytes (Vulkan maxPushConstantsSize limit): y = strength (0 =
    // disabled), z = 1/fullRenderWidth, w = 1/fullRenderHeight. The 1-frame
    // lag is imperceptible for diffuse GI (which changes slowly). Screen UV
    // is derived from SV_POSITION using the full-resolution render extent.
    // Texture sampling uses normalized UV regardless of the GI texture's half
    // resolution.
    //
    // The indirect term is split into diffuse and specular components:
    //   - indirectDiffuse  = giIrradiance * baseColor * kD
    //   - indirectSpecular = giIrradiance * fresnel * roughnessScale
    // For metals kD≈0, so indirectDiffuse vanishes — the indirectSpecular
    // term (where f0==baseColor for metals, making it baseColor-tinted) is
    // what rescues metallic surfaces (armor) from near-black on the shadowed
    // /back side. roughnessScale widens the proxy lobe for rougher metals so
    // they pick up more bounce light. ambientShadow (contact-shadow AO) and
    // material occlusion are applied to the full indirect term — previously
    // both were computed but never wired into the ambient contribution.
    if (gScene.environmentParams.y > 0.5) {
        float2 screenUv = input.position.xy * gScene.environmentParams.zw;
        float3 giIrradiance = GiIrradiance.Sample(GiSampler, screenUv).rgb;
        float giStrength = gScene.environmentParams.y;

        if (gScene.environmentParams.x > 0.5) {
            return taaSceneOutput(float4(giIrradiance * giStrength, 1.0), input);
        }

        float3 indirectDiffuse = giIrradiance * baseColor * kD;

        float roughnessScale = lerp(0.5, 1.0, roughness);
        float3 indirectSpecular = giIrradiance * fresnel * roughnessScale;

        ambientColor += (indirectDiffuse + indirectSpecular)
                      * ambientShadow * occlusion * giStrength
                      * directionalAmbient;
    }

    // --- Multi-light direct contribution ----------------------------------
    // Loop over the active lights in the UBO. Directional lights use a
    // constant direction with no attenuation; Point/Spot use distance-based
    // inverse-decay attenuation with an optional range cutoff, and Spot adds
    // a cone soft-edge factor. Only Directional lights with castShadow>0.5
    // sample the existing single shadow map (Phase 1 shadow policy).
    float3 directLight = float3(0.0, 0.0, 0.0);
    uint activeCount = (uint)gLights.countAndPadding.x;
    for (uint i = 0u; i < activeCount; ++i) {
        WoccGpuLight L = gLights.lights[i];
        uint ltype = (uint)L.positionType.w;
        float3 toLight;
        float atten = 1.0;
        if (ltype == 0u) {
            // Directional: direction is already surface-to-light.
            toLight = normalize(L.directionRange.xyz);
        } else {
            // Point or Spot: direction and attenuation from world position.
            float3 d = L.positionType.xyz - input.worldPosition;
            float dist = length(d);
            toLight = d / max(dist, 0.001);
            // Range cutoff (0 range = infinite).
            float rangeAtten = (L.directionRange.w > 0.0)
                ? saturate(1.0 - dist / L.directionRange.w)
                : 1.0;
            // Inverse-decay: 1 / dist^decay (decay=2 → inverse-square).
            float decay = L.coneDecayShadow.z;
            float distAtten = 1.0 / pow(max(dist, 0.001), decay);
            atten = rangeAtten * distAtten;
            if (ltype == 2u) {
                // Spot cone: soft edge between inner and outer half-angles.
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
        float3 diff = (1.0 - lightFresnel) * (1.0 - metallic) * baseColor;

        // Shadow: only the castShadow directional light samples the shadow map.
        // Phase 1.4 (CSM): sample via the cascade-aware path - the receiver
        // picks the cascade by view depth and rescales UVs into the cascade's
        // atlas sub-region. The legacy sampleShadow(input.shadowCoord) path is
        // retained for non-CSM fallback but not used when CSM is active
        // (gCsmShadow.shadowParams.x > 0.5).
        float lightShadow = 1.0;
        if (ltype == 0u && L.coneDecayShadow.w > 0.5) {
            if (gCsmShadow.shadowParams.x > 0.5) {
                lightShadow = sampleShadowCsm(input.worldPosition, viewDepth,
                                              input.position.xy);
            } else {
                lightShadow = sampleShadow(input.shadowCoord);
            }
        }

        directLight += (diff + specular) * nDotL * atten
            * L.colorIntensity.w * L.colorIntensity.rgb * lightShadow;
    }

    // Compose final color. The scene shader outputs HDR linear color: exposure
    // and ACES tone mapping are applied centrally by the post-process chain
    // (shaders/wocc/post.slang toneMappingFragmentMain) so the bloom pass sees
    // true HDR values. cameraPositionExposure.w (exposure) is consumed by the
    // post tonemap pass, not here.
    float3 color = ambientColor + directLight + emissive;

    return taaSceneOutput(float4(color, alpha), input);
}

// ───────────────────────────────────────────────────────────────────────────
// Lightweight foliage fragment shader for instanced vegetation.
//
// The full fragmentMain runs 4 texture samples + DDGI irradiance lookup + a
// multi-light GGX loop on every fragment. For alpha-tested crossed-quad grass
// cards rendered double-sided with heavy overdraw, that fragment cost dominates
// the frame (measured ~350 ms GPU for ~3000 instances). This entrypoint keeps
// the same VertexOutput/FragmentInput and descriptor layout (so it shares
// pipelineLayout_ and the same vertex shader) but cuts the fragment work to:
//   - 1 albedo texture sample + alpha-test discard
//   - single ambient term from the light UBO environment factor
//   - single directional lambert term (first active light, no specular)
//   - contact-shadow AO
// No DDGI, no multi-light loop, no MR/Occlusion/Emissive textures, no GGX.
// Trees/rocks also route through here for the perf win; their normal comes
// from the vertex normal (no normal-map sample) which is acceptable for the
// instanced template meshes.
// ───────────────────────────────────────────────────────────────────────────
[shader("fragment")]
PbrSceneOutput fragmentMainFoliage(FragmentInput input)
{
    float4 sampledBase = BaseColorTexture.Sample(LinearSampler, input.uv);
    float3 baseColor = sampledBase.rgb * gScene.baseColorAlpha.rgb;
    float alpha = sampledBase.a * gScene.baseColorAlpha.a;
    float alphaMode = gScene.pbrParams.w;
    if (alphaMode > 0.5 && alphaMode < 1.5 && alpha < gScene.pbrParams.z) {
        discard;
    }

    // Rock-terrain contact darkening (same as full path; cheap).
    if (input.terrainContactY >= 0.0) {
        float heightAboveGround = input.worldPosition.y - input.terrainContactY;
        float groundBlend = 1.0 - smoothstep(0.0, 0.4, heightAboveGround);
        baseColor *= 1.0 - groundBlend * 0.35;
    }

    // Vertex normal is good enough for foliage lighting (no normal-map sample).
    // Back faces (grass cards rendered CullMode::None) flip the normal so both
    // sides shade consistently.
    float3 normal = normalize(input.normal);
    if (!input.frontFacing) {
        normal = -normal;
    }
    // Phase 1.4 (CSM): view depth for cascade selection.
    float viewDepth = length(gScene.cameraPositionExposure.xyz - input.worldPosition);

    // Contact-shadow ambient occlusion (cheap, same formula as full path).
    float upness = saturate(normal.y * 0.5 + 0.5);
    float ambientShadow = 1.0 - input.contactShadow * kContactShadowStrength *
        (kContactShadowBase + upness * kContactShadowNormalScale);

    // Ambient term from the light UBO environment factor (no DDGI).
    float3 ambientColor = baseColor * gScene.lightColor.rgb
        * saturate(gScene.lightColor.w) * ambientShadow;

    // Single directional light lambert (first active light only). No specular,
    // no point/spot lights, no attenuation. Foliage does not need GGX.
    float3 directLight = float3(0.0, 0.0, 0.0);
    uint activeCount = (uint)gLights.countAndPadding.x;
    if (activeCount > 0u) {
        WoccGpuLight L = gLights.lights[0u];
        uint ltype = (uint)L.positionType.w;
        if (ltype == 0u) {
            // Directional light: constant direction, optional shadow.
            float3 toLight = normalize(L.directionRange.xyz);
            float nDotL = saturate(dot(normal, toLight));
            float lightShadow = 1.0;
            if (L.coneDecayShadow.w > 0.5) {
                // Phase 1.4 (CSM): use the cascade-aware path when active.
                if (gCsmShadow.shadowParams.x > 0.5) {
                    lightShadow = sampleShadowCsm(input.worldPosition, viewDepth,
                                                  input.position.xy);
                } else {
                    lightShadow = sampleShadow(input.shadowCoord);
                }
            }
            directLight = baseColor * nDotL
                * L.colorIntensity.w * L.colorIntensity.rgb * lightShadow;
        }
    }

    float3 color = ambientColor + directLight;
    return taaSceneOutput(float4(color, alpha), input);
}
