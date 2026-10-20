#include "core/instance_transform.slang"
#include "core/gpu_visibility.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ArpGpuVisibilityDrawGroup> Groups;
[[vk::binding(1, 0)]] RWStructuredBuffer<ArpGpuIndexedIndirectCommand> Draws;
[[vk::binding(2, 0)]] RWStructuredBuffer<ArpGpuIndirectInstanceBase> Bases;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint4> Results;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x != 0u) return;
    ArpGpuVisibilityDrawGroup group = Groups[0u];
    ArpGpuIndexedIndirectCommand draw;
    ArpGpuIndirectInstanceBase mapping;
    bool encoded = arpEncodeVisibilityDraw(group, 2u, 4u, 0u, false, draw, mapping);
    Draws[0u] = draw;
    Bases[0u] = mapping;
    uint logical = 0u;
    bool resolved = arpResolveVisibilityInstance(mapping, 1u, false, logical);
    uint slot = 0u;
    bool appended = arpVisibilityOutputIndex(group.globalFirstInstance,
        group.globalInstanceCount, 1u, 4u, slot);
    bool invalid = !arpVisibilityOutputIndex(group.globalFirstInstance,
        group.globalInstanceCount, group.globalInstanceCount, 4u, slot);
    Results[0u] = uint4(logical, encoded, resolved && appended, invalid);
    encoded = arpEncodeVisibilityDraw(group, 2u, 4u, 0u, true, draw, mapping);
    Draws[1u] = draw;
    Bases[1u] = mapping;
    resolved = arpResolveVisibilityInstance(mapping, group.globalFirstInstance + 1u, true, logical);
    Results[1u] = uint4(logical, encoded, resolved, group.materialIndex);
    encoded = arpEncodeVisibilityDraw(group, 2u, 4u, 1u, false, draw, mapping);
    Draws[2u] = draw;
    Bases[2u] = mapping;
    Results[2u] = uint4(draw.firstIndex, draw.indexCount, draw.firstInstance, encoded);
    encoded = arpEncodeVisibilityDraw(group, group.globalInstanceCount + 1u,
        4u, 0u, false, draw, mapping);
    Draws[3u] = draw;
    Bases[3u] = mapping;
    Results[3u] = uint4(draw.instanceCount, mapping.contractVersion, encoded, 0u);
}
