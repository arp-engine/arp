// Authored element-zero access: native pointee sizeof(T) proves the full fetch.
struct SharedStorage
{
    float4 positionScaleAndBias;
    float4 color;
};
[[vk::binding(3, 0)]] StructuredBuffer<SharedStorage> Shared[2];
[[vk::binding(8, 0)]] StructuredBuffer<float4> Fragment[2];

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p * Shared[0][0].positionScaleAndBias.x + Shared[1][0].positionScaleAndBias.yz, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    return Shared[0][0].color + Shared[1][0].color + Fragment[0][0] + Fragment[1][0];
}
