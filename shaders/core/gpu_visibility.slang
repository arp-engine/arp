// Compose core/instance_transform.slang before this source library.
// GpuVisibilityLayout/1. The host validates complete ranges, explicit shared
// material-section owners and capacities before recording. These helpers
// preserve the existing Instance64 / DrawGroup112 / DrawIndexed20 ABIs.
struct ArpGpuVisibilityDrawGroup
{
    uint templateId;
    uint sectionIndex;
    uint pipelineVariantId;
    uint globalFirstInstance;
    uint globalInstanceCount;
    uint padding0;
    uint padding1;
    uint padding2;
    float4 baseColor;
    float4 localBoundsCenterRadius;
    uint materialIndex;
    float cullDistance;
    uint useAlphaTest;
    uint doubleSided;
    uint useFullPbrFragment;
    uint singleSidedLighting;
    uint castsShadow;
    uint firstIndex;
    uint indexCount;
    uint shadowCascade1TemplateId;
    uint shadowCascade1FirstIndex;
    uint shadowCascade1IndexCount;
};

struct ArpGpuIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct ArpGpuIndirectInstanceBase
{
    uint firstInstance;
    uint instanceCount;
    uint instanceCapacity;
    uint contractVersion;
};

bool arpVisibilityOutputIndex(uint first, uint count, uint localSlot,
    uint capacity, out uint outputIndex)
{
    outputIndex = 0u;
    if (first > capacity || count > capacity - first || localSlot >= count) return false;
    outputIndex = first + localSlot;
    return true;
}

// Only a real queried native firstInstance capability selects nativeBase.
// Failure must latch the existing workload fault; it cannot publish a draw.
bool arpEncodeVisibilityDraw(ArpGpuVisibilityDrawGroup group, uint visibleCount,
    uint capacity, uint cascade, bool nativeBase,
    out ArpGpuIndexedIndirectCommand draw, out ArpGpuIndirectInstanceBase mapping)
{
    draw.indexCount = 0u;
    draw.instanceCount = 0u;
    draw.firstIndex = 0u;
    draw.vertexOffset = 0;
    draw.firstInstance = 0u;
    mapping.firstInstance = 0u;
    mapping.instanceCount = 0u;
    mapping.instanceCapacity = 0u;
    mapping.contractVersion = 0u;
    if (capacity == 0u || group.globalInstanceCount == 0u || group.indexCount == 0u
        || group.globalFirstInstance > capacity
        || group.globalInstanceCount > capacity - group.globalFirstInstance
        || visibleCount > group.globalInstanceCount || cascade >= 3u) return false;
    bool alternate = cascade == 1u && group.shadowCascade1IndexCount != 0u;
    draw.indexCount = alternate ? group.shadowCascade1IndexCount : group.indexCount;
    draw.instanceCount = visibleCount;
    draw.firstIndex = alternate ? group.shadowCascade1FirstIndex : group.firstIndex;
    draw.firstInstance = nativeBase ? group.globalFirstInstance : 0u;
    mapping.firstInstance = group.globalFirstInstance;
    mapping.instanceCount = visibleCount;
    mapping.instanceCapacity = capacity;
    mapping.contractVersion = 1u;
    return true;
}

// SV_InstanceID includes firstInstance on a native draw. A zero-base draw uses
// the same original mapping and adds its logical base after checking capacity.
bool arpResolveVisibilityInstance(ArpGpuIndirectInstanceBase mapping,
    uint instanceId, bool nativeBase, out uint logicalInstance)
{
    logicalInstance = 0u;
    if (mapping.contractVersion != 1u || mapping.firstInstance > mapping.instanceCapacity
        || mapping.instanceCount > mapping.instanceCapacity - mapping.firstInstance) return false;
    uint physical = instanceId;
    if (nativeBase) {
        if (instanceId < mapping.firstInstance) return false;
        physical = instanceId - mapping.firstInstance;
    }
    if (physical >= mapping.instanceCount) return false;
    logicalInstance = mapping.firstInstance + physical;
    return true;
}

// All arithmetic below is in the shader's uint32 index domain, including
// cascaded slices. A failed result never names a writable output element.
bool arpVisibilitySliceIndex(uint slice, uint slices, uint capacity,
    uint localIndex, out uint index)
{
    index = 0u;
    if (slices == 0u || slices > 3u || slice >= slices || capacity == 0u
        || localIndex >= capacity || capacity > 0xffffffffu / slices) return false;
    index = slice * capacity + localIndex;
    return true;
}

// Planes need not be normalized: scale the sphere extent by the plane normal.
// The wind envelope is authored by the geometry family; no backend guesses it.
bool arpVisibilitySphereInFrustum(float3 center, float radius, float4 planes[6])
{
    [unroll]
    for (uint plane = 0u; plane < 6u; ++plane) {
        if (dot(planes[plane].xyz, center) + planes[plane].w
            < -radius * length(planes[plane].xyz)) return false;
    }
    return true;
}

