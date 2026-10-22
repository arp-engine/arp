#include "renderer/cloud_common.slang"
[[vk::binding(0,0)]] ConstantBuffer<CloudGpuConstants> CloudParams;
[[vk::binding(1,0)]] StructuredBuffer<float4> CloudDensityCells;
[[vk::binding(3,0)]] RWStructuredBuffer<CloudGpuSample> CloudSlice;

// Diagnostic slice for CloudDensity/1. An XZ plane spans one weather period
// at the authored shell's middle altitude; all coordinates are world metres.
[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    CloudGpuConstants p = CloudParams;
    if (any(id.xy >= p.dimensions.xy)) return;
    float2 uv = (float2(id.xy) + 0.5) / float2(p.dimensions.xy);
    float3 world = float3(uv.x * p.periodsCoverage.x,
        p.shell.x + p.shell.y * 0.5, uv.y * p.periodsCoverage.x);
    float density = cloudDensity(CloudDensityCells, world - p.cameraTime.xyz, p);
    CloudGpuSample result;
    result.radianceTransmittance = float4(density, density, density, 1);
    result.metadata = float4(cloudAltitude(world - p.cameraTime.xyz, p), 0, 0, 0);
    CloudSlice[id.y * p.dimensions.x + id.x] = result;
}
