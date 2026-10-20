// Binding3 repeats in three sets; Vertex and Fragment consume ordinary inputs.
[[vk::binding(3, 0)]] ConstantBuffer<float4> FrameValues;
[[vk::binding(3, 2)]] StructuredBuffer<uint> SharedValues;
[[vk::binding(4, 2)]] RWStructuredBuffer<uint> PassValues;

struct DomainVertex {
    float4 position : SV_Position;
    nointerpolation uint sum : TEXCOORD0;
};

[shader("vertex")]
DomainVertex vertexMain(uint vertexId : SV_VertexID) {
    float2 p = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    DomainVertex output;
    output.position = float4(p, 0.0, 1.0);
    output.sum = uint(FrameValues.x) + SharedValues[0];
    return output;
}

float4 shadeDomains(DomainVertex input, bool injectFault) {
    uint index = uint(input.position.x) & 1u;
    bool fault;
    uint value = IndexedValues_load(index, 0u, fault);
    bool failed = fault || input.sum != 49u || value != 777u + index;
    uint sum = uint(FrameValues.x) + SharedValues[0];
    failed = failed || sum != 49u;
    // Each pixel owns one ordinary output word; no invocation ordering assumed.
    PassValues[uint(input.position.y) * 2u + uint(input.position.x)] = value + input.sum + sum;
    if (injectFault) {
        uint rejected = IndexedValues_load(2u, 0u, fault);
        failed = failed || !fault || rejected != 0u;
    }
    return failed ? float4(1.0, 0.0, 1.0, 1.0)
        : (index == 0u ? float4(1.0, 0.0, 0.0, 1.0) : float4(0.0, 1.0, 0.0, 1.0));
}

[shader("fragment")]
float4 cleanFragment(DomainVertex input) : SV_Target0 { return shadeDomains(input, false); }

[shader("fragment")]
float4 faultFragment(DomainVertex input) : SV_Target0 { return shadeDomains(input, true); }
