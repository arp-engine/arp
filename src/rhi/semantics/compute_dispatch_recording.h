#pragma once

#include "rhi/gpu_commands.h"

namespace arp::rhi::semantics {

// Finite device-issued coverage is an independent Recording3 subdomain.
// Eligibility to inspect the proof grants no recording or submit payload.
class ComputeDispatchRecordingContract final {
public:
    struct Decision {
        bool validatorOnly = false, inspectionAvailable = false, executionAvailable = false;
        const char* diagnostic = "RHI-DISPATCH-RECORDING-BACKEND";
    };

    static constexpr const char* validateIntent(std::uint32_t version, bool optIn) noexcept
    {
        if (!version && !optIn)
            return nullptr;
        if (version != 1u)
            return "RHI-DISPATCH-RECORDING-VERSION";
        return optIn ? nullptr : "RHI-DISPATCH-RECORDING-OPT-IN";
    }

    static constexpr Decision lowering(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, "RHI-DISPATCH-RECORDING-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, "RHI-DISPATCH-RECORDING-QUERIED-LIMITS"};
        case Backend::Metal:
            return {false, true, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, "RHI-DISPATCH-RECORDING-UNSUPPORTED"};
        }
        return {};
    }
};

} // namespace arp::rhi::semantics
