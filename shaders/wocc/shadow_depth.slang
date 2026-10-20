// ShadowDepthPushConstants - push constants for the shadow depth pass.
//
// Phase 1.3 (CSM): the push constant now carries all 3 cascade light VPs
// (192B) + depthBiasAndFlags (16B) + cascadeIndex (4B + 12B pad = 16B) =
// 224B, within Vulkan's 256B min guaranteed push-constant size. Each draw
// selects its cascade via gShadowDepth.cascadeIndex and writes into the
// corresponding atlas sub-region (viewport/scissor set by the pipeline).
//
// lightViewProjection[3] uses the DEFAULT float4x4 layout (Slang emits a
// SPIR-V RowMajor decoration per array element), which reads the row-major
// float4x4 produced by renderer::computeCsmLightViewProjections() (core::math
// stores m[row*4+col]) without transposition. The caster (this shader) and
// the PBR receiver (pbr_scene.slang WoccCsmShadow.lightViewProjection[3],
// also default) must share the EXACT same matrix convention: if one declared
// row_major (SPIR-V ColMajor) and the other default (SPIR-V RowMajor), they
// would project the same C++ data into TRANSPOSED clip spaces - the shadow
// map would be written at texel X but sampled at texel X^T, so receivers
// would show no shadow. Keeping both default makes caster and receiver
// bit-identical, matching the row-major M*v semantics validated by
// shadow_system_tests. (Note: the explanatory comment lives ABOVE the struct
// so the push-constant size reflection, which parses struct members off
// ';'-terminated statements, does not absorb the comment into the first
// field's type token.)
struct ShadowDepthPushConstants
{
    float4x4 lightViewProjection[3];
    float4 depthBiasAndFlags;
    uint cascadeIndex;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
ShadowDepthPushConstants gShadowDepth;

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
[[vk::binding(0, 0)]]
ConstantBuffer<WoccGpuSkinningPalette> gSkinning;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    // P0 (Track C): Forward packed instance metadata. The shadow pass does
    // not consume these, but declaring the output and reading the input is
    // what prevents the Slang compiler from optimizing away the
    // speciesLodState vertex input (location 8). Without this read, the SPV
    // input list would drop location 8 while the C++ VertexBufferLayoutDesc
    // (shared across all three instanced pipelines via makeInstanceDataLayout)
    // still declares it, tripping Vulkan validation. The rasterizer discards
    // unused varyings on a depth-only pipeline, so this is effectively free.
    nointerpolation uint4 instanceMetadata : TEXCOORD0;
};

float3 depthBiasedPosition(float3 position, float3 normal)
{
    return position + normal * gShadowDepth.depthBiasAndFlags.z;
}

VertexOutput buildShadowVertex(float3 position, float3 normal)
{
    VertexOutput output;
    // Phase 1.3 (CSM): select the cascade VP by gShadowDepth.cascadeIndex.
    // Each draw writes into one cascade's atlas sub-region; the pipeline
    // sets viewport/scissor to match. The index is clamped to [0,2] so a
    // stale/misconfigured PC never reads out of bounds.
    const uint cascadeIndex = min(gShadowDepth.cascadeIndex, 2u);
    output.position = mul(
        gShadowDepth.lightViewProjection[cascadeIndex],
        float4(depthBiasedPosition(position, normal), 1.0));
    // P0 (Track C): default metadata for non-instanced/skinned paths.
    // vertexMainInstanced overrides this after calling buildShadowVertex.
    output.instanceMetadata = uint4(0u, 0u, 0u, 0u);
    return output;
}

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    return buildShadowVertex(input.position, input.normal);
}

struct SkinningInfluenceInput
{
    float4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
};

float3 rotateByQuaternion(float3 value, float4 q)
{
    const float3 twiceCross = 2.0 * cross(q.xyz, value);
    return value + q.w * twiceCross + cross(q.xyz, twiceCross);
}

[shader("vertex")]
VertexOutput vertexMainSkinned(VertexInput input, SkinningInfluenceInput influence)
{
    float4 skinnedPosition = float4(0.0, 0.0, 0.0, 0.0);
    float3 skinnedNormal = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    [unroll]
    for (uint lane = 0; lane < 4; ++lane) {
        const float weight = influence.weights[lane];
        const uint joint = min((uint)max(influence.joints[lane], 0.0),
            gSkinning.jointCount > 0 ? gSkinning.jointCount - 1 : 0);
        if (weight > 0.0 && joint < kMaxGpuSkinningJoints) {
            const float4x4 skin = gSkinning.skinMatrices[joint];
            skinnedPosition += mul(skin, float4(input.position, 1.0)) * weight;
            skinnedNormal += mul((float3x3)skin, input.normal) * weight;
            weightSum += weight;
        }
    }
    const float invWeight = weightSum > 0.000001 ? rcp(weightSum) : 1.0;
    const float3 worldPosition = rotateByQuaternion(
        skinnedPosition.xyz * invWeight * gSkinning.worldTranslationAndScale.w,
        gSkinning.worldRotation) + gSkinning.worldTranslationAndScale.xyz;
    const float3 worldNormal = normalize(rotateByQuaternion(
        normalize(skinnedNormal), gSkinning.worldRotation));
    return buildShadowVertex(worldPosition, worldNormal);
}

