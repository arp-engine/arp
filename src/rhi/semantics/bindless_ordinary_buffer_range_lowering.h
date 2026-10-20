#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Ordinary Graphics range metadata (/1 static, /2 runtime capacity) and mandatory pre-record range
// admission over the same authenticated original Root. No member-layout equivalence or
// execution, recording, state, content or completion authority is issued.
class OriginalBindlessOrdinaryBufferRangeLowering final {
public:
    struct Decision {
        bool available = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-ORDINARY-RANGE-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u && version != 2u)
            return {false, false, false, "RHI-BINDLESS-ORDINARY-RANGE-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-WEBGL1-BINDLESS-ORDINARY-RANGE-UNSUPPORTED"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
