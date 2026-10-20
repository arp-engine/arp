// Explicit sampled-depth semantics, independent of resource names.
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0, 0)]] ARP_SAMPLED_DEPTH(Inputs)[3];

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
    float depth = Inputs[element].Load(int3(0, 0, 0));
    return float4(depth, depth, depth, 1);
}
