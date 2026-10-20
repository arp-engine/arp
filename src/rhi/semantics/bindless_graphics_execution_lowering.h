#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Public adapter for the original Graphics/9 issuer. This says nothing about
// ordinary pass encoders, implicit sampling or the complete scene workload.
class OriginalBindlessGraphicsExecutionLowering final {
public:
    struct Decision {
        bool executionAvailable = false, completionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-GRAPHICS-EXECUTION-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version == 2u) {
            switch (backend) {
            case Backend::Null:
                return {false, false, "RHI-GRAPHICS-DEPTH-EXECUTION-NULL-VALIDATOR"};
            case Backend::Vulkan:
                return {true, true, nullptr};
            case Backend::Metal:
                return {true, true, nullptr};
            case Backend::WebGPU:
                return {false, false, "RHI-GRAPHICS-DEPTH-EXECUTION-WEBGPU-UNAVAILABLE"};
            case Backend::WebGL:
                return {false, false, "RHI-GRAPHICS-DEPTH-EXECUTION-WEBGL-UNAVAILABLE"};
            }
            return {};
        }
        if (version != 1u)
            return {false, false, "RHI-BINDLESS-GRAPHICS-EXECUTION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {false, false, "RHI-BINDLESS-GRAPHICS-EXECUTION-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {true, true, nullptr};
        case Backend::Metal:
            return {true, true, nullptr};
        case Backend::WebGPU:
            return {false, false, "RHI-BINDLESS-GRAPHICS-EXECUTION-WEBGPU-UNAVAILABLE"};
        case Backend::WebGL:
            break;
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
