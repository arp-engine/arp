#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
class OriginalBindlessRootSequenceLowering final {
public:
    struct Decision {
        bool projectionAvailable = false, nativeExecutionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ROOT-SEQUENCE-BACKEND";
    };

    static constexpr Decision current(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {false, false, "RHI-BINDLESS-ROOT-SEQUENCE-VERSION"};
        switch (backend) {
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, nullptr};
        case Backend::WebGL:
            return {false, false, "RHI-WEBGL1-BINDLESS-ROOT-SEQUENCE-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
