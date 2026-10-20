#pragma once
#include "rhi/gpu_capabilities.h"
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Actual native-device and executable payload facts, never OS-name inference.
// Creation eligibility alone grants neither native execution nor output evidence.
class OriginalSynchronousPipelineCreationLowering final {
public:
    struct Facts {
        bool device = false, queriedCapabilities = false, immutableTarget = false;
    };

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false, creationEligible = false, executionAvailable = false;
        const char* diagnostic = nullptr;
    };

    static constexpr Decision decide(Backend backend, Facts f) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {CapabilityMode::Equivalent,
                true,
                false,
                false,
                "RHI-ORIGINAL-SYNC-PIPELINE-NULL-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-SYNC-PIPELINE-EXISTING-ISSUER-REQUIRED"};
        case Backend::Metal:
            if (!f.immutableTarget)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-SYNC-PIPELINE-TARGET"};
            if (!f.queriedCapabilities)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-SYNC-PIPELINE-CAPABILITY-QUERY"};
            if (!f.device)
                return {CapabilityMode::DiagnosticFailure,
                    false,
                    false,
                    false,
                    "RHI-ORIGINAL-SYNC-PIPELINE-NATIVE-DEVICE"};
            return {CapabilityMode::Native, false, true, false, nullptr};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORIGINAL-SYNC-PIPELINE-ORDERED-ISSUER-REQUIRED"};
        }
        return {CapabilityMode::DiagnosticFailure,
            false,
            false,
            false,
            "RHI-ORIGINAL-SYNC-PIPELINE-BACKEND"};
    }
};
} // namespace arp::rhi::semantics
