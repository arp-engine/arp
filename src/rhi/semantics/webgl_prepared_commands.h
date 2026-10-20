#pragma once
#include "rhi/semantics/prepared_graphics_commands.h"

namespace arp::rhi::semantics {
class WebGlPreparedCommandsContract final {
public:
    static const char* validate(Backend backend, const WebGlPreparedCommandsRequest& r) noexcept
    {
        if (const auto* id = PreparedGraphicsCommandsContract::validate(r))
            return id;
        switch (backend) {
        case Backend::WebGL:
            return nullptr;
        case Backend::Null:
            return "RHI-WEBGL1-PREPARED-COMMAND-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return "RHI-WEBGL1-PREPARED-COMMAND-BACKEND-NOT-APPLICABLE";
        }
        return "RHI-WEBGL1-PREPARED-COMMAND-BACKEND-UNKNOWN";
    }
};
} // namespace arp::rhi::semantics
