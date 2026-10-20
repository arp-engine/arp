struct GeneralUniform { float4 value; };
[[vk::binding(0, 0)]] ConstantBuffer<GeneralUniform> UniformInput;
[[vk::binding(1, 0)]] RWTexture2D<float4> StorageOutput;
[[vk::binding(2, 0)]] Texture2D<float4> SampledInput;
[[vk::binding(3, 0)]] SamplerState SamplerInput;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    StorageOutput[thread.xy] = UniformInput.value + SampledInput.SampleLevel(SamplerInput, float2(thread.xy) + 0.5, 0.0);
}
