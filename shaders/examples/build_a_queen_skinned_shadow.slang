static const uint kMaxQueenSkinningJoints = 256;

struct QueenSkinningPalette
{
    float4x4 skinMatrices[kMaxQueenSkinningJoints];
    float4 worldTranslationAndScale;
    float4 worldRotation;
    uint jointCount;
    uint pad0;
    uint pad1;
    uint pad2;
};
[[vk::binding(0, 0)]] ConstantBuffer<QueenSkinningPalette> gSkinning;

struct QueenSkinnedShadowPushConstants
{
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 modelTranslationScale;
    float4 modelOriginYaw;
};
[[vk::push_constant]] QueenSkinnedShadowPushConstants gQueen;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
    float4 joints : TEXCOORD0;
    float4 weights : TEXCOORD1;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    float4 skinnedPosition = float4(0.0, 0.0, 0.0, 0.0);
    float totalWeight = 0.0;
    [unroll]
    for (uint lane = 0; lane < 4; ++lane)
    {
        const float weight = input.weights[lane];
        const uint joint = (uint)max(input.joints[lane], 0.0);
        if (weight > 0.0 && joint < gSkinning.jointCount && joint < kMaxQueenSkinningJoints)
        {
            skinnedPosition += mul(gSkinning.skinMatrices[joint], float4(input.position, 1.0)) * weight;
            totalWeight += weight;
        }
    }
    const float inverseWeight = totalWeight > 0.000001 ? rcp(totalWeight) : 0.0;
    float3 localPosition = skinnedPosition.xyz * inverseWeight;
    localPosition = (localPosition - gQueen.modelOriginYaw.xyz) * gQueen.modelTranslationScale.w;
    const float sinYaw = sin(gQueen.modelOriginYaw.w);
    const float cosYaw = cos(gQueen.modelOriginYaw.w);
    const float3 rotatedPosition = float3(
        localPosition.x * cosYaw + localPosition.z * sinYaw,
        localPosition.y,
        -localPosition.x * sinYaw + localPosition.z * cosYaw);
    const float3 worldPosition = rotatedPosition + gQueen.modelTranslationScale.xyz;
    VertexOutput output;
    output.position = mul(gQueen.lightViewProjection, float4(worldPosition, 1.0));
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
}
