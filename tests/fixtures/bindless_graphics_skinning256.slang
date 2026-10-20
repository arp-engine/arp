// Mirrors renderer::GpuSkinningPalette and WOCC's temporal palette declaration.
static const uint kMaxGpuSkinningJoints = 256;
struct SkinningPalette
{
    float4x4 skinMatrices[kMaxGpuSkinningJoints];
    float4 worldTranslationAndScale;
    float4 worldRotation;
    uint jointCount;
    uint pad0;
    uint pad1;
    uint pad2;
};
struct TemporalPalette
{
    SkinningPalette current;
    SkinningPalette previous;
};
[[vk::binding(0, 0)]] ConstantBuffer<TemporalPalette> Palettes;
struct InfluenceData
{
    float4 joints;
    float4 weights;
};
[[vk::binding(1, 0)]] ConstantBuffer<InfluenceData> Influence;

// Same current/previous index validation, weight normalization, row-memory
// multiplication and object transform as WOCC's temporal skinned vertex path.
ArpSkinningBlend blendGeometry(float3 position)
{
    ArpSkinningBlend blend = arpEmptySkinningBlend();
    [unroll]
    for (uint lane = 0; lane < 4; ++lane) {
        float weight = Influence.weights[lane];
        uint joint = min((uint)max(Influence.joints[lane], 0.0),
            Palettes.current.jointCount > 0 ? Palettes.current.jointCount - 1 : 0);
        if (weight > 0.0 && joint < kMaxGpuSkinningJoints) {
            uint previousJoint = min((uint)max(Influence.joints[lane], 0.0),
                Palettes.previous.jointCount > 0 ? Palettes.previous.jointCount - 1 : 0);
            bool previousValid = previousJoint < kMaxGpuSkinningJoints;
            float4x4 previous = Palettes.previous.skinMatrices[previousValid ? previousJoint : 0u];
            arpAccumulateSkinning(blend, Palettes.current.skinMatrices[joint], previous,
                previousValid, weight, position, float3(2.0, 3.0, 5.0), float3(-3.0, 2.0, 1.0));
        }
    }
    return blend;
}

float4 skinPosition(float3 position, bool previous)
{
    ArpSkinningBlend blend = blendGeometry(position);
    float4 blended = previous ? blend.previousPosition : blend.currentPosition;
    float4 translationScale = previous ? Palettes.previous.worldTranslationAndScale : Palettes.current.worldTranslationAndScale;
    float4 rotation = previous ? Palettes.previous.worldRotation : Palettes.current.worldRotation;
    return float4(arpSkinningWorldPosition(blended, blend.weightSum, translationScale, rotation), blended.w);
}

struct SkinningVertex
{
    float4 position : SV_Position;
    nointerpolation float4 transformed : TEXCOORD0;
    nointerpolation float3 normal : TEXCOORD1;
    nointerpolation float4 tangent : TEXCOORD2;
};
[shader("vertex")]
SkinningVertex vertexMain(uint vertexId : SV_VertexID)
{
    SkinningVertex result;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    result.position = float4(position, 0.0, 1.0);
    result.transformed = skinPosition(float3(2.0, 3.0, 5.0), false);
    ArpSkinningBlend blend = blendGeometry(float3(2.0, 3.0, 5.0));
    result.normal = arpSkinningWorldNormal(blend, Palettes.current.worldRotation);
    result.tangent = arpSkinningWorldTangent(blend, Palettes.current.worldRotation, -1.0);
    return result;
}

float4 skinningFragment(SkinningVertex input, bool injectFault)
{
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    float4 transformed = skinPosition(float3(float(pixel + 1u), 3.0, 5.0), true);
    bool fault;
    for (uint lane = 0u; lane < 4u; ++lane) {
        Results_store(0u, pixel * 16u + lane, asuint(input.transformed[lane]), fault);
        Results_store(0u, pixel * 16u + 4u + lane, asuint(transformed[lane]), fault);
        Results_store(0u, pixel * 16u + 8u + lane, asuint(float4(input.normal, 0.0)[lane]), fault);
        Results_store(0u, pixel * 16u + 12u + lane, asuint(input.tangent[lane]), fault);
    }
    if (injectFault) {
        Results_store(1u, 0u, 0xffffffffu, fault);
        Results_store(0u, 64u, 0xffffffffu, fault);
    }
    return float4(0.25, 0.5, 0.75, 1.0);
}
[shader("fragment")]
float4 cleanFragment(SkinningVertex input) : SV_Target0
{
    return skinningFragment(input, false);
}
[shader("fragment")]
float4 faultFragment(SkinningVertex input) : SV_Target0
{
    return skinningFragment(input, true);
}
