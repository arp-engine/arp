#include "rhi/semantics/native_pipeline_state_lowering.h"

#include "rhi/gpu_pipeline.h"

namespace arp::rhi::semantics {

namespace {

void addUnique(NativePipelineStateLoweringDecision& decision, NativePipelineStateSemantic semantic)
{
    if (!hasNativePipelineStateSemantic(decision, semantic)) {
        decision.semantics.push_back(semantic);
    }
}

NativePipelineStateLoweringDecision makeDecision(
    const NativeBackendCapabilityContract& contract, const rhi::RenderPipelineDesc& desc)
{
    NativePipelineStateLoweringDecision decision;
    decision.success = true;
    decision.backend = contract.backend;
    decision.fallbackMode = rhi::FallbackMode::Native;
    decision.nativeHandleExposedToUpperLayer = false;
    decision.backendVocabularyHiddenFromUpperLayer = true;
    decision.colorTargetCount = rhi::effectiveColorTargetCount(desc);
    decision.sampleCount = desc.multisample.sampleCount;
    // The lowering decision MUST reuse the shared stable key so that the
    // decision and the pipeline cache agree on pipeline identity. Do not
    // re-hash here.
    decision.pipelineCacheKey = rhi::stableRenderPipelineStateKey(desc);
    return decision;
}

NativePipelineStateLoweringDecision makeFailure(const NativeBackendCapabilityContract& contract,
    const rhi::RenderPipelineDesc& desc,
    const char* diagnostics)
{
    auto decision = makeDecision(contract, desc);
    decision.success = false;
    decision.fallbackMode = rhi::FallbackMode::DiagnosticFailure;
    decision.diagnostics = diagnostics;
    addUnique(decision, NativePipelineStateSemantic::UnsupportedStateDiagnostic);
    return decision;
}

bool isDepthOnly(const rhi::RenderPipelineDesc& desc)
{
    return rhi::isDepthOnlyRenderPipeline(desc);
}

bool hasVertexInputState(const rhi::RenderPipelineDesc& desc)
{
    return !desc.vertexBuffers.empty();
}

bool hasResolveState(const rhi::RenderPipelineDesc& desc)
{
    return rhi::renderPipelineRequiresResolve(desc);
}

void addNullSemantics(
    NativePipelineStateLoweringDecision& decision, const rhi::RenderPipelineDesc& desc)
{
    addUnique(decision, NativePipelineStateSemantic::NullRecordedPipelineState);
    if (isDepthOnly(desc)) {
        addUnique(decision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState);
        addUnique(decision, NativePipelineStateSemantic::NullDepthOnlyRenderPassState);
        decision.depthOnlyStateLowered = true;
    }
    if (hasVertexInputState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::NullVertexInputState);
        decision.vertexInputStateLowered = true;
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        addUnique(decision, NativePipelineStateSemantic::L2PatchListTessellation);
    }
    // Null validates but does not execute; report the same lowered flags as
    // the other backends so parity tests can assert a uniform surface.
    if (decision.colorTargetCount > 0u) {
        decision.blendWriteMaskLowered = true;
    }
    if (desc.depthStencil.stencilEnabled) {
        decision.stencilStateLowered = true;
    }
    if (desc.depthStencil.depthBiasEnabled) {
        decision.depthBiasStateLowered = true;
    }
    decision.rasterStateLowered = true;
    decision.topologyLowered = true;
    decision.viewportScissorLowered = true;
    if (hasResolveState(desc)) {
        decision.resolveStateLowered = true;
    }
}

void addVulkanSemantics(
    NativePipelineStateLoweringDecision& decision, const rhi::RenderPipelineDesc& desc)
{
    addUnique(decision, NativePipelineStateSemantic::VulkanGraphicsPipelineState);
    addUnique(decision, NativePipelineStateSemantic::VulkanRasterizationState);
    decision.rasterStateLowered = true;
    if (decision.colorTargetCount > 0u) {
        addUnique(decision, NativePipelineStateSemantic::VulkanMrtBlendState);
        decision.blendWriteMaskLowered = true;
    }
    addUnique(decision, NativePipelineStateSemantic::VulkanDepthStencilState);
    if (desc.depthStencil.stencilEnabled) {
        decision.stencilStateLowered = true;
    }
    if (desc.depthStencil.depthBiasEnabled) {
        decision.depthBiasStateLowered = true;
    }
    addUnique(decision, NativePipelineStateSemantic::VulkanDynamicViewportScissor);
    decision.viewportScissorLowered = true;
    decision.topologyLowered = true;
    if (hasResolveState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::VulkanMsaaResolveState);
        decision.resolveStateLowered = true;
    }
    if (hasVertexInputState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::VulkanVertexInputState);
        decision.vertexInputStateLowered = true;
    }
    if (isDepthOnly(desc)) {
        addUnique(decision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState);
        addUnique(decision, NativePipelineStateSemantic::VulkanDepthOnlyRenderPassState);
        decision.depthOnlyStateLowered = true;
    }
}

