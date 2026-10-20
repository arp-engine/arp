struct TracePushConstants
{
    float4 sceneBvhParams;
    float4 origin;
    float4 direction;
};
[[vk::push_constant]] TracePushConstants gDdgi;

// These declarations mirror the production GPU buffer ABI; traversal itself
// is included from production. Material textures are outside this fixture.
struct GpuBvhNode
{
    float3 aabbMin;
    uint leftChild;
    float3 aabbMax;
    uint rightChild;
};
struct GpuBvhLeaf
{
    float3 v0;
    float pad0;
    float3 v1;
    float pad1;
    float3 v2;
    float pad2;
    float3 normal;
    uint materialIdx;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    float2 uvPad;
};
static const uint kBvhInvalidLeafMarker = 0xFFFFFFFFu;
[[vk::binding(10, 0)]] StructuredBuffer<GpuBvhNode> SceneBvhNodes;
[[vk::binding(11, 0)]] StructuredBuffer<GpuBvhLeaf> SceneBvhLeaves;
[[vk::binding(22, 0)]] StructuredBuffer<float4> SceneBvhEmissive;
[[vk::binding(24, 0)]] StructuredBuffer<GpuBvhNode> DynamicTlasNodes;
[[vk::binding(25, 0)]] StructuredBuffer<GpuBvhLeaf> DynamicTlasLeaves;
[[vk::binding(26, 0)]] StructuredBuffer<float4> DynamicTlasEmissive;
[[vk::binding(27, 0)]] StructuredBuffer<float4> SceneBvhInstances;
[[vk::binding(6, 0)]] RWStructuredBuffer<float4> Output;

float3 materialAlbedo(uint materialIdx, float3 position, float2 uv)
{
    return 0.0.xxx;
}

// This geometry-only fixture has no indexed-material domain; legacy IDs are opaque.
bool ddgiUsesSceneMaterial(uint material) {return false;}
float4 ddgiSceneMaterialColor(uint material,float2 uv) {return 1.0.xxxx;}
bool ddgiSceneMaterialVisible(uint material,float alpha) {return true;}

#include "renderer/ddgi_scene_bvh_trace.slang"

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    SceneRayHit hit;
    hit.hitDist = -1.0;
    hit.hitNormal = float3(0.0, 1.0, 0.0);
    hit.materialIdx = 0u;
    hit.emissive = 0.0.xxx;
    if (hasSceneBvhGeometry()) {
        hit = traceSceneBvh(gDdgi.origin.xyz, gDdgi.direction.xyz, gDdgi.direction.w);
    }
    Output[0] = float4(hit.hitDist, float(hit.materialIdx), hit.hitNormal.y, hit.emissive.x);
    Output[1] = float4(float(ddgiStaticInstanceCount()), hit.hitNormal.x, hit.hitNormal.z,
        hasSceneBvhGeometry() ? 1.0 : 0.0);
}
