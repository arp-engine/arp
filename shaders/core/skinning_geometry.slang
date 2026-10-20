// SkinningGeometry/1. One calculation supplies color/depth/shadow positions and
// current world normal/tangent. The caller validates palette and vertex ranges.
struct ArpSkinningBlend
{
    float4 currentPosition;
    float4 previousPosition;
    float3 normal;
    float3 tangent;
    float weightSum;
};

ArpSkinningBlend arpEmptySkinningBlend()
{
    ArpSkinningBlend blend;
    blend.currentPosition = float4(0.0);
    blend.previousPosition = float4(0.0);
    blend.normal = float3(0.0);
    blend.tangent = float3(0.0);
    blend.weightSum = 0.0;
    return blend;
}

void arpAccumulateSkinning(inout ArpSkinningBlend blend, float4x4 current,
    float4x4 previous, bool previousValid, float weight, float3 position,
    float3 normal, float3 tangent)
{
    blend.currentPosition += mul(current, float4(position, 1.0)) * weight;
    if (previousValid)
        blend.previousPosition += mul(previous, float4(position, 1.0)) * weight;
    blend.normal += mul((float3x3)current, normal) * weight;
    blend.tangent += mul((float3x3)current, tangent) * weight;
    blend.weightSum += weight;
}

float3 arpRotateSkinningDirection(float3 value, float4 quaternion)
{
    float3 twiceCross = 2.0 * cross(quaternion.xyz, value);
    return value + quaternion.w * twiceCross + cross(quaternion.xyz, twiceCross);
}

float3 arpSkinningWorldPosition(float4 blended, float weightSum,
    float4 translationScale, float4 rotation)
{
    float inverseWeight = weightSum > 0.000001 ? rcp(weightSum) : 1.0;
    return arpRotateSkinningDirection(blended.xyz * inverseWeight * translationScale.w,
        rotation) + translationScale.xyz;
}

float3 arpSkinningWorldNormal(ArpSkinningBlend blend, float4 rotation)
{
    return normalize(arpRotateSkinningDirection(blend.normal, rotation));
}

float4 arpSkinningWorldTangent(ArpSkinningBlend blend, float4 rotation, float handedness)
{
    return float4(normalize(arpRotateSkinningDirection(blend.tangent, rotation)), handedness);
}
