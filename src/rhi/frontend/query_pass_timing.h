#pragma once

#include "rhi/frontend/command_profiling_budget.h"
#include "rhi/frontend/query_command_points.h"

namespace arp::rhi {

// Authored pass intervals are independent of whole-command profiling. These
// ceilings bound the original stream and the read-only pair projection; they
// neither reserve native storage nor authorize recording or publication.
struct OriginalPassTimingBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxPoolInputs = 0u, maxTimestampWrites = 0u, maxPublishedScopes = 0u;
    std::uint64_t maxStreamCommands = 0u, maxPointVisits = 0u;
};

// A view of canonical QueryCommandPoints, never another query owner inventory.
// Each interval measures the inside of exactly one render/compute pass. Its
// timestamps immediately follow Begin and precede End; untimed passes remain
// allowed. Original query slots and actual completion retain their own contract.
class OriginalPassTimingContract final {
public:
    struct Inspection {
        std::uint32_t poolInputs = 0u, timestampWrites = 0u, publishedScopes = 0u;
        std::uint64_t pointVisits = 0u;
        const char* diagnostic = nullptr;
    };

    struct ScopeView {
        std::uint64_t scopeId;
        std::size_t passIndex, beginPointIndex, endPointIndex;
    };

    struct Lowering {
        bool validatorOnly = false, inspectionAvailable = false, recordingAvailable = false;
        const char* diagnostic = "RHI-PASS-TIMING-BACKEND";
    };

    static const char* validateIntent(const OriginalPassTimingBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-PASS-TIMING-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-PASS-TIMING-OPT-IN";
        if (!b.maxPoolInputs || !b.maxTimestampWrites || !b.maxPublishedScopes
            || b.maxPublishedScopes > GpuTimingSample::kMaxScopes || !b.maxStreamCommands
            || !b.maxPointVisits)
            return "RHI-PASS-TIMING-BUDGET";
        return nullptr;
    }

    static Lowering lowering(Backend backend, const OriginalPassTimingBudget& budget) noexcept
    {
        if (const auto* id = validateIntent(budget))
            return {false, false, false, id};
        return semantics::QueryLowering::OriginalPassTiming_lowering<Lowering>(backend);
    }

