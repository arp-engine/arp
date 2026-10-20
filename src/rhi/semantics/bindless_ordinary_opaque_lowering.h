#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// OrdinaryOpaqueBinding/1 is original-owner inspection only. Native resource
// format/usage, prepared descriptor publication, completed image cells and real
// completion must independently authorize any later execution profile.
class OriginalBindlessOrdinaryOpaqueLowering final {
public:
    struct Decision {
        bool inspectionAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORDINARY-OPAQUE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-ORDINARY-OPAQUE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-OPAQUE-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
