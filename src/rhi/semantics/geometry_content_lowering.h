#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Independent geometry execution /1 consumes initialized Fetch2 and the same
// RootState5, ContentPlan9/Geometry1 and Lifecycle9 seal. It is not scene-wide
// bindless capability, and cold geometry correspondence cannot grant it.
class OriginalBindlessGeometryExecutionLowering final {
public:
    // Independent Geometry/1 over WebGPU's existing Graphics callback profile.
    // Native fence backends retain their separate Closure3/Lifecycle9 contract.
    static constexpr const char* callbackClosureDiagnostic(
        Backend backend, std::uint32_t version, bool optIn) noexcept
    {
        if (!version && !optIn)
            return nullptr;
        if (version != 1u || !optIn)
            return "RHI-BINDLESS-ROOT-GEOMETRY-ATTACHMENT-PROFILE";
        switch (backend) {
        case Backend::Null:
        case Backend::WebGPU:
            return nullptr;
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGL:
            return "RHI-BINDLESS-ROOT-GEOMETRY-ATTACHMENT-BACKEND-PROFILE";
        }
        return "RHI-BINDLESS-ROOT-GEOMETRY-ATTACHMENT-BACKEND-PROFILE";
    }

    struct Decision {
        bool validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-GEOMETRY-EXECUTION-BACKEND";
    };

    static constexpr Decision current(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, "RHI-BINDLESS-GEOMETRY-EXECUTION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, "RHI-BINDLESS-GEOMETRY-EXECUTION-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {false, true, nullptr};
        case Backend::Metal:
            return {false, true, nullptr};
        case Backend::WebGPU:
            return {false, false, "RHI-BINDLESS-GEOMETRY-EXECUTION-WEBGPU-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-BINDLESS-GEOMETRY-UNSUPPORTED"};
        }
        return {};
    }
};

// Custody of genuine mapped immutable Shader/2 content inside an existing
// native Root closure. This decision grants neither recording nor execution.
class OriginalBindlessInitializedShaderLowering final {
public:
    struct Decision {
        bool validatorOnly = false, correspondenceAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-INITIALIZED-SHADER-BACKEND";
    };

    static constexpr Decision current(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-INITIALIZED-SHADER-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-BINDLESS-INITIALIZED-SHADER-NULL-VALIDATOR"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, true, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-INITIALIZED-SHADER-UNSUPPORTED"};
        }
        return {};
    }
};

class OriginalHostGeometryContentLowering final {
public:
    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, initializationAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-HOST-GEOMETRY-CONTENT-BACKEND-UNKNOWN";
    };

    static constexpr Decision current(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u && version != 2u)
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-HOST-GEOMETRY-CONTENT-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-HOST-GEOMETRY-CONTENT-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::Metal:
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-WEBGL1-HOST-GEOMETRY-CONTENT-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
