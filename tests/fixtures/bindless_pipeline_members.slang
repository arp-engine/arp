struct FrameRecord {
    float4 tint;
    uint4 flags;
};
struct InstanceRecord {
    uint4 indices;
    float4 weights;
};
[[vk::binding(3, 0)]] ConstantBuffer<FrameRecord> FrameValues;
[[vk::binding(3, 2)]] StructuredBuffer<InstanceRecord> SharedValues;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> PassValues;

[shader("compute")][numthreads(1,1,1)]
void computeMain(uint3 tid : SV_DispatchThreadID)
{
    bool fault;
    InstanceRecord instance = SharedValues[tid.x];
    uint value = uint(FrameValues.tint.x) + FrameValues.flags.x
        + instance.indices.x + uint(instance.weights.x);
    IndexedValues_store(tid.x & 1u, 0u, value, fault);
    PassValues[0] = value + uint(fault);
}

[shader("fragment")]
float4 fragmentMain(float4 position : SV_Position) : SV_Target
{
    bool fault;
    uint value = IndexedValues_load(uint(position.x) & 1u, 0u, fault);
    PassValues[1] = value;
    return FrameValues.tint + float4(float(value + FrameValues.flags.x), float(fault), 0.0, 1.0);
}
