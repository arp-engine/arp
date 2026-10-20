#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class OriginalGeneralFiniteInitializedContentLowering final {
public:
    template <class Decision> static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::DiagnosticFailure,
                true,
                false,
                false,
                "RHI-GENERAL-FINITE-CONTENT-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-GENERAL-FINITE-CONTENT-VULKAN-PAYLOAD-MISSING"};
        case Backend::Metal:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-GENERAL-FINITE-CONTENT-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {CapabilityMode::Equivalent, false, true, false, nullptr};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
