#ifndef ARP_CLOUD_RAY_INTEGRATION
#define ARP_CLOUD_RAY_INTEGRATION
#include "renderer/cloud_common.slang"

float cloudSunVisibility(float3 position, CloudGpuConstants p, StructuredBuffer<float4> cells)
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
            opticalDepth += p.shell.w * step * cloudDensity(cells,
                position + p.sunDirection.xyz * distance, p);
        }
    }
    return exp(-opticalDepth);
}

CloudGpuSample cloudIntegrateRay(float2 uv, float depth, CloudGpuConstants p,
    StructuredBuffer<float4> cells)
{
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
    float depthWeight = 0;
    float phase = arpVolumePhase(dot(direction, p.sunDirection.xyz), p.albedoAnisotropy.w);
    for (uint segment = 0; segment < count; ++segment) {
        float2 interval = segment == 0 ? intervals.xy : intervals.zw;
        float step = (interval.y - interval.x) / float(p.sampling.x);
        for (uint i = 0; i < p.sampling.x; ++i) {
            float distance = interval.x + (float(i) + p.jitter.z) * step;
            float3 position = origin + direction * distance;
            float sigma = p.shell.w * cloudDensity(cells, position, p);
            if (sigma <= 0) continue;
            float3 source = sigma * p.albedoAnisotropy.xyz
                * (p.ambientRadiance.xyz + p.sunRadiance.xyz * phase * cloudSunVisibility(position, p, cells));
            float segmentT = exp(-sigma * step);
            radiance += transmittance * source * arpVolumeSegmentLength(sigma, step);
            float absorbed = transmittance * sigma * arpVolumeSegmentLength(sigma, step);
            depthMoment += absorbed * distance;
            depthWeight += absorbed;
            transmittance *= segmentT;
        }
    }
    CloudGpuSample result;
    result.radianceTransmittance = float4(radiance, transmittance);
    result.metadata = float4(transmittance < 1 && depthWeight > 0 ? depthMoment / depthWeight
        : p.temporal.z, opaqueDistance, 0, 0);
    return result;
}
#endif
