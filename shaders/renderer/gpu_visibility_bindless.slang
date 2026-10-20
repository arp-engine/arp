// VisibilityKernel/1 adapter over authenticated guarded table helpers.
// Layout conversion is explicit and bit-preserving: DrawGroup112, Instance64,
// Indexed20, Mapping16 and Parameters448. There are no physical resource names,
// descriptor-array assumptions or authored atomics in this source.
ArpGpuVisibilityParameters arpVisibilityDecodeParameters(ArpBindlessRecord_VisibilityParameters value) {
    ArpGpuVisibilityParameters result;
    result.counts = value.counts;
    result.capacities = value.capacities;
    result.cameraAndEnvelope = value.cameraAndEnvelope;
    result.selection = value.selection;
    result.frustumPlanes[0] = value.plane0;
    result.frustumPlanes[1] = value.plane1;
    result.frustumPlanes[2] = value.plane2;
    result.frustumPlanes[3] = value.plane3;
    result.frustumPlanes[4] = value.plane4;
    result.frustumPlanes[5] = value.plane5;
    result.frustumPlanes[6] = value.plane6;
    result.frustumPlanes[7] = value.plane7;
    result.frustumPlanes[8] = value.plane8;
    result.frustumPlanes[9] = value.plane9;
    result.frustumPlanes[10] = value.plane10;
    result.frustumPlanes[11] = value.plane11;
    result.frustumPlanes[12] = value.plane12;
    result.frustumPlanes[13] = value.plane13;
    result.frustumPlanes[14] = value.plane14;
    result.frustumPlanes[15] = value.plane15;
    result.frustumPlanes[16] = value.plane16;
    result.frustumPlanes[17] = value.plane17;
    result.viewProjectionRows[0] = value.row0;
    result.viewProjectionRows[1] = value.row1;
    result.viewProjectionRows[2] = value.row2;
    result.viewProjectionRows[3] = value.row3;
    result.depthInfo = value.depthInfo;
    result.policy = value.policy;
    return result;
}
ArpGpuVisibilityParameters arpVisibilityParameters() {
    bool fault;
    return arpVisibilityDecodeParameters(VisibilityParameters_load(0u, fault));
}
ArpGpuVisibilityDrawGroup arpVisibilityDecodeGroup(ArpBindlessRecord_VisibilityGroups value) {
    ArpGpuVisibilityDrawGroup result;
    result.templateId = value.head0.x;
    result.sectionIndex = value.head0.y;
    result.pipelineVariantId = value.head0.z;
    result.globalFirstInstance = value.head0.w;
    result.globalInstanceCount = value.head1.x;
    result.padding0 = value.head1.y;
    result.padding1 = value.head1.z;
    result.padding2 = value.head1.w;
    result.baseColor = value.baseColor;
    result.localBoundsCenterRadius = value.bounds;
    result.materialIndex = value.tail0.x;
    result.cullDistance = asfloat(value.tail0.y);
    result.useAlphaTest = value.tail0.z;
    result.doubleSided = value.tail0.w;
    result.useFullPbrFragment = value.tail1.x;
    result.singleSidedLighting = value.tail1.y;
    result.castsShadow = value.tail1.z;
    result.firstIndex = value.tail1.w;
    result.indexCount = value.tail2.x;
    result.shadowCascade1TemplateId = value.tail2.y;
    result.shadowCascade1FirstIndex = value.tail2.z;
    result.shadowCascade1IndexCount = value.tail2.w;
    return result;
}
ArpGpuVisibilityDrawGroup arpVisibilityGroup(uint index) {
    bool fault;
    return arpVisibilityDecodeGroup(VisibilityGroups_load(0u, index, fault));
}
ArpInstanceDataGpu arpVisibilityDecodeInstance(ArpBindlessRecord_VisibilityInstances value) {
    ArpInstanceDataGpu result;
    result.positionScale = value.positionScale;
    result.yawPhaseIdLod = value.yawPhaseIdLod;
    result.extraTransform = value.extraTransform;
    result.speciesLodState = value.speciesLodState;
    return result;
}
ArpInstanceDataGpu arpVisibilityInstance(uint index) {
    bool fault;
    return arpVisibilityDecodeInstance(VisibilityInstances_load(0u, index, fault));
}
uint arpVisibilityOwner(uint index) {
    bool fault;
    return VisibilityOwners_load(0u, index, fault);
}
uint arpVisibilityCount(uint index) {
    bool fault;
    return VisibilityCounts_load(0u, index, fault);
}
void arpVisibilityWriteCount(uint index, uint value) {
    bool fault;
    VisibilityCounts_store(0u, index, value, fault);
}
void arpVisibilityWriteControl(uint index, uint value) {
    bool fault;
    VisibilityControl_store(0u, index, value, fault);
}
void arpVisibilityWriteInstance(uint index, ArpInstanceDataGpu value) {
    bool fault;
    ArpBindlessRecord_VisibilityVisible record;
    record.positionScale = value.positionScale;
    record.yawPhaseIdLod = value.yawPhaseIdLod;
    record.extraTransform = value.extraTransform;
    record.speciesLodState = value.speciesLodState;
    VisibilityVisible_store(0u, index, record, fault);
}
void arpVisibilityWriteDraw(uint index, ArpGpuIndexedIndirectCommand value) {
    bool fault;
    // Host closure authenticates a uint32-addressable Indexed20 table.
    if (index > (0xffffffffu - 4u) / 5u) { arpBindlessReportFault(); return; }
    uint word = index * 5u;
    VisibilityDraws_store(0u, word, value.indexCount, fault);
    VisibilityDraws_store(0u, word + 1u, value.instanceCount, fault);
    VisibilityDraws_store(0u, word + 2u, value.firstIndex, fault);
    VisibilityDraws_store(0u, word + 3u, asuint(value.vertexOffset), fault);
    VisibilityDraws_store(0u, word + 4u, value.firstInstance, fault);
}
void arpVisibilityWriteBase(uint index, ArpGpuIndirectInstanceBase value) {
    bool fault;
    ArpBindlessRecord_VisibilityBases record;
    record.mapping = uint4(value.firstInstance, value.instanceCount,
        value.instanceCapacity, value.contractVersion);
    VisibilityBases_store(0u, index, record, fault);
}
float4 arpVisibilityDepth(int3 location) {
    bool fault;
    return VisibilityDepth_load(0u, location, fault);
}
uint arpVisibilityReserve(uint index) {
    bool fault;
    return VisibilityCounts_atomicAdd(0u, index, 1u, fault);
}
void arpVisibilityFault() {
    bool fault;
    VisibilityControl_atomicOr(0u, 0u, 1u, fault);
    arpBindlessReportFault();
}
uint arpVisibilityFaultBits() {
    bool fault;
    uint control = VisibilityControl_atomicOr(0u, 0u, 0u, fault);
    // Read the same immutable snapshot's descriptor fault as well as the
    // computation fault. CPU readback alone cannot suppress a GPU draw.
    return control | arpBindlessFaultBits();
}
// These are the frozen original descriptor ranges, independent of the
// parameter payload. The original closure authenticates slot zero of each
// output before recording. A failed parameter read must not preserve draws.
uint arpVisibilityDrawWords() { bool fault; return VisibilityDraws_query(0u, fault); }
uint arpVisibilityBaseSlots() { bool fault; return VisibilityBases_query(0u, fault); }
uint arpVisibilityControlWords() { bool fault; return VisibilityControl_query(0u, fault); }

[shader("compute")]
[numthreads(64, 1, 1)]
void visibilityResetComputeMain(uint3 id : SV_DispatchThreadID) { arpVisibilityResetKernel(id); }
[shader("compute")]
[numthreads(64, 1, 1)]
void visibilityCullComputeMain(uint3 id : SV_DispatchThreadID) { arpVisibilityCullKernel(id); }
[shader("compute")]
[numthreads(64, 1, 1)]
void visibilityEmitComputeMain(uint3 id : SV_DispatchThreadID) { arpVisibilityEmitKernel(id); }
[shader("compute")]
[numthreads(64, 1, 1)]
void visibilityFinalizeComputeMain(uint3 id : SV_DispatchThreadID) { arpVisibilityFinalizeKernel(id); }
