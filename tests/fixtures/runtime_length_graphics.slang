// Test-only both-stage and fragment-only runtime length inventory.
[[vk::binding(0,0)]] StructuredBuffer<float3> Shared;
[[vk::binding(1,0)]] StructuredBuffer<uint> FragmentOnly;
[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    uint count,stride; Shared.GetDimensions(count,stride);
    return float4(float(vertex),float(count),float(stride),1);
}
[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    uint count,stride,fragmentCount; Shared.GetDimensions(count,stride);
    FragmentOnly.GetDimensions(fragmentCount,stride);
    return float4(float(count),float(fragmentCount),float(stride),1);
}
