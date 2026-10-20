#pragma once

#include "rhi/gpu_common.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace arp::rhi {

// OrderedCommandIssue/1: bounds CPU custody, never native authorization.
struct OrderedCommandIssueBudget {
    std::uint32_t contractVersion = 1u;
    bool optIn = false;
    std::uint32_t maxOperations = 0u;
    std::uint32_t maxResourceOwners = 0u;
    std::uint64_t maxSnapshotBytes = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
    // Occurrences, including duplicates, bound record-time owner traversal.
    std::uint64_t maxOwnerReferences = 0u;
};

namespace semantics {

struct OrderedCommandIssueDecision {
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    bool validatorOnly = false;
    bool executable = false;
    bool provesGpuCompletion = false;
    const char* diagnostic = "RHI-ORDERED-ISSUE-BACKEND-UNKNOWN";
};

class OrderedCommandIssueLowering final {
public:
    static const char* validate(const OrderedCommandIssueBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-ORDERED-ISSUE-VERSION";
        if (!budget.optIn)
            return "RHI-ORDERED-ISSUE-OPT-IN";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-ORDERED-ISSUE-CPU-QUOTA";
        if (budget.maxSnapshotBytes > std::numeric_limits<std::size_t>::max())
            return "RHI-ORDERED-ISSUE-SNAPSHOT-CAPACITY";
        return nullptr;
    }

    static constexpr OrderedCommandIssueDecision current(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return {
                CapabilityMode::Equivalent, true, false, false, "RHI-ORDERED-ISSUE-VALIDATOR-ONLY"};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORDERED-ISSUE-COMPLETION-PATH-REQUIRED"};
        case Backend::WebGL:
            return {CapabilityMode::DiagnosticFailure,
                false,
                false,
                false,
                "RHI-ORDERED-ISSUE-PAYLOAD-UNAVAILABLE"};
        }
        return {};
    }
};
} // namespace semantics
} // namespace arp::rhi
