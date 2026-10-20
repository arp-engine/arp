// Test-only dynamic resource-array addressing with a separate scalar binding.
[[vk::binding(7, 0)]] Texture2D<float4> Inputs[3] : register(t7);
[[vk::binding(2, 0)]] Texture2D<float4> Scalar : register(t2);

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    uint element = uint(position.x) % 3;
    return float4(Inputs[element].Load(int3(0, 0, 0)).rgb, Scalar.Load(int3(0, 0, 0)).r);
}
