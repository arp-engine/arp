#pragma once
#include "rhi/frontend/memory_cache_budget.h"
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
struct WebGlDeviceCompositionRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint64_t maxCpuMetadataBytes = 0;
    CacheBudget pipelines, descriptors;
};

class WebGlDeviceCompositionContract final {
public:
    static const char* validate(Backend backend, const WebGlDeviceCompositionRequest& r) noexcept
    {
        if (r.contractVersion != 1)
            return "RHI-WEBGL1-DEVICE-COMPOSITION-VERSION";
        if (!r.prototypeOptIn)
            return "RHI-WEBGL1-DEVICE-COMPOSITION-PROTOTYPE-OPT-IN";
        if (!r.maxCpuMetadataBytes || !r.pipelines.valid() || !r.descriptors.valid())
            return "RHI-WEBGL1-DEVICE-COMPOSITION-BUDGET";
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-DEVICE-COMPOSITION-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-DEVICE-COMPOSITION-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-DEVICE-COMPOSITION-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
