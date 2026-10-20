#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
struct WebGlProgramCreationRequest {
    std::uint32_t contractVersion = 0;
    std::uint32_t maxInterfaceNameBytes = 0;
    std::uint32_t maxDiagnosticBytes = 0;
};

class WebGlProgramCreationContract final {
public:
    static const char* validate(Backend backend, const WebGlProgramCreationRequest& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-WEBGL1-PROGRAM-CREATION-VERSION";
        if (!r.maxInterfaceNameBytes || !r.maxDiagnosticBytes
            || r.maxInterfaceNameBytes > std::uint32_t{INT32_MAX}
            || r.maxDiagnosticBytes > std::uint32_t{INT32_MAX})
            return "RHI-WEBGL1-PROGRAM-CREATION-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-PROGRAM-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-PROGRAM-BACKEND-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-PROGRAM-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
