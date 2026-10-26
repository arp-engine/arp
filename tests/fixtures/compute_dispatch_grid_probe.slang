struct DispatchGridProbeConstants {
    uint maxX;
    uint maxY;
    uint maxZ;
    uint reserved;
};
[[vk::binding(0, 0)]] ConstantBuffer<DispatchGridProbeConstants> Probe;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> Output;
groupshared uint participants[64];

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 group : SV_GroupID, uint lane : SV_GroupIndex)
{
    if (lane == 0u)
        for (uint i = 0u; i < 64u; ++i) participants[i] = 0u;
    GroupMemoryBarrierWithGroupSync();
    participants[lane] = 1u;
    GroupMemoryBarrierWithGroupSync();
    if (lane == 0u) {
        uint count = 0u;
        for (uint i = 0u; i < 64u; ++i) count += participants[i];
        uint index = (group.z * Probe.maxY + group.y) * Probe.maxX + group.x;
        Output[index] = count + 128u * index;
    }
}
