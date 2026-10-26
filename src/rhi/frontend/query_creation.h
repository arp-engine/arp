#pragma once
#include "core/text/utf8.h"
#include "rhi/frontend/query_pool_arguments.h"
#include "rhi/semantics/query_lowering.h"

namespace arp::rhi {
struct OriginalQueryPoolCreationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxNativeQueryPools = 0u, maxQuerySlots = 0u, maxScopeIds = 0u;
    std::uint64_t maxLabelBytes = 0u, maxCpuOwnerBytes = 0u;

    // Preserve the established private /1 diagnostic IDs when moving its
    // descriptor to the semantic owner; only Vulkan currently admits creation.
    const char* diagnostic() const noexcept
    {
        if (contractVersion != 1u)
            return "RHI-VULKAN-ORIGINAL-QUERY-VERSION";
        if (!prototypeOptIn)
            return "RHI-VULKAN-ORIGINAL-QUERY-OPT-IN";
        if (!maxCpuOwnerBytes || maxLabelBytes > SIZE_MAX)
            return "RHI-VULKAN-ORIGINAL-QUERY-BUDGET";
        return maxNativeQueryPools ? nullptr : "RHI-VULKAN-ORIGINAL-QUERY-POOL-CAPACITY";
    }
};
} // namespace arp::rhi

namespace arp::rhi::semantics {
// OriginalQueryCreation/1 admits cold creation only, never command execution.
class OriginalQueryCreationLowering final {
public:
    // WebGPU specification: validating GPUQuerySetDescriptor, count <= 4096; L2 separately requires a nonzero count.
    static constexpr std::uint32_t maximumWebGpuQueryCount = 4096u;

    class Decision final {
        friend class OriginalQueryCreationLowering;

        Decision(std::shared_ptr<const QueryPoolInputSnapshot> input,
            CapabilityMode mode,
            bool validator,
            const char* diagnostic)
            : input_(std::move(input)), mode_(mode), validator_(validator), diagnostic_(diagnostic)
        {}

    public:
        const auto& originalInput() const noexcept { return input_; }

        CapabilityMode mode() const noexcept { return mode_; }

        bool validatorOnly() const noexcept { return validator_; }

        bool creationEligible() const noexcept { return mode_ == CapabilityMode::Native; }

        bool executionSupported() const noexcept { return false; }

        const char* diagnostic() const noexcept { return diagnostic_; }

        bool matchesOriginal(
            const std::shared_ptr<const QueryPoolInputSnapshot>& input) const noexcept
        {
            return input && input_ && input->hasQuotaBoundSharedOwner(input)
                && input_->hasQuotaBoundSharedOwner(input_)
                && core::SharedOwnerProjection::sameOwner(input, input_);
        }

    private:
        const std::shared_ptr<const QueryPoolInputSnapshot> input_;
        const CapabilityMode mode_;
        const bool validator_;
        const char* const diagnostic_;
    };

    static Decision decide(Backend backend,
        std::uint32_t version,
        bool optIn,
        std::shared_ptr<const QueryPoolInputSnapshot> input,
        const OriginalQueryCreationFacts& facts = {}) noexcept
    {
        if (version != 1u)
            return {input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-VERSION"};
        if (!optIn)
            return {input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-OPT-IN"};
        if (!input || !input->hasQuotaBoundSharedOwner(input))
            return {input, CapabilityMode::DiagnosticFailure, false, "RHI-QUERY-CREATION-INPUT"};
        const auto validation = input->validateDescriptor();
        if (!validation.success)
            return {input, CapabilityMode::DiagnosticFailure, false, validation.diagnostics};
        const auto selected = QueryLowering::originalCreation(backend,
            input->scalarFields().type(),
            input->scalarFields().count(),
            input->label(),
            facts,
            maximumWebGpuQueryCount);
        return {input, selected.mode, selected.validatorOnly, selected.diagnostic};
    }
};
} // namespace arp::rhi::semantics
