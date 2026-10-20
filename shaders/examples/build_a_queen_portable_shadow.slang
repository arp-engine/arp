struct QueenShadowPushConstants
{
    float4x4 lightViewProjection;
};
[[vk::push_constant]] QueenShadowPushConstants gQueenShadow;

struct VertexInput
{
    float3 position : POSITION0;
};
struct VertexOutput
{
    float4 position : SV_Position;
    float depth : TEXCOORD0;
};
[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(gQueenShadow.lightViewProjection, float4(input.position, 1.0));
    output.depth = output.position.z / output.position.w;
    return output;
}
// The WebGL1 ordered framebuffer has a real RGBA8 color attachment. Publish
// its depth visualization while the D24S8 attachment stores the sampled depth.
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.depth, input.depth, input.depth, 1.0);
}