void addMetalSemantics(
    NativePipelineStateLoweringDecision& decision, const rhi::RenderPipelineDesc& desc)
{
    addUnique(decision, NativePipelineStateSemantic::MetalRenderPipelineDescriptor);
    addUnique(decision, NativePipelineStateSemantic::MetalRasterizationState);
    decision.rasterStateLowered = true;
    addUnique(decision, NativePipelineStateSemantic::MetalDepthStencilDescriptor);
    if (desc.depthStencil.stencilEnabled) {
        decision.stencilStateLowered = true;
    }
    if (desc.depthStencil.depthBiasEnabled) {
        decision.depthBiasStateLowered = true;
    }
    addUnique(decision, NativePipelineStateSemantic::MetalViewportScissorState);
    decision.viewportScissorLowered = true;
    decision.topologyLowered = true;
    if (hasResolveState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::MetalMsaaResolveState);
        decision.resolveStateLowered = true;
    }
    if (hasVertexInputState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::MetalVertexDescriptorState);
        decision.vertexInputStateLowered = true;
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        addUnique(decision, NativePipelineStateSemantic::L2PatchListTessellation);
        addUnique(decision, NativePipelineStateSemantic::MetalTessellationPipelineState);
    }
    if (isDepthOnly(desc)) {
        addUnique(decision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState);
        addUnique(decision, NativePipelineStateSemantic::MetalDepthOnlyRenderPassState);
        decision.depthOnlyStateLowered = true;
    }
    if (decision.colorTargetCount > 0u) {
        decision.blendWriteMaskLowered = true;
    }
}

void addWebGpuSemantics(
    NativePipelineStateLoweringDecision& decision, const rhi::RenderPipelineDesc& desc)
{
    addUnique(decision, NativePipelineStateSemantic::WebGpuRenderPipelineDescriptor);
    addUnique(decision, NativePipelineStateSemantic::WebGpuPrimitiveState);
    decision.rasterStateLowered = true;
    decision.topologyLowered = true;
    addUnique(decision, NativePipelineStateSemantic::WebGpuDepthStencilState);
    if (desc.depthStencil.stencilEnabled) {
        decision.stencilStateLowered = true;
    }
    if (desc.depthStencil.depthBiasEnabled) {
        decision.depthBiasStateLowered = true;
    }
    addUnique(decision, NativePipelineStateSemantic::WebGpuViewportScissorState);
    decision.viewportScissorLowered = true;
    if (hasResolveState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::WebGpuMsaaResolveState);
        decision.resolveStateLowered = true;
    }
    if (hasVertexInputState(desc)) {
        addUnique(decision, NativePipelineStateSemantic::WebGpuVertexState);
        decision.vertexInputStateLowered = true;
    }
    if (isDepthOnly(desc)) {
        addUnique(decision, NativePipelineStateSemantic::L2DepthOnlyRenderPassState);
        addUnique(decision, NativePipelineStateSemantic::WebGpuDepthOnlyRenderPassState);
        decision.depthOnlyStateLowered = true;
    }
    if (decision.colorTargetCount > 0u) {
        decision.blendWriteMaskLowered = true;
    }
}

void addBackendSemantics(NativePipelineStateLoweringDecision& decision,
    const rhi::RenderPipelineDesc& desc,
    const NativeBackendCapabilityContract& contract)
{
    switch (contract.backend) {
    case rhi::Backend::WebGL:
        decision = makeFailure(contract, desc, "RHI-WEBGL1-PIPELINE-EXECUTION-UNAVAILABLE");
        break;
    case rhi::Backend::Null:
        addNullSemantics(decision, desc);
        break;
    case rhi::Backend::Vulkan:
        addVulkanSemantics(decision, desc);
        break;
    case rhi::Backend::Metal:
        addMetalSemantics(decision, desc);
        break;
    case rhi::Backend::WebGPU:
        addWebGpuSemantics(decision, desc);
        break;
    }
}

} // namespace

