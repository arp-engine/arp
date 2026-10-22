// depthBiasClamp capability gate contract test.
//
// The RHI "default fail closed" rule (AGENTS.md, RHI contract section 3)
// requires that a render pipeline descriptor using a non-zero
// `depthBiasClamp` must NOT reach the native pipeline builder on a device
// that cannot honor it. Vulkan gates this behind the optional
// `depthBiasClamp` physical device feature (VUID-00754); Metal and WebGPU
// guarantee it by spec; Null reports true for parity.
//
// `lowerRenderPipelineState` consumes `NativeBackendCapabilityContract::
// depthBiasClampSupported` to enforce this: when `depthBiasEnabled &&
// depthBiasClamp != 0.0f && !depthBiasClampSupported` the decision becomes
// `DiagnosticFailure` with the `UnsupportedStateDiagnostic` semantic and a
// diagnostic mentioning `depthBiasClamp`, instead of letting the native
// path silently drop the clamp value (or, on Vulkan, emit an invalid
// pipeline).
//
// This contract test pins the four observable facets of that gate without
// requiring a live device:
//   1. the capability contract's static defaults per backend;
//   2. the fail-closed path on a Vulkan fallback contract (the only backend
//      whose static contract reports `depthBiasClampSupported == false`);
//   3. the pass-through path on Metal / WebGPU / Null (spec-guaranteed);
//   4. the gate does NOT fire when depthBiasClamp is zero (a pipeline that
//      only uses the constant/slope factors is valid on every backend,
//      including a device that lacks the depthBiasClamp feature).

#include "core/memory/cpu_allocator.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_pipeline.h"
#include "rhi/semantics/native_backend_capabilities.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "tests/rhi_shader_package_test_helpers.h"

#include <cassert>
#include <cstdint>

namespace {

arp::rhi::ShaderPackageDesc makeDepthBiasClampShaderPackage()
{
    arp::rhi::ShaderPackageDesc shaderPackage;
    shaderPackage.label = "DepthBiasClampContractShaderPackage";
    shaderPackage.slangModulePath = "depth_bias_clamp_contract.slang";
    shaderPackage.entrypoints.push_back(
        {"depth_bias_clamp_contract.slang", "vsMain", arp::rhi::ShaderStage::Vertex});
    shaderPackage.entrypoints.push_back(
        {"depth_bias_clamp_contract.slang", "fsMain", arp::rhi::ShaderStage::Fragment});
    shaderPackage.artifacts.spirvPath = "depth_bias_clamp_contract.spv";
    shaderPackage.artifacts.mslPath = "depth_bias_clamp_contract.metal";
    shaderPackage.artifacts.wgslPath = "depth_bias_clamp_contract.wgsl";
    shaderPackage.artifacts.reflectionPath = "depth_bias_clamp_contract.reflection.yaml";
    shaderPackage.reflection.pushConstantByteCount = 64;
    // Track A P5: validateShaderPackage requires the push-constant lowering
    // contract version and stage visibility whenever pushConstantByteCount > 0.
    shaderPackage.reflection.pushConstantContractVersion = "AtomicFullBlockPush/1";
    shaderPackage.reflection.pushConstantStages = "VS|FS";
    shaderPackage.reflection.pushConstantWebGpuBindingGroup = 0;
    shaderPackage.reflection.pushConstantWebGpuBindingIndex = 0;
    shaderPackage.reflection.pushConstantMetalBufferIndex = 0;
    shaderPackage.reflection.metalGpuCountedIndirectStatus = "Unavailable";
    shaderPackage.reflection.metalTessellationStatus = "Unavailable";
    arp::rhi::test::addAtomicFullBlockPushMember(shaderPackage.reflection, 64u);
    return shaderPackage;
}

// Build a depth-only shadow-caster descriptor with depthBias enabled. The
// caller tweaks `depthBiasClamp` per scenario; everything else mirrors the
// real shadow pipeline shape (depth-only, D32Float, bias enabled, non-zero
// constant + slope factors).
arp::rhi::RenderPipelineDesc makeDepthBiasPipelineDesc(const arp::rhi::PipelineLayout& layout,
    const arp::rhi::ShaderPackageDesc& shaderPackage,
    float depthBiasClampValue)
{
    using namespace arp::rhi;
    RenderPipelineDesc desc;
    desc.label = "DepthBiasClampContractCaster";
    desc.layout = &layout;
    desc.shaderPackage = &shaderPackage;
    desc.vertexShader = {"depth_bias_clamp_contract.slang", "vsMain", ShaderStage::Vertex};
    desc.fragmentShader = {"depth_bias_clamp_contract.slang", "fsMain", ShaderStage::Fragment};
    desc.colorFormat = TextureFormat::Unknown;
    desc.colorTargets.clear();
    desc.depthFormat = TextureFormat::D32Float;
    desc.depthStencil.format = TextureFormat::D32Float;
    desc.depthStencil.depthTestEnabled = true;
    desc.depthStencil.depthWriteEnabled = true;
    desc.depthStencil.depthCompare = CompareOp::LessEqual;
    desc.depthStencil.depthBiasEnabled = true;
    desc.depthStencil.depthBiasConstantFactor = 2.0f;
    desc.depthStencil.depthBiasSlopeFactor = 1.25f;
    desc.depthStencil.depthBiasClamp = depthBiasClampValue;
    desc.cullMode = CullMode::Back;
    desc.frontFace = FrontFace::CounterClockwise;
    desc.topology = PrimitiveTopology::TriangleList;
    desc.hasViewport = true;
    desc.viewport = {0.0f, 0.0f, 2048.0f, 2048.0f, 0.0f, 1.0f};
    desc.hasScissor = true;
    desc.scissor = {0, 0, 2048, 2048};
    return desc;
}

bool hasSemantic(const arp::rhi::semantics::NativePipelineStateLoweringDecision& decision,
    arp::rhi::semantics::NativePipelineStateSemantic semantic)
{
    return arp::rhi::semantics::hasNativePipelineStateSemantic(decision, semantic);
}

} // namespace

