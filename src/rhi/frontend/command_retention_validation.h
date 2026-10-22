#pragma once

#include "rhi/gpu_commands.h"

namespace arp::rhi::semantics {

struct CommandRetentionLoweringPlan {
    bool success = false;
    bool executable = false;
    CapabilityMode mode = CapabilityMode::DiagnosticFailure;
    CommandRetentionBudget budget;
    const char* diagnosticId = "RHI-COMMAND-RETENTION-UNSUPPORTED";
};

// Value-only planning never allocates or grants device authorization. Backend
// factories consume the actual approved capability/payload facts separately.
class CommandRetentionBudgetLowering final {
public:
    static const char* validateIntent(const CommandRetentionBudget& budget) noexcept
    {
        if (budget.contractVersion != kCommandRetentionBudgetContractVersion)
            return "RHI-COMMAND-RETENTION-VERSION";
        if (!budget.optIn)
            return "RHI-COMMAND-RETENTION-OPT-IN-REQUIRED";
        if (!budget.maxCpuMetadataBytes)
            return "RHI-COMMAND-RETENTION-CPU-BUDGET";
        if ((budget.maxTimestampWrites || budget.maxPipelineStatisticsWrites)
            && !budget.maxQueryPools)
            return "RHI-COMMAND-RETENTION-QUERY-POOL-BUDGET";
        return nullptr;
    }

    static CommandRetentionLoweringPlan plan(
        const CommandRetentionBudget& budget, const CommandRetentionCapabilities& caps) noexcept
    {
        CommandRetentionLoweringPlan result;
        result.budget = budget;
        if (const auto diagnostic = validateIntent(budget))
            return fail(result, diagnostic);
        if (caps.contractVersion != kCommandRetentionBudgetContractVersion
            || (caps.mode != CapabilityMode::Native && caps.mode != CapabilityMode::Equivalent)
            || (caps.validatorOnly && caps.mode != CapabilityMode::Equivalent))
            return fail(result, "RHI-COMMAND-RETENTION-UNSUPPORTED");
        if (!caps.boundedRecordingPayload)
            return fail(result, "RHI-COMMAND-RETENTION-PAYLOAD-MISSING");
        if (!caps.validatorOnly && !caps.completionRetiresOwners)
            return fail(result, "RHI-COMMAND-RETENTION-COMPLETION-MISSING");
        result.success = true;
        result.executable = !caps.validatorOnly;
        result.mode = caps.mode;
        result.diagnosticId = nullptr;
        return result;
    }

private:
    static CommandRetentionLoweringPlan fail(
        CommandRetentionLoweringPlan result, const char* id) noexcept
    {
        result.diagnosticId = id;
        return result;
    }
};

// All four current factories pass version-zero unsupported contract facts.
// This gate runs before native allocation or staging submission mutation.
inline bool commandRetentionRequestAllowed(const CommandEncoderDesc& desc,
    const CommandRetentionCapabilities& caps,
    Backend backend = Backend::Null)
{
    if (desc.prototypeBatch) {
        const auto decision =
            PrototypeCommandBatchContract::lowering(backend, *desc.prototypeBatch, {});
        RhiErrorHandler::get().report(RhiSeverity::Error, decision.diagnostic);
        return false;
    }
    if (!desc.retentionBudget)
        return true;
    const auto decision = CommandRetentionBudgetLowering::plan(*desc.retentionBudget, caps);
    if (!decision.success)
        RhiErrorHandler::get().report(RhiSeverity::Error, decision.diagnosticId);
    return decision.success;
}

} // namespace arp::rhi::semantics
