#pragma once

#include "core/cpu_allocator.h"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace arp::core::runtime {

// Single-owner-thread storage for asynchronous completion lifetimes. Capacity
// is allocated at construction. Callers retain the pool through every callback
// and provide completion/invalidation proof; this class never manufactures it.
template <typename T> class CompletionRetentionPool final {
public:
    struct Ticket {
        std::size_t index = 0u;
        std::uint64_t generation = 0u;

        explicit operator bool() const noexcept { return generation != 0u; }
    };

    explicit CompletionRetentionPool(std::size_t capacity) { slots_.resize(capacity); }

    CompletionRetentionPool(const CompletionRetentionPool&) = delete;
    CompletionRetentionPool& operator=(const CompletionRetentionPool&) = delete;

    Ticket reserve(core::UniquePtr<T>& candidate) noexcept
    {
        if (!candidate || closed_ || failed_
            || nextGeneration_ == std::numeric_limits<std::uint64_t>::max())
            return {};
        for (std::size_t i = 0u; i < slots_.size(); ++i) {
            auto& slot = slots_[i];
            if (slot.value)
                continue;
            slot.value = std::move(candidate);
            slot.generation = ++nextGeneration_;
            slot.submitted = false;
            slot.invalidated = false;
            ++pending_;
            return {i, slot.generation};
        }
        return {};
    }

    T* get(Ticket ticket) noexcept
    {
        auto* slot = find(ticket);
        return slot ? slot->value.get() : nullptr;
    }

    // Inspect original reserved/submitted/quarantined graphs without creating
    // another owner inventory. Empty and cancelled slots are never projected.
    template <class Predicate> bool containsMatching(Predicate&& predicate) const noexcept
    {
        static_assert(noexcept(predicate(std::declval<const T&>())),
            "Completion graph predicate must not throw");
        static_assert(std::is_same_v<decltype(predicate(std::declval<const T&>())), bool>,
            "Completion graph predicate must return bool");
        for (const auto& slot : slots_)
            if (slot.value && predicate(*slot.value))
                return true;
        return false;
    }

    bool submit(Ticket ticket) noexcept
    {
        return submitWith(ticket, [](T&) noexcept {
            return true;
        });
    }

    // Exact-ticket admission and owner publication are one externally serialized
    // operation. The callback must not reenter this pool or destroy its owner.
    // False must preserve its own transaction or quarantine partial changes.
    template <class Commit> bool submitWith(Ticket ticket, Commit&& commit) noexcept
    {
        static_assert(
            noexcept(commit(std::declval<T&>())), "Submission commit callback must not throw");
        static_assert(std::is_same_v<decltype(commit(std::declval<T&>())), bool>,
            "Submission commit callback must return bool");
        auto* slot = find(ticket);
        if (!slot || slot->submitted || slot->invalidated || failed_ || closed_)
            return false;
        if (!commit(*slot->value))
            return false;
        slot->submitted = true;
        return true;
    }

    // Read-only preflight for native userdata suppression. A submitted or stale
    // ticket must be refused before detaching any completion/loss observer.
    // Preflight and cancellation require the same external serialization.
    bool canCancel(Ticket ticket) const noexcept
    {
        const auto* slot = find(ticket);
        return slot && !slot->submitted && !slot->invalidated;
    }

    bool cancel(Ticket ticket) noexcept
    {
        auto* slot = find(ticket);
        if (!slot || slot->submitted || slot->invalidated)
            return false;
        release(*slot);
        return true;
    }

    // An unproven completion closes further admission and keeps its owner.
    // Other submitted tickets may still complete, but admission stays closed.
    bool resolve(Ticket ticket, bool completionProven) noexcept
    {
        auto* slot = find(ticket);
        if (!slot || !slot->submitted || slot->invalidated)
            return false;
        if (completionProven)
            release(*slot);
        else
            failed_ = true;
        return true;
    }

    // Immediate release requires the owning runtime to prove that no foreign
    // userdata still borrows these graphs. Already retained invalidations stay
    // retained; this older entry point cannot bypass their drain obligation.
    void invalidate() noexcept
    {
        invalidate([](T&) noexcept {});
    }

    template <class Invalidated> void invalidate(Invalidated&& invalidated) noexcept
    {
        static_assert(
            noexcept(invalidated(std::declval<T&>())), "Invalidation callback must not throw");
        static_assert(std::is_same_v<decltype(invalidated(std::declval<T&>())), void>,
            "Invalidation callback must return void");
        closed_ = true;
        for (auto& slot : slots_)
            if (slot.value && !slot.invalidated) {
                invalidated(*slot.value);
                release(slot);
            }
    }

    // Terminal invalidation closes admission and notifies each live graph once,
    // preserving its exact ticket/owner for foreign callback drain. Notification
    // must invalidate semantic state, never report successful completion.
    template <class Invalidated> void invalidateAndRetain(Invalidated&& invalidated) noexcept
    {
        static_assert(
            noexcept(invalidated(std::declval<T&>())), "Invalidation callback must not throw");
        static_assert(std::is_same_v<decltype(invalidated(std::declval<T&>())), void>,
            "Invalidation callback must return void");
        closed_ = true;
        for (auto& slot : slots_)
            if (slot.value && !slot.invalidated) {
                slot.invalidated = true;
                invalidated(*slot.value);
            }
    }

    bool retainedInvalidated(Ticket ticket) const noexcept
    {
        const auto* slot = find(ticket);
        return slot && slot->invalidated;
    }

    // Only the owning runtime can supply drain/suppression proof. A ticket is
    // identity, not that proof; callers must not expose this as a completion API.
    bool releaseInvalidated(Ticket ticket) noexcept
    {
        auto* slot = find(ticket);
        if (!slot || !slot->invalidated)
            return false;
        release(*slot);
        return true;
    }

    bool accepting() const noexcept { return !failed_ && !closed_; }

    bool failed() const noexcept { return failed_; }

    bool closed() const noexcept { return closed_; }

    std::size_t pending() const noexcept { return pending_; }

    std::size_t capacity() const noexcept { return slots_.size(); }

private:
    struct Slot {
        core::UniquePtr<T> value;
        std::uint64_t generation = 0u;
        bool submitted = false;
        bool invalidated = false;
    };

    Slot* find(Ticket ticket) noexcept
    {
        return const_cast<Slot*>(std::as_const(*this).find(ticket));
    }

    const Slot* find(Ticket ticket) const noexcept
    {
        if (!ticket || ticket.index >= slots_.size())
            return nullptr;
        const auto& slot = slots_[ticket.index];
        return slot.value && slot.generation == ticket.generation ? &slot : nullptr;
    }

    void release(Slot& slot) noexcept
    {
        slot.value.reset();
        slot.generation = 0u;
        slot.submitted = false;
        slot.invalidated = false;
        --pending_;
    }

    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
    std::uint64_t nextGeneration_ = 0u;
    std::size_t pending_ = 0u;
    bool failed_ = false;
    bool closed_ = false;
};

} // namespace arp::core::runtime