NativePipelineStateLoweringDecision lowerRenderPipelineState(
    const NativeBackendCapabilityContract& contract, const rhi::RenderPipelineDesc& desc)
{
    // Run the shared L2 validation first. A descriptor that fails validation
    // cannot be lowered by any backend; report a deterministic diagnostic
    // failure rather than letting each backend discover the problem at native
    // pipeline creation time.
    const auto validation = rhi::validateRenderPipelineState(desc);
    if (!validation.success) {
        return makeFailure(contract, desc, validation.diagnostics.c_str());
    }

    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        if (contract.backend == rhi::Backend::Metal) {
            if (desc.shaderPackage == nullptr
                || !rhi::hasMetalTessellationAbi(
                    desc.shaderPackage->reflection, desc.patchControlPointCount)) {
                return makeFailure(
                    contract, desc, "RHI-METAL-PATCH-LIST-TESSELLATION-ABI-NOT-LOWERED");
            }
        } else if (contract.backend != rhi::Backend::Null) {
            // The shared RHI descriptor has separate control/evaluation
            // entrypoints. Backends without an approved package ABI must
            // reject the operation instead of treating PatchList as triangles.
            return makeFailure(contract, desc, "RHI-PATCH-LIST-TESSELLATION-ABI-NOT-LOWERED");
        }
    }

    auto decision = makeDecision(contract, desc);
    addBackendSemantics(decision, desc, contract);
    if (!decision.success) {
        return decision;
    }
    decision.tessellationStateLowered = desc.topology != rhi::PrimitiveTopology::PatchList
        || hasNativePipelineStateSemantic(
            decision, NativePipelineStateSemantic::MetalTessellationPipelineState)
        || contract.backend == rhi::Backend::Null;

    // L2 fact: descriptor opts into dynamic viewport/scissor. This is a
    // backend-agnostic descriptor property; every backend records the same
    // L2 semantic so parity tests can assert uniform opt-in recognition even
    // though only Vulkan changes its pipeline-creation path in response.
    // (Metal/WebGPU/Null set viewport/scissor on the encoder at record time,
    // so the opt-in does not alter their pipeline objects.)
    if (desc.dynamicViewportScissor) {
        addUnique(decision, NativePipelineStateSemantic::L2DynamicViewportScissorOptIn);
    }

    if (contract.backend == rhi::Backend::Metal
        && !sampleMaskHasFullCoverage(desc.multisample.sampleCount, desc.multisample.sampleMask)) {
        return makeFailure(contract, desc, "RHI-METAL-SAMPLE-MASK-SHADER-LOWERING-REQUIRED");
    }

    // Capability gate: a non-zero depthBiasClamp requires the backend to
    // actually honor it. Vulkan gates this behind the depthBiasClamp physical
    // device feature (VUID-00754); Metal and WebGPU guarantee it by spec and
    // Null reports true for parity. Without this gate the native path would
    // silently drop the clamp value (or, on Vulkan, emit an invalid pipeline)
    // instead of failing closed as the RHI contract requires.
    const bool depthBiasClampUsed =
        desc.depthStencil.depthBiasEnabled && desc.depthStencil.depthBiasClamp != 0.0f;
    if (depthBiasClampUsed && !contract.depthBiasClampSupported) {
        auto failure = makeFailure(contract,
            desc,
            "render pipeline uses a non-zero depthBiasClamp but the backend "
            "device does not support the depthBiasClamp capability "
            "(Vulkan VUID-00754: VkPhysicalDeviceFeatures::depthBiasClamp "
            "is not enabled); refusing to lower to avoid a silent native "
            "semantics drop");
        // Preserve the per-backend vocabulary semantics already recorded so
        // parity tests can still inspect what would have applied. makeFailure
        // already added UnsupportedStateDiagnostic, but the wholesale
        // replacement below would drop it; re-merge it so the decision still
        // carries the diagnostic semantic alongside the backend vocabulary.
        failure.semantics = decision.semantics;
        addUnique(failure, NativePipelineStateSemantic::UnsupportedStateDiagnostic);
        return failure;
    }

    return decision;
}

bool hasNativePipelineStateSemantic(
    const NativePipelineStateLoweringDecision& decision, NativePipelineStateSemantic semantic)
{
    for (const auto entry : decision.semantics) {
        if (entry == semantic) {
            return true;
        }
    }
    return false;
}

// ── Compute pipeline state lowering ──────────────────────────────────────

