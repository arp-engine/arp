#include "renderer/cloud_ray_integration.slang"
#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name
[[vk::binding(0,0)]] ConstantBuffer<CloudGpuConstants> CloudParams;
[[vk::binding(1,0)]] StructuredBuffer<float4> CloudDensityCells;
[[vk::binding(2,0)]] ARP_SAMPLED_DEPTH(CloudDepth);
[[vk::binding(3,0)]] RWStructuredBuffer<CloudGpuSample> CloudRaw;

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
    CloudRaw[index] = cloudIntegrateRay(uv, depth, p, CloudDensityCells);
}
