struct QueenShadowPushConstants
{
    float4x4 lightViewProjection;
};
[[vk::push_constant]] QueenShadowPushConstants gQueenShadow;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(gQueenShadow.lightViewProjection, float4(input.position, 1.0));
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
}
