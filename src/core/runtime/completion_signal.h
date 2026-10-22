#pragma once

#include "core/ownership/monotonic_id_sequence.h"

namespace arp::core::runtime {

// Reset/host mutation, reservation and callback delivery are externally
// serialized. Atomic polling may occur elsewhere. No operation allocates or
// waits; a reservation remains exclusive until its original graph retires.
class CompletionSignal final {
public:
    struct Ticket {
        std::uint64_t generation = 0u;

        explicit operator bool() const noexcept { return generation != 0u; }
    };

    CompletionSignal() noexcept : CompletionSignal(1u) {}

    explicit CompletionSignal(std::uint64_t first) noexcept
        : sequence_(first), current_(sequence_.tryAcquire())
    {}

    void reset() noexcept { current_.store(sequence_.tryAcquire(), std::memory_order_release); }

    void signalFromHost() noexcept
    {
        reset();
        const auto current = current_.load(std::memory_order_acquire);
        if (current)
            completed_.store(current, std::memory_order_release);
    }

    bool isComplete() const noexcept
    {
        const auto current = current_.load(std::memory_order_acquire);
        return current && completed_.load(std::memory_order_acquire) == current;
    }

    bool isComplete(Ticket ticket) const noexcept
    {
        return ticket && ticket.generation == current_.load(std::memory_order_acquire)
            && ticket.generation == completed_.load(std::memory_order_acquire);
    }

    bool canReserve() const noexcept
    {
        return current_.load(std::memory_order_acquire) && !reserved_ && !isComplete();
    }

    Ticket reserve() noexcept
    {
        if (!canReserve())
            return {};
        reserved_ = current_.load(std::memory_order_acquire);
        return {reserved_};
    }

    bool matches(Ticket ticket) const noexcept
    {
        return ticket && ticket.generation == reserved_
            && ticket.generation == current_.load(std::memory_order_acquire) && !isComplete();
    }

    bool complete(Ticket ticket) noexcept
    {
        if (!matches(ticket))
            return false;
        completed_.store(ticket.generation, std::memory_order_release);
        return true;
    }

    bool release(Ticket ticket) noexcept
    {
        if (!ticket || ticket.generation != reserved_)
            return false;
        reserved_ = 0u;
        return true;
    }

private:
    MonotonicIdSequence sequence_;
    std::atomic<std::uint64_t> current_;
    std::atomic<std::uint64_t> completed_{0u};
    std::uint64_t reserved_ = 0u;
};
} // namespace arp::core::runtime
