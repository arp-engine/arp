#pragma once

#include "rhi/semantics/command_budget_lowering.h"

namespace arp::rhi {

// One whole-command interval. Authored Query sources/slots retain their own
// ceilings and identity. CPU storage remains in the enclosing Recording quota;
// these independent counts are not a claim about unobservable driver memory.
struct CommandProfilingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxInternalPools = 0u, maxInternalTimestampWrites = 0u;
    std::uint32_t maxPublishedScopes = 0u;
    bool requireNativeAllocationFootprint = false;
};

class CommandProfilingContract final {
public:
    static constexpr std::uint32_t poolCount = 1u, timestampWrites = 2u;
    static constexpr std::uint64_t wholeCommandScopeId = 0u;

    static const char* validateIntent(const CommandProfilingBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-COMMAND-PROFILING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-COMMAND-PROFILING-OPT-IN";
        if (b.maxInternalPools < poolCount)
            return "RHI-COMMAND-PROFILING-POOL-CAPACITY";
        if (b.maxInternalTimestampWrites < timestampWrites)
            return "RHI-COMMAND-PROFILING-WRITE-CAPACITY";
        if (!b.maxPublishedScopes || b.maxPublishedScopes > GpuTimingSample::kMaxScopes)
            return "RHI-COMMAND-PROFILING-PUBLICATION-CAPACITY";
        return nullptr;
    }

    struct Decision {
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool validatorOnly = false;
        bool nativeAllocationFootprintKnown = false;
        double timestampPeriodNanoseconds = 0.0;
        const char* diagnostic = "RHI-COMMAND-PROFILING-BACKEND";

        bool executable() const noexcept
        {
            return mode == CapabilityMode::Native && !validatorOnly && !diagnostic;
        }
    };

    // Four-backend disposition is fixed before entering a private executor.
    // Vulkan facts must come from the actual runtime/queried queue/enablement.
    static Decision lower(Backend backend,
        const CommandProfilingBudget& budget,
        const semantics::VulkanTimestampQueryFacts& facts = {},
        const char* runtimeDiagnostic = nullptr) noexcept
    {
        if (const auto* id = validateIntent(budget))
            return {CapabilityMode::DiagnosticFailure, false, false, 0.0, id};
        return semantics::CommandBudgetLowering::profiling<Decision>(
            backend, budget.requireNativeAllocationFootprint, facts, runtimeDiagnostic);
    }
};

} // namespace arp::rhi
