#pragma once

#include "rhi/frontend/memory_allocator_backing.h"

#include <atomic>
#include <limits>

namespace arp::tests {
class AllocatorBackingLedgerFixture;
}

namespace arp::rhi::detail {

// Optional diagnostic accounting must never block allocation or retirement.
// One attempt, no spin/retry. Lost events withdraw the observation forever;
// native execution/resource ownership never depends on these counters.
class AllocatorBackingLedger final {
    friend class arp::tests::AllocatorBackingLedgerFixture;

    class Guard final {
    public:
        explicit Guard(std::atomic_flag& flag) noexcept
            : flag_(flag), acquired_(!flag_.test_and_set(std::memory_order_acquire))
        {}

        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        ~Guard()
        {
            if (acquired_)
                flag_.clear(std::memory_order_release);
        }

        bool acquired() const noexcept { return acquired_; }

    private:
        std::atomic_flag& flag_;
        const bool acquired_;
    };

    static AllocatorBackingObservation failure(const char* diagnostic) noexcept
    {
        AllocatorBackingObservation result;
        result.state = AllocatorBackingState::DiagnosticFailure;
        result.diagnostic = diagnostic;
        return result;
    }

public:
    void allocated(std::uint64_t bytes) noexcept
    {
        const Guard guard(guard_);
        if (!guard.acquired()) {
            failure_.store(eventContention, std::memory_order_release);
            return;
        }
        if (failure_.load(std::memory_order_acquire))
            return;
        const auto limit = std::numeric_limits<std::uint64_t>::max();
        if (!bytes || bytes > limit - stats_.backingBytes || stats_.blockCount == limit
            || stats_.allocationEvents == limit) {
            failure_.store(counterInvariant, std::memory_order_release);
            return;
        }
        stats_.backingBytes += bytes;
        ++stats_.blockCount;
        ++stats_.allocationEvents;
        if (stats_.backingBytes > stats_.peakBackingBytes)
            stats_.peakBackingBytes = stats_.backingBytes;
        if (stats_.blockCount > stats_.peakBlockCount)
            stats_.peakBlockCount = stats_.blockCount;
    }

    void released(std::uint64_t bytes) noexcept
    {
        const Guard guard(guard_);
        if (!guard.acquired()) {
            failure_.store(eventContention, std::memory_order_release);
            return;
        }
        if (failure_.load(std::memory_order_acquire))
            return;
        if (!bytes || bytes > stats_.backingBytes || !stats_.blockCount
            || stats_.releaseEvents == std::numeric_limits<std::uint64_t>::max()) {
            failure_.store(counterInvariant, std::memory_order_release);
            return;
        }
        stats_.backingBytes -= bytes;
        --stats_.blockCount;
        ++stats_.releaseEvents;
        if (bool(stats_.backingBytes) != bool(stats_.blockCount))
            failure_.store(counterInvariant, std::memory_order_release);
    }

    AllocatorBackingObservation observe(std::uint64_t backingBytes,
        std::uint64_t blockCount,
        std::uint64_t occupiedBytes,
        std::uint64_t allocationCount) const noexcept
    {
        const Guard guard(guard_);
        if (!guard.acquired())
            return failure("RHI-ALLOCATOR-BACKING-BUSY");
        const auto lost = failure_.load(std::memory_order_acquire);
        if (lost == eventContention)
            return failure("RHI-ALLOCATOR-BACKING-EVENT-CONTENTION");
        if (lost || backingBytes != stats_.backingBytes || blockCount != stats_.blockCount
            || occupiedBytes > backingBytes || bool(occupiedBytes) != bool(allocationCount))
            return failure("RHI-ALLOCATOR-BACKING-COUNTER-INVARIANT");
        auto result = stats_;
        result.state = AllocatorBackingState::Observed;
        result.diagnostic = nullptr;
        result.occupiedBytes = occupiedBytes;
        result.allocationCount = allocationCount;
        result.unusedBytes = backingBytes - occupiedBytes;
        return result;
    }

private:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static constexpr std::uint32_t counterInvariant = 1u, eventContention = 2u;
    mutable std::atomic_flag guard_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint32_t> failure_{0u};
    AllocatorBackingObservation stats_;
};

} // namespace arp::rhi::detail
