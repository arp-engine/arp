#include "renderer/cloud_common.slang"
#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name
[[vk::binding(0,0)]] ConstantBuffer<CloudGpuConstants> CloudParams;
[[vk::binding(1,0)]] StructuredBuffer<float4> CloudDensityCells;
[[vk::binding(2,0)]] ARP_SAMPLED_DEPTH(CloudDepth);
[[vk::binding(3,0)]] RWStructuredBuffer<CloudGpuSample> CloudRaw;

float cloudSunVisibility(float3 position, CloudGpuConstants p)
{
    float4 intervals;
    bool blocked;
    uint count = cloudIntervals(position, p.sunDirection.xyz, 3.402823466e+38,
        p, intervals, blocked);
    if (blocked) return 0;
    float opticalDepth = 0;
    for (uint segment = 0; segment < count; ++segment) {
        float2 interval = segment == 0 ? intervals.xy : intervals.zw;
        float step = (interval.y - interval.x) / float(p.sampling.y);
        for (uint i = 0; i < p.sampling.y; ++i) {
            float distance = interval.x + (float(i) + 0.5) * step;
            opticalDepth += p.shell.w * step * cloudDensity(CloudDensityCells,
                position + p.sunDirection.xyz * distance, p);
        }
    }
    return exp(-opticalDepth);
}

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    CloudGpuConstants p = CloudParams;
    if (any(id.xy >= p.dimensions.xy)) return;
    uint index = id.y * p.dimensions.x + id.x;
    float2 uv = (float2(id.xy) + p.jitter.xy) / float2(p.dimensions.xy);
    uint2 pixel = min(uint2(uv * float2(p.dimensions.zw)), p.dimensions.zw - 1u);
    float depth = CloudDepth.Load(int3(pixel, 0));
    float3 nearPoint = cloudUnproject(uv, 0, p.inverseViewProjection);
    float3 farPoint = cloudUnproject(uv, 1, p.inverseViewProjection);
    float3 direction = normalize(farPoint - nearPoint);
    float3 origin = p.jitter.w > 0 ? nearPoint - p.cameraTime.xyz : float3(0,0,0);
    float opaqueDistance = depth >= 1 ? p.temporal.z
        : max(0.0, dot(cloudUnproject(uv, depth, p.inverseViewProjection)
            - p.cameraTime.xyz - origin, direction));
    float4 intervals;
    bool groundBlocked;
    uint count = cloudIntervals(origin, direction, min(opaqueDistance, p.temporal.z),
        p, intervals, groundBlocked);
    float3 radiance = 0;
    float transmittance = 1;
    float depthMoment = 0;
    float phase = arpVolumePhase(dot(direction, p.sunDirection.xyz), p.albedoAnisotropy.w);
    for (uint segment = 0; segment < count; ++segment) {
        float2 interval = segment == 0 ? intervals.xy : intervals.zw;
        float step = (interval.y - interval.x) / float(p.sampling.x);
        for (uint i = 0; i < p.sampling.x; ++i) {
            float distance = interval.x + (float(i) + p.jitter.z) * step;
            float3 position = origin + direction * distance;
            float sigma = p.shell.w * cloudDensity(CloudDensityCells, position, p);
            if (sigma <= 0) continue;
            float3 source = sigma * p.albedoAnisotropy.xyz
                * (p.ambientRadiance.xyz + p.sunRadiance.xyz * phase * cloudSunVisibility(position, p));
            float segmentT = exp(-sigma * step);
            radiance += transmittance * source * arpVolumeSegmentLength(sigma, step);
            depthMoment += transmittance * (1.0 - segmentT) * distance;
            transmittance *= segmentT;
        }
    }
    CloudGpuSample result;
    result.radianceTransmittance = float4(radiance, transmittance);
    result.metadata = float4(transmittance < 1 ? depthMoment / (1.0 - transmittance)
        : p.temporal.z, opaqueDistance, 0, 0);
    CloudRaw[index] = result;
}
