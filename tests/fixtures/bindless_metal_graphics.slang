struct GraphicsFixtureVertex {
    float4 position : SV_Position;
    nointerpolation uint fault : TEXCOORD0;
};

[shader("vertex")]
GraphicsFixtureVertex vertexMain(uint vertexId : SV_VertexID) {
    uint index = vertexId % 2u;
    bool fault;
    bool failed = false;
    float4 globals = Globals_load(index, fault); failed = failed || fault;
    uint count = Inputs_query(index, fault); failed = failed || fault;
    uint value = Inputs_load(index, 0u, fault); failed = failed || fault;
    failed = failed || count != 1u || value != 10u * (index + 1u) || globals.w != 1.0;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    GraphicsFixtureVertex output;
    output.position = float4(position, 0.0, 1.0);
    output.fault = failed ? 1u : 0u;
    return output;
}

float4 shadeFragment(GraphicsFixtureVertex input, bool injectFault) {
    if (input.fault != 0u) {
        arpBindlessReportFault();
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    bool fault;
    if (injectFault) {
        Inputs_load(2u, 0u, fault);
        Counters_atomicAdd(2u, 0u, 999u, fault);
    }
    uint index = uint(input.position.x) % 2u;
    bool failed = false;
    float4 globals = Globals_load(index, fault); failed = failed || fault;
    uint count = Inputs_query(index, fault); failed = failed || fault;
    uint value = Inputs_load(index, 0u, fault); failed = failed || fault;
    uint counterCount = Counters_query(index, fault); failed = failed || fault;
    uint before = Counters_atomicAdd(index, 0u, 1u, fault); failed = failed || fault;
    failed = failed || count != 1u || counterCount != 1u
        || value != 10u * (index + 1u) || before < 100u * (index + 1u);
    return failed ? float4(1.0, 0.0, 1.0, 1.0) : globals;
}

[shader("fragment")]
float4 cleanFragment(GraphicsFixtureVertex input) : SV_Target0 {
    return shadeFragment(input, false);
}

[shader("fragment")]
float4 faultFragment(GraphicsFixtureVertex input) : SV_Target0 {
    return shadeFragment(input, true);
}
