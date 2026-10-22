#pragma once
#include "core/container/bounded_sequence.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "core/runtime/external_callback_gate.h"

#include <optional>
#include <type_traits>

namespace arp::core::runtime {
// Externally serialized terminal notification, not completion evidence. Allocate
// stable slots during preparation. The caller retains this registry through
// every operation and suppresses any foreign borrow before destroying it.
// Tickets identify this instance, including after storage address reuse.
template <class Event> class BoundedTerminalObservers final {
    static_assert(std::is_trivially_copyable_v<Event>, "Terminal events must be bounded values");
    using Owner = std::shared_ptr<const void>;
    using Callback = void (*)(const void*, Event) noexcept;

    struct Slot {
        Owner owner;
        Callback callback = nullptr;
        std::uint64_t generation = 0u;
        std::optional<ExternalCallbackGate> gate;
    };

    class Operation final {
    public:
        explicit Operation(bool& active) noexcept : active_(active) { active_ = true; }

        ~Operation() { active_ = false; }

    private:
        bool& active_;
    };

public:
    enum class Status {
        Complete,
        InvalidInput,
        Unissued,
        Foreign,
        Stale,
        Capacity,
        IdentityExhausted,
        Closed,
        Reentrant,
        SuppressionFailed,
        Invariant
    };

    struct Ticket {
        std::uint64_t domain = 0u, generation = 0u;
        std::size_t index = 0u;

        explicit operator bool() const noexcept { return domain && generation; }
    };

    explicit BoundedTerminalObservers(std::size_t capacity, std::uint64_t firstGeneration = 1u)
        : domain_(domains_.tryAcquire()), generations_(firstGeneration), slots_(capacity)
    {
        for (std::size_t i = 0u; i < capacity; ++i)
            slots_.append(core::makeUnique<Slot>());
    }

    BoundedTerminalObservers(const BoundedTerminalObservers&) = delete;
    BoundedTerminalObservers& operator=(const BoundedTerminalObservers&) = delete;

    Status attach(Owner owner, Callback callback, Ticket& output) noexcept
    {
        output = {};
        if (inOperation_)
            return Status::Reentrant;
        if (terminal_)
            return Status::Closed;
        if (!owner || !callback)
            return Status::InvalidInput;
        if (!domain_ || !generations_.hasRemaining())
            return Status::IdentityExhausted;
        std::size_t index = 0u;
        for (const auto& entry : slots_.values()) {
            auto& slot = *entry;
            if (!slot.owner) {
                const auto generation = generations_.tryAcquire();
                if (!generation)
                    return Status::IdentityExhausted;
                slot.gate.emplace();
                if (!slot.gate->arm())
                    return Status::Invariant;
                slot.owner = std::move(owner);
                slot.callback = callback;
                slot.generation = generation;
                ++pending_;
                output = {domain_, generation, index};
                return Status::Complete;
            }
            ++index;
        }
        return Status::Capacity;
    }

    Status inspect(Ticket ticket) const noexcept
    {
        if (!ticket)
            return Status::Unissued;
        if (ticket.domain != domain_)
            return Status::Foreign;
        if (ticket.index >= slots_.values().size())
            return Status::Stale;
        const auto& slot = *slots_.values()[ticket.index];
        return slot.owner && slot.generation == ticket.generation ? Status::Complete
                                                                  : Status::Stale;
    }

    // The suppression adapter must actually suppress its foreign borrow. False
    // retains the exact owner, ticket and armed gate for later drain/delivery.
    // Reentry from suppression, receiver or owner destruction is rejected.
    template <class Suppress> Status suppress(Ticket ticket, Suppress&& suppress) noexcept
    {
        static_assert(std::is_nothrow_invocable_r_v<bool, Suppress, const void*>,
            "Suppression must not throw");
        if (inOperation_)
            return Status::Reentrant;
        if (const auto status = inspect(ticket); status != Status::Complete)
            return status;
        Operation operation(inOperation_);
        auto& slot = *slots_.values()[ticket.index];
        if (!suppress(slot.owner.get()))
            return Status::SuppressionFailed;
        if (!slot.gate || !slot.gate->detach())
            return Status::Invariant;
        release(slot);
        return Status::Complete;
    }

    Status deliver(Event event) noexcept
    {
        if (inOperation_)
            return Status::Reentrant;
        if (delivered_)
            return Status::Closed;
        // Admission closes before the first callback. No snapshot vector or
        // second completion pool is created; each exact owner remains retained
        // until its callback gate finishes delivery.
        Operation operation(inOperation_);
        terminal_ = event;
        delivered_ = true;
        for (const auto& entry : slots_.values()) {
            auto& slot = *entry;
            if (!slot.owner)
                continue;
            if (!slot.gate || !slot.gate->beginDelivery())
                return Status::Invariant;
            slot.callback(slot.owner.get(), event);
            if (!slot.gate->finishDelivery())
                return Status::Invariant;
            release(slot);
        }
        return Status::Complete;
    }

    // Close admission while preserving every receiver/ticket. This is only a
    // logical terminal marker; the owning runtime still owes actual delivery or
    // suppression. A later real event may replace this undelivered marker.
    Status closeAdmission(Event reason) noexcept
    {
        if (inOperation_)
            return Status::Reentrant;
        if (terminal_)
            return Status::Closed;
        terminal_ = reason;
        return Status::Complete;
    }

    std::size_t pending() const noexcept { return pending_; }

    std::size_t capacity() const noexcept { return slots_.values().size(); }

    bool accepting() const noexcept
    {
        return !inOperation_ && !terminal_ && domain_ && generations_.hasRemaining();
    }

    const std::optional<Event>& terminal() const noexcept { return terminal_; }

    bool delivered() const noexcept { return delivered_; }

private:
    void release(Slot& slot) noexcept
    {
        slot.callback = nullptr;
        slot.generation = 0u;
        --pending_;
        slot.owner.reset();
    }

    inline static core::MonotonicIdSequence domains_;
    const std::uint64_t domain_;
    core::MonotonicIdSequence generations_;
    core::BoundedSequence<core::UniquePtr<Slot>> slots_;
    std::optional<Event> terminal_;
    std::size_t pending_ = 0u;
    bool inOperation_ = false, delivered_ = false;
};
} // namespace arp::core::runtime
