// Renderer-owned WOCC GGX/Smith/Schlick model. Existing regularizers are preserved.
float distributionGGX(float3 normal, float3 halfway, float roughness)
{
    float alpha = max(roughness * roughness, 0.045);
    float alpha2 = alpha * alpha;
    float nDotH = saturate(dot(normal, halfway));
    float nDotH2 = nDotH * nDotH;
    float denom = nDotH2 * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(3.14159265 * denom * denom, 0.001);
}

float geometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.001);
}

float geometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    float nDotV = saturate(dot(normal, viewDirection));
    float nDotL = saturate(dot(normal, lightDirection));
    return geometrySchlickGGX(nDotV, roughness) * geometrySchlickGGX(nDotL, roughness);
}

float3 fresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 fresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    float3 roughF0 = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), f0);
    return f0 + (roughF0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}