// P7: Instanced shadow caster entry point. Applies the same instance
// transform as pbr_scene.slang::vertexMainInstanced (scale -> yaw -> tilt ->
// translate -> burial) so vegetation casts correctly-shaped shadows. Wind
// deformation is omitted (the wind UBO is not bound in the shadow pass);
// shadow casters use the rest pose, which is visually acceptable.
struct InstanceVertexInput
{
    float3 position   : POSITION0;
    float3 normal     : NORMAL0;
    float2 uv         : TEXCOORD0;
    float4 tangent    : TANGENT0;
    float3 windWeight : COLOR0;
};

struct InstanceDataInput
{
    float4 positionScale  : POSITION1;
    float4 yawPhaseIdLod  : POSITION2;
    float4 extraTransform : POSITION3;
    // P0 (Track C): packed metadata (species/age/geometryLod/shadowLod/state).
    // P6: shadowLod (bits 8-15 of word 1) is now consumed by vertexMainInstanced
    // to early-exit instances that should not cast shadows (shadowLod >= 2).
    float4 speciesLodState : POSITION4;
};

[shader("vertex")]
VertexOutput vertexMainInstanced(InstanceVertexInput input,
                                 InstanceDataInput instanceData)
{
    // P6: Per-instance shadow cull. Decode shadowLod from the packed metadata
    // (speciesLodState[1] bits 8-15, matching the InstanceDataGpu layout in
    // foliage_instance_batch.h). When shadowLod >= 2 (off), emit a degenerate
    // clip-space position so the rasterizer clips this vertex -- effectively
    // skipping the instance's shadow contribution without splitting the batch.
    // This must happen before any transform work to save the ALU cost.
    uint4 meta = asuint(instanceData.speciesLodState);
    uint shadowLod = (meta.y >> 8u) & 0xFFu;
    if (shadowLod >= 2u) {
        VertexOutput culled;
        culled.position = float4(2.0, 2.0, 2.0, 1.0);  // outside clip volume
        culled.instanceMetadata = uint4(0u, 0u, 0u, 0u);
        return culled;
    }

    float scale = instanceData.positionScale.w;
    float scaleY = instanceData.extraTransform.z > 0.0
        ? instanceData.extraTransform.z
        : scale;
    float3 scaledPos;
    scaledPos.x = input.position.x * scale;
    scaledPos.y = input.position.y * scaleY;
    scaledPos.z = input.position.z * scale;

    float yaw = instanceData.yawPhaseIdLod.x;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float3 yawedPos;
    yawedPos.x = scaledPos.x * cy + scaledPos.z * sy;
    yawedPos.y = scaledPos.y;
    yawedPos.z = -scaledPos.x * sy + scaledPos.z * cy;

    float pitchX = instanceData.extraTransform.x;
    float rollZ = instanceData.extraTransform.y;
    float3 tiltedPos = yawedPos;
    if (abs(pitchX) > 0.001 || abs(rollZ) > 0.001) {
        float cp = cos(pitchX);
        float sp = sin(pitchX);
        float py = tiltedPos.y * cp - tiltedPos.z * sp;
        float pz = tiltedPos.y * sp + tiltedPos.z * cp;
        tiltedPos.y = py;
        tiltedPos.z = pz;
        float cr = cos(rollZ);
        float sr = sin(rollZ);
        float rx = tiltedPos.x * cr - tiltedPos.y * sr;
        float ry = tiltedPos.x * sr + tiltedPos.y * cr;
        tiltedPos.x = rx;
        tiltedPos.y = ry;
    }

    float3 worldPos = tiltedPos + instanceData.positionScale.xyz;
    worldPos.y += instanceData.extraTransform.w;

    float3 rotatedNormal;
    rotatedNormal.x = input.normal.x * cy + input.normal.z * sy;
    rotatedNormal.y = input.normal.y;
    rotatedNormal.z = -input.normal.x * sy + input.normal.z * cy;

    VertexOutput output = buildShadowVertex(worldPos, rotatedNormal);
    // P0 (Track C): Forward packed metadata. P6: shadowLod is already consumed
    // above; the full metadata is forwarded for parity with pbr_scene.slang
    // (and to prevent Slang from optimizing away the location-8 vertex input).
    output.instanceMetadata = asuint(instanceData.speciesLodState);
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
}
