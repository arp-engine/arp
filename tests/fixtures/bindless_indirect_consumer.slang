struct IndirectConsumerVertex {
    float4 position : SV_Position;
    nointerpolation uint logicalInstance : TEXCOORD0;
    nointerpolation uint instanceFault : TEXCOORD1;
};

[shader("vertex")]
IndirectConsumerVertex vertexMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID) {
    IndirectConsumerVertex output;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0);
    output.position = float4(position, 0.0, 1.0);
    uint logicalInstance;
    bool valid = arpBindlessLoadIndirectInstance(instanceId, logicalInstance);
    output.logicalInstance = logicalInstance;
    output.instanceFault = valid ? 0u : 1u;
    return output;
}

[shader("fragment")]
float4 fragmentMain(IndirectConsumerVertex input) : SV_Target0 {
    if (input.instanceFault != 0u) {
        arpBindlessReportFault();
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    bool fault;
    return Images_load(input.logicalInstance, int3(0, 0, 0), fault);
}
