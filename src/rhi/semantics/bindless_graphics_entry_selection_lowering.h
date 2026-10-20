#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class OriginalBindlessGraphicsEntrySelectionLowering final {
public:
    struct Decision {
        bool inspectionAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-GRAPHICS-ENTRY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-GRAPHICS-ENTRY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-GRAPHICS-ENTRY-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
