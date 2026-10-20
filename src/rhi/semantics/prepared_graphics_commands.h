#pragma once
#include "rhi/semantics/ordered_command_issue.h"

namespace arp::rhi::semantics {
// Keep the original public type identity and mangled signatures. New engine
// callers use the neutral alias; descriptor layout and diagnostic IDs are frozen.
struct WebGlPreparedCommandsRequest {
    std::uint32_t contractVersion = 0;
    bool prototypeOptIn = false;
    std::uint32_t maxDraws = 0;
    std::uint64_t maxUniformBytes = 0, maxAnalysisIndices = 0;
    OrderedCommandIssueBudget issue;
    std::uint32_t maxPipelines = 0;
    std::uint32_t maxTargets = 0, maxPasses = 0, maxTransitions = 0;
};

using PreparedGraphicsCommandsRequest = WebGlPreparedCommandsRequest;

class PreparedGraphicsCommandsContract final {
public:
    static const char* validate(const PreparedGraphicsCommandsRequest& r) noexcept
    {
        if (r.contractVersion < 1 || r.contractVersion > 4)
            return "RHI-WEBGL1-PREPARED-COMMAND-VERSION";
        if ((r.contractVersion == 1 && r.maxPipelines)
            || (r.contractVersion >= 2 && !r.maxPipelines))
            return "RHI-WEBGL1-PREPARED-PIPELINE-BUDGET";
        if ((r.contractVersion < 3 && (r.maxTargets || r.maxPasses || r.maxTransitions))
            || (r.contractVersion >= 3 && (!r.maxTargets || !r.maxPasses || !r.maxTransitions)))
            return "RHI-WEBGL1-PREPARED-PASS-BUDGET";
        if (!r.prototypeOptIn)
            return "RHI-WEBGL1-PREPARED-COMMAND-PROTOTYPE-OPT-IN";
        if (const auto* id = OrderedCommandIssueLowering::validate(r.issue))
            return id;
        const std::uint64_t operations =
            std::uint64_t{r.maxDraws} + 2 * std::uint64_t{r.contractVersion >= 3 ? r.maxPasses : 1};
        if (!r.maxDraws || operations >= UINT32_MAX || !r.maxUniformBytes || !r.maxAnalysisIndices
            || r.issue.maxOperations < operations || !r.issue.maxResourceOwners
            || !r.issue.maxOwnerReferences
            || r.issue.maxSnapshotBytes < operations * sizeof(std::uint32_t))
            return "RHI-WEBGL1-PREPARED-COMMAND-BUDGET";
        return nullptr;
    }

    // Version4 explicitly admits different immutable shader/layout/input ABIs
    // BETWEEN render passes. An active pass retains Version1-3 compatibility.
    static const char* pipelineSwitch(
        const PreparedGraphicsCommandsRequest& r, bool sameAbi, bool boundInPass) noexcept
    {
        if (sameAbi)
            return nullptr;
        if (r.contractVersion < 4)
            return "RHI-WEBGL1-PREPARED-PIPELINE-ABI-MISMATCH";
        return boundInPass ? "RHI-ORDERED-GRAPHICS-ABI-PASS-BOUNDARY-REQUIRED" : nullptr;
    }
};
} // namespace arp::rhi::semantics
