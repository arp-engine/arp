#include "common/forward_plus_lighting.slang"

#define ARP_SAMPLED_DEPTH(name) Texture2D<float> name

struct ForwardPlusCullConstants
{
    uint2 extent;
    uint2 tileCount;
    uint localLightCount;
    uint maxLocalLightsPerTile;
    float2 projectionScale;
    float depthScale;
    float depthBias;
    float2 padding;
    float4 viewRows[4];
};

[[vk::binding(0, 0)]] ConstantBuffer<ForwardPlusCullConstants> ForwardPlusCullParams;
[[vk::binding(1, 0)]] ARP_SAMPLED_DEPTH(ForwardPlusDepth);
[[vk::binding(2, 0)]] StructuredBuffer<GpuLocalLight> ForwardPlusLocalLights;
[[vk::binding(3, 0)]] RWStructuredBuffer<ForwardPlusTileHeader> ForwardPlusOpaqueHeaders;
[[vk::binding(4, 0)]] RWStructuredBuffer<uint> ForwardPlusOpaqueIndices;
[[vk::binding(5, 0)]] RWStructuredBuffer<ForwardPlusTileHeader> ForwardPlusConservativeHeaders;
[[vk::binding(6, 0)]] RWStructuredBuffer<uint> ForwardPlusConservativeIndices;
[[vk::binding(7, 0)]] RWStructuredBuffer<uint4> ForwardPlusOverflowTelemetry;

groupshared float ForwardPlusDepthMin[256];
groupshared float ForwardPlusDepthMax[256];
groupshared uint ForwardPlusOpaqueFlags[256];
groupshared uint ForwardPlusConservativeFlags[256];
groupshared uint ForwardPlusOpaquePrefix[256];
groupshared uint ForwardPlusConservativePrefix[256];
// The scan must not read and write the same shared prefix array in one
// synchronization phase. A barrier only orders workgroup accesses around the
// barrier; it does not make all lanes finish their reads before another lane
// starts its write. Reuse one scratch array while scanning each list in turn.
groupshared uint ForwardPlusScanScratch[256];
groupshared uint ForwardPlusOpaqueBase;
groupshared uint ForwardPlusConservativeBase;
groupshared uint ForwardPlusOpaqueTotal;
groupshared uint ForwardPlusConservativeTotal;

float forwardPlusViewDepthToNdc(float viewDepth)
{
    return ForwardPlusCullParams.depthScale
        + ForwardPlusCullParams.depthBias / max(viewDepth, 0.00001);
}

bool forwardPlusSphereOverlapsTile(
    GpuLocalLight light,
    uint2 tile,
    float tileMinDepth,
    float tileMaxDepth,
    out bool overlapsConservative)
{
    float4 worldCenter = float4(light.positionAndRange.xyz, 1.0);
    float3 center = float3(
        dot(ForwardPlusCullParams.viewRows[0], worldCenter),
        dot(ForwardPlusCullParams.viewRows[1], worldCenter),
        dot(ForwardPlusCullParams.viewRows[2], worldCenter));
    float radius = light.positionAndRange.w;
    float nearestDepth = center.z - radius;
    float farthestDepth = center.z + radius;
    overlapsConservative = false;
    if (farthestDepth <= 0.0)
    {
        return false;
    }

    float2 tileMinPixel = float2(tile * ARP_FORWARD_PLUS_TILE_SIZE);
    float2 tileMaxPixel = min(
        tileMinPixel + float2(ARP_FORWARD_PLUS_TILE_SIZE, ARP_FORWARD_PLUS_TILE_SIZE),
        float2(ForwardPlusCullParams.extent));
    float safeDepth = max(nearestDepth, 0.00001);
    float2 centerNdc = center.xy * ForwardPlusCullParams.projectionScale
        / max(center.z, 0.00001);
    float2 radiusNdc = abs(ForwardPlusCullParams.projectionScale) * radius / safeDepth;
    float2 centerPixel = (centerNdc * 0.5 + 0.5)
        * float2(ForwardPlusCullParams.extent);
    float2 radiusPixel = radiusNdc * 0.5
        * float2(ForwardPlusCullParams.extent);
    overlapsConservative = all(centerPixel + radiusPixel >= tileMinPixel)
        && all(centerPixel - radiusPixel <= tileMaxPixel);
    if (!overlapsConservative)
    {
        return false;
    }

    float sphereMinDepth = nearestDepth <= 0.0
        ? 0.0 : forwardPlusViewDepthToNdc(nearestDepth);
    float sphereMaxDepth = forwardPlusViewDepthToNdc(farthestDepth);
    float orderedMin = min(sphereMinDepth, sphereMaxDepth);
    float orderedMax = max(sphereMinDepth, sphereMaxDepth);
    return orderedMax >= tileMinDepth && orderedMin <= tileMaxDepth;
}

