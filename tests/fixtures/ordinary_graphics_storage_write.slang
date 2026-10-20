// GraphicsStorageWrite/1: literal uint32 atomic element0; returned value unused.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> Output;

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    uint oldValue;
    InterlockedExchange(Output[0], 305419896u, oldValue);
    return float4(0.125, 0.25, 0.5, 1);
}
