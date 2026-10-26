#pragma once

#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {

// A decision to prepare cold validation is never proof that validation ran,
// and never enables a command, ordinary preparation profile or public gate.
class ComputeDispatchValidationLowering final {
public:
    struct Facts {
        bool originalDeviceCurrent = false;
        bool nativeDevice = false;
        bool ownedProbe = false;
        bool completionRetainsBudget = false;
        bool exhaustiveValidationPayload = false;
    };

    struct Decision {
        bool validatorOnly = false;
        bool coldValidationEligible = false;
        bool domainIssued = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-DISPATCH-VALIDATION-BACKEND";
    };

    static constexpr Decision decide(Backend backend, const Facts& facts) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {true, false, false, false, "RHI-DISPATCH-VALIDATION-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {false, false, false, false, "RHI-DISPATCH-VALIDATION-QUERIED-LIMITS"};
        case Backend::Metal:
            if (!facts.originalDeviceCurrent)
                return {false, false, false, false, "RHI-DISPATCH-VALIDATION-DEVICE-LIFETIME"};
            if (!facts.nativeDevice)
                return {false, false, false, false, "RHI-DISPATCH-VALIDATION-NATIVE-DEVICE"};
            if (!facts.ownedProbe)
                return {false, false, false, false, "RHI-DISPATCH-VALIDATION-PROBE-CUSTODY"};
            if (!facts.completionRetainsBudget)
                return {false, false, false, false, "RHI-DISPATCH-VALIDATION-COMPLETION"};
            if (!facts.exhaustiveValidationPayload)
                return {false, false, false, false, "RHI-DISPATCH-VALIDATION-PAYLOAD-MISSING"};
            return {false, true, false, false, nullptr};
        case Backend::WebGL:
            return {false, false, false, false, "RHI-DISPATCH-VALIDATION-UNSUPPORTED"};
        }
        return {};
    }
};

} // namespace arp::rhi::semantics
