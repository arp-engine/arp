// World of ClaudeCraft P5: GPU-driven vegetation cull compute shader.
//
// Per-instance frustum + distance cull on the GPU.  Two entry points share
// one push-constant block and descriptor set:
//
//   vegetationCullMain      - count+scatter phase: per-instance frustum +
//                             distance cull, atomic-append visible instances
//                             into each draw group's contiguous output region,
//                             atomic-increment each group's visible count.
//
//   vegetationEmitDrawMain  - emit phase: per-group thread reads the visible
//                             count (written by the cull phase) and writes the
//                             DrawIndexedIndirectCommand record.  Must run in
//                             a separate FrameGraph pass so the graph compiler
//                             inserts a cross-pass buffer barrier.
//
// Output layout:
//   VisibleInstances[]  - compacted InstanceDataGpu records, laid out per-group
//                         contiguously starting at DrawGroupMeta.globalFirstInstance.
//                         The indirect draw binds this buffer as instance-rate
//                         vertex input (slot 1), so the vertex shader reads
//                         InstanceDataGpu directly - no shader changes needed.
//                         Groups are contiguous and non-overlapping, so each
//                         draw's firstInstance = globalFirstInstance and
//                         instanceCount = the GPU-written visible count.
//
//   DrawCommands[]       - one DrawIndexedIndirectCommand per draw group.  The
//                         emit phase fills indexCount/firstIndex/vertexOffset/
//                         firstInstance from DrawGroupMeta and instanceCount from
//                         the GPU-written visible count.
//
//   DrawCount[]          - [0] = number of draw groups with instanceCount > 0
//                         (the "draw count" for drawIndexedIndirectCount).  The
//                         emit phase atomically increments this for each group
//                         whose visible count > 0.
//
// The cull+scatter design writes visible instances into each group's own
// [globalFirstInstance, globalFirstInstance + globalInstanceCount) range, so
// groups never overlap and no global prefix-sum is needed.  Within a group,
// the atomic counter provides the local slot.

// ── Instance data (matches arp::pcg::InstanceDataGpu, 64 bytes) ──
struct InstanceDataGpu {
    float4 positionScale;    // xyz = position, w = scale
    float4 yawPhaseIdLod;    // x = yaw, y = phase, z = instanceId, w = lodClass
    float4 extraTransform;   // pitchX, rollZ, scaleY, burialY
    float4 speciesLodState;  // packed uint bits
};

// ── Draw group metadata (matches DrawGroupMetaGpu, std430 layout) ──
// The struct is 96 bytes (16-byte struct alignment). Explicit padding ensures
// the Slang/GLSL layout matches the C++ DrawGroupMetaGpu struct exactly.
struct DrawGroupMeta {
    uint templateId;           // 0
    uint sectionIndex;         // 4
    uint pipelineVariantId;    // 8
    uint globalFirstInstance;  // 12
    uint globalInstanceCount;  // 16
    uint _pad0;                // 20 (align baseColor to 16 bytes)
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
    uint _pad5;                // 92 (pad to 96 for 16-byte struct align)
};

// ── DrawIndexedIndirectCommand (20 bytes, matches native Vulkan layout) ──
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// ── Push constants (128 bytes, under the 128-byte Vulkan minimum limit) ──
struct VegetationCullPushConstants {
    float4 cameraPos;        // xyz = camera position, w = totalInstanceCount
    float4 frustumPlanes[6]; // 6 frustum planes: xyz = normal (pointing inward), w = distance
    uint  drawGroupCount;    // number of draw groups
    uint  maxInstances;      // capacity of VisibleInstances buffer
    uint  maxDrawGroups;     // capacity of DrawCommands / GroupVisibleCount
    uint  padding0;
};


[[vk::push_constant]]
VegetationCullPushConstants gCull;

// ── Bindings (set 0) ──
[[vk::binding(0, 0)]]
StructuredBuffer<InstanceDataGpu> InstanceBuffer;       // global instance data

[[vk::binding(1, 0)]]
StructuredBuffer<DrawGroupMeta> DrawGroups;             // per-group metadata

[[vk::binding(2, 0)]]
RWStructuredBuffer<InstanceDataGpu> VisibleInstances;    // compacted visible instance data

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> GroupVisibleCount;             // per-group visible count (uint each)

[[vk::binding(4, 0)]]
RWStructuredBuffer<DrawIndexedIndirectCommand> DrawCommands;  // per-group draw args

