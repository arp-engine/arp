#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace arp::core {

// Non-recycling identity source. Zero is the exhausted/invalid sentinel.
// Default construction starts at 1; a caller may supply a starting value for
// a bounded identity domain; after UINT64_MAX is issued, the sequence stays
// exhausted permanently.
class MonotonicIdSequence final {
public:
    // GCC 16 fails to default-construct inline static members from an
    // explicit ctor with a default argument; a defaulted ctor keeps identity
    // 1 as the first issued value on every conforming compiler.
    MonotonicIdSequence() noexcept : next_(1u) {}

    // A caller may supply a starting value for a bounded identity domain.
    explicit MonotonicIdSequence(std::uint64_t first) noexcept : next_(first) {}

    // A preflight for externally serialized transactions. This does not reserve
    // a value against concurrent tryAcquire callers.
    bool hasRemaining() const noexcept { return next_.load(std::memory_order_relaxed) != 0u; }

    // Non-reserving batch preflight under the same external serialization as
    // hasRemaining(). Subtraction proves count without overflow at UINT64_MAX.
    bool hasRemaining(std::uint64_t count) const noexcept
    {
        const auto next = next_.load(std::memory_order_relaxed);
        return !count || (next && count - 1u <= std::numeric_limits<std::uint64_t>::max() - next);
    }

    std::uint64_t tryAcquire() noexcept
    {
        auto candidate = next_.load(std::memory_order_relaxed);
        while (candidate != 0u) {
            const auto following =
                candidate == std::numeric_limits<std::uint64_t>::max() ? 0u : candidate + 1u;
            if (next_.compare_exchange_weak(
                    candidate, following, std::memory_order_relaxed, std::memory_order_relaxed)) {
                return candidate;
            }
        }
        return 0u;
    }

private:
    std::atomic<std::uint64_t> next_;
};

} // namespace arp::core
