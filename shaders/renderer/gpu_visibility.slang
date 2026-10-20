// Shared engine visibility kernels. Publication/recording still requires the
// authenticated original Root, exact per-entry accesses and canonical journal.
// Dispatch reset -> cull -> emit in separate scopes with original dependencies.
#include "core/instance_transform.slang"
#include "core/gpu_visibility.slang"

[[vk::binding(0, 0)]] StructuredBuffer<ArpGpuVisibilityDrawGroup> Groups;
[[vk::binding(1, 0)]] StructuredBuffer<ArpInstanceDataGpu> Instances;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Owners;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> Counts;
[[vk::binding(4, 0)]] RWStructuredBuffer<ArpInstanceDataGpu> Visible;
[[vk::binding(5, 0)]] RWStructuredBuffer<ArpGpuIndexedIndirectCommand> Draws;
[[vk::binding(6, 0)]] RWStructuredBuffer<ArpGpuIndirectInstanceBase> Bases;
// Word zero is the workload fault, words [1, 1+cascades) are physical draw counts.
[[vk::binding(7, 0)]] RWStructuredBuffer<uint> Control;
[[vk::binding(8, 0)]] ConstantBuffer<ArpGpuVisibilityParameters> Parameters;
[[vk::binding(9, 0)]] Texture2D<float4> DepthPyramid;
// Immutable cold-authenticated original output ranges. Metal's structured
// buffer GetDimensions is unavailable in this Slang target; no inferred size.
struct ArpVisibilityOutputRanges { uint4 words; };
[[vk::binding(10, 0)]] ConstantBuffer<ArpVisibilityOutputRanges> OutputRanges;

ArpGpuVisibilityParameters arpVisibilityParameters() { return Parameters; }
ArpGpuVisibilityDrawGroup arpVisibilityGroup(uint index) { return Groups[index]; }
ArpInstanceDataGpu arpVisibilityInstance(uint index) { return Instances[index]; }
uint arpVisibilityOwner(uint index) { return Owners[index]; }
uint arpVisibilityCount(uint index) { return Counts[index]; }
void arpVisibilityWriteCount(uint index, uint value) { Counts[index] = value; }
void arpVisibilityWriteControl(uint index, uint value) { Control[index] = value; }
void arpVisibilityWriteInstance(uint index, ArpInstanceDataGpu value) { Visible[index] = value; }
void arpVisibilityWriteDraw(uint index, ArpGpuIndexedIndirectCommand value) { Draws[index] = value; }
void arpVisibilityWriteBase(uint index, ArpGpuIndirectInstanceBase value) { Bases[index] = value; }
float4 arpVisibilityDepth(int3 location) { return DepthPyramid.Load(location); }
uint arpVisibilityReserve(uint index) {
    uint previous;
    InterlockedAdd(Counts[index], 1u, previous);
    return previous;
}
void arpVisibilityFault() {
    uint previous;
    InterlockedOr(Control[0u], 1u, previous);
}
uint arpVisibilityFaultBits() {
    uint previous;
    InterlockedOr(Control[0u], 0u, previous);
    return previous;
}
uint arpVisibilityDrawWords() { return OutputRanges.words.x; }
uint arpVisibilityBaseSlots() { return OutputRanges.words.y; }
uint arpVisibilityControlWords() { return OutputRanges.words.z; }
#include "core/gpu_visibility_kernel.slang"

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
