#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
struct WebGlSurfaceLifecycleRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint32_t maxMutations = 0;
};

class WebGlSurfaceLifecycleContract final {
public:
    static const char* validate(Backend backend, const WebGlSurfaceLifecycleRequest& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-WEBGL1-SURFACE-LIFECYCLE-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-WEBGL1-SURFACE-LIFECYCLE-PROTOTYPE-OPT-IN";
        if (!r.maxMutations)
            return "RHI-WEBGL1-SURFACE-LIFECYCLE-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-SURFACE-LIFECYCLE-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-SURFACE-LIFECYCLE-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-SURFACE-LIFECYCLE-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
