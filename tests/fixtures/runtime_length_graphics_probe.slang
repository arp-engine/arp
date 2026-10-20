// Test-only numeric observation: each UNORM8 channel stores one exact integer.
// Fullscreen triangle has CCW winding; all vertices carry the same flat facts.
[[vk::binding(0,0)]] StructuredBuffer<float3> Shared;
[[vk::binding(1,0)]] StructuredBuffer<uint> FragmentOnly;
struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation uint2 dimensions : TEXCOORD0;
};
[shader("vertex")]
VertexOutput vertexMain(uint vertex : SV_VertexID)
{
    VertexOutput result;
    float2 positions[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
    result.position = float4(positions[vertex],0,1);
    uint count,stride; Shared.GetDimensions(count,stride);
    result.dimensions = uint2(count,stride);
    return result;
}
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    uint count,stride,fragmentCount; Shared.GetDimensions(count,stride);
    FragmentOnly.GetDimensions(fragmentCount,stride);
    return float4(float(input.dimensions.x),float(input.dimensions.y),float(count),float(fragmentCount))/255.0;
}
