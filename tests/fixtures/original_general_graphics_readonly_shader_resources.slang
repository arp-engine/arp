// FiniteGeneralGraphicsOutput/1: four selected uint32 words converted to float4
// plus the uploaded sampled pixel. Storage and attachment outputs are separate.
[[vk::binding(0, 0)]] StructuredBuffer<uint> ReadOnlyInput;
[[vk::binding(1, 0)]] RWTexture2D<float4> StorageOutput;
[[vk::binding(2, 0)]] Texture2D<float4> SampledInput;
[[vk::binding(3, 0)]] SamplerState SamplerInput;

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 p = vertex == 0 ? float2(-1, -1) : vertex == 1 ? float2(3, -1) : float2(-1, 3);
    return float4(p, 0, 1);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target0
{
    uint2 pixel = uint2(position.xy);
    uint4 selected = uint4(ReadOnlyInput[0], ReadOnlyInput[1], ReadOnlyInput[2], ReadOnlyInput[3]);
    float4 value = float4(selected) + SampledInput.SampleLevel(SamplerInput, float2(pixel) + 0.5, 0.0);
    StorageOutput[pixel] = value;
    return value;
}
