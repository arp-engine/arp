struct RootGraphicsBufferVertex {
    float4 position : SV_Position;
    nointerpolation uint fault : TEXCOORD0;
};

[shader("vertex")]
RootGraphicsBufferVertex vertexMain(uint vertexId : SV_VertexID) {
    uint index = vertexId % 2u;
    bool fault;
    float4 globals = Globals_load(index, fault);
    bool failed = fault;
    uint count = Inputs_query(index, fault); failed = failed || fault;
    uint value = Inputs_load(index, 0u, fault); failed = failed || fault;
    failed = failed || count != 1u || value != 10u * (index + 1u) || globals.w != 1.0;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    RootGraphicsBufferVertex output;
    output.position = float4(position, 0.0, 1.0);
    output.fault = failed ? 1u : 0u;
    return output;
}

float4 shadeRootBuffers(float4 pixelPosition, uint vertexFault, bool injectFault) {
    if (vertexFault != 0u) {
        arpBindlessReportFault();
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    uint index = uint(pixelPosition.x) % 2u;
    bool fault;
    float4 globals = Globals_load(index, fault);
    bool failed = fault;
    uint count = Inputs_query(index, fault); failed = failed || fault;
    uint value = Inputs_load(index, 0u, fault); failed = failed || fault;
    uint counterCount = Counters_query(index, fault); failed = failed || fault;
    uint before = Counters_atomicAdd(index, 0u, 1u, fault); failed = failed || fault;
    // Each pixel owns a different word; the shared counter is atomic. No
    // ordering assumption between fragment invocations is used for readback.
    uint outputWord = 1u + uint(pixelPosition.y);
    Counters_store(index, outputWord, value, fault); failed = failed || fault;
    uint echoed = Counters_load(index, outputWord, fault); failed = failed || fault;
    failed = failed || count != 1u || counterCount != 3u || echoed != value
        || value != 10u * (index + 1u) || before < 100u * (index + 1u);
    if (injectFault) {
        // Reject both an inactive table index and element == buffer length.
        uint rejected = Inputs_load(2u, 0u, fault); failed = failed || !fault || rejected != 0u;
        rejected = Inputs_load(index, count, fault); failed = failed || !fault || rejected != 0u;
        rejected = Counters_atomicAdd(2u, 0u, 999u, fault); failed = failed || !fault || rejected != 0u;
        Counters_store(index, counterCount, 999u, fault); failed = failed || !fault;
    }
    return failed ? float4(1.0, 0.0, 1.0, 1.0) : globals;
}

[shader("fragment")]
float4 cleanFragment(RootGraphicsBufferVertex input) : SV_Target0 {
    return shadeRootBuffers(input.position, input.fault, false);
}

[shader("fragment")]
float4 faultFragment(RootGraphicsBufferVertex input) : SV_Target0 {
    return shadeRootBuffers(input.position, input.fault, true);
}

// Fragment-only preparation/execution profile: no indexed Vertex access or
// writable Vertex fault sink. The indexed Vertex entry above remains explicit
// and must pass its own provenance gate before it can be selected.
[shader("vertex")]
RootGraphicsBufferVertex vertexUnindexed(uint vertexId : SV_VertexID) {
    RootGraphicsBufferVertex output;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(position, 0.0, 1.0);
    output.fault = 0u;
    return output;
}