[[vk::binding(5, 0)]]
RWStructuredBuffer<uint> DrawCount;                      // [0] = active draw group count

[[vk::binding(6, 0)]]
StructuredBuffer<uint> InstanceToGroup;                 // per-instance -> draw group index

// ── Helpers ──

// Frustum cull: test a world-space point against 6 planes.
// Each plane: inside if dot(plane.xyz, pos) + plane.w >= 0.
bool frustumContainsPoint(float3 pos)
{
    [unroll]
    for (int i = 0; i < 6; ++i) {
        float4 plane = gCull.frustumPlanes[i];
        if (dot(plane.xyz, pos) + plane.w < 0.0) {
            return false;
        }
    }
    return true;
}

// ── Phase 0: clear count buffers ──
// One thread per draw group + 1 thread for DrawCount. Resets GroupVisibleCount
// and DrawCount to zero before the cull phase runs. Without this, InterlockedAdd
// accumulates across frames, causing instanceCount to grow unbounded.
[shader("compute")]
[numthreads(64, 1, 1)]
void vegetationClearMain(uint3 dtid : SV_DispatchThreadID)
{
    uint idx = dtid.x;
    // Thread 0 clears DrawCount[0] (active draw group count).
    if (idx == 0u) {
        DrawCount[0] = 0u;
    }
    // Threads 0..drawGroupCount-1 clear GroupVisibleCount[groupIdx].
    if (idx < gCull.drawGroupCount) {
        GroupVisibleCount[idx] = 0u;
    }
}

// ── Phase 1: count + scatter ──
// One thread per instance.  Cull, then atomic-append into the group's
// contiguous output region starting at globalFirstInstance.
[shader("compute")]
[numthreads(64, 1, 1)]
void vegetationCullMain(uint3 dtid : SV_DispatchThreadID)
{
    uint instanceIdx = dtid.x;
    uint totalInstances = asuint(gCull.cameraPos.w);
    if (instanceIdx >= totalInstances) return;

    InstanceDataGpu inst = InstanceBuffer[instanceIdx];
    float3 pos = inst.positionScale.xyz;

    // Frustum cull (point test; scale margin handled by distance cull + LOD).
    if (!frustumContainsPoint(pos)) return;

    // Distance cull using the draw group's cullDistance.
    uint groupIdx = InstanceToGroup[instanceIdx];
    if (groupIdx >= gCull.drawGroupCount) return;

    DrawGroupMeta group = DrawGroups[groupIdx];
    float3 camPos = gCull.cameraPos.xyz;
    float dx = pos.x - camPos.x;
    float dz = pos.z - camPos.z;
    float dist = sqrt(dx * dx + dz * dz);
    if (dist > group.cullDistance) return;

    // Atomic-append into this group's contiguous output region.
    // localSlot is the per-group visible counter value before increment.
    uint localSlot = 0;
    InterlockedAdd(GroupVisibleCount[groupIdx], 1, localSlot);

    // Write the instance DATA (not just the index) into the group's region.
    // The group's output base is globalFirstInstance; visible instances are
    // packed contiguously starting there. This buffer is bound as the
    // instance-rate vertex buffer (slot 1) by the indirect draw, so the
    // vertex shader reads InstanceDataGpu directly - no shader changes needed.
    uint outputIdx = group.globalFirstInstance + localSlot;
    if (outputIdx < gCull.maxInstances) {
        VisibleInstances[outputIdx] = inst;
    }
}

// ── Phase 2: emit draw commands ──
// One thread per draw group.  Reads the visible count (written by phase 1)
// and writes the DrawIndexedIndirectCommand.  Must run in a separate pass
// after a buffer barrier on GroupVisibleCount.
[shader("compute")]
[numthreads(64, 1, 1)]
void vegetationEmitDrawMain(uint3 dtid : SV_DispatchThreadID)
{
    uint groupIdx = dtid.x;
    if (groupIdx >= gCull.drawGroupCount) return;

    DrawGroupMeta group = DrawGroups[groupIdx];
    uint visibleCount = GroupVisibleCount[groupIdx];

    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = group.indexCount;
    cmd.instanceCount = visibleCount;
    cmd.firstIndex = group.firstIndex;
    cmd.vertexOffset = 0;
    cmd.firstInstance = group.globalFirstInstance;
    DrawCommands[groupIdx] = cmd;

    // If this group has visible instances, increment the active draw count.
    if (visibleCount > 0) {
        uint dummy;
        InterlockedAdd(DrawCount[0], 1, dummy);
    }
}
