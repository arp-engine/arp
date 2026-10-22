#include "renderer/cloud_common.slang"
[[vk::binding(0,0)]] ConstantBuffer<CloudGpuConstants> CloudParams;
[[vk::binding(1,0)]] StructuredBuffer<CloudGpuSample> CloudRaw;
[[vk::binding(2,0)]] StructuredBuffer<CloudGpuSample> CloudPrevious;
[[vk::binding(3,0)]] RWStructuredBuffer<CloudGpuSample> CloudResolved;

// Orthographic sample distances start at the near plane, not at the camera.
// An affine zero-to-one depth row has magnitude 1 / (far - near), so dividing
// its clip Z by that magnitude recovers the signed near-plane ray distance.
float cloudPreviousDistance(float3 world, CloudGpuConstants p)
{
    if (p.jitter.w <= 0) return length(world - p.previousCamera.xyz);
    float4 clip = mul(p.previousViewProjection, float4(world, 1));
    float3 row = float3(p.previousViewProjection[2][0],
        p.previousViewProjection[2][1], p.previousViewProjection[2][2]);
    return clip.z / length(row);
}

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    CloudGpuConstants p = CloudParams;
    if (any(id.xy >= p.dimensions.xy)) return;
    uint index = id.y * p.dimensions.x + id.x;
    CloudGpuSample current = CloudRaw[index];
    if (p.previousCamera.w > 0) {
        float2 uv = (float2(id.xy) + 0.5) / float2(p.dimensions.xy);
        float3 nearPoint = cloudUnproject(uv, 0, p.inverseViewProjection);
        float3 farPoint = cloudUnproject(uv, 1, p.inverseViewProjection);
        float3 direction = normalize(farPoint - nearPoint);
        float3 origin = p.jitter.w > 0 ? nearPoint : p.cameraTime.xyz;
        float3 previousPosition = origin + direction * current.metadata.x
            - p.windErosion.xyz * (p.cameraTime.w - p.temporal.w);
        float4 previousClip = mul(p.previousViewProjection, float4(previousPosition, 1));
        float2 previousUv = previousClip.xy / previousClip.w * 0.5 + 0.5;
        if (previousClip.w > 0 && all(previousUv >= 0) && all(previousUv < 1)) {
            uint2 pixel = uint2(previousUv * float2(p.dimensions.xy));
            CloudGpuSample old = CloudPrevious[pixel.y * p.dimensions.x + pixel.x];
            float distance = cloudPreviousDistance(previousPosition, p);
            bool sameMedium = abs(old.metadata.x - distance) <= p.temporal.y;
            bool currentSky = current.metadata.y >= p.temporal.z;
            bool previousSky = old.metadata.y >= p.temporal.z;
            float3 opaquePosition = origin + direction * current.metadata.y;
            float previousOpaqueDistance = cloudPreviousDistance(opaquePosition, p);
            bool sameOpaque = currentSky && previousSky
                || (!currentSky && !previousSky && previousOpaqueDistance >= 0
                    && abs(old.metadata.y - previousOpaqueDistance) <= p.temporal.y);
            if (sameMedium && sameOpaque) {
                float4 lower = current.radianceTransmittance;
                float4 upper = lower;
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x) {
                        int2 neighbor = clamp(int2(id.xy) + int2(x,y), int2(0,0), int2(p.dimensions.xy) - 1);
                        float4 value = CloudRaw[uint(neighbor.y) * p.dimensions.x + uint(neighbor.x)].radianceTransmittance;
                        lower = min(lower, value);
                        upper = max(upper, value);
                    }
                current.radianceTransmittance = lerp(current.radianceTransmittance,
                    clamp(old.radianceTransmittance, lower, upper), p.temporal.x);
            }
        }
    }
    CloudResolved[index] = current;
}
