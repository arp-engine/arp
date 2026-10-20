// The production depth_pyramid.slang copy/reduction operations expressed with
// typed table access. Views identify the exact mip: no implicit LOD, sampler,
// native resource access, or authority to record/submit is introduced here.
// The public execution workload must bind source mip n and destination mip n+1,
// declare storage-write -> sampled-read, and retain the original depth proof.

float2 loadDepthPyramidRange(uint2 sourceCoord, uint2 sourceSize)
{
    bool fault;
    const uint2 maxCoord = max(sourceSize, uint2(1u, 1u)) - uint2(1u, 1u);
    return Sources_load(0u, int3(min(sourceCoord, maxCoord), 0), fault).rg;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void depthPyramidCopyMain(uint3 id : SV_DispatchThreadID)
{
    bool fault;
    const uint2 destinationSize = CopyOutputs_query(0u, fault);
    if (id.x >= destinationSize.x || id.y >= destinationSize.y)
        return;
    const float depth = Depths_load(0u, int3(id.xy, 0), fault);
    CopyOutputs_store(0u, id.xy, float4(depth, depth, depth, 1.0), fault);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void depthPyramidReduceMain(uint3 id : SV_DispatchThreadID)
{
    bool fault;
    const uint2 destinationSize = ReduceOutputs_query(0u, fault);
    if (id.x >= destinationSize.x || id.y >= destinationSize.y)
        return;
    const uint2 sourceSize = Sources_query(0u, fault);
    const uint2 baseCoord = id.xy * 2u;
    const float2 d00 = loadDepthPyramidRange(baseCoord, sourceSize);
    const float2 d10 = loadDepthPyramidRange(baseCoord + uint2(1u, 0u), sourceSize);
    const float2 d01 = loadDepthPyramidRange(baseCoord + uint2(0u, 1u), sourceSize);
    const float2 d11 = loadDepthPyramidRange(baseCoord + uint2(1u, 1u), sourceSize);
    const float maxDepth = max(max(d00.x, d10.x), max(d01.x, d11.x));
    const float minDepth = min(min(d00.y, d10.y), min(d01.y, d11.y));
    ReduceOutputs_store(0u, id.xy, float4(maxDepth, minDepth, maxDepth, 1.0), fault);
}

// Independent buffer receipt for both exact mip views. Eight uint lanes must
// be declared by the original Results buffer; dispatch index2 latches a fault.
[shader("compute")]
[numthreads(1, 1, 1)]
void depthPyramidReadMain(uint3 id : SV_DispatchThreadID)
{
    bool fault;
    const float4 value = id.x == 0u ? Sources_load(0u, int3(0, 0, 0), fault)
        : ReducedSources_load(id.x - 1u, int3(0, 0, 0), fault);
    Results_store(0u, id.x * 4u, asuint(value.x), fault);
    Results_store(0u, id.x * 4u + 1u, asuint(value.y), fault);
    Results_store(0u, id.x * 4u + 2u, asuint(value.z), fault);
    Results_store(0u, id.x * 4u + 3u, asuint(value.w), fault);
}
