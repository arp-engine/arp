[[vk::binding(0, 0)]] StructuredBuffer<uint> ReadOnlyInput;
[[vk::binding(1, 0)]] RWTexture2D<float4> StorageOutput;
[[vk::binding(2, 0)]] Texture2D<float4> SampledInput;
[[vk::binding(3, 0)]] SamplerState SamplerInput;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    uint4 selected = uint4(ReadOnlyInput[0], ReadOnlyInput[1], ReadOnlyInput[2], ReadOnlyInput[3]);
    StorageOutput[thread.xy] = float4(selected) + SampledInput.SampleLevel(SamplerInput, float2(thread.xy) + 0.5, 0.0);
}
