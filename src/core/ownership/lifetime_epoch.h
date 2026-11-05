#pragma once

#include "core/memory/cpu_allocator.h"
#include "core/ownership/monotonic_id_sequence.h"

#include <cstdint>
#include <memory>

namespace arp::core {

// One owner, weak observation tickets. Advance and destruction invalidate old
// tickets without prolonging the owner's lifetime or allocating on advance.
// Owner mutation/destruction and ticket observation must be externally serialized.
class LifetimeEpoch final {
    struct State {
        explicit State(std::uint64_t first) noexcept
            : sequence(first), current(sequence.tryAcquire())
        {}

        MonotonicIdSequence sequence;
        std::uint64_t current;
    };

public:
    class Observation;

    class Ticket final {
    public:
        Ticket() = default;

        bool isCurrent() const noexcept
        {
            const auto state = state_.lock();
            return state && generation_ != 0u && state->current == generation_;
        }

        // Exact owner/epoch join, without exposing or synthesizing a public ID.
        bool sameCurrentEpoch(const Ticket& other) const noexcept
        {
            const auto state = state_.lock(), otherState = other.state_.lock();
            return state && state == otherState && generation_ != 0u
                && generation_ == other.generation_ && state->current == generation_;
        }

    private:
        friend class LifetimeEpoch;
        friend class Observation;

        explicit Ticket(const std::shared_ptr<State>& state) noexcept
            : state_(state), generation_(state->current)
        {}

        std::weak_ptr<State> state_;
        std::uint64_t generation_ = 0u;
    };

    // Repeated observation under the same external serialization as Ticket.
    // The weak ticket guards this immutable borrow of the private state. No
    // temporary strong owner is acquired by isCurrent(), and the owner is
    // still the only object that can advance or destroy that state.
    class Observation final {
    public:
        Observation() = default;

        bool isCurrent() const noexcept
        {
            return identity_.generation_ != 0u && !identity_.state_.expired()
                && state_->current == identity_.generation_;
        }

        Ticket ticket() const noexcept { return identity_; }

    private:
        friend class LifetimeEpoch;

        explicit Observation(const std::shared_ptr<State>& state) noexcept
            : identity_(state), state_(state.get())
        {}

        Ticket identity_;
        const State* state_ = nullptr;
    };

    // A starting value permits bounded-domain exhaustion tests. Zero starts
    // permanently exhausted. This is not a public resource identity source.
    explicit LifetimeEpoch(std::uint64_t first = 1u) : state_(core::makeShared<State>(first)) {}

    LifetimeEpoch(const LifetimeEpoch&) = delete;
    LifetimeEpoch& operator=(const LifetimeEpoch&) = delete;
    LifetimeEpoch(LifetimeEpoch&&) = delete;
    LifetimeEpoch& operator=(LifetimeEpoch&&) = delete;

    Ticket ticket() const noexcept { return Ticket(state_); }

    Observation observe() const noexcept { return Observation(state_); }

    bool isActive() const noexcept { return state_->current != 0u; }

    void advance() noexcept { state_->current = state_->sequence.tryAcquire(); }

private:
    std::shared_ptr<State> state_;
};

// A non-owning pointer guarded by the owner's epoch. get() is the only
// dereference gateway; observation and owner destruction are externally
// serialized just like LifetimeEpoch. This does not make a raw object shared.
template <typename T> class LifetimeObserver final {
public:
    LifetimeObserver() = default;

    LifetimeObserver(T* object, LifetimeEpoch::Ticket lifetime) noexcept
        : object_(object), lifetime_(std::move(lifetime))
    {}

    T* get() const noexcept { return lifetime_.isCurrent() ? object_ : nullptr; }

private:
    T* object_ = nullptr;
    LifetimeEpoch::Ticket lifetime_;
};

} // namespace arp::core
