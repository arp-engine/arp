#pragma once

#include "core/memory_budget_ledger.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace arp::core {

enum class BudgetVectorStatus { Fits, LiveOverflow, LiveExceedsCapacity, Overflow, Capacity };

template <class Counters> struct BudgetVectorProjection {
    BudgetVectorStatus status = BudgetVectorStatus::Capacity;
    std::size_t dimension = 0u;
    Counters projected{};

    bool fits() const noexcept { return status == BudgetVectorStatus::Fits; }
};

// Immutable metadata preflight, not a reservation or ownership authority.
// Check each complete failure category before the next: a later dimension's
// malformed live value takes priority over an earlier dimension's capacity.
// No partial projection is exposed on failure. Callers serialize snapshots
// and commit the associated ownership transaction outside this helper.
// The schema-owned field table must contain distinct, non-null members.
template <class Counters, std::size_t DimensionCount> class BudgetVectorPreflight final {
public:
    static_assert(std::is_nothrow_default_constructible_v<Counters>
        && std::is_nothrow_copy_constructible_v<Counters>);
    using Fields = std::array<std::uint64_t Counters::*, DimensionCount>;

    template <std::size_t PartitionCount>
    static BudgetVectorProjection<Counters> assess(const Counters& capacity,
        const std::array<Counters, PartitionCount>& partitions,
        const Counters& addition,
        const Fields& fields) noexcept
    {
        Counters live{};
        for (std::size_t i = 0u; i < DimensionCount; ++i) {
            const auto field = fields[i];
            for (const auto& partition : partitions) {
                if (!MemoryBudgetLedger::canAdd(live.*field, partition.*field))
                    return {BudgetVectorStatus::LiveOverflow, i, {}};
                live.*field += partition.*field;
            }
        }
        for (std::size_t i = 0u; i < DimensionCount; ++i) {
            if (live.*fields[i] > capacity.*fields[i])
                return {BudgetVectorStatus::LiveExceedsCapacity, i, {}};
        }
        for (std::size_t i = 0u; i < DimensionCount; ++i) {
            if (!MemoryBudgetLedger::canAdd(live.*fields[i], addition.*fields[i]))
                return {BudgetVectorStatus::Overflow, i, {}};
        }
        for (std::size_t i = 0u; i < DimensionCount; ++i) {
            if (live.*fields[i] + addition.*fields[i] > capacity.*fields[i])
                return {BudgetVectorStatus::Capacity, i, {}};
        }
        Counters projected{};
        for (const auto field : fields)
            projected.*field = live.*field + addition.*field;
        return {BudgetVectorStatus::Fits, DimensionCount, projected};
    }
};

} // namespace arp::core
