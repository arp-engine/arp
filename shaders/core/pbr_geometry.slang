// PbrGeometry/1: value-only geometry shared by color, depth and shadow.
// Compose instance_transform, foliage_geometry and skinning_geometry first.
// These values are not an uploaded buffer ABI. Their inputs retain the
// existing Instance64, Wind64 and Skinning256 memory contracts.
struct ArpPbrGeometry
{
    float3 worldPosition;
    float3 previousWorldPosition;
    float3 normal;
    float4 tangent;
    uint4 instanceMetadata;
    float lodTime;
    float terrainContactY;
};

ArpPbrGeometry arpPbrWorldGeometry(float3 position, float3 normal, float4 tangent)
{
    ArpPbrGeometry result;
    result.worldPosition = position;
    result.previousWorldPosition = position;
    result.normal = normalize(normal);
    result.tangent = tangent;
    result.instanceMetadata = uint4(0u);
    result.lodTime = 0.0;
    result.terrainContactY = -1.0;
    return result;
}

ArpPbrGeometry arpPbrInstanceGeometry(ArpInstanceDataGpu instance,
    float3 position, float3 normal, float4 tangent, float3 windWeights,
    ArpFoliageWindField currentWind, ArpFoliageWindField previousWind,
    float authoredHeightReference)
{
    ArpPbrGeometry result;
    float3 rest = arpTransformInstancePosition(instance, position);
    result.worldPosition = arpDeformFoliagePosition(rest, position, windWeights,
        instance.yawPhaseIdLod.yw, instance.positionScale.xyz, currentWind,
        authoredHeightReference);
    result.previousWorldPosition = arpDeformFoliagePosition(rest, position, windWeights,
        instance.yawPhaseIdLod.yw, instance.positionScale.xyz, previousWind,
        authoredHeightReference);
    result.normal = arpTransformInstanceNormal(instance, normal);
    result.tangent = arpTransformInstanceTangent(instance, tangent);
    result.instanceMetadata = arpInstanceMetadata(instance);
    result.lodTime = currentWind.timeGustTurb.x;
    result.terrainContactY = instance.positionScale.y;
    return result;
}

ArpPbrGeometry arpPbrSkinnedGeometry(ArpSkinningBlend blend,
    float4 currentTranslationScale, float4 currentRotation,
    float4 previousTranslationScale, float4 previousRotation, float handedness)
{
    ArpPbrGeometry result;
    result.worldPosition = arpSkinningWorldPosition(blend.currentPosition,
        blend.weightSum, currentTranslationScale, currentRotation);
    result.previousWorldPosition = arpSkinningWorldPosition(blend.previousPosition,
        blend.weightSum, previousTranslationScale, previousRotation);
    result.normal = arpSkinningWorldNormal(blend, currentRotation);
    result.tangent = arpSkinningWorldTangent(blend, currentRotation, handedness);
    result.instanceMetadata = uint4(0u);
    result.lodTime = 0.0;
    result.terrainContactY = -1.0;
    return result;
}

// The pass chooses its explicit view-projection; no pass recomputes deformation.
float4 arpProjectPbrGeometry(ArpPbrGeometry geometry, float4x4 viewProjection)
{
    return mul(viewProjection, float4(geometry.worldPosition, 1.0));
}
