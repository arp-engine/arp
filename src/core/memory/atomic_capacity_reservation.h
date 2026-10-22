#pragma once
#include "core/memory/atomic_capacity.h"

#include <array>

namespace arp::core {
// All-or-nothing reservation over independently enforced original counters.
// Failed acquisition rolls back only this attempt, preserving other users.
// This object supplies no completion authority; its owner controls retirement.
template <std::size_t Count> class AtomicCapacityReservation final {
public:
    using Budgets = std::array<AtomicCapacityBudget*, Count>;
    using Amounts = std::array<std::uint64_t, Count>;

    AtomicCapacityReservation() = default;
    AtomicCapacityReservation(const AtomicCapacityReservation&) = delete;
    AtomicCapacityReservation& operator=(const AtomicCapacityReservation&) = delete;

    ~AtomicCapacityReservation() { release(); }

    bool acquire(const Budgets& budgets, const Amounts& amounts) noexcept
    {
        if (charged_)
            return false;
        for (std::size_t i = 0u; i < Count; ++i)
            if (!budgets[i])
                return false;
        for (std::size_t i = 0u; i < Count; ++i) {
            if (amounts[i] && !budgets[i]->tryAcquire(amounts[i])) {
                for (std::size_t j = 0u; j < i; ++j)
                    if (amounts[j])
                        budgets[j]->release(amounts[j]);
                return false;
            }
        }
        budgets_ = budgets;
        amounts_ = amounts;
        charged_ = true;
        return true;
    }

    bool charged() const noexcept { return charged_; }

    const Amounts& amounts() const noexcept { return amounts_; }

private:
    // No external early-release entry point. Original command custody must
    // retain this reservation until real completion and callback drainage.
    void release() noexcept
    {
        if (!charged_)
            return;
        for (std::size_t i = 0u; i < Count; ++i)
            if (amounts_[i])
                budgets_[i]->release(amounts_[i]);
        charged_ = false;
    }

    Budgets budgets_{};
    Amounts amounts_{};
    bool charged_ = false;
};
} // namespace arp::core
