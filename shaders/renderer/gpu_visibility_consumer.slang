// VisibilityConsumer/2 composes the original four producer kernels. Each
// indexed-indirect record has an explicit cascade/group selector; no DrawID,
// fixed record index or host-authored instance base substitutes for Mapping16.
struct ArpVisibilityDrawPushConstants
{
    uint cascade;
    uint group;
    uint reserved0;
    uint reserved1;
};
[[vk::push_constant]] ArpVisibilityDrawPushConstants VisibilityDraw;

// VisibilityConsumer/2 is a closed read-only consumer. These are aliases of
// the original producer buffers, authenticated at the same exact byte ranges
// by RootConsumer/2. No separate allocation, snapshot or state graph exists.
// Four producer passes, frozen input actors, exact dispatch coverage, original
// barriers and matching push/draw records are mandatory cold admission inputs.
// Every potentially failing resource operation remains in Compute and latches
// the original fault before Finalize suppresses the whole indirect workload.
[[vk::binding(0, 2)]] StructuredBuffer<ArpBindlessRecord_VisibilityGroups> VisibilityConsumerGroups;
[[vk::binding(1, 2)]] StructuredBuffer<ArpBindlessRecord_VisibilityVisible> VisibilityConsumerInstances;
[[vk::binding(2, 2)]] StructuredBuffer<ArpBindlessRecord_VisibilityBases> VisibilityConsumerMappings;
[[vk::binding(3, 2)]] ConstantBuffer<ArpBindlessRecord_VisibilityParameters> VisibilityConsumerParameters;

// This function is not an indexed table API and issues no fault authority.
// It is only reachable through the compiler-pinned consumer entry below. The
// original geometry join proves the same parameters, group selector, mapping,
// instance base mode, initialized ranges and producer output correspondence.
void arpVisibilityLoadProvenConsumer(uint instanceId, out ArpInstanceDataGpu instance,
    out ArpGpuVisibilityDrawGroup group, out ArpGpuVisibilityParameters parameters)
{
    parameters = arpVisibilityDecodeParameters(VisibilityConsumerParameters);
    uint slot = VisibilityDraw.cascade * parameters.capacities.y + VisibilityDraw.group;
    uint4 mapping = VisibilityConsumerMappings[slot].mapping;
    uint logicalInstance = parameters.counts.w != 0u ? instanceId : mapping.x + instanceId;
    uint physical = VisibilityDraw.cascade * parameters.capacities.x + logicalInstance;
    group = arpVisibilityDecodeGroup(VisibilityConsumerGroups[VisibilityDraw.group]);
    ArpBindlessRecord_VisibilityVisible visible = VisibilityConsumerInstances[physical];
    instance.positionScale = visible.positionScale;
    instance.yawPhaseIdLod = visible.yawPhaseIdLod;
    instance.extraTransform = visible.extraTransform;
    instance.speciesLodState = visible.speciesLodState;
}

// The closed consumer entry exercises real vertex fetch plus GPU-produced
// instance addressing. Material shading families compose the same helper;
// this entry's color is the declared group base color, not a PBR replacement.
struct ArpVisibilityVertex
{
    float4 position : SV_Position;
    nointerpolation float4 color : COLOR0;
    nointerpolation uint material : TEXCOORD0;
};

[shader("vertex")]
ArpVisibilityVertex visibilityVertexMain(float3 position : POSITION,
    uint instanceId : SV_InstanceID)
{
    ArpVisibilityVertex output;
    output.position = float4(0.0, 0.0, 2.0, 1.0);
    output.color = float4(0.0);
    output.material = 0u;
    ArpInstanceDataGpu instance;
    ArpGpuVisibilityDrawGroup group;
    ArpGpuVisibilityParameters p;
    arpVisibilityLoadProvenConsumer(instanceId, instance, group, p);
    float4 world = float4(arpTransformInstancePosition(instance, position), 1.0);
    output.position = float4(dot(p.viewProjectionRows[0], world),
        dot(p.viewProjectionRows[1], world), dot(p.viewProjectionRows[2], world),
        dot(p.viewProjectionRows[3], world));
    output.color = group.baseColor;
    output.material = group.materialIndex;
    return output;
}

[shader("fragment")]
float4 visibilityFragmentMain(ArpVisibilityVertex input) : SV_Target0
{
    return input.color;
}