    static Inspection inspect(const std::shared_ptr<const QueryCommandPoints>& points,
        const std::shared_ptr<const CommandVariableArguments>& stream,
        const OriginalPassTimingBudget& budget) noexcept
    {
        Inspection result;
        const auto fail = [&](const char* id) noexcept {
            result.diagnostic = id;
            return result;
        };
        if (const auto* id = validateIntent(budget))
            return fail(id);
        if (const auto* id = QueryCommandPoints::validateOriginalStream(points, stream))
            return fail(id);
        if (stream->records().size() > budget.maxStreamCommands)
            return fail("RHI-PASS-TIMING-COMMAND-CAPACITY");
        const auto& queries = points->points();
        if (queries.size() > budget.maxTimestampWrites)
            return fail("RHI-PASS-TIMING-WRITE-CAPACITY");
        if (queries.empty() || queries.size() % 2u)
            return fail("RHI-PASS-TIMING-PAIR-COVERAGE");
        if (queries.size() / 2u > budget.maxPublishedScopes)
            return fail("RHI-PASS-TIMING-PUBLICATION-CAPACITY");
        // Include the final read-only ScopeView traversal in this ceiling.
        result.pointVisits = queries.size() / 2u;
        const auto visit = [&]() noexcept {
            if (result.pointVisits == budget.maxPointVisits)
                return false;
            ++result.pointVisits;
            return true;
        };
        if (result.pointVisits > budget.maxPointVisits)
            return fail("RHI-PASS-TIMING-POINT-CAPACITY");
        for (std::size_t i = 0u; i < queries.size(); i += 2u) {
            if (!visit() || !visit())
                return fail("RHI-PASS-TIMING-POINT-CAPACITY");
            const auto& begin = queries[i];
            const auto& end = queries[i + 1u];
            if (begin.original.kind() != CommandUsageKind::Timestamp
                || end.original.kind() != CommandUsageKind::Timestamp)
                return fail("RHI-PASS-TIMING-QUERY-KIND");
            const auto& pool = begin.original.originalInput();
            const auto beginSlot =
                begin.original.scalar().get<CommandScalarArguments::Query>()->index;
            const auto endSlot = end.original.scalar().get<CommandScalarArguments::Query>()->index;
            if (!core::SharedOwnerProjection::sameOwner(pool, end.original.originalInput())
                || beginSlot % 2u || endSlot != beginSlot + 1u
                || beginSlot / 2u >= pool->scopeIds().size()
                || pool->scalarFields().count() / 2u != pool->scopeIds().size()
                || pool->scalarFields().count() % 2u)
                return fail("RHI-PASS-TIMING-SLOT-PAIR");
            const auto scopeId = pool->scopeIds()[beginSlot / 2u];
            if (scopeId == CommandProfilingContract::wholeCommandScopeId)
                return fail("RHI-PASS-TIMING-SCOPE-IDENTITY");
            if (begin.passIndex != end.passIndex || begin.passIndex >= points->passes().size())
                return fail("RHI-PASS-TIMING-PASS-PAIR");
            const auto& pass = points->passes()[begin.passIndex];
            using S = semantics::CommandRecordingOrder::Scope;
            if ((pass.scope != S::Render && pass.scope != S::Compute)
                || begin.commandOrdinal != pass.begin + 1u || end.commandOrdinal + 1u != pass.end)
                return fail("RHI-PASS-TIMING-PASS-BOUNDARY");
            if (end.commandOrdinal <= begin.commandOrdinal + 1u)
                return fail("RHI-PASS-TIMING-EMPTY-INTERVAL");
            bool firstPool = true;
            for (std::size_t j = 0u; j < i; j += 2u) {
                if (!visit())
                    return fail("RHI-PASS-TIMING-POINT-CAPACITY");
                const auto& previous = queries[j].original;
                const auto& previousPool = previous.originalInput();
                const auto previousSlot =
                    previous.scalar().get<CommandScalarArguments::Query>()->index;
                if (scopeId == previousPool->scopeIds()[previousSlot / 2u])
                    return fail("RHI-PASS-TIMING-SCOPE-IDENTITY");
                firstPool =
                    firstPool && !core::SharedOwnerProjection::sameOwner(pool, previousPool);
            }
            if (firstPool) {
                if (result.poolInputs == budget.maxPoolInputs)
                    return fail("RHI-PASS-TIMING-POOL-CAPACITY");
                ++result.poolInputs;
                std::size_t pairs = 0u;
                for (std::size_t j = i; j < queries.size(); j += 2u) {
                    if (!visit())
                        return fail("RHI-PASS-TIMING-POINT-CAPACITY");
                    if (core::SharedOwnerProjection::sameOwner(
                            pool, queries[j].original.originalInput()))
                        ++pairs;
                }
                // Native publication enumerates complete pools. A missing pair
                // must fail here, before any timestamp write or journal claim.
                if (pairs != pool->scopeIds().size())
                    return fail("RHI-PASS-TIMING-POOL-COVERAGE");
            }
            result.timestampWrites += 2u;
            ++result.publishedScopes;
        }
        return result;
    }

    template <class Visitor>
    static Inspection visit(const std::shared_ptr<const QueryCommandPoints>& points,
        const std::shared_ptr<const CommandVariableArguments>& stream,
        const OriginalPassTimingBudget& budget,
        Visitor&& visitor) noexcept
    {
        static_assert(noexcept(visitor(std::declval<const ScopeView&>())),
            "Pass timing projection visitor must not throw");
        static_assert(
            std::is_same_v<decltype(visitor(std::declval<const ScopeView&>())), const char*>,
            "Pass timing projection visitor returns a diagnostic");
        auto result = inspect(points, stream, budget);
        if (result.diagnostic)
            return result;
        const auto& queries = points->points();
        for (std::size_t i = 0u; i < queries.size(); i += 2u) {
            const auto& begin = queries[i];
            const auto slot = begin.original.scalar().get<CommandScalarArguments::Query>()->index;
            if ((result.diagnostic =
                        visitor(ScopeView{begin.original.originalInput()->scopeIds()[slot / 2u],
                            begin.passIndex,
                            i,
                            i + 1u})))
                return result;
        }
        return result;
    }
};

} // namespace arp::rhi
