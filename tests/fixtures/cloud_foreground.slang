// CloudForeground/1 acceptance geometry. Clip-space triangle at z/w = 0.5;
// no edge passes through a sample center at the declared 32x24 extent.
[[vk::binding(0,0)]] ConstantBuffer<float4> ForegroundColor;

[shader("vertex")]
float4 vertexMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = id == 0 ? float2(-0.75, -0.75)
        : id == 1 ? float2(0.75, -0.75) : float2(0, 0.75);
    return float4(p, 0.5, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    return ForegroundColor;
}
