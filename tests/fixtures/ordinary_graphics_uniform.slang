// Direct ordinary uniform ranges: shared VS/FS data plus a fragment-only input.
struct SharedUniform
{
    float4 positionScaleAndBias;
    float4 color;
};
struct FragmentUniform
{
    float4 colorOffset;
};
[[vk::binding(3, 0)]] ConstantBuffer<SharedUniform> Shared;
[[vk::binding(8, 0)]] ConstantBuffer<FragmentUniform> Fragment;

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p * Shared.positionScaleAndBias.x + Shared.positionScaleAndBias.yz, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    return Shared.color + Fragment.colorOffset;
}
