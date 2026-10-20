// Test-only target minimum and bound-region element probe. This does not query
// runtime length: Slang's StructuredBuffer.GetDimensions has no Metal lowering.
struct LayoutElement
{
    float3 position;
    uint marker;
    float2 pairs[3];
};
[[vk::binding(0, 0)]] ConstantBuffer<uint> Scalar;
[[vk::binding(1, 0)]] StructuredBuffer<LayoutElement> Input;
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    Output[0] = Scalar + Input[0].marker;
}
