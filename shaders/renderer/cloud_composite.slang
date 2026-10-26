#include "renderer/cloud_common.slang"
#define ARP_SAMPLED_DEPTH(name) DepthTexture2D name
[[vk::binding(0,0)]] ConstantBuffer<CloudGpuConstants> CloudParams;
[[vk::binding(1,0)]] StructuredBuffer<CloudGpuSample> CloudResolved;
[[vk::binding(2,0)]] Texture2D<float4> CloudSceneColor;
[[vk::binding(3,0)]] ARP_SAMPLED_DEPTH(CloudDepth);

struct VertexOutput { float4 position : SV_Position; };
#include "renderer/cloud_composite_impl.slang"

[shader("vertex")]
VertexOutput vertexMain(uint id : SV_VertexID)
{
    return cloudCompositeVertex(id);
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return cloudCompositeFragment(input);
}
