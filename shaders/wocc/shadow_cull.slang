// World of ClaudeCraft P6-A2: GPU-driven shadow cascade cull compute shader.
//
// Per-instance cascade frustum + shadow-distance cull on the GPU, for the
// instanced shadow pass.  Two entry points share one push-constant block and
// descriptor set, mirroring vegetation_cull.slang but handling all 3 CSM
// cascades in a single dispatch:
//
//   shadowCullMain        - count+scatter phase: per-instance, skip groups
//                           where castsShadow==0, distance-cull against
//                           shadowCullDistance, then for each of the 3
//                           cascade frustums that contains the instance,
//                           atomic-append into that cascade's per-group
//                           output region.
//
//   shadowEmitDrawMain    - emit phase: one thread per (draw group, cascade)
//                           pair.  Reads the per-cascade visible count and
//                           writes the DrawIndexedIndirectCommand record.
//
// Output layout (3 cascades, indexed by cascade c in [0,3)):
//
//   ShadowVisibleInstances[c * maxInstances + ...]
//     - compacted InstanceDataGpu records, laid out per-group contiguously
//       starting at DrawGroupMeta.globalFirstInstance, within each cascade.
//
//   ShadowGroupVisibleCount[c * drawGroupCount + groupIdx]
//     - per-cascade, per-group visible count.
//
//   ShadowDrawCommands[c * drawGroupCount + groupIdx]
//     - one DrawIndexedIndirectCommand per (cascade, draw group).
//
//   ShadowDrawCount[c]
//     - active draw group count per cascade (for diagnostics / future
//       drawIndexedIndirectCount use).

// ── Instance data (matches arp::pcg::InstanceDataGpu, 64 bytes) ──
struct InstanceDataGpu {
    float4 positionScale;    // xyz = position, w = scale
    float4 yawPhaseIdLod;    // x = yaw, y = phase, z = instanceId, w = lodClass
    float4 extraTransform;   // pitchX, rollZ, scaleY, burialY
    float4 speciesLodState;  // packed uint bits
};

// ── Draw group metadata (matches DrawGroupMetaGpu, std430 layout) ──
struct DrawGroupMeta {
    uint templateId;           // 0
    uint sectionIndex;         // 4
    uint pipelineVariantId;    // 8
    uint globalFirstInstance;  // 12
    uint globalInstanceCount;  // 16
    uint _pad0;                // 20
    uint _pad1;                // 24
    uint _pad2;                // 28
    float4 baseColor;          // 32
    uint materialIndex;        // 48
    float cullDistance;        // 52
    uint useAlphaTest;         // 56
    uint doubleSided;          // 60
    uint useFullPbrFragment;   // 64
    uint singleSidedLighting;  // 68
    uint castsShadow;          // 72
    uint firstIndex;           // 76
    uint indexCount;           // 80
    uint _pad3;                // 84
    uint _pad4;                // 88
    uint _pad5;                // 92
};

// ── DrawIndexedIndirectCommand (20 bytes, matches native Vulkan layout) ──
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// ── Push constants (128 bytes) ──
// The 3 cascade frustum planes (3 × 6 × float4 = 288 bytes) are too large for
// push constants, so they're passed via a dedicated SSBO (CascadeFrustumPlanes,
// binding 7). Push constants carry only the scalar params.
struct ShadowCullPushConstants {
    float4 cameraPos;                   // 0: xyz = camera, w = totalInstanceCount (asfloat)
    uint  drawGroupCount;               // 16
    uint  maxInstances;                 // 20
    uint  maxDrawGroups;                // 24
    float shadowCullDistance;           // 28 (80m, global -- matches LodProfile)
    uint  _pad0;                        // 32
    uint  _pad1;                        // 36
    uint  _pad2;                        // 40
    uint  _pad3;                        // 44
    uint  _pad4;                        // 48
    uint  _pad5;                        // 52
    uint  _pad6;                        // 56
    uint  _pad7;                        // 60
    uint  _pad8;                        // 64
    uint  _pad9;                        // 68
    uint  _pad10;                       // 72
    uint  _pad11;                       // 76
    uint  _pad12;                       // 80
    uint  _pad13;                       // 84
    uint  _pad14;                       // 88
    uint  _pad15;                       // 92
    uint  _pad16;                       // 96
    uint  _pad17;                       // 100
    uint  _pad18;                       // 104
    uint  _pad19;                       // 108
    uint  _pad20;                       // 112
    uint  _pad21;                       // 116
    uint  _pad22;                       // 120
    uint  _pad23;                       // 124 (pad to 128)
};


[[vk::push_constant]]
ShadowCullPushConstants gShadow;

// ── Bindings (set 0) ──
[[vk::binding(0, 0)]]
StructuredBuffer<InstanceDataGpu> InstanceBuffer;       // global instance data

[[vk::binding(1, 0)]]
StructuredBuffer<DrawGroupMeta> DrawGroups;             // per-group metadata

[[vk::binding(2, 0)]]
RWStructuredBuffer<InstanceDataGpu> ShadowVisibleInstances;  // 3 × maxInstances

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> ShadowGroupVisibleCount;       // 3 × drawGroupCount

