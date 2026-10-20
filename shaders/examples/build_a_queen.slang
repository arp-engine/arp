struct QueenScenePushConstants
{
    float4x4 viewProjection;
    float4x4 lightViewProjection;
};
[[vk::push_constant]] QueenScenePushConstants gQueenScene;

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(gQueenShadowMap);
[[vk::binding(1, 0)]] SamplerState gQueenShadowSampler;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float4 color : COLOR0;
    float3 worldPosition : TEXCOORD1;
    float4 lightPosition : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(gQueenScene.viewProjection, float4(input.position, 1.0));
    output.normal = input.normal;
    output.color = input.color;
    output.worldPosition = input.position;
    output.lightPosition = mul(gQueenScene.lightViewProjection, float4(input.position, 1.0));
    return output;
}

#include "queen_surface.slang"
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    uint width;
    uint height;
    gQueenShadowMap.GetDimensions(width, height);
    return queenSurface(input.normal, input.worldPosition, input.lightPosition,
        input.color, 1.0 / float2(width, height), 1.0, 0.0);
}