namespace {

void addComputeUnique(NativeComputePipelineStateLoweringDecision& decision,
    NativeComputePipelineStateSemantic semantic)
{
    if (!hasNativeComputePipelineStateSemantic(decision, semantic)) {
        decision.semantics.push_back(semantic);
    }
}

NativeComputePipelineStateLoweringDecision makeComputeDecision(
    const NativeBackendCapabilityContract& contract, const rhi::ComputePipelineDesc& desc)
{
    NativeComputePipelineStateLoweringDecision decision;
    decision.success = true;
    decision.backend = contract.backend;
    decision.fallbackMode = rhi::FallbackMode::Native;
    decision.nativeHandleExposedToUpperLayer = false;
    decision.backendVocabularyHiddenFromUpperLayer = true;
    // Reuse the shared stable key so the lowering decision and the pipeline
    // cache agree on pipeline identity. Do not re-hash here.
    decision.pipelineCacheKey = rhi::stableComputePipelineStateKey(desc);
    return decision;
}

NativeComputePipelineStateLoweringDecision makeComputeFailure(
    const NativeBackendCapabilityContract& contract,
    const rhi::ComputePipelineDesc& desc,
    const char* diagnostics)
{
    auto decision = makeComputeDecision(contract, desc);
    decision.success = false;
    decision.fallbackMode = rhi::FallbackMode::DiagnosticFailure;
    decision.diagnostics = diagnostics;
    addComputeUnique(
        decision, NativeComputePipelineStateSemantic::UnsupportedComputeStateDiagnostic);
    return decision;
}

void addNullComputeSemantics(
    NativeComputePipelineStateLoweringDecision& decision, const rhi::ComputePipelineDesc& /*desc*/)
{
    addComputeUnique(
        decision, NativeComputePipelineStateSemantic::NullRecordedComputePipelineState);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::L2ComputePipelineState);
    // Null validates but does not execute; report the same lowered flags as
    // the other backends so parity tests can assert a uniform surface.
    decision.shaderStateLowered = true;
    decision.layoutStateLowered = true;
    decision.pushConstantStateLowered = true;
    decision.descriptorBindingStateLowered = true;
}

void addVulkanComputeSemantics(
    NativeComputePipelineStateLoweringDecision& decision, const rhi::ComputePipelineDesc& /*desc*/)
{
    addComputeUnique(decision, NativeComputePipelineStateSemantic::VulkanComputePipelineState);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::VulkanComputeShaderStage);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::VulkanComputePipelineLayout);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::L2ComputePipelineState);
    decision.shaderStateLowered = true;
    decision.layoutStateLowered = true;
    decision.pushConstantStateLowered = true;
    decision.descriptorBindingStateLowered = true;
}

void addMetalComputeSemantics(
    NativeComputePipelineStateLoweringDecision& decision, const rhi::ComputePipelineDesc& /*desc*/)
{
    addComputeUnique(decision, NativeComputePipelineStateSemantic::MetalComputePipelineState);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::MetalComputeShaderFunction);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::MetalComputePipelineLayout);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::L2ComputePipelineState);
    decision.shaderStateLowered = true;
    decision.layoutStateLowered = true;
    decision.pushConstantStateLowered = true;
    decision.descriptorBindingStateLowered = true;
}

void addWebGpuComputeSemantics(
    NativeComputePipelineStateLoweringDecision& decision, const rhi::ComputePipelineDesc& /*desc*/)
{
    addComputeUnique(decision, NativeComputePipelineStateSemantic::WebGpuComputePipelineDescriptor);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::WebGpuComputeShaderModule);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::WebGpuComputeBindGroupLayout);
    addComputeUnique(decision, NativeComputePipelineStateSemantic::L2ComputePipelineState);
    decision.shaderStateLowered = true;
    decision.layoutStateLowered = true;
    decision.pushConstantStateLowered = true;
    decision.descriptorBindingStateLowered = true;
}

void addBackendComputeSemantics(NativeComputePipelineStateLoweringDecision& decision,
    const rhi::ComputePipelineDesc& desc,
    const NativeBackendCapabilityContract& contract)
{
    switch (contract.backend) {
    case rhi::Backend::WebGL:
        decision = makeComputeFailure(contract, desc, "RHI-WEBGL1-COMPUTE-UNSUPPORTED");
        break;
    case rhi::Backend::Null:
        addNullComputeSemantics(decision, desc);
        break;
    case rhi::Backend::Vulkan:
        addVulkanComputeSemantics(decision, desc);
        break;
    case rhi::Backend::Metal:
        addMetalComputeSemantics(decision, desc);
        break;
    case rhi::Backend::WebGPU:
        addWebGpuComputeSemantics(decision, desc);
        break;
    }
}

} // namespace

NativeComputePipelineStateLoweringDecision lowerComputePipelineState(
    const NativeBackendCapabilityContract& contract, const rhi::ComputePipelineDesc& desc)
{
    // Run the shared L2 validation first. A descriptor that fails validation
    // cannot be lowered by any backend; report a deterministic diagnostic
    // failure rather than letting each backend discover the problem at native
    // compute pipeline creation time.
    const auto validation = rhi::validateComputePipelineState(desc);
    if (!validation.success) {
        return makeComputeFailure(contract, desc, validation.diagnostics.c_str());
    }

    auto decision = makeComputeDecision(contract, desc);
    addBackendComputeSemantics(decision, desc, contract);
    return decision;
}

bool hasNativeComputePipelineStateSemantic(
    const NativeComputePipelineStateLoweringDecision& decision,
    NativeComputePipelineStateSemantic semantic)
{
    for (const auto entry : decision.semantics) {
        if (entry == semantic) {
            return true;
        }
    }
    return false;
}

} // namespace arp::rhi::semantics
