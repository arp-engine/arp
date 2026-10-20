// Representative GI declarations, not a WOCC scene or execution proof.
[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(0, 2)]] TextureCube<float4> Environment;
[[vk::binding(1, 2)]] Texture2D<float4> BrdfLut;
[[vk::binding(2, 2)]] SamplerState LinearSampler;
[[vk::binding(3, 2)]] DepthTexture2D ShadowMap;
[[vk::binding(4, 2)]] SamplerComparisonState ShadowSampler;
[[vk::binding(5, 2)]] Texture2DArray<float4> Irradiance;
[[vk::binding(6, 2)]] Texture3D<float4> SceneVolume;

float4 readGi(float2 uv)
{
    return FrameValues + Environment.SampleLevel(LinearSampler, float3(uv, 1.0), 0.0)
        + BrdfLut.SampleLevel(LinearSampler, uv, 0.0)
        + Irradiance.SampleLevel(LinearSampler, float3(uv, 0.0), 0.0)
        + SceneVolume.SampleLevel(LinearSampler, float3(uv, 0.5), 0.0)
        + ShadowMap.SampleCmpLevelZero(ShadowSampler, uv, 0.5);
}

[shader("compute")][numthreads(1,1,1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    bool fault;
    IndexedValues_store(tid.x & 1u, 0u, asuint(readGi(float2(0.25, 0.75)).x), fault);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    bool fault;
    uint value = IndexedValues_load(uint(position.x) & 1u, 0u, fault);
    return readGi(position.xy) + float4(float(value), float(fault), 0.0, 0.0);
}

// The graphics query must distinguish the selected stage's active resources.
// Only the Cube and its sampler are active here; the other images stay inactive.
[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 position = float2(float((vertex << 1u) & 2u), float(vertex & 2u));
    float environment = Environment.SampleLevel(LinearSampler, float3(1.0, 0.0, 0.0), 0.0).x;
    return float4(position * 2.0 - 1.0, FrameValues.w + environment, 1.0);
}
