#pragma once
#include "core/ownership/monotonic_id_sequence.h"
#include "core/ownership/mutation_journal.h"

#include <optional>

namespace arp::core {

// Scalar state in a stable retained object. Observation requires that object's
// original lifetime/ownership contract; a value/revision pair is not an owner.
template <class T> class VersionedMutationValue final {
    static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_copy_assignable_v<T>
            && std::is_nothrow_swappable_v<T>,
        "bounded non-allocating scalar value required");

public:
    enum class WriteResult { Written, Conflict, Exhausted, EnlistmentExhausted };

    struct Observation {
        T value;
        std::uint64_t revision;
    };

    class PendingObservation final {
        friend class VersionedMutationValue;

        PendingObservation(const VersionedMutationValue* source,
            const MutationJournal* journal,
            T value,
            std::uint64_t revision,
            std::uint64_t enlistmentEpoch) noexcept
            : source_(source), journal_(journal), value_(value), revision_(revision),
              enlistmentEpoch_(enlistmentEpoch)
        {}

    public:
        PendingObservation(const PendingObservation&) = default;

        const T& value() const noexcept { return value_; }

        std::uint64_t revision() const noexcept { return revision_; }

    private:
        const VersionedMutationValue* source_;
        const MutationJournal* journal_;
        T value_;
        std::uint64_t revision_;
        std::uint64_t enlistmentEpoch_;
    };

    explicit VersionedMutationValue(T initial,
        std::uint64_t firstRevision = 1u,
        std::uint64_t firstEnlistmentEpoch = 1u) noexcept
        : value_(initial), sequence_(firstRevision), enlistments_(firstEnlistmentEpoch),
          revision_(sequence_.tryAcquire()),
          snapshot_(
              value_,
              this,
              [](void* context) noexcept {
                  auto& self = *static_cast<VersionedMutationValue*>(context);
                  self.revision_ = self.invalidated_ ? 0u : self.sequence_.tryAcquire();
              },
              [](void* context) noexcept {
                  static_cast<VersionedMutationValue*>(context)->invalidate();
              })
    {}

    VersionedMutationValue(const VersionedMutationValue&) = delete;

    const T& value() const noexcept { return value_; }

    std::uint64_t revision() const noexcept { return revision_; }

    bool active() const noexcept { return revision_ != 0u; }

    // Terminal invalidation never restores validity, including on journal rollback.
    void invalidate() noexcept
    {
        invalidated_ = true;
        revision_ = 0u;
    }

    bool pending() const noexcept { return snapshot_.pending(); }

    bool discardPending(MutationJournal& journal) noexcept { return snapshot_.discard(journal); }

    bool enlistmentExhausted() const noexcept
    {
        return !snapshot_.pending() && !enlistments_.hasRemaining();
    }

    bool canPrepare(const MutationJournal& journal) const noexcept
    {
        return active() && snapshot_.canPrepare(journal)
            && (snapshot_.pending() || enlistments_.hasRemaining());
    }

    // One rollback of an enlisted snapshot consumes one additional revision.
    // This preflight never reserves identities or modifies snapshot ownership.
    bool canWriteBatch(const MutationJournal& journal, std::uint64_t writes) const noexcept
    {
        return canPrepare(journal) && writes != UINT64_MAX && sequence_.hasRemaining(writes + 1u);
    }

    // Serialized direct writes need no rollback reservation. This dry check
    // does not consume revisions or invalidate an exhausted value.
    bool canWriteBatch(std::uint64_t writes) const noexcept
    {
        return active() && !pending() && sequence_.hasRemaining(writes);
    }

    bool prepare(MutationJournal& journal) noexcept
    {
        if (!canPrepare(journal))
            return false;
        if (snapshot_.pending())
            return true;
        const auto epoch = enlistments_.tryAcquire();
        if (!epoch || !snapshot_.prepare(journal))
            return false;
        enlistmentEpoch_ = epoch;
        return true;
    }

    std::optional<Observation> observe() const noexcept
    {
        if (!active() || pending())
            return {};
        return Observation{value_, revision_};
    }

    bool matches(const Observation& input) const noexcept
    {
        return active() && !pending() && input.revision == revision_ && input.value == value_;
    }

    // Pending scalar evidence is specific to this object and this exact journal.
    // The caller must still retain the original object/journal lifetime; this
    // observation neither commits mutations nor grants execution authority.
    std::optional<PendingObservation> observePending(const MutationJournal& journal) const noexcept
    {
        if (!active() || !enlistmentEpoch_ || !snapshot_.ownedBy(journal))
            return {};
        return PendingObservation{this, &journal, value_, revision_, enlistmentEpoch_};
    }

    bool matchesPending(
        const PendingObservation& input, const MutationJournal& journal) const noexcept
    {
        return active() && input.source_ == this && input.journal_ == &journal
            && snapshot_.ownedBy(journal) && input.enlistmentEpoch_ == enlistmentEpoch_
            && input.revision_ == revision_ && input.value_ == value_;
    }

    WriteResult tryWrite(T value, MutationJournal* journal = nullptr) noexcept
    {
        if (!active())
            return WriteResult::Exhausted;
        if (journal ? !snapshot_.canPrepare(*journal) : snapshot_.pending())
            return WriteResult::Conflict;
        // New journal ownership is a non-recycling identity domain separate
        // from value revisions. Refusal leaves both value and snapshot intact.
        if (journal && enlistmentExhausted())
            return WriteResult::EnlistmentExhausted;
        const auto next = sequence_.tryAcquire();
        if (!next) {
            revision_ = 0u;
            return WriteResult::Exhausted;
        }
        // External serialization and the prior canPrepare check guarantee
        // admission. Scalar snapshot copying cannot allocate or throw.
        if (journal && !prepare(*journal))
            return WriteResult::Conflict;
        value_ = value;
        revision_ = next;
        return WriteResult::Written;
    }

private:
    T value_;
    MonotonicIdSequence sequence_;
    MonotonicIdSequence enlistments_;
    std::uint64_t revision_;
    std::uint64_t enlistmentEpoch_ = 0u;
    MutationSnapshot<T> snapshot_;
    bool invalidated_ = false;
};
} // namespace arp::core
