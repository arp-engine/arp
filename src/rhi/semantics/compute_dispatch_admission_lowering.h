#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class ComputeDispatchAdmissionLowering final {
public:
    static constexpr const char* issuanceDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-DISPATCH-DOMAIN-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::WebGPU:
            return "RHI-DISPATCH-DOMAIN-QUERIED-LIMITS";
        case Backend::Metal:
            return nullptr;
        case Backend::WebGL:
            return "RHI-DISPATCH-DOMAIN-UNSUPPORTED";
        }
        return "RHI-DISPATCH-DOMAIN-BACKEND";
    }

    static constexpr const char* reservationDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Metal:
            return nullptr;
        case Backend::Null:
            return "RHI-DISPATCH-VALIDATION-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::WebGPU:
            return "RHI-DISPATCH-VALIDATION-QUERIED-LIMITS";
        case Backend::WebGL:
            return "RHI-DISPATCH-VALIDATION-UNSUPPORTED";
        }
        return "RHI-DISPATCH-VALIDATION-BACKEND";
    }
};
} // namespace arp::rhi::semantics
