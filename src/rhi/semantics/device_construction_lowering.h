#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Backend construction mapping only. A known identity grants no runtime
// feature, memory, synchronization or executable payload capability.
class DeviceConstructionBackendAdmission final {
public:
    static constexpr const char* diagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return nullptr;
        case Backend::WebGL:
            return "RHI-WEBGL1-DEVICE-CONSTRUCTION-UNAVAILABLE";
        }
        return "RHI-DEVICE-CONSTRUCTION-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
