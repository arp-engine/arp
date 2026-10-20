#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Declaration-to-retained-buffer correspondence only. No shader operation,
// native closure, recording, submission or completion authority is issued.
class OriginalBindlessOrdinaryMemoryLowering final {
public:
    struct Decision {
        bool inspectionAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORDINARY-MEMORY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {false, false, false, "RHI-BINDLESS-ORDINARY-MEMORY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-MEMORY-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
