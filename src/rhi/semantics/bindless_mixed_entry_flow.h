#pragma once
#include "rhi/gpu_bindless.h"

namespace arp::rhi::semantics {
// Entry-flow validation consumes authentic Root metadata. It never lowers a
// command, issues completion or promotes a cold mixed preparation to execution.
class OriginalBindlessMixedEntryFlowLowering final {
public:
    struct Decision {
        bool validationAvailable = false, validatorOnly = false;
        bool executionAvailable = false, completionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-MIXED-ENTRY-FLOW-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, false, "RHI-BINDLESS-MIXED-ENTRY-FLOW-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, false, nullptr};
        case Backend::Vulkan:
            return {true, false, false, false, nullptr};
        case Backend::Metal:
            return {true, false, false, false, nullptr};
        case Backend::WebGPU:
            return {true, false, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, false, "RHI-BINDLESS-MIXED-ENTRY-FLOW-WEBGL-UNAVAILABLE"};
        }
        return {};
    }
};
} // namespace arp::rhi::semantics
