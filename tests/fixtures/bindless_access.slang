[[vk::binding(12, 0)]] RWStructuredBuffer<uint> FaultOutput;
[shader("compute")][numthreads(8,1,1)]
void computeMain(uint3 tid : SV_DispatchThreadID) {
    bool fault; bool failed = false;
    uint i = tid.x;
    if (i >= 5u) return;
    if (i == 4u) {
        uint mask = 0;
        Inputs_load(2u, 0u, fault); mask |= fault ? 1u : 0u;
        Counters_atomicAdd(2u, 0u, 100u, fault); mask |= fault ? 2u : 0u;
        Outputs_store(2u, uint2(0u), 100.0, fault); mask |= fault ? 4u : 0u;
        Constants_load(2u, fault); mask |= fault ? 8u : 0u;
        Inputs_query(2u, fault); mask |= fault ? 16u : 0u;
        FaultOutput[i] = mask;
        return;
    }
    if (i == 3u) {
        uint mask = 0;
        Inputs_load(0u, 1u, fault); mask |= fault ? 1u : 0u;
        Counters_atomicAdd(0u, 1u, 100u, fault); mask |= fault ? 2u : 0u;
        Outputs_store(0u, uint2(1u), 100.0, fault); mask |= fault ? 4u : 0u;
        FaultOutput[i] = mask;
        return;
    }
    float4 c = Constants_load(i, fault); failed = failed || fault;
    uint n = Inputs_query(i, fault); failed = failed || fault;
    uint v = Inputs_load(i, 0, fault); failed = failed || fault;
    uint2 size = Outputs_query(i, fault); failed = failed || fault;
    float p = Outputs_load(i, uint2(0), fault); failed = failed || fault;
    Outputs_store(i, uint2(0), p + c.x, fault); failed = failed || fault;
    uint count = Counters_query(i, fault); failed = failed || fault;
    uint old = Counters_load(i, 0, fault); failed = failed || fault;
    Counters_store(i, 0, v + n + count + size.x + old, fault); failed = failed || fault;
    uint prior = Counters_atomicAdd(i, 0, 1, fault); failed = failed || fault;
    FaultOutput[i] = uint(failed) + prior;
}
[shader("fragment")]
float4 fragmentMain(float4 pos : SV_Position) : SV_Target {
    bool fault; bool failed = false;
    uint i = uint(pos.x) % 2u; uint s = uint(pos.y) % 2u;
    float4 a = Images_load(i, int3(0, 0, 1), fault); failed = failed || fault;
    uint2 size = Images_query(i, fault); failed = failed || fault;
    float2 uv = pos.xy / float2(size) * 0.75 + 0.1;
    float2 dx = ddx(uv) * 8.0; float2 dy = ddy(uv) * 8.0;
    float4 b = Images_sampleLevel(i, s, uv, 0.25, fault); failed = failed || fault;
    float4 c = Images_sampleGrad(i, s, uv, dx, dy, fault); failed = failed || fault;
    return failed ? float4(1,0,1,1) : (a + b + c) / 3.0;
}
