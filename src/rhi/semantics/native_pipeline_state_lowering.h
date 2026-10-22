#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/native_backend_capabilities.h"

#include <cstdint>

namespace arp::rhi::semantics {

/// SampleCoverage/1: bits above sampleCount have no observable effect.
/// Metal has no fixed-function sample-mask setter. Full coverage is an
/// identity lowering; partial coverage requires a shader ABI not yet provided.
inline constexpr bool sampleMaskHasFullCoverage(std::uint32_t sampleCount, std::uint64_t mask)
{
    if (sampleCount == 0u || sampleCount > 64u)
        return false;
    const auto active = sampleCount == 64u ? UINT64_MAX : (std::uint64_t{1} << sampleCount) - 1u;
    return (mask & active) == active;
}

/// L2 pipeline-state lowering semantics.
///
/// Mirrors the shape of `NativeCommandSemantic` (see native_command_lowering.h):
/// each entry names one concrete native concept that a backend produces when
/// lowering an L2 `RenderPipelineDesc`. The L2 layer entries (`L2*`) are
/// backend-agnostic facts derived purely from the descriptor; the per-backend
/// entries name the native vocabulary object the backend builds. A lowering
/// decision collects the set of semantics that apply to a given
/// `RenderPipelineDesc` + backend pair so that contract tests can assert
/// cross-backend parity without holding native handles.
enum class NativePipelineStateSemantic {
    // L2 (backend-agnostic) facts about the descriptor.
    L2DepthOnlyRenderPassState,
    /// L2 fact: PatchList is a tessellation draw and requires an explicit
    /// generated tessellation ABI. It must never be reinterpreted as triangles.
    L2PatchListTessellation,
    /// L2 fact: descriptor.dynamicViewportScissor == true. The pipeline opts
    /// into receiving viewport/scissor via RenderPassEncoder::setViewport /
    /// setScissor at record time. Vulkan emits VkPipelineDynamicStateCreateInfo
    /// with VK_DYNAMIC_STATE_VIEWPORT + VK_DYNAMIC_STATE_SCISSOR; Metal/WebGPU/
    /// Null add the semantic for parity (their native model is encoder-side
    /// dynamic state, so the opt-in does not change pipeline creation).
    L2DynamicViewportScissorOptIn,

    // Null backend: validation-only, no native payload.
    NullRecordedPipelineState,
    NullDepthOnlyRenderPassState,
    NullVertexInputState,

    // Vulkan native pipeline-state vocabulary.
    VulkanGraphicsPipelineState,
    VulkanRasterizationState,
    VulkanMrtBlendState,
    VulkanDepthStencilState,
    VulkanDynamicViewportScissor,
    VulkanMsaaResolveState,
    VulkanVertexInputState,
    VulkanDepthOnlyRenderPassState,

    // Metal native pipeline-state vocabulary.
    MetalRenderPipelineDescriptor,
    MetalRasterizationState,
    MetalDepthStencilDescriptor,
    MetalViewportScissorState,
    MetalMsaaResolveState,
    MetalVertexDescriptorState,
    MetalDepthOnlyRenderPassState,
    MetalTessellationPipelineState,

    // WebGPU native pipeline-state vocabulary.
    WebGpuRenderPipelineDescriptor,
    WebGpuPrimitiveState,
    WebGpuDepthStencilState,
    WebGpuViewportScissorState,
    WebGpuMsaaResolveState,
    WebGpuVertexState,
    WebGpuDepthOnlyRenderPassState,

