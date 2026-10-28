#include "renderer/cloud_ray_integration.slang"
#include "renderer/cloud_history.slang"
#include "renderer/cloud_sample_storage.slang"

struct CloudReconstructionGpuConstants {
    uint contractVersion;
    float maxRadianceRange;
    float maxTransmittanceRange;
    uint reserved;
};
[[vk::binding(0,0)]] ConstantBuffer<CloudGpuConstants> CloudParams;
[[vk::binding(1,0)]] StructuredBuffer<CloudGpuSample> CloudRaw;
[[vk::binding(2,0)]] StructuredBuffer<CloudStoredSample> CloudPrevious;
[[vk::binding(3,0)]] RWStructuredBuffer<CloudStoredSample> CloudResolved;
[[vk::binding(4,0)]] StructuredBuffer<float4> CloudDensityCells;
#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name
[[vk::binding(5,0)]] ARP_SAMPLED_DEPTH(CloudDepth);
[[vk::binding(6,0)]] ConstantBuffer<CloudReconstructionGpuConstants> CloudReconstruction;

#include "renderer/cloud_spatial_resolve_impl.slang"

[shader("compute")]
[numthreads(8,8,1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    cloudResolveAt(id);
}
