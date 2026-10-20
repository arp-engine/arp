#pragma once

#include "rhi/frontend/command_model.h"
#include "rhi/gpu_common.h"

// region original_command_descriptor_consumption
namespace arp::rhi::semantics {
class OriginalCommandDescriptorConsumptionLowering final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, projectionAvailable = false, stateAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-CONSUMPTION-BACKEND";
    };

    static constexpr Decision indexedDrawLowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-INDEXED-CONSUMPTION-UNAVAILABLE"};
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-INDEXED-CONSUMPTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-INDEXED-CONSUMPTION-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-INDEXED-CONSUMPTION-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, false, nullptr};
        }
        return {};
    }

    // Independent shader-only projection; never authorizes geometry state,
    // immutable content, recording, native submission or completion.
    static constexpr Decision vertexStreamShaderLowering(
        Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONSUMPTION-VERTEX-STREAM-VERSION"};
        switch (backend) {
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-VERTEX-SHADER-CONSUMPTION-UNAVAILABLE"};
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-VERTEX-SHADER-CONSUMPTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-VERTEX-SHADER-CONSUMPTION-VULKAN-JOIN-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-VERTEX-SHADER-CONSUMPTION-METAL-JOIN-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, false, nullptr};
        }
        return {};
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t contractVersion = 1u) noexcept
    {
        if (contractVersion != 1u && contractVersion != 2u && contractVersion != 3u
            && contractVersion != 4u)
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONSUMPTION-VERSION"};
        // /2 adds exact full-view depth-table access projection only. Neither
        // version supplies state, recording, queue or completion authority.
        switch (backend) {
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-WEBGL1-ORIGINAL-CONSUMPTION-UNAVAILABLE"};
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONSUMPTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONSUMPTION-VULKAN-ACTOR-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                false,
                "RHI-ORIGINAL-CONSUMPTION-METAL-ACTOR-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, false, nullptr};
        }
        return {};
    }
};

// endregion original_command_descriptor_consumption

// region original_command_admission

class OriginalCommandAdmissionLowering final {
public:
    enum class Disposition { ValidatorOnly, DiagnosticFailure };

    struct Decision {
        std::uint32_t contractVersion = 1u;
        Disposition disposition = Disposition::DiagnosticFailure;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-COMMAND-BACKEND-UNKNOWN";
    };

    // Version-controlled absence of a full original-source recording/completion
    // payload on each backend. Selected private/owned prototypes cannot satisfy
    // this contract. No caller facts or backend-name inference can open it.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::WebGL:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                "RHI-WEBGL1-ORIGINAL-COMMAND-UNAVAILABLE"};
        case Backend::Null:
            return {
                1u, Disposition::ValidatorOnly, false, "RHI-ORIGINAL-COMMAND-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-COMMAND-VULKAN-UNIMPLEMENTED"};
        case Backend::Metal:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-COMMAND-METAL-UNIMPLEMENTED"};
        case Backend::WebGPU:
            return {1u,
                Disposition::DiagnosticFailure,
                false,
                "RHI-ORIGINAL-COMMAND-WEBGPU-UNIMPLEMENTED"};
        }
        return {};
    }

    static constexpr const char* executionDiagnostic(
        Backend backend, const OriginalCommandAdmissionBudget& b) noexcept
    {
        if (const auto* id = OriginalCommandAdmissionContract::validateIntent(b))
            return id;
        return lowering(backend).diagnostic;
    }
};
} // namespace arp::rhi::semantics

// endregion original_command_admission
