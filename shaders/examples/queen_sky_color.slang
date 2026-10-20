float4 queenSkyColor(float2 uv)
{
    const float height = saturate(1.0 - uv.y);
    const float3 horizon = float3(0.94, 0.66, 0.66);
    const float3 zenith = float3(0.20, 0.43, 0.76);
    float3 color = lerp(horizon, zenith, pow(height, 0.72));

    const float2 sunCenter = float2(0.68, 0.29);
    const float sunDistance = distance(uv, sunCenter);
    const float glow = exp(-sunDistance * 20.0) * 0.22;
    const float disc = 1.0 - smoothstep(0.018, 0.024, sunDistance);
    color += float3(1.0, 0.66, 0.32) * (glow + disc * 0.72);
    return float4(color, 1.0);
}