int main()
{
    using namespace arp::rhi;
    using namespace arp::rhi::semantics;

    // --- Capability contract static defaults per backend -------------------
    // These defaults are the fail-closed baseline; a real Vulkan device query
    // overwrites depthBiasClampSupported to the queried feature value at
    // runtime. Metal/WebGPU/Null report true because their specs guarantee
    // depthBiasClamp on every conformant implementation.
    {
        const auto nullContract = nativeBackendCapabilityContract(Backend::Null);
        assert(nullContract.depthBiasClampSupported);
        const auto vulkanContract = nativeBackendCapabilityContract(Backend::Vulkan);
        assert(!vulkanContract.depthBiasClampSupported);
        const auto metalContract = nativeBackendCapabilityContract(Backend::Metal);
        assert(metalContract.depthBiasClampSupported);
        const auto webGpuContract = nativeBackendCapabilityContract(Backend::WebGPU);
        assert(webGpuContract.depthBiasClampSupported);
    }

    // Build a Null device + pipeline layout + shader package once; every
    // scenario below lowers against the same descriptor shape so the only
    // variable is the capability contract / clamp value.
    auto device = createNullDevice();
    assert(device);
    PipelineLayoutDesc layoutDesc;
    layoutDesc.pushConstantRanges.push_back({ShaderStage::Vertex | ShaderStage::Fragment, 0, 64});
    auto layout = device->createPipelineLayout(layoutDesc);
    assert(layout);
    auto shaderPackage = makeDepthBiasClampShaderPackage();
    assert(validateShaderPackage(shaderPackage).success);

    // --- Fail-closed: Vulkan fallback contract + non-zero clamp ------------
    // The static Vulkan fallback contract reports depthBiasClampSupported ==
    // false (only a real device query overwrites it). A descriptor with a
    // non-zero clamp on such a device MUST fail closed instead of reaching
    // the native pipeline builder where the clamp would be silently dropped
    // or produce an invalid pipeline (VUID-00754).
    {
        const auto vulkanFallbackContract = nativeBackendCapabilityContract(Backend::Vulkan);
        assert(!vulkanFallbackContract.depthBiasClampSupported);

        auto desc = makeDepthBiasPipelineDesc(*layout, shaderPackage, 0.5f);
        const auto decision = lowerRenderPipelineState(vulkanFallbackContract, desc);

        assert(!decision.success);
        assert(decision.fallbackMode == FallbackMode::DiagnosticFailure);
        assert(hasSemantic(decision, NativePipelineStateSemantic::UnsupportedStateDiagnostic));
        // The diagnostic must name the offending capability so a developer
        // can trace the failure back to the depthBiasClamp feature gate.
        assert(decision.diagnostics.find("depthBiasClamp") != std::pmr::string::npos);
    }

    // --- Pass-through: Metal / WebGPU / Null + non-zero clamp --------------
    // These backends guarantee depthBiasClamp by spec (or, for Null, report
    // true for parity). The gate must let the descriptor through and record
    // that the depthBias state was lowered.
    {
        auto desc = makeDepthBiasPipelineDesc(*layout, shaderPackage, 0.5f);

        const auto metalDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Metal), desc);
        assert(metalDecision.success);
        assert(metalDecision.depthBiasStateLowered);

        const auto webGpuDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::WebGPU), desc);
        assert(webGpuDecision.success);
        assert(webGpuDecision.depthBiasStateLowered);

        const auto nullDecision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(Backend::Null), desc);
        assert(nullDecision.success);
        assert(nullDecision.depthBiasStateLowered);
    }

    // --- Gate does NOT fire when clamp == 0 --------------------------------
    // A shadow pipeline that only uses the constant + slope bias factors
    // (clamp left at zero) is valid on every backend, including a device
    // that lacks the depthBiasClamp feature. The gate's trigger condition is
    // `depthBiasEnabled && depthBiasClamp != 0.0f`; a zero clamp must pass
    // through even on the Vulkan fallback contract. This is the real shadow
    // pipeline shape used by the game (wocc_pbr_pipeline.cpp: shadowPd sets
    // depthBiasConstantFactor + depthBiasSlopeFactor but leaves clamp at 0).
    {
        const auto vulkanFallbackContract = nativeBackendCapabilityContract(Backend::Vulkan);
        assert(!vulkanFallbackContract.depthBiasClampSupported);

        auto desc = makeDepthBiasPipelineDesc(*layout, shaderPackage, 0.0f);
        const auto decision = lowerRenderPipelineState(vulkanFallbackContract, desc);

        assert(decision.success);
        assert(decision.fallbackMode == FallbackMode::Native);
        assert(decision.depthBiasStateLowered);
    }

    // --- Gate does NOT fire when depthBias is disabled ---------------------
    // Even with a non-zero clamp value, a descriptor that has
    // depthBiasEnabled == false does not exercise the depthBiasClamp feature
    // at all. The gate must not fire on any backend.
    {
        const auto vulkanFallbackContract = nativeBackendCapabilityContract(Backend::Vulkan);
        assert(!vulkanFallbackContract.depthBiasClampSupported);

        auto desc = makeDepthBiasPipelineDesc(*layout, shaderPackage, 0.5f);
        desc.depthStencil.depthBiasEnabled = false;
        const auto decision = lowerRenderPipelineState(vulkanFallbackContract, desc);

        assert(decision.success);
        assert(decision.fallbackMode == FallbackMode::Native);
    }

    // Disabled bias is a complete state on all backends, even when the
    // descriptor retains nonzero factors from an earlier enabled variant.
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        auto desc = makeDepthBiasPipelineDesc(*layout, shaderPackage, 0.5f);
        desc.depthStencil.depthBiasEnabled = false;
        const auto decision =
            lowerRenderPipelineState(nativeBackendCapabilityContract(backend), desc);
        assert(decision.success && !decision.depthBiasStateLowered);
    }

    return 0;
}
