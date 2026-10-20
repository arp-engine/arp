#include "renderer/ddgi_depth_cache.slang"

struct SamplingConstants
{
    float4 originNormalBias;
    float4 spacingViewBias;
    uint4 probeCount;
    int4 ringOffset;
};
[[vk::binding(0, 0)]] ConstantBuffer<SamplingConstants> Input;
[[vk::binding(1, 0)]] RWStructuredBuffer<float4> Output;
[[vk::binding(2, 0)]] RWTexture2D<float4> Cache;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    if (thread.x == 0u) Output[0] = Input.originNormalBias;
    float depth = 0.95 + 0.049 * float(thread.x) / 255.0;
    Cache[uint2(thread.x, 0)] = float4(0.0, 0.0, 0.0, ddgiEncodeCacheDepth(depth));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void cacheReadMain(uint3 thread : SV_DispatchThreadID)
{
    float depth = 0.95 + 0.049 * float(thread.x) / 255.0;
    float stored = Cache[uint2(thread.x, 0)].a;
    Output[thread.x + 1u] = float4(
        ddgiCacheDepthDistance(stored, depth),
        ddgiCacheDepthDistance(stored, depth - 0.001),
        abs(stored - depth), 1.0);
}
