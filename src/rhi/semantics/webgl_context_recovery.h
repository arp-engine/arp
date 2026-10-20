#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
enum class WebGlContextPhase { Ready, Lost, RestorePending, Rebuilding, Failed };

struct WebGlContextRecoveryRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint32_t maxRestorations = 0;
};

class WebGlContextRecoveryContract final {
public:
    static const char* validate(
        Backend backend, const WebGlContextRecoveryRequest& request) noexcept
    {
        if (request.contractVersion != 1)
            return "RHI-WEBGL1-RECOVERY-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-WEBGL1-RECOVERY-PROTOTYPE-OPT-IN";
        if (!request.maxRestorations)
            return "RHI-WEBGL1-RECOVERY-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-RECOVERY-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-RECOVERY-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-RECOVERY-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
