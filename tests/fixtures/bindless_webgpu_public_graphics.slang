[shader("compute")]
[numthreads(1, 1, 1)]
void instanceBaseMain() {
    bool fault;
    uint firstInstance = MappingWords_load(0u, 0u, fault);
    uint instanceCount = MappingWords_load(0u, 1u, fault);
    uint capacity = MappingWords_load(0u, 2u, fault);
    uint count = MappingWords_query(0u, fault);
    if (fault || count != 4u) {
        arpBindlessReportFault();
        return;
    }
    ArpBindlessIndirectCommand physical;
    uint4 mapping;
    arpBindlessEncodeIndirectInstanceBase(3u, instanceCount, 0u, 0, firstInstance,
        capacity, physical, mapping);
    MappingWords_store(0u, 0u, mapping.x, fault);
    MappingWords_store(0u, 1u, mapping.y, fault);
    MappingWords_store(0u, 2u, mapping.z, fault);
    MappingWords_store(0u, 3u, mapping.w, fault);
}

struct BindlessGraphicsVertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint instanceId : TEXCOORD1;
    nointerpolation uint instanceFault : TEXCOORD2;
};

[shader("vertex")]
BindlessGraphicsVertexOutput vertexMain(uint vertexId : SV_VertexID,
    uint instanceId : SV_InstanceID) {
    BindlessGraphicsVertexOutput output;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    output.position = float4(position, 0.0, 1.0);
    output.uv = float2(position.x * 0.5 + 0.5, position.y * 0.5 + 0.5);
    uint logicalInstance;
    bool valid = arpBindlessLoadIndirectInstance(instanceId, logicalInstance);
    output.instanceId = logicalInstance;
    output.instanceFault = valid ? 0u : 1u;
    return output;
}

[shader("fragment")]
float4 fragmentMain(BindlessGraphicsVertexOutput input) : SV_Target0 {
    float2 gradientX = ddx(input.uv);
    float2 gradientY = ddy(input.uv);
    if (input.instanceFault != 0u) {
        arpBindlessReportFault();
        return float4(abs(gradientX.x), abs(gradientY.y), 0.0, 1.0);
    }
    uint index = input.uv.x < 0.5 ? 0u : 1u;
    bool fault;
    uint materialIndex = Materials_load(index, 0u, fault);
    uint materialCount = Materials_query(index, fault);
    uint counterBefore = Counters_load(index, 0u, fault);
    uint counterCount = Counters_query(index, fault);
    uint counterPrevious = Counters_atomicAdd(index, 0u, 1u, fault);
    Counters_store(index, 0u, counterPrevious + 2u, fault);
    uint2 dimensions = Images_query(index, fault);
    float4 value = Images_load(index, int3(0, 0, 0), fault);
    if (materialIndex != index || materialCount != 1u
        || counterBefore != (index == 0u ? 10u : 20u)
        || counterCount != 1u || counterPrevious != counterBefore
        || dimensions.x != 2u || dimensions.y != 1u || value.w != 1.0) {
        return float4(1.0, 0.0, 1.0, 1.0);
    }
    uint samplerIndex = index == 0u ? 1u : 0u;
    float2 sampleUv = index == 0u ? float2(0.5, 0.5) : float2(0.75, 0.5);
    float4 sampled = Images_sampleLevel(index, samplerIndex, sampleUv, 0.0, fault);
    float4 sampledGrad = Images_sampleGrad(index, samplerIndex, sampleUv,
        gradientX, gradientY, fault);
    float4 factor = Factors_load(index, fault);
    return fault || any(abs(sampled - sampledGrad) > 0.01)
        ? float4(1.0, 0.0, 1.0, 1.0)
        : sampled * factor * (input.instanceId == 0u ? 1.0
            : input.instanceId == 1u ? 0.5 : 0.0);
}
