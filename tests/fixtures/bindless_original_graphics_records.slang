struct RootGraphicsRecordsVertex {
    float4 position : SV_Position;
    nointerpolation uint fault : TEXCOORD0;
};

bool validInstance(ArpBindlessRecord_Instances instance, uint index, uint element) {
    return all(instance.positionScale == float4(float(index + 1u), float(element + 2u), 3.0, 1.0))
        && all(instance.yawPhaseIdLod == float4(0.125, 0.25, float(index), float(element)))
        && all(instance.extraTransform == float4(5.0, 6.0, 7.0, 8.0))
        && all(asuint(instance.speciesLodState) == uint4(0x00040302u + index + element, 0x87654321u, 0u, 0u));
}

[shader("vertex")]
RootGraphicsRecordsVertex vertexMain(uint vertexId : SV_VertexID) {
    uint index = vertexId % 2u;
    bool fault;
    ArpBindlessRecord_Materials material = Materials_load(index, fault);
    bool failed = fault;
    uint count = Instances_query(index, fault); failed = failed || fault;
    ArpBindlessRecord_Instances instance = Instances_load(index, index, fault);
    failed = failed || fault || count != 2u || any(material.resources != uint4(index, 5u, 7u, 9u)) || !validInstance(instance, index, index);
    RootGraphicsRecordsVertex output;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(position, 0.0, 1.0);
    output.fault = failed ? 1u : 0u;
    return output;
}

float4 shadeRootRecords(float4 pixelPosition, uint vertexFault, bool injectFault) {
    if (vertexFault != 0u) { arpBindlessReportFault(); return float4(1.0, 0.0, 1.0, 1.0); }
    uint index = uint(pixelPosition.x) % 2u;
    uint element = uint(pixelPosition.y);
    bool fault;
    ArpBindlessRecord_Materials material = Materials_load(index, fault);
    bool failed = fault || any(material.resources != uint4(index, 5u, 7u, 9u));
    uint count = Instances_query(index, fault); failed = failed || fault;
    ArpBindlessRecord_Instances instance = Instances_load(index, element, fault);
    failed = failed || fault || count != 2u || !validInstance(instance, index, element);
    uint outputs = Outputs_query(index, fault); failed = failed || fault || outputs != 2u;
    ArpBindlessRecord_Outputs value;
    value.color = material.color;
    value.metadata = asuint(instance.speciesLodState);
    value.signedValues = int4(-1 - int(index), -2 - int(element), 7, -9);
    Outputs_store(index, element, value, fault); failed = failed || fault;
    ArpBindlessRecord_Outputs echoed = Outputs_load(index, element, fault);
    failed = failed || fault || any(echoed.color != value.color)
        || any(echoed.metadata != value.metadata) || any(echoed.signedValues != value.signedValues);
    if (injectFault) {
        ArpBindlessRecord_Instances rejected = Instances_load(index, count, fault);
        failed = failed || !fault || any(rejected.positionScale != 0.0) || any(rejected.speciesLodState != 0.0);
        Outputs_store(index, outputs, value, fault); failed = failed || !fault;
        Outputs_store(2u, element, value, fault); failed = failed || !fault;
        ArpBindlessRecord_Materials missing = Materials_load(2u, fault);
        failed = failed || !fault || any(missing.color != 0.0) || any(missing.resources != 0u);
    }
    return failed ? float4(1.0, 0.0, 1.0, 1.0) : material.color;
}

[shader("fragment")]
float4 cleanFragment(RootGraphicsRecordsVertex input) : SV_Target0 { return shadeRootRecords(input.position, input.fault, false); }

[shader("fragment")]
float4 faultFragment(RootGraphicsRecordsVertex input) : SV_Target0 { return shadeRootRecords(input.position, input.fault, true); }

// Fragment-only preparation/execution profile: no indexed Vertex access or
// writable Vertex fault sink. The indexed Vertex entry above remains explicit
// and must pass its own provenance gate before it can be selected.
[shader("vertex")]
RootGraphicsRecordsVertex vertexUnindexed(uint vertexId : SV_VertexID) {
    RootGraphicsRecordsVertex output;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(position, 0.0, 1.0);
    output.fault = 0u;
    return output;
}