[[vk::binding(4, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> ShadowDrawCommands;  // 3 × drawGroupCount

[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> ShadowDrawCount;               // 3 (one per cascade)

[[vk::binding(6, 0)]]
StructuredBuffer<uint> InstanceToGroup;                 // per-instance -> draw group index

[[vk::binding(7, 0)]]
StructuredBuffer<float4> CascadeFrustumPlanes;          // 3 × 6 = 18 float4s (288 bytes)

// ── Helpers ──

// Frustum cull: test a world-space point against 6 planes for one cascade.
// Each plane: inside if dot(plane.xyz, pos) + plane.w >= 0.
bool cascadeContainsPoint(uint cascade, float3 pos)
{
    [unroll]
    for (int i = 0; i < 6; ++i) {
        float4 plane = CascadeFrustumPlanes[cascade * 6u + i];
        if (dot(plane.xyz, pos) + plane.w < 0.0) {
            return false;
        }
    }
    return true;
}

// ── Phase 0: clear count buffers ──
// One thread per (cascade, draw group) pair + 3 threads for the draw count.
// Resets ShadowGroupVisibleCount and ShadowDrawCount to zero before the cull
// phase runs. Without this, InterlockedAdd accumulates across frames.
// Thread mapping: dtid.x < 3 clears ShadowDrawCount[dtid.x].
//                 dtid.x >= 3 clears ShadowGroupVisibleCount for the (cascade,
//                 group) pair at linearIdx = dtid.x - 3.
[shader("compute")]
[numthreads(64, 1, 1)]
void shadowClearMain(uint3 dtid : SV_DispatchThreadID)
{
    uint idx = dtid.x;
    // First 3 threads clear ShadowDrawCount[0..2].
    if (idx < 3u) {
        ShadowDrawCount[idx] = 0u;
        return;
    }
    // Remaining threads clear ShadowGroupVisibleCount[cascade * drawGroupCount + group].
    uint linearIdx = idx - 3u;
    uint totalCounts = 3u * gShadow.drawGroupCount;
    if (linearIdx >= totalCounts) return;
    ShadowGroupVisibleCount[linearIdx] = 0u;
}

// ── Phase 1: count + scatter ──
// One thread per instance.  Skip non-shadow-casters, distance-cull against
// shadowCullDistance, then for each of the 3 cascades whose frustum contains
// the instance, atomic-append into that cascade's per-group output region.
[shader("compute")]
[numthreads(64, 1, 1)]
void shadowCullMain(uint3 dtid : SV_DispatchThreadID)
{
    uint instanceIdx = dtid.x;
    uint totalInstances = asuint(gShadow.cameraPos.w);
    if (instanceIdx >= totalInstances) return;

    // Look up the draw group for this instance.
    uint groupIdx = InstanceToGroup[instanceIdx];
    if (groupIdx >= gShadow.drawGroupCount) return;

    DrawGroupMeta group = DrawGroups[groupIdx];

    // Skip groups that don't cast shadows (grass/ground cover).
    if (group.castsShadow == 0u) return;

    InstanceDataGpu inst = InstanceBuffer[instanceIdx];
    float3 pos = inst.positionScale.xyz;

    // Shadow distance cull: instances beyond shadowCullDistance don't cast
    // meaningful shadows (they'd only appear in the lowest-resolution cascade).
    float3 camPos = gShadow.cameraPos.xyz;
    float dx = pos.x - camPos.x;
    float dz = pos.z - camPos.z;
    float dist = sqrt(dx * dx + dz * dz);
    if (dist > gShadow.shadowCullDistance) return;

    // Per-cascade frustum cull + scatter.  An instance can be visible in
    // 0, 1, 2, or all 3 cascades.  Each cascade has its own per-group output
    // region, so the atomic appends are independent across cascades.
    [unroll]
    for (uint c = 0u; c < 3u; ++c) {
        if (!cascadeContainsPoint(c, pos)) continue;

        uint countIdx = c * gShadow.drawGroupCount + groupIdx;
        uint localSlot = 0u;
        InterlockedAdd(ShadowGroupVisibleCount[countIdx], 1u, localSlot);

        uint outputIdx = c * gShadow.maxInstances
                       + group.globalFirstInstance + localSlot;
        if (outputIdx < (c + 1u) * gShadow.maxInstances) {
            ShadowVisibleInstances[outputIdx] = inst;
        }
    }
}

// ── Phase 2: emit draw commands ──
// One thread per (draw group, cascade) pair.  Thread index is linearized as
// dtid.x = cascade * drawGroupCount + groupIdx.  Reads the per-cascade visible
// count and writes the DrawIndexedIndirectCommand.  Must run in a separate
// pass after a buffer barrier on ShadowGroupVisibleCount.
[shader("compute")]
[numthreads(64, 1, 1)]
void shadowEmitDrawMain(uint3 dtid : SV_DispatchThreadID)
{
    uint linearIdx = dtid.x;
    uint totalThreads = gShadow.drawGroupCount * 3u;
    if (linearIdx >= totalThreads) return;

    uint cascade = linearIdx / gShadow.drawGroupCount;
    uint groupIdx = linearIdx % gShadow.drawGroupCount;

    DrawGroupMeta group = DrawGroups[groupIdx];
    uint countIdx = cascade * gShadow.drawGroupCount + groupIdx;
    uint visibleCount = ShadowGroupVisibleCount[countIdx];

    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = group.indexCount;
    cmd.instanceCount = visibleCount;
    cmd.firstIndex = group.firstIndex;
    cmd.vertexOffset = 0;
    // firstInstance points into the cascade's slice of ShadowVisibleInstances.
    // The instance-rate vertex buffer is bound with an offset of
    // cascade * maxInstances * sizeof(InstanceDataGpu), so firstInstance is
    // relative to that offset (i.e., just globalFirstInstance).
    cmd.firstInstance = group.globalFirstInstance;

    uint cmdIdx = cascade * gShadow.drawGroupCount + groupIdx;
    ShadowDrawCommands[cmdIdx] = cmd;

    if (visibleCount > 0u) {
        uint dummy;
        InterlockedAdd(ShadowDrawCount[cascade], 1u, dummy);
    }
}
