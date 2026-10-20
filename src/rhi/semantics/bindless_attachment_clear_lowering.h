#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class OriginalBindlessAttachmentClearLowering final {
public:
    struct Decision {
        bool preparationAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ATTACHMENT-CLEAR-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-ATTACHMENT-CLEAR-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, "RHI-BINDLESS-ATTACHMENT-CLEAR-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
            return {true, false, false, nullptr};
        case Backend::Metal:
            return {false, false, false, "RHI-BINDLESS-ATTACHMENT-CLEAR-METAL-UNAVAILABLE"};
        case Backend::WebGPU:
            return {false, false, false, "RHI-BINDLESS-ATTACHMENT-CLEAR-WEBGPU-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ATTACHMENT-CLEAR-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
