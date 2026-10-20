#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Versioned value-only decisions. Template results preserve existing public decision types.
class DescriptorLowering final {
public:
    template <class Decision>
    static constexpr Decision OriginalTextureCellDescriptor_lowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version < 1u || version > 4u)
            return {false, false, false, "RHI-TEXTURE-CELL-DESCRIPTOR-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-TEXTURE-CELL-DESCRIPTOR-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
            return (version == 1u || version == 3u)
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-TEXTURE-CELL-DESCRIPTOR-NATIVE-PROFILE"};
        case Backend::WebGPU:
            return (version == 2u || version == 4u)
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-TEXTURE-CELL-DESCRIPTOR-WEBGPU-PROFILE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-TEXTURE-CELL-DESCRIPTOR-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalBufferRangeDescriptor_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
            return {false, true, false, nullptr};
        case Backend::Metal:
            return {false, false, false, "RHI-BUFFER-RANGE-DESCRIPTOR-METAL-ISSUER-MISSING"};
        // Explicit OriginalBufferRangeDescriptor1 publication only. Physical
        // group preparation authenticates the same snapshot/lease and queried
        // limits; this decision grants no shader or completion capability.
        case Backend::WebGPU:
            return {false, true, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BUFFER-RANGE-DESCRIPTOR-UNAVAILABLE"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision OriginalCompletedTextureDescriptor_lowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u && version != 5u
            && version != 6u)
            return {false, false, false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return (version == 1u || version == 4u || version == 5u)
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-VULKAN-PROFILE"};
        // Existing Publication/1 consumes a completed queried-allocation
        // upload (control/3). Cold publication grants no shader/state authority.
        case Backend::Metal:
            return (version == 1u || version == 4u || version == 5u)
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-METAL-PROFILE"};
        case Backend::WebGPU:
            return (version == 2u || version == 3u || version == 6u)
                ? Decision{false, true, false, nullptr}
                : Decision{false, false, false, "RHI-COMPLETED-TEXTURE-DESCRIPTOR-WEBGPU-MISSING"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-COMPLETED-TEXTURE-DESCRIPTOR-UNAVAILABLE"};
        }
        return {};
    }

    template <class Decision>
    static constexpr Decision OriginalGeneralDescriptorBinding_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-BINDING-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-BINDING-VULKAN-ADAPTER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-BINDING-METAL-ADAPTER-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-GENERAL-BINDING-UNAVAILABLE"};
        }
        return {};
    }

    template <class Decision, class Facts>
    static constexpr Decision OriginalDepthTablePublication_decide(
        Backend backend, Facts f) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PUBLICATION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PUBLICATION-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PUBLICATION-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            if (f.device && f.group && f.layoutQuery && f.pushScope && f.popScope)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TABLE-PUBLICATION-NATIVE-METHODS"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-TABLE-PUBLICATION-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalTargetMemoryInterval_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-TARGET-INTERVAL-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TARGET-INTERVAL-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-TARGET-INTERVAL-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-TARGET-INTERVAL-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalDescriptorBufferBinding_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-DESCRIPTOR-BUFFER-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-DESCRIPTOR-BUFFER-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-DESCRIPTOR-BUFFER-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-DESCRIPTOR-BUFFER-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalDescriptorBufferBinding_intervalDiagnosticLowering(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-RUNTIME-INTERVAL-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-RUNTIME-INTERVAL-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-RUNTIME-INTERVAL-UNAVAILABLE"};
        }
        return {CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            "RHI-ORIGINAL-RUNTIME-INTERVAL-BACKEND"};
    }

    template <class Result, class Value0>
    static constexpr Result OriginalFiniteDescriptorWrite_decide(Backend backend, Value0 f) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-FINITE-WRITE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-FINITE-WRITE-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"};
        case Backend::WebGPU:
            if (f.device && f.group && f.layoutQuery && f.pushScope && f.popScope)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-FINITE-WRITE-NATIVE-METHODS"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-FINITE-WRITE-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalFiniteDescriptorWrite_sourceDiagnosticDecision(
        Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-FINITE-WRITE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-FINITE-WRITE-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-FINITE-SOURCE-DIAGNOSTIC-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Value0>
    static constexpr Result OriginalFiniteTargetMemoryWrite_decide(
        Backend backend, Value0 f) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-TARGET-FINITE-WRITE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TARGET-FINITE-WRITE-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TARGET-FINITE-WRITE-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            if (f.device && f.group && f.layoutQuery && f.pushScope && f.popScope)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-TARGET-FINITE-WRITE-NATIVE-METHODS"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-TARGET-FINITE-WRITE-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result, class Value0>
    static constexpr Result OriginalDescriptorNativeIdentity_lowering(
        Backend backend, Value0 profile) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-GROUP-IDENTITY-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GROUP-IDENTITY-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GROUP-IDENTITY-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            if (profile == Value0::FiniteWrite1 || profile == Value0::FiniteTargetMemory1)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            // Whole identity additionally requires final L2 commit, real scopes,
            // original lifetime/revision and no pending replacement.
            if (profile == Value0::WholeWrite1 || profile == Value0::GeneralWrite1
                || profile == Value0::GeneralWrite2)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GROUP-IDENTITY-PROFILE"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-GROUP-IDENTITY-UNAVAILABLE"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result OriginalRenderDescriptorUsage_lowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u && version != 3u && version != 4u)
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-RENDER-USAGE-VERSION"};
        // /2 adds full-view table references at every render binding, even
        // when shadowed before Draw. State and native execution remain closed.
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-RENDER-USAGE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-RENDER-USAGE-VULKAN-ACTOR-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-RENDER-USAGE-METAL-ACTOR-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-RENDER-USAGE-UNAVAILABLE"};
        }
        return {CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            false,
            "RHI-ORIGINAL-RENDER-USAGE-BACKEND-INVALID"};
    }

    template <class Decision, class Facts>
    static constexpr Decision OriginalGeneralDescriptorWrite_decide(
        Backend backend, Facts f) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-WRITE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-WRITE-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-WRITE-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            if (f.device && f.group && f.layoutQuery && f.pushScope && f.popScope)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-GENERAL-WRITE-NATIVE-METHODS"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-GENERAL-WRITE-UNAVAILABLE"};
        }
        return {CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            "RHI-ORIGINAL-GENERAL-WRITE-BACKEND"};
    }

    template <class Decision, class Facts>
    static constexpr Decision OriginalWholeDescriptorWrite_decide(Backend backend, Facts f) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-ORIGINAL-WHOLE-WRITE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-WHOLE-WRITE-VULKAN-ISSUER-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-WHOLE-WRITE-METAL-ISSUER-MISSING"};
        case Backend::WebGPU:
            if (f.device && f.group && f.layoutQuery && f.pushScope && f.popScope)
                return {CapabilityMode::Equivalent, false, true, false, nullptr};
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-WHOLE-WRITE-NATIVE-METHODS"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-WHOLE-WRITE-UNAVAILABLE"};
        }
        return {CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            "RHI-ORIGINAL-WHOLE-WRITE-BACKEND"};
    }
};
} // namespace arp::rhi::semantics
