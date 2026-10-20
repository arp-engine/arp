#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
struct WebGlCpuRetirementRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
};

struct WebGlCpuRetirementReport {
    bool success = false;
    bool admissionClosed = false;
    bool gpuCompletionKnown = false;
    std::uint64_t retainedResourceCount = 0;
    const char* diagnostic = nullptr;
};

class WebGlCpuRetirementContract final {
public:
    static const char* validate(Backend backend, const WebGlCpuRetirementRequest& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-WEBGL1-CPU-RETIREMENT-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-WEBGL1-CPU-RETIREMENT-PROTOTYPE-OPT-IN";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-CPU-RETIREMENT-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-CPU-RETIREMENT-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-CPU-RETIREMENT-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