// Owner selection and range validation are shared by count/scatter and emit.
// An authored section alias resolves to exactly the same counter and span.
bool arpVisibilityOwnerRange(ArpGpuVisibilityDrawGroup group,
    ArpGpuVisibilityDrawGroup owner, uint instanceIndex, uint capacity)
{
    uint ignored = 0u;
    return group.globalFirstInstance == owner.globalFirstInstance
        && group.globalInstanceCount == owner.globalInstanceCount
        && instanceIndex >= owner.globalFirstInstance
        && arpVisibilityOutputIndex(owner.globalFirstInstance, owner.globalInstanceCount,
            instanceIndex - owner.globalFirstInstance, capacity, ignored);
}

// The caller reserves localSlot with its original typed atomic-add operation.
// Exact dispatch coverage and a reset->cull dependency bound additions by the
// authored owner span; this store checks both group and cascade slices.
bool arpStoreVisibleInstance(ArpInstanceDataGpu instance,
    uint inputIndex, uint localSlot, ArpGpuVisibilityDrawGroup owner,
    uint instanceCapacity, uint cascade, uint cascades,
    RWStructuredBuffer<ArpInstanceDataGpu> visible)
{
    uint localOutput = 0u;
    uint output = 0u;
    if (!arpVisibilityOwnerRange(owner, owner, inputIndex, instanceCapacity)
        || !arpVisibilityOutputIndex(owner.globalFirstInstance, owner.globalInstanceCount,
            localSlot, instanceCapacity, localOutput)
        || !arpVisibilitySliceIndex(cascade, cascades, instanceCapacity, localOutput, output))
        return false;
    visible[output] = instance;
    return true;
}

// One invocation per draw section after the original count-write/read barrier.
// Every args/base slot is assigned, including failed and empty output. This
// preserves holes: activeDrawCount is telemetry, never a compact prefix count.
// A count draw requires a separately authenticated compaction operation.
bool arpEmitVisibilityDraw(ArpGpuVisibilityDrawGroup group,
    ArpGpuVisibilityDrawGroup owner, uint sectionIndex, uint ownerIndex,
    uint groupCount, uint groupCapacity, uint instanceCapacity,
    uint cascade, uint cascades, bool nativeBase, RWStructuredBuffer<uint> counts,
    RWStructuredBuffer<ArpGpuIndexedIndirectCommand> draws,
    RWStructuredBuffer<ArpGpuIndirectInstanceBase> mappings)
{
    uint output = 0u;
    if (sectionIndex >= groupCount || groupCount > groupCapacity
        || !arpVisibilitySliceIndex(cascade, cascades, groupCapacity, sectionIndex, output))
        return false;
    ArpGpuIndexedIndirectCommand draw;
    ArpGpuIndirectInstanceBase mapping;
    // Construct the canonical suppressed record before any owner/counter read.
    arpEncodeVisibilityDraw(group, 0u, 0u, cascade, nativeBase, draw, mapping);
    uint countIndex = 0u;
    bool valid = ownerIndex < groupCount
        && arpVisibilityOwnerRange(group, owner, group.globalFirstInstance, instanceCapacity)
        && arpVisibilitySliceIndex(cascade, cascades, groupCapacity, ownerIndex, countIndex);
    if (valid)
        valid = arpEncodeVisibilityDraw(group, counts[countIndex], instanceCapacity,
            cascade, nativeBase, draw, mapping);
    draws[output] = draw;
    mappings[output] = mapping;
    return valid;
}

// Phase zero records the physical slot count, not the number of nonempty
// groups. Emission preserves section slots, so prefix-count consumption must
// include suppressed holes as well. Each original pass barrier still belongs
// to the caller's canonical journal; these shader routines cannot create one.
bool arpResetVisibilityCounts(uint lane, uint groupCount, uint groupCapacity,
    uint cascades, RWStructuredBuffer<uint> counts, RWStructuredBuffer<uint> drawCounts,
    uint drawCountWordOffset)
{
    uint ignored = 0u;
    if (groupCount == 0u || groupCount > groupCapacity
        || drawCountWordOffset > 0xffffffffu - cascades
        || !arpVisibilitySliceIndex(0u, cascades, groupCapacity, 0u, ignored)) return false;
    if (lane < cascades) drawCounts[drawCountWordOffset + lane] = groupCount;
    if (lane < groupCount * cascades) {
        uint countIndex = (lane / groupCount) * groupCapacity + lane % groupCount;
        counts[countIndex] = 0u;
    }
    return true;
}

struct ArpGpuVisibilityParameters
{
    uint4 counts;
    uint4 capacities;
    float4 cameraAndEnvelope;
    float4 selection;
    float4 frustumPlanes[18];
    float4 viewProjectionRows[4];
    uint4 depthInfo;
    uint4 policy;
};
