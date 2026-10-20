#include "common/forward_plus_lighting.slang"

struct ForwardPlusDebugConstants
{
    uint2 tileCount;
    uint expectedCountMode;
    uint padding;
};

[[vk::binding(3, 0)]]
ConstantBuffer<ForwardPlusDebugConstants> ForwardPlusDebugParams;

[[vk::binding(0, 0)]] StructuredBuffer<ForwardPlusTileHeader> ForwardPlusHeaders;
[[vk::binding(1, 0)]] StructuredBuffer<uint> ForwardPlusExpectedCounts;
[[vk::binding(2, 0)]] RWTexture2D<float4> ForwardPlusDebugOutput;

[numthreads(8, 8, 1)]
[shader("compute")]
void forwardPlusDebugMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 tile = dispatchThreadId.xy;
    if (tile.x >= ForwardPlusDebugParams.tileCount.x
        || tile.y >= ForwardPlusDebugParams.tileCount.y)
    {
        return;
    }

    uint tileIndex = tile.y * ForwardPlusDebugParams.tileCount.x + tile.x;
    uint actualCount = ForwardPlusHeaders[tileIndex].count;
    uint expectedCount = ForwardPlusExpectedCounts[tileIndex];
    float matches = actualCount == expectedCount ? 1.0 : 0.0;
    ForwardPlusDebugOutput[tile] = float4(matches, matches, matches, matches);
}
