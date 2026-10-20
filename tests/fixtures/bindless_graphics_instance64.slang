// Explicit source library: shaders/core/instance_transform.slang.

[[vk::binding(0, 0)]] StructuredBuffer<ArpInstanceDataGpu> Instances;
struct InstanceProbe
{
    float4 position;
    float4 normal;
    float4 tangent;
    uint4 selectors;
    uint4 offsets;
    float4 windWeightsAndHeight;
    ArpFoliageWindField currentWind;
    ArpFoliageWindField previousWind;
};
[[vk::binding(1, 0)]] ConstantBuffer<InstanceProbe> Probe;

struct InstanceVertex
{
    float4 position : SV_Position;
    nointerpolation float4 transformed : TEXCOORD0;
    nointerpolation float4 deformed : TEXCOORD1;
};

float3 deformInstance(ArpInstanceDataGpu instance, ArpFoliageWindField wind,
    float heightReference, float stiffness)
{
    float3 local = float3(Probe.position.x, Probe.windWeightsAndHeight.w, Probe.position.z);
    return arpDeformFoliagePosition(arpTransformInstancePosition(instance, Probe.position.xyz),
        local, float3(Probe.windWeightsAndHeight.xy, stiffness), instance.yawPhaseIdLod.yw,
        instance.positionScale.xyz, wind, heightReference);
}
[shader("vertex")]
InstanceVertex vertexMain(uint vertexId : SV_VertexID)
{
    InstanceVertex result;
    float2 position = vertexId == 0u ? float2(-1.0, -1.0)
        : (vertexId == 1u ? float2(3.0, -1.0) : float2(-1.0, 3.0));
    result.position = float4(position, 0.0, 1.0);
    result.transformed = float4(arpTransformInstancePosition(
        Instances[Probe.selectors.x + Probe.selectors.y], Probe.position.xyz), 1.0);
    result.deformed = float4(deformInstance(Instances[Probe.selectors.x + Probe.selectors.y],
        Probe.currentWind, 0.5, Probe.windWeightsAndHeight.z), 1.0);
    return result;
}

float4 instanceFragment(InstanceVertex input, bool injectFault)
{
    uint pixel = uint(input.position.y) * 2u + uint(input.position.x);
    ArpInstanceDataGpu instance = Instances[Probe.selectors.x + Probe.offsets[pixel]];
    float4 position = float4(arpTransformInstancePosition(instance, Probe.position.xyz), 1.0);
    float4 normal = float4(arpTransformInstanceNormal(instance, Probe.normal.xyz), 0.0);
    float4 tangent = arpTransformInstanceTangent(instance, Probe.tangent);
    uint4 metadata = arpInstanceMetadata(instance);
    float4 grass = float4(deformInstance(instance, Probe.currentWind, 0.5,
        Probe.windWeightsAndHeight.z), 1.0);
    float4 previous = float4(deformInstance(instance, Probe.previousWind, 0.5,
        Probe.windWeightsAndHeight.z), 1.0);
    float4 canopy = float4(deformInstance(instance, Probe.currentWind, 2.0,
        Probe.windWeightsAndHeight.z), 1.0);
    float4 rigid = float4(deformInstance(instance, Probe.currentWind, 0.5, 1.0), 1.0);
    bool fault;
    for (uint lane = 0u; lane < 4u; ++lane) {
        Results_store(0u, pixel * 44u + lane, asuint(input.transformed[lane]), fault);
        Results_store(0u, pixel * 44u + 4u + lane, asuint(position[lane]), fault);
        Results_store(0u, pixel * 44u + 8u + lane, asuint(normal[lane]), fault);
        Results_store(0u, pixel * 44u + 12u + lane, asuint(tangent[lane]), fault);
        Results_store(0u, pixel * 44u + 16u + lane, metadata[lane], fault);
        Results_store(0u, pixel * 44u + 20u + lane, asuint(instance.yawPhaseIdLod[lane]), fault);
        Results_store(0u, pixel * 44u + 24u + lane, asuint(input.deformed[lane]), fault);
        Results_store(0u, pixel * 44u + 28u + lane, asuint(grass[lane]), fault);
        Results_store(0u, pixel * 44u + 32u + lane, asuint(previous[lane]), fault);
        Results_store(0u, pixel * 44u + 36u + lane, asuint(canopy[lane]), fault);
        Results_store(0u, pixel * 44u + 40u + lane, asuint(rigid[lane]), fault);
    }
    if (injectFault) {
        Results_store(1u, 0u, 0xffffffffu, fault);
        Results_store(0u, 176u, 0xffffffffu, fault);
    }
    return float4(0.25, 0.5, 0.75, 1.0);
}
[shader("fragment")]
float4 cleanFragment(InstanceVertex input) : SV_Target0
{
    return instanceFragment(input, false);
}
[shader("fragment")]
float4 faultFragment(InstanceVertex input) : SV_Target0
{
    return instanceFragment(input, true);
}
