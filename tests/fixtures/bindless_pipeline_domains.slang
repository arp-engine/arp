// Binding3 deliberately repeats across sets0,1,2. Only the full pair is an ABI
// identity. Frame/GI-like explicit buffers coexist with the indexed domain.
[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(3, 2)]] StructuredBuffer<uint> SharedValues;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> PassValues;

void updateIndexed(uint tableIndex, uint outputIndex)
{
    bool fault;
    uint value = uint(FrameValues.x) + SharedValues[0];
    IndexedValues_store(tableIndex, 0u, value, fault);
    PassValues[outputIndex] = value + uint(fault);
}

[shader("compute")][numthreads(1,1,1)]
void computeMain(uint3 tid : SV_DispatchThreadID) { updateIndexed(tid.x & 1u, tid.x); }

[shader("compute")][numthreads(1,1,1)]
void faultMain(uint3 tid : SV_DispatchThreadID) { updateIndexed(tid.x == 0u ? 0u : 2u, tid.x); }

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    bool fault;
    uint value = IndexedValues_load(uint(position.x) & 1u, 0u, fault);
    PassValues[1] = value;
    return FrameValues + float4(float(value), float(fault), 0.0, 1.0);
}
