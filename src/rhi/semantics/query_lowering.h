#pragma once
#include "core/utf8.h"
#include "rhi/gpu_common.h"

#include <string_view>

namespace arp::rhi::semantics {
// Versioned value-only decisions. Template results preserve existing public decision types.
struct OriginalQueryCreationFacts {
    bool timestampFeature = false, create = false, pushScope = false, popScope = false;
    bool countAccess = false, typeAccess = false, destroy = false;
};

class QueryLowering final {
public:
    template <class Result>
    static constexpr Result QueryCommandPoints_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-QUERY-POINTS-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-QUERY-POINTS-NATIVE-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result QueryPoolInputSnapshot_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    template <class Result>
    static constexpr Result QuerySlotDeclarationControl_lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, true, "RHI-QUERY-SLOT-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {false, true, "RHI-QUERY-SLOT-NATIVE-LIFECYCLE-UNIMPLEMENTED"};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        }
        return {};
    }

    struct CreationDecision {
        CapabilityMode mode;
        bool validatorOnly;
        const char* diagnostic;
    };

    static CreationDecision originalCreation(Backend backend,
        QueryType type,
        std::uint32_t count,
        std::string_view label,
        const OriginalQueryCreationFacts& facts,
        std::uint32_t maximumWebGpuQueryCount) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure, true, "RHI-QUERY-CREATION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                "RHI-QUERY-CREATION-NATIVE-UNIMPLEMENTED"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure, false, "RHI-WEBGL1-QUERY-UNSUPPORTED"};
        case Backend::WebGPU:
            if (type != QueryType::Timestamp)
                return {CapabilityMode::Degraded,
                    false,
                    "RHI-WEBGPU-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED"};
            if (label.find('\0') != std::string_view::npos || core::Utf8Decoder::validate(label))
                return {CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-LABEL"};
            if (count > maximumWebGpuQueryCount)
                return {
                    CapabilityMode::DiagnosticFailure, false, "RHI-WEBGPU-QUERY-CREATION-COUNT"};
            if (!facts.timestampFeature)
                return {CapabilityMode::Degraded, false, "RHI-WEBGPU-QUERY-CREATION-FEATURE"};
            if (!facts.create || !facts.pushScope || !facts.popScope || !facts.countAccess
                || !facts.typeAccess || !facts.destroy)
                return {CapabilityMode::Degraded, false, "RHI-WEBGPU-QUERY-CREATION-API"};
            return {CapabilityMode::Native, false, nullptr};
        }
        return {CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-BACKEND"};
    }
};
} // namespace arp::rhi::semantics
