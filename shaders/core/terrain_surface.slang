// Shared terrain material arithmetic. No descriptors, resource access or
// derivatives: Traditional and indexed frontends can pass identical samples.
float3 arpTerrainNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return all(isfinite(value)) && all(abs(value) < 1e20) && lengthSquared > 1e-8
        ? value * rsqrt(lengthSquared) : fallback;
}

float2 arpTerrainRotate(float2 uv, float angle)
{
    float s = sin(angle), c = cos(angle);
    return float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
}

float arpTerrainCellAngle(float2 worldXZ)
{
    return frac(sin(dot(floor(worldXZ * 0.5), float2(127.1, 311.7))) * 43758.5453) * 6.2831853;
}

struct ArpTerrainWeights
{
    float4 first;
    float2 last;
    float wetness;
};

ArpTerrainWeights arpTerrainBlendWeights(float4 pageA, float4 pageB,
    float height, float snowLine, float snowFeather)
{
    ArpTerrainWeights result;
    float invSum = 1.0 / max(dot(pageA, float4(1.0)) + pageB.x + pageB.y, 0.001);
    result.first = pageA * invSum;
    result.last = pageB.xy * invSum;
    float snowPull = pageB.z * saturate((height - snowLine) / max(snowFeather, 0.001)) * 0.7;
    result.last.y += snowPull;
    result.first.x *= 1.0 - snowPull;
    result.last.x *= 1.0 - snowPull * 0.8;
    float wetBoost = pageB.w * 0.3;
    result.first.z += wetBoost;
    result.first.x *= 1.0 - wetBoost * 0.5;
    invSum = 1.0 / max(dot(result.first, float4(1.0)) + result.last.x + result.last.y, 0.001);
    result.first *= invSum;
    result.last *= invSum;
    result.wetness = pageB.w;
    return result;
}

float arpTerrainLayerWeight(ArpTerrainWeights weights, uint layer)
{
    return layer < 4u ? weights.first[layer] : weights.last[layer - 4u];
}

float arpTerrainRoughness(float weightedBase, float weightedTexture,
    float effectiveWetness, float shoreWetness)
{
    float roughness = lerp(weightedBase, weightedTexture, 0.2);
    roughness *= lerp(1.0, 0.7, effectiveWetness * 0.5);
    roughness *= lerp(1.0, 0.62, shoreWetness);
    return clamp(roughness, lerp(0.45, 0.28, shoreWetness), 1.0);
}
