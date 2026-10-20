struct QueenScenePushConstants
{
    float4x4 viewProjection;
    float4x4 lightViewProjection;
    float4 shadowSettings;
};
[[vk::push_constant]] QueenScenePushConstants gQueenScene;

// Raw depth-channel sampling, matching the ordered WebGL1 depth receiver contract.
// WEBGL_depth_texture supplies normalized D24 values through sampler2D.r.
[[vk::binding(0, 0)]] Texture2D<float> gQueenShadowMap;
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
    float4 shadowSettings : TEXCOORD3;
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
    output.shadowSettings = gQueenScene.shadowSettings;
    return output;
}

#include "queen_surface.slang"
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return queenSurface(input.normal, input.worldPosition, input.lightPosition,
        input.color, input.shadowSettings.xy, input.shadowSettings.z, input.shadowSettings.w);
}
