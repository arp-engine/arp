#pragma once
#include "rhi/gpu_common.h"

namespace arp::rhi::semantics {
// Completion slots include writers; pending-read commands/pins/links keep their
// separate OriginalSubmissionRead1 capacities. Neither budget grants execution.
struct OriginalSubmissionCompletionBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxSubmissions = 0u, maxPredecessorLinks = 0u, maxCpuMetadataBytes = 0u;
};

class OriginalSubmissionCompletionContract final {
public:
    struct Decision {
        bool validatorOnly = false, validatorStorageAvailable = false;
        bool nativeResultHandoffAvailable = false, pendingResourceAdmissionAvailable = false,
             provesGpuCompletion = false;
        const char* diagnostic = "RHI-SUBMISSION-COMPLETION-BACKEND";
    };

    static constexpr const char* inspect(const OriginalSubmissionCompletionBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-SUBMISSION-COMPLETION-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-SUBMISSION-COMPLETION-OPT-IN";
        if (!budget.maxSubmissions || !budget.maxPredecessorLinks || !budget.maxCpuMetadataBytes)
            return "RHI-SUBMISSION-COMPLETION-CAPACITY";
        return nullptr;
    }

    static constexpr Decision lowering(Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return {false, false, false, false, false, "RHI-SUBMISSION-COMPLETION-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, false, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {
                false, false, false, false, false, "RHI-SUBMISSION-COMPLETION-NATIVE-UNAVAILABLE"};
        case Backend::WebGL:
            return {false, false, false, false, false, "RHI-SUBMISSION-COMPLETION-UNSUPPORTED"};
        }
        return {};
    }

    // Separate private ordinary-command join. It grants neither caller-issued
    // native result tickets nor pending-resource/read admission.
    static constexpr const char* ordinaryOwnerJoinLowering(
        Backend backend, std::uint32_t version) noexcept
    {
        if (version != 1u)
            return "RHI-SUBMISSION-COMPLETION-VERSION";
        switch (backend) {
        case Backend::Metal:
            return nullptr;
        case Backend::Null:
        case Backend::Vulkan:
        case Backend::WebGPU:
            return "RHI-SUBMISSION-COMPLETION-OWNER-JOIN-UNAVAILABLE";
        case Backend::WebGL:
            return "RHI-SUBMISSION-COMPLETION-UNSUPPORTED";
        }
        return "RHI-SUBMISSION-COMPLETION-BACKEND";
    }
};
} // namespace arp::rhi::semantics
