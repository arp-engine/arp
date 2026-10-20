// Current InstanceDataGpu/PBR transform semantics. Metadata remains raw bits.
struct ArpInstanceDataGpu
{
    float4 positionScale;
    float4 yawPhaseIdLod;
    float4 extraTransform;
    float4 speciesLodState;
};

float3 arpRotateInstanceDirection(float3 value, float yaw, float pitch, float roll)
{
    float cy = cos(yaw);
    float sy = sin(yaw);
    float3 result = float3(value.x * cy + value.z * sy, value.y,
        -value.x * sy + value.z * cy);
    if (abs(pitch) > 0.001 || abs(roll) > 0.001) {
        float cp = cos(pitch);
        float sp = sin(pitch);
        result = float3(result.x, result.y * cp - result.z * sp,
            result.y * sp + result.z * cp);
        float cr = cos(roll);
        float sr = sin(roll);
        result = float3(result.x * cr - result.y * sr,
            result.x * sr + result.y * cr, result.z);
    }
    return result;
}

float3 arpTransformInstancePosition(ArpInstanceDataGpu instance, float3 position)
{
    float scaleY = instance.extraTransform.z > 0.0
        ? instance.extraTransform.z : instance.positionScale.w;
    float3 scaled = position * float3(instance.positionScale.w, scaleY,
        instance.positionScale.w);
    float3 result = arpRotateInstanceDirection(scaled, instance.yawPhaseIdLod.x,
        instance.extraTransform.x, instance.extraTransform.y) + instance.positionScale.xyz;
    result.y += instance.extraTransform.w;
    return result;
}

float3 arpTransformInstanceNormal(ArpInstanceDataGpu instance, float3 normal)
{
    return normalize(arpRotateInstanceDirection(normal, instance.yawPhaseIdLod.x,
        instance.extraTransform.x, instance.extraTransform.y));
}

float4 arpTransformInstanceTangent(ArpInstanceDataGpu instance, float4 tangent)
{
    return float4(arpRotateInstanceDirection(tangent.xyz, instance.yawPhaseIdLod.x,
        instance.extraTransform.x, instance.extraTransform.y), tangent.w);
}

uint4 arpInstanceMetadata(ArpInstanceDataGpu instance)
{
    return asuint(instance.speciesLodState);
}
