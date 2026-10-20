// Opaque blocks test ownership without introducing a second matrix layout.
struct SkyBytes
{
    float4 values[6];
};
struct CsmBytes
{
    float4 values[17];
};
[[vk::binding(0, 0)]] ConstantBuffer<SkyBytes> Sky;
[[vk::binding(1, 0)]] ConstantBuffer<CsmBytes> Csm;
[[vk::binding(2, 0)]] RWStructuredBuffer<float4> Output;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    for (uint i = 0; i < 6; ++i) Output[i] = Sky.values[i];
    for (uint i = 0; i < 17; ++i) Output[6 + i] = Csm.values[i];
}
