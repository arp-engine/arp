#ifndef ARP_CLOUD_HISTORY
#define ARP_CLOUD_HISTORY
#include "renderer/cloud_common.slang"

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

#endif
