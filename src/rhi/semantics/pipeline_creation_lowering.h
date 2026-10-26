#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Versioned value-only decisions. Template results preserve existing public decision types.
class PipelineCreationLowering final {
public:
    template <class Result>
    static constexpr Result synchronousComputeCreation(
        Backend backend, bool device, bool methods) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-SYNC-COMPUTE-CREATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
            if (!device || !methods)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    !device ? "RHI-SYNC-COMPUTE-CREATION-DEVICE"
                            : "RHI-SYNC-COMPUTE-CREATION-METHODS"};
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-SYNC-COMPUTE-CREATION-WEBGPU-ASYNC-REQUIRED"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-SYNC-COMPUTE-CREATION-WEBGL-UNAVAILABLE"};
        }
        return {};
    }

    // SPIR-V capability numbers are grammar values, not heuristic feature IDs.
    // This finite profile admits Shader and proven-enabled DrawParameters only.
    static constexpr const char* synchronousVulkanGraphicsShaderCapability(
        std::uint32_t capability, bool drawParametersEnabled) noexcept
    {
        if (capability == 1u)
            return nullptr;
        if (capability == 4427u)
            return drawParametersEnabled ? nullptr
                                         : "RHI-SYNC-GRAPHICS-CREATION-DRAW-PARAMETERS-UNAVAILABLE";
        return "RHI-SYNC-GRAPHICS-CREATION-SPIRV-CAPABILITY";
    }

    template <class Result>
    static constexpr Result synchronousGraphicsCreation(
        Backend backend, bool device, bool methods) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-SYNC-GRAPHICS-CREATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
            if (!device || !methods)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    !device ? "RHI-SYNC-GRAPHICS-CREATION-DEVICE"
                            : "RHI-SYNC-GRAPHICS-CREATION-METHODS"};
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-SYNC-GRAPHICS-CREATION-WEBGPU-ASYNC-REQUIRED"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class Value0>
    static constexpr Result OriginalAcceptedPipelineFactory_lowering(
        Backend backend, bool accepted, Value0 d) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure, true, false, false, d.nullBackend};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure, false, false, false, d.vulkan};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure, false, false, false, d.metal};
        case Backend::WebGPU:
            return accepted
                ? Result{CapabilityMode::Native, false, true, false, nullptr}
                : Result{CapabilityMode::DiagnosticFailure, false, false, false, d.missing};
        case Backend::WebGL:
            break;
        }
        return {CapabilityMode::DiagnosticFailure, false, false, false, d.backend};
    }

    template <class Result>
    static constexpr Result OriginalPipelineArtifactConsumption_lowering(
        Backend backend, bool nativeDeviceAvailable) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, false, nullptr};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-ARTIFACT-VULKAN-COMPUTE-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-ARTIFACT-METAL-COMPUTE-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return nativeDeviceAvailable ? Result{CapabilityMode::Native, false, false, nullptr}
                                         : Result{CapabilityMode::DiagnosticFailure,
                                               false,
                                               false,
                                               "RHI-ORIGINAL-ARTIFACT-WEBGPU-DEVICE-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class Value0>
    static Result OriginalDepthTableExpansionCreation_lowering(
        Backend backend, const Value0& emission) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-ORIGINAL-TABLE-CREATION-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-CREATION-VULKAN-PAYLOAD"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-CREATION-METAL-PAYLOAD"};
        case Backend::WebGPU:
            if (emission.mode != CapabilityMode::Native || !emission.emissionAvailable
                || emission.validatorOnly || emission.diagnostic)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    emission.diagnostic ? emission.diagnostic
                                        : "RHI-ORIGINAL-TABLE-CREATION-EMISSION"};
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static Result OriginalDepthTableExpansionCreation_wrapperLowering(
        Backend backend, bool readyOriginalActor, bool actualDescriptorLimits) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-ORIGINAL-TABLE-WRAPPER-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-WRAPPER-VULKAN-PAYLOAD"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-WRAPPER-METAL-PAYLOAD"};
        case Backend::WebGPU:
            if (!readyOriginalActor)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-TABLE-WRAPPER-CREATION"};
            if (!actualDescriptorLimits)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-TABLE-WRAPPER-DESCRIPTOR-LIMITS"};
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class Value0, class Value1, class Value2, class Validator3>
    static Result OriginalDepthTableExpansionPipelinePreflight_lowering(Backend backend,
        const Value0& r,
        const Value1& f,
        const Value2& l,
        Validator3 validateLimits) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PREFLIGHT-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PREFLIGHT-VULKAN-PAYLOAD"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PREFLIGHT-METAL-PAYLOAD"};
        case Backend::WebGPU:
            if (!f.device)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-TABLE-PREFLIGHT-DEVICE"};
            if (!f.shader || !f.pipeline || !f.bindGroupLayout || !f.pipelineLayout || !f.pushScope
                || !f.popScope)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-TABLE-PREFLIGHT-METHODS"};
            if (const auto* id = validateLimits(r, l))
                return {CapabilityMode::DiagnosticFailure, false, false, false, false, id};
            return {CapabilityMode::Native, false, true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result, class Value0>
    static Result OriginalDepthTableExpansionNativeEmission_lowering(
        Backend backend, const Value0& preflight) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-EMISSION-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-EMISSION-VULKAN-PAYLOAD"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-EMISSION-METAL-PAYLOAD"};
        case Backend::WebGPU:
            if (!preflight.preflightEligible || preflight.mode != CapabilityMode::Native
                || preflight.diagnostic)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    false,
                    preflight.diagnostic ? preflight.diagnostic
                                         : "RHI-ORIGINAL-TABLE-EMISSION-PREFLIGHT"};
            return {CapabilityMode::Native, false, true, false, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalGraphicsArtifactConsumption_lowering(
        Backend backend, bool liveNativeDevice) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent, true, false, nullptr};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                "RHI-ORIGINAL-GRAPHICS-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return liveNativeDevice ? Result{CapabilityMode::Native, false, false, nullptr}
                                    : Result{CapabilityMode::DiagnosticFailure,
                                          false,
                                          false,
                                          "RHI-ORIGINAL-GRAPHICS-WEBGPU-DEVICE-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
    enum class ArtifactRole { ValidatorOnly, Spirv, Metal, Wgsl, DiagnosticFailure };

    static constexpr ArtifactRole artifactRole(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return ArtifactRole::ValidatorOnly;
        case Backend::Vulkan:
            return ArtifactRole::Spirv;
        case Backend::Metal:
            return ArtifactRole::Metal;
        case Backend::WebGPU:
            return ArtifactRole::Wgsl;
        case Backend::WebGL:
            break;
        }
        return ArtifactRole::DiagnosticFailure;
    }
};
} // namespace arp::rhi::semantics
