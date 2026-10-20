// FragmentStorageReadProof/1: exact uint32 element0 equality; green on match, red otherwise.
[[vk::binding(3, 0)]] StructuredBuffer<uint> Input;
[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p, 0, 1);
}
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    return select(Input[0] == 305419896u, float4(0, 1, 0, 1), float4(1, 0, 0, 1));
}
