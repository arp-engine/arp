void updateCounter(uint index) {
    bool fault;
    float4 globals = Globals_load(index, fault);
    uint inputCount = Inputs_query(index, fault);
    uint input = Inputs_load(index, 0u, fault);
    uint count = Counters_query(index, fault);
    uint old = Counters_load(index, 0u, fault);
    Counters_store(index, 0u, old + input + uint(globals.x) + inputCount + count, fault);
    Counters_atomicAdd(index, 0u, 1u, fault);
}

[shader("compute")]
[numthreads(2, 1, 1)]
void cleanMain(uint3 tid : SV_DispatchThreadID) {
    updateCounter(1u - tid.x);
}

[shader("compute")]
[numthreads(4, 1, 1)]
void faultMain(uint3 tid : SV_DispatchThreadID) {
    if (tid.x < 2u) {
        updateCounter(1u - tid.x);
    } else {
        bool fault;
        uint tableIndex = tid.x == 2u ? 2u : 0u;
        uint elementIndex = tid.x == 2u ? 0u : 1u;
        Inputs_load(tableIndex, elementIndex, fault);
        Counters_store(tableIndex, elementIndex, 999u, fault);
        Counters_atomicAdd(tableIndex, elementIndex, 999u, fault);
        if (tid.x == 2u) Globals_load(tableIndex, fault);
    }
}