[numthreads(16, 16, 1)]
[shader("compute")]
void forwardPlusCullMain(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex)
{
    uint2 clampedPixel = min(
        dispatchThreadId.xy,
        ForwardPlusCullParams.extent - uint2(1, 1));
    float depth = ForwardPlusDepth.Load(int3(clampedPixel, 0));
    ForwardPlusDepthMin[groupIndex] = depth;
    ForwardPlusDepthMax[groupIndex] = depth;
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (groupIndex < stride)
        {
            ForwardPlusDepthMin[groupIndex] = min(
                ForwardPlusDepthMin[groupIndex],
                ForwardPlusDepthMin[groupIndex + stride]);
            ForwardPlusDepthMax[groupIndex] = max(
                ForwardPlusDepthMax[groupIndex],
                ForwardPlusDepthMax[groupIndex + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
    {
        ForwardPlusOpaqueTotal = 0;
        ForwardPlusConservativeTotal = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint lightBase = 0; lightBase < ForwardPlusCullParams.localLightCount;
         lightBase += 256)
    {
        uint lightIndex = lightBase + groupIndex;
        uint opaqueFlag = 0;
        uint conservativeFlag = 0;
        if (lightIndex < ForwardPlusCullParams.localLightCount)
        {
            bool conservative = false;
            bool opaque = forwardPlusSphereOverlapsTile(
                ForwardPlusLocalLights[lightIndex],
                groupId.xy,
                ForwardPlusDepthMin[0],
                ForwardPlusDepthMax[0],
                conservative);
            opaqueFlag = opaque ? 1 : 0;
            conservativeFlag = conservative ? 1 : 0;
        }
        ForwardPlusOpaqueFlags[groupIndex] = opaqueFlag;
        ForwardPlusConservativeFlags[groupIndex] = conservativeFlag;
        ForwardPlusOpaquePrefix[groupIndex] = opaqueFlag;
        ForwardPlusConservativePrefix[groupIndex] = conservativeFlag;
        GroupMemoryBarrierWithGroupSync();

        for (uint offset = 1; offset < 256; offset <<= 1)
        {
            // Snapshot every lane before any lane reads a neighbour. This is
            // deliberately two-phase: a workgroup barrier cannot make a
            // read-before-write expression race-free by itself.
            ForwardPlusScanScratch[groupIndex] = ForwardPlusOpaquePrefix[groupIndex];
            GroupMemoryBarrierWithGroupSync();
            ForwardPlusOpaquePrefix[groupIndex] = ForwardPlusScanScratch[groupIndex]
                + (groupIndex >= offset
                    ? ForwardPlusScanScratch[groupIndex - offset] : 0);
            GroupMemoryBarrierWithGroupSync();
        }
        for (uint offset = 1; offset < 256; offset <<= 1)
        {
            ForwardPlusScanScratch[groupIndex] = ForwardPlusConservativePrefix[groupIndex];
            GroupMemoryBarrierWithGroupSync();
            ForwardPlusConservativePrefix[groupIndex] = ForwardPlusScanScratch[groupIndex]
                + (groupIndex >= offset
                    ? ForwardPlusScanScratch[groupIndex - offset] : 0);
            GroupMemoryBarrierWithGroupSync();
        }

        if (groupIndex == 0)
        {
            ForwardPlusOpaqueBase = ForwardPlusOpaqueTotal;
            ForwardPlusConservativeBase = ForwardPlusConservativeTotal;
            ForwardPlusOpaqueTotal += ForwardPlusOpaquePrefix[255];
            ForwardPlusConservativeTotal += ForwardPlusConservativePrefix[255];
        }
        GroupMemoryBarrierWithGroupSync();

        uint tileIndex = groupId.y * ForwardPlusCullParams.tileCount.x + groupId.x;
        uint tileOffset = tileIndex * ForwardPlusCullParams.maxLocalLightsPerTile;
        if (ForwardPlusOpaqueFlags[groupIndex] != 0)
        {
            // The index buffer is laid out as a fixed-capacity span per tile.
            // Prefix values are therefore local to this tile; the batch base
            // is only useful for a global append buffer and must not be added
            // to the per-tile destination.
            uint destination = ForwardPlusOpaquePrefix[groupIndex] - 1;
            if (destination < ForwardPlusCullParams.maxLocalLightsPerTile)
            {
                ForwardPlusOpaqueIndices[tileOffset + destination] = lightIndex;
            }
        }
        if (ForwardPlusConservativeFlags[groupIndex] != 0)
        {
            uint destination = ForwardPlusConservativePrefix[groupIndex] - 1;
            if (destination < ForwardPlusCullParams.maxLocalLightsPerTile)
            {
                ForwardPlusConservativeIndices[tileOffset + destination] = lightIndex;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (groupIndex == 0)
    {
        uint tileIndex = groupId.y * ForwardPlusCullParams.tileCount.x + groupId.x;
        uint tileOffset = tileIndex * ForwardPlusCullParams.maxLocalLightsPerTile;
        ForwardPlusOpaqueHeaders[tileIndex] = {
            tileOffset, ForwardPlusOpaqueTotal
        };
        ForwardPlusConservativeHeaders[tileIndex] = {
            tileOffset, ForwardPlusConservativeTotal
        };
        uint overflowMask = (ForwardPlusOpaqueTotal > ForwardPlusCullParams.maxLocalLightsPerTile ? 1 : 0)
            | (ForwardPlusConservativeTotal > ForwardPlusCullParams.maxLocalLightsPerTile ? 2 : 0);
        ForwardPlusOverflowTelemetry[tileIndex] = uint4(
            overflowMask,
            ForwardPlusOpaqueTotal,
            ForwardPlusConservativeTotal,
            ForwardPlusCullParams.maxLocalLightsPerTile);
    }
}
