#pragma once
#include "core/atomic_capacity_reservation.h"
#include "core/quota_bound_shared_identity.h"
#include "rhi/semantics/bindless_workload_capacity.h"

namespace arp::rhi {
class OriginalBindlessWorkloadRetention;
class OriginalCommandResourceSources;

class BindlessWorkloadCapacityPool final : public core::SharedObjectIdentity {
public:
    using Budget = semantics::BindlessWorkloadCapacityBudget;
    using Domain = semantics::BindlessWorkloadCapacityDomain;
    using Amounts = semantics::BindlessWorkloadCapacityAmounts;
    static constexpr auto domainCount = semantics::bindlessWorkloadCapacityDomainCount;

    explicit BindlessWorkloadCapacityPool(Budget budget)
        : budget_(budget), counters_{{core::AtomicCapacityBudget(budget.limits[0u]),
                               core::AtomicCapacityBudget(budget.limits[1u]),
                               core::AtomicCapacityBudget(budget.limits[2u]),
                               core::AtomicCapacityBudget(budget.limits[3u]),
                               core::AtomicCapacityBudget(budget.limits[4u]),
                               core::AtomicCapacityBudget(budget.limits[5u]),
                               core::AtomicCapacityBudget(budget.limits[6u])}}
    {}

    const Budget& budget() const noexcept { return budget_; }

    const core::AtomicCapacityBudget& observation(Domain domain) const noexcept
    {
        return counters_[static_cast<std::size_t>(domain)];
    }

private:
    friend class OriginalBindlessWorkloadReservation;
    const Budget budget_;
    std::array<core::AtomicCapacityBudget, domainCount> counters_;
};

// Owned by the one canonical inventory, not an alternate resource graph.
// Its pool outlives every original counter pointer in the reservation.
class OriginalBindlessWorkloadReservation final : public core::QuotaBoundSharedObjectIdentity {
    friend class OriginalBindlessWorkloadRetention;

    class ConstructionKey {
        friend class OriginalBindlessWorkloadRetention;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    static constexpr std::uint32_t retainedOwnerCount = 2u; // reservation + pool
    using Amounts = BindlessWorkloadCapacityPool::Amounts;

    OriginalBindlessWorkloadReservation(ConstructionKey,
        std::shared_ptr<BindlessWorkloadCapacityPool> pool,
        const OriginalCommandResourceSources* inventory,
        const Amounts& amounts) noexcept
        : pool_(std::move(pool)), inventory_(inventory)
    {
        core::AtomicCapacityReservation<BindlessWorkloadCapacityPool::domainCount>::Budgets
            counters;
        for (std::size_t i = 0u; i < counters.size(); ++i)
            counters[i] = &pool_->counters_[i];
        reservation_.acquire(counters, amounts);
    }

    bool charged() const noexcept { return reservation_.charged(); }

    const Amounts& amounts() const noexcept { return reservation_.amounts(); }

    const OriginalCommandResourceSources* originalInventory() const noexcept { return inventory_; }

    const auto& originalPool() const noexcept { return pool_; }

private:
    const std::shared_ptr<BindlessWorkloadCapacityPool> pool_;
    const OriginalCommandResourceSources* const inventory_;
    core::AtomicCapacityReservation<BindlessWorkloadCapacityPool::domainCount> reservation_;
};
} // namespace arp::rhi
