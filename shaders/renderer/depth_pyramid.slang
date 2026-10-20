// Conservative depth pyramid used by renderer-level visibility features.
//
// The source depth texture uses the RHI depth-sampled ABI. Mip 0 copies it
// into a portable Rgba16Float storage texture; later levels reduce the
// previous pyramid mip. With the engine's non-reversed [0,1] depth contract,
// R stores the maximum source depth used by conservative occlusion culling,
// while G stores the minimum source depth used by hierarchical ray queries.

struct DepthPyramidPushConstants
{
    uint destinationWidth;
    uint destinationHeight;
    uint sourceWidth;
    uint sourceHeight;
};

[[vk::push_constant]]
DepthPyramidPushConstants gDepthPyramid;

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name

[[vk::binding(0, 0)]]
ARP_SAMPLED_DEPTH(DepthSource);

[[vk::binding(1, 0)]]
Texture2D<float4> PyramidSource;

[[vk::binding(2, 0)]]
RWTexture2D<float4> PyramidDestination;

float2 loadPyramidDepthRange(uint2 sourceCoord)
{
    const uint2 maxCoord = uint2(
        max(gDepthPyramid.sourceWidth, 1u) - 1u,
        max(gDepthPyramid.sourceHeight, 1u) - 1u);
    return PyramidSource.Load(int3(min(sourceCoord, maxCoord), 0)).rg;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void depthPyramidCopyMain(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= gDepthPyramid.destinationWidth
        || dispatchId.y >= gDepthPyramid.destinationHeight) {
        return;
    }
    const float depth = DepthSource.Load(int3(dispatchId.xy, 0));
    PyramidDestination[dispatchId.xy] = float4(depth, depth, depth, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void depthPyramidReduceMain(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= gDepthPyramid.destinationWidth
        || dispatchId.y >= gDepthPyramid.destinationHeight) {
        return;
    }
    const uint2 baseCoord = dispatchId.xy * 2u;
    const float2 d00 = loadPyramidDepthRange(baseCoord);
    const float2 d10 = loadPyramidDepthRange(baseCoord + uint2(1u, 0u));
    const float2 d01 = loadPyramidDepthRange(baseCoord + uint2(0u, 1u));
    const float2 d11 = loadPyramidDepthRange(baseCoord + uint2(1u, 1u));
    const float maxDepth = max(max(d00.x, d10.x), max(d01.x, d11.x));
    const float minDepth = min(min(d00.y, d10.y), min(d01.y, d11.y));
    PyramidDestination[dispatchId.xy] = float4(maxDepth, minDepth, maxDepth, 1.0);
}
