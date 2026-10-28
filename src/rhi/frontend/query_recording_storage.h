#pragma once

#include "rhi/frontend/command_retention_validation.h"
#include "rhi/frontend/command_usage.h"

namespace arp::rhi {

// Cold metadata projection only. Canonical Inputs retain the original stream;
// this inspection creates neither a second owner table nor execution authority.
class OriginalQueryRecordingStorageContract final {
public:
    struct Inspection {
        std::size_t poolOwners = 0u, timestampWrites = 0u;
        const char* diagnostic = nullptr;
    };

    struct Lowering {
        bool validatorOnly = false, nativeStorageImplemented = false;
        bool executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-QUERY-STORAGE-BACKEND";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::QueryLowering::OriginalQueryStorage_lowering<Lowering>(backend);
    }

    static Inspection inspect(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const CommandRetentionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        return inspectProfile(inputs, budget, quota, false);
    }

    // Independent cold projection for Inputs11. Native recording remains
    // unavailable until the same Program/meter/journal join is authenticated.
    // The legacy query-only entry above must continue rejecting this profile.
    static Inspection inspectPassTiming(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const CommandRetentionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) noexcept
    {
        return inspectProfile(inputs, budget, quota, true);
    }

private:
    static Inspection inspectProfile(const std::shared_ptr<const OriginalCommandInputs>& inputs,
        const CommandRetentionBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool passTiming) noexcept
    {
        Inspection result;
        const auto fail = [&](const char* id) noexcept {
            result.diagnostic = id;
            return result;
        };
        if (const auto* id = semantics::CommandRetentionBudgetLowering::validateIntent(budget))
            return fail(id);
        if (const auto* id = OriginalCommandInputs::validateOriginal(inputs))
            return fail(id);
        if (inputs->originalBudget().contractVersion != (passTiming ? 11u : 9u))
            return fail("RHI-ORIGINAL-QUERY-STORAGE-INPUT-PROFILE");
        const auto& sources = *inputs->originalScalars()->originalUsage()->originalSources();
        using D = OriginalCommandAdmissionBudget::CpuDomain;
        if (!quota || quota->limitBytes() != budget.maxCpuMetadataBytes
            || quota->limitBytes()
                != sources.originalBudget().maxCpuBytes[static_cast<std::size_t>(D::Recording)])
            return fail("RHI-ORIGINAL-QUERY-STORAGE-RECORDING-QUOTA");
        if (core::SharedOwnerProjection::overlapsOwner(quota, inputs->originalMetadataQuota())
            || core::SharedOwnerProjection::overlapsOwner(quota, sources.originalMetadataQuota()))
            return fail("RHI-ORIGINAL-QUERY-STORAGE-QUOTA-DOMAIN");
        for (const auto& source : sources.sources()) {
            const auto* query =
                std::get_if<std::shared_ptr<const OriginalCommandQueryPoolSource>>(&source);
            if (query) {
                if (const auto* id = (*query)->independentCpuQuotaDiagnostic(quota))
                    return fail(id);
                if (core::SharedOwnerProjection::overlapsOwner(
                        quota, (*query)->originalMetadataQuota())
                    || core::cpuAllocationOverlapsQuotaOwner(
                        (*query)->originalInput()->label().data(), quota)
                    || core::cpuAllocationOverlapsQuotaOwner(
                        (*query)->originalInput()->scopeIds().data(), quota))
                    return fail("RHI-ORIGINAL-QUERY-STORAGE-QUOTA-DOMAIN");
            }
        }
        for (std::size_t i = 0u; i < inputs->descriptors().size(); ++i) {
            const auto* binding =
                std::get_if<OriginalCommandQueryBinding>(&inputs->descriptors()[i].arguments);
            if (!binding)
                continue;
            if (binding->kind() != CommandUsageKind::Timestamp)
                return fail("RHI-ORIGINAL-QUERY-STORAGE-KIND");
            const auto& pool = std::get<std::shared_ptr<const OriginalCommandQueryPoolSource>>(
                sources.sources()[binding->sourceIndex])
                                   ->originalPool();
            bool first = true;
            for (std::size_t j = 0u; j < i; ++j) {
                const auto* previous =
                    std::get_if<OriginalCommandQueryBinding>(&inputs->descriptors()[j].arguments);
                if (previous) {
                    const auto& previousPool =
                        std::get<std::shared_ptr<const OriginalCommandQueryPoolSource>>(
                            sources.sources()[previous->sourceIndex])
                            ->originalPool();
                    if (core::SharedOwnerProjection::sameOwner(pool, previousPool)) {
                        first = false;
                        if (binding->original.scalar().get<CommandScalarArguments::Query>()->index
                            == previous->original.scalar()
                                .get<CommandScalarArguments::Query>()
                                ->index)
                            return fail("RHI-ORIGINAL-QUERY-STORAGE-DUPLICATE-SLOT");
                    }
                }
            }
            if (first) {
                if (result.poolOwners >= budget.maxQueryPools)
                    return fail("RHI-ORIGINAL-QUERY-STORAGE-POOL-CAPACITY");
                ++result.poolOwners;
            }
            if (result.timestampWrites >= budget.maxTimestampWrites)
                return fail("RHI-ORIGINAL-QUERY-STORAGE-WRITE-CAPACITY");
            ++result.timestampWrites;
        }
        return result;
    }
};

} // namespace arp::rhi
