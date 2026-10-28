#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class TransferUploadLowering final {
public:
    template <class Decision>
    static constexpr Decision poolInitialization(
        Backend backend, bool registeredDestination) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-NULL-VALIDATOR",
                registeredDestination};
        case Backend::Vulkan:
            return {false, true, false, nullptr, registeredDestination};
        case Backend::Metal:
            return {false,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-METAL-PAYLOAD-MISSING",
                registeredDestination};
        case Backend::WebGPU:
            return {false,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-WEBGPU-PAYLOAD-MISSING",
                registeredDestination};
        case Backend::WebGL:
            return {false,
                false,
                false,
                "RHI-TEXTURE-POOL-INITIALIZATION-WEBGL-UNSUPPORTED",
                registeredDestination};
        }
        return {};
    }

    template <class Decision> static constexpr Decision ordinaryRecording(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-NULL-VALIDATOR"};
        case Backend::Vulkan:
            return {false, true, true, nullptr};
        case Backend::Metal:
            return {false, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-METAL-PAYLOAD-MISSING"};
        case Backend::WebGPU:
            return {false, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-WEBGPU-PAYLOAD-MISSING"};
        case Backend::WebGL:
            return {false, false, true, "RHI-ORDINARY-TEXTURE-UPLOAD-WEBGL-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