    // Diagnostic: the descriptor contains state no backend can lower.
    UnsupportedStateDiagnostic
};

/// Result of lowering one `RenderPipelineDesc` against a backend capability
/// contract. Fields mirror `NativeCommandLoweringDecision` plus the per-state
/// "lowered" booleans the parity test asserts. `pipelineCacheKey` MUST equal
/// `stableRenderPipelineStateKey(desc)` so that the lowering decision and the
/// shared pipeline cache agree on identity.
struct NativePipelineStateLoweringDecision {
    bool success = false;
    rhi::Backend backend = rhi::Backend::Null;
    rhi::FallbackMode fallbackMode = rhi::FallbackMode::Native;
    bool nativeHandleExposedToUpperLayer = false;
    bool backendVocabularyHiddenFromUpperLayer = true;
    std::uint32_t colorTargetCount = 0;
    std::uint32_t sampleCount = 1;
    std::uint64_t pipelineCacheKey = 0;
    bool blendWriteMaskLowered = false;
    bool stencilStateLowered = false;
    bool depthBiasStateLowered = false;
    bool rasterStateLowered = false;
    bool topologyLowered = false;
    bool viewportScissorLowered = false;
    bool resolveStateLowered = false;
    bool depthOnlyStateLowered = false;
    bool vertexInputStateLowered = false;
    bool tessellationStateLowered = false;
    core::pmr::Vector<NativePipelineStateSemantic> semantics{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Lower an L2 render pipeline descriptor against a backend capability
/// contract. Runs the shared `validateRenderPipelineState` first; on failure
/// the decision carries `UnsupportedStateDiagnostic` and the validation
/// diagnostics. On success the decision records the per-backend native
/// vocabulary semantics that apply to the descriptor.
NativePipelineStateLoweringDecision lowerRenderPipelineState(
    const NativeBackendCapabilityContract& contract, const rhi::RenderPipelineDesc& desc);

/// Query helper: true if `decision.semantics` contains `semantic`.
bool hasNativePipelineStateSemantic(
    const NativePipelineStateLoweringDecision& decision, NativePipelineStateSemantic semantic);

// ── Compute pipeline state lowering ──────────────────────────────────────
//
// Mirrors `lowerRenderPipelineState` for `ComputePipelineDesc`. Compute has a
// much smaller state surface than render (no raster/blend/depth/stencil/
// MSAA/vertex-input), so the decision carries only the shader/layout/push-
// constant/descriptor-binding "lowered" flags. The same contract-first shape
// applies: the shared `validateComputePipelineState` runs first, then the
// per-backend vocabulary semantics are recorded so parity tests can assert a
// uniform surface without holding native handles.

enum class NativeComputePipelineStateSemantic {
    // L2 (backend-agnostic) facts about the descriptor.
    L2ComputePipelineState,

    // Null backend: validation-only, no native payload.
    NullRecordedComputePipelineState,

    // Vulkan native compute pipeline-state vocabulary.
    VulkanComputePipelineState,
    VulkanComputeShaderStage,
    VulkanComputePipelineLayout,

    // Metal native compute pipeline-state vocabulary.
    MetalComputePipelineState,
    MetalComputeShaderFunction,
    MetalComputePipelineLayout,

    // WebGPU native compute pipeline-state vocabulary.
    WebGpuComputePipelineDescriptor,
    WebGpuComputeShaderModule,
    WebGpuComputeBindGroupLayout,

    // Diagnostic: the descriptor contains state no backend can lower.
    UnsupportedComputeStateDiagnostic
};

struct NativeComputePipelineStateLoweringDecision {
    bool success = false;
    rhi::Backend backend = rhi::Backend::Null;
    rhi::FallbackMode fallbackMode = rhi::FallbackMode::Native;
    bool nativeHandleExposedToUpperLayer = false;
    bool backendVocabularyHiddenFromUpperLayer = true;
    std::uint64_t pipelineCacheKey = 0;
    bool shaderStateLowered = false;
    bool layoutStateLowered = false;
    bool pushConstantStateLowered = false;
    bool descriptorBindingStateLowered = false;
    core::pmr::Vector<NativeComputePipelineStateSemantic> semantics{
        &core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Lower an L2 compute pipeline descriptor against a backend capability
/// contract. Runs the shared `validateComputePipelineState` first; on failure
/// the decision carries `UnsupportedComputeStateDiagnostic` and the
/// validation diagnostics. On success the decision records the per-backend
/// native vocabulary semantics that apply to the descriptor.
NativeComputePipelineStateLoweringDecision lowerComputePipelineState(
    const NativeBackendCapabilityContract& contract, const rhi::ComputePipelineDesc& desc);

/// Query helper: true if `decision.semantics` contains `semantic`.
bool hasNativeComputePipelineStateSemantic(
    const NativeComputePipelineStateLoweringDecision& decision,
    NativeComputePipelineStateSemantic semantic);

} // namespace arp::rhi::semantics
