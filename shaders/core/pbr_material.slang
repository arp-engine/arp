// Directive-free source library. Compose core/leaf_surface.slang before this file.
// Inputs preserve the current material lanes; frame/debug state stays external.
bool arpPbrMaterialAlphaMasked(float mode)
{
    return (mode > 0.5 && mode < 1.5) || (mode >= 2.0 && mode <= 3.0);
}

bool arpPbrMaterialAlphaCovered(float alpha, float mode, float cutoff)
{
    return !(arpPbrMaterialAlphaMasked(mode) && alpha < cutoff);
}

struct ArpPbrMaterialSurface
{
    float4 baseColorAlpha;
    float metallic;
    float roughness;
    float occlusion;
    float3 emissive;
    float3 translucentColor;
    float leafWeight;
    float dielectricF0;
    bool covered;
};

ArpPbrMaterialSurface arpEvaluatePbrMaterial(float4 baseColorAlpha,
    float4 pbrParams, float4 extraParams, float4 environmentParams,
    float4 sampledBase, float4 sampledMetallicRoughness,
    float4 sampledEmissive, float4 sampledOcclusion)
{
    ArpPbrMaterialSurface surface;
    surface.baseColorAlpha = sampledBase * baseColorAlpha;
    surface.metallic = saturate(sampledMetallicRoughness.b * pbrParams.x);
    surface.roughness = clamp(sampledMetallicRoughness.g * pbrParams.y, 0.045, 1.0);
    surface.occlusion = lerp(1.0, sampledOcclusion.r, saturate(extraParams.x));
    surface.emissive = sampledEmissive.rgb * extraParams.y;
    bool authoredLeaf = pbrParams.w >= 2.0 && pbrParams.w <= 3.0;
    surface.translucentColor = authoredLeaf
        ? leafTranslucentColor(surface.baseColorAlpha.rgb, -environmentParams.x)
        : surface.baseColorAlpha.rgb;
    surface.leafWeight = authoredLeaf ? max(-extraParams.z, 0.0) : 0.0;
    surface.dielectricF0 = authoredLeaf ? pbrParams.w - 2.0 : 0.04;
    surface.covered = arpPbrMaterialAlphaCovered(
        surface.baseColorAlpha.a, pbrParams.w, pbrParams.z);
    return surface;
}

float3 arpPbrMaterialNormal(float3 normal, float4 tangentAndSign,
    bool frontFacing, float singleSidedLighting, float3 sampledNormal)
{
    float3 geometryNormal = normalize(normal);
    float3 candidate = tangentAndSign.xyz
        - geometryNormal * dot(geometryNormal, tangentAndSign.xyz);
    float lengthSquared = dot(candidate, candidate);
    float3 fallbackAxis = abs(geometryNormal.z) < 0.999
        ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    float3 tangent = lengthSquared > 1e-8
        ? candidate * rsqrt(lengthSquared)
        : normalize(cross(fallbackAxis, geometryNormal));
    float sign = tangentAndSign.w;
    if (!frontFacing && singleSidedLighting < 0.5) {
        geometryNormal = -geometryNormal;
        sign = -sign;
    }
    float3 bitangent = normalize(cross(geometryNormal, tangent) * sign);
    float3 mapped = sampledNormal * 2.0 - 1.0;
    return normalize(tangent * mapped.x + bitangent * mapped.y + geometryNormal * mapped.z);
}
