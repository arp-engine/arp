#pragma once

#include "core/math/unsigned_range.h"
#include "core/memory/cpu_allocator.h"

#include <atomic>
#include <cstdint>
#include <limits>

namespace arp::core {

// Shared reservation arithmetic for byte quotas and bounded resource counts.
inline bool tryReserveAtomicCapacity(std::atomic<std::uint64_t>& used,
    const std::atomic<std::uint64_t>& limit,
    std::uint64_t amount,
    std::uint64_t& total,
    bool zeroMeansUnlimited = false) noexcept
{
    if (amount == 0u)
        return false;
    auto current = used.load(std::memory_order_relaxed);
    for (;;) {
        if (amount > std::numeric_limits<std::uint64_t>::max() - current)
            return false;
        const auto next = current + amount;
        const auto maximum = limit.load(std::memory_order_acquire);
        if (!(zeroMeansUnlimited && maximum == 0u) && next > maximum)
            return false;
        if (used.compare_exchange_weak(
                current, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            total = next;
            return true;
        }
    }
}

inline void releaseAtomicCapacity(std::atomic<std::uint64_t>& used, std::uint64_t amount) noexcept
{
    auto current = used.load(std::memory_order_relaxed);
    for (;;) {
        const auto next = amount > current ? 0u : current - amount;
        if (used.compare_exchange_weak(
                current, next, std::memory_order_acq_rel, std::memory_order_relaxed))
            return;
    }
}

class AtomicCapacityBudget final {
public:
    explicit AtomicCapacityBudget(std::uint64_t capacity) noexcept : capacity_(capacity) {}

    // Non-reserving observation; externally serialize it with acquisition when
    // using it as a publication preflight. No telemetry or capacity is changed.
    bool canAcquire(std::uint64_t count) const noexcept
    {
        return containsUnsignedRange<std::uint64_t>(0u, capacity(), used(), count);
    }

    bool tryAcquire(std::uint64_t count) noexcept
    {
        if (count == 0u)
            return false;
        auto current = used_.load(std::memory_order_relaxed);
        for (;;) {
            // Arithmetic overflow is a malformed request, not a capacity
            // rejection. It must not make the physical-budget telemetry claim
            // that a valid unit was admitted or rejected by the queue limit.
            if (count > std::numeric_limits<std::uint64_t>::max() - current)
                return false;
            const auto next = current + count;
            if (next > capacity_.load(std::memory_order_acquire)) {
                rejectionCount_.fetch_add(1u, std::memory_order_relaxed);
                return false;
            }
            if (used_.compare_exchange_weak(
                    current, next, std::memory_order_acq_rel, std::memory_order_relaxed)) {
                updatePeak(next);
                return true;
            }
        }
    }

    void release(std::uint64_t count) noexcept { releaseAtomicCapacity(used_, count); }

    std::uint64_t used() const noexcept { return used_.load(std::memory_order_acquire); }

    std::uint64_t capacity() const noexcept { return capacity_.load(std::memory_order_acquire); }

    std::uint64_t peak() const noexcept { return peak_.load(std::memory_order_acquire); }

    std::uint64_t rejectionCount() const noexcept
    {
        return rejectionCount_.load(std::memory_order_acquire);
    }

    // A caller that performs a non-reserving admission preflight can report a
    // real capacity refusal without transiently acquiring a unit.
    void recordCapacityRejection() noexcept
    {
        rejectionCount_.fetch_add(1u, std::memory_order_relaxed);
    }

private:
    void updatePeak(const std::uint64_t observed) noexcept
    {
        auto peak = peak_.load(std::memory_order_relaxed);
        while (peak < observed
            && !peak_.compare_exchange_weak(
                peak, observed, std::memory_order_release, std::memory_order_relaxed)) {
        }
    }

    const std::atomic<std::uint64_t> capacity_;
    std::atomic<std::uint64_t> used_{0u};
    std::atomic<std::uint64_t> peak_{0u};
    std::atomic<std::uint64_t> rejectionCount_{0u};
};

// Completion and the last owner may both release safely. Units are counts,
// never a measurement of CPU or GPU bytes.
class AtomicCapacityLease final {
public:
    AtomicCapacityLease(std::shared_ptr<AtomicCapacityBudget> budget, std::uint64_t count) noexcept
        : budget_(std::move(budget))
    {
        if (budget_ && budget_->tryAcquire(count))
            units_.store(count, std::memory_order_release);
    }

    ~AtomicCapacityLease() { release(); }

    AtomicCapacityLease(const AtomicCapacityLease&) = delete;
    AtomicCapacityLease& operator=(const AtomicCapacityLease&) = delete;

    std::uint64_t units() const noexcept { return units_.load(std::memory_order_acquire); }

    bool charged() const noexcept { return units() != 0u; }

    const std::shared_ptr<AtomicCapacityBudget>& originalBudgetOwner() const noexcept
    {
        return budget_;
    }

    void release() noexcept
    {
        const auto count = units_.exchange(0u, std::memory_order_acq_rel);
        if (count != 0u)
            budget_->release(count);
    }

private:
    const std::shared_ptr<AtomicCapacityBudget> budget_;
    std::atomic<std::uint64_t> units_{0u};
};

} // namespace arp::core
