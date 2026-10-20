[[vk::binding(0, 2)]] Texture2D<float4> ColorTexture;
[[vk::binding(1, 2)]] SamplerState ColorSampler;
[[vk::binding(2, 2)]] DepthTexture2D ShadowMap;
[[vk::binding(3, 2)]] SamplerComparisonState LessSampler;
[[vk::binding(4, 2)]] SamplerComparisonState GreaterSampler;

[shader("vertex")]
float4 vertexMain(uint vertex : SV_VertexID) : SV_Position
{
    float2 corner = float2(float((vertex << 1u) & 2u), float(vertex & 2u));
    return float4(corner * 2.0 - 1.0, 0.0, 1.0);
}

float4 shadeDepthColor(float4 position, bool injectFault)
{
    // The authored numerical surface is 2x2; every invocation owns ten words.
    uint base = (uint(position.y) * 2u + uint(position.x)) * 10u;
    float2 uv = position.xy / float2(2.0, 2.0);
    float4 color = ColorTexture.SampleLevel(ColorSampler, uv, 0.0);
    bool fault;
    for (uint channel = 0u; channel < 4u; ++channel)
        Results_store(0u, base + channel, asuint(color[channel]), fault);
    // The original producer has interpolated depth, independently checked by
    // readback/replay. Load the exact same texel so the middle probe tests true
    // equality rather than an approximate CPU reconstruction of interpolation.
    float storedDepth = ShadowMap.Load(int3(uint2(position.xy), 0));
    const float references[3] = {storedDepth - 0.005, storedDepth, storedDepth + 0.005};
    for (uint probe = 0u; probe < 3u; ++probe) {
        float less = ShadowMap.SampleCmpLevelZero(LessSampler, uv, references[probe]);
        float greater = ShadowMap.SampleCmpLevelZero(GreaterSampler, uv, references[probe]);
        Results_store(0u, base + 4u + probe, asuint(less), fault);
        Results_store(0u, base + 7u + probe, asuint(greater), fault);
    }
    if (injectFault)
        Results_store(2u, 0u, 0xffffffffu, fault);
    return color;
}

[shader("fragment")]
float4 cleanFragment(float4 position : SV_Position) : SV_Target0
{
    return shadeDepthColor(position, false);
}

[shader("fragment")]
float4 faultFragment(float4 position : SV_Position) : SV_Target0
{
    return shadeDepthColor(position, true);
}
