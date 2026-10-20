#pragma once
#include "core/cpu_allocator.h"
#include "core/versioned_mutation_value.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class VersionedMutationValueFixture final {
public:
    static void verify()
    {
        using Value = core::VersionedMutationValue<std::uint32_t>;
        using Result = Value::WriteResult;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            Value prepared(5u);
            core::MutationJournal original, foreign;
            const auto initial = prepared.observe();
            assert(initial);
            assert(prepared.canPrepare(original) && prepared.prepare(original));
            assert(prepared.revision() == initial->revision && prepared.value() == initial->value
                && !prepared.observe());
            assert(prepared.prepare(original) && !prepared.canPrepare(foreign)
                && !prepared.prepare(foreign) && foreign.empty());
            const auto pending = prepared.observePending(original);
            assert(pending);
            original.rollback();
            assert(!prepared.matches(*initial) && !prepared.matchesPending(*pending, original));
            const auto beforeCommit = prepared.observe();
            assert(beforeCommit && prepared.prepare(original));
            const auto beforeCommitPending = prepared.observePending(original);
            assert(beforeCommitPending);
            original.commit();
            assert(prepared.matches(*beforeCommit)); // No write occurred.
            assert(prepared.prepare(original));
            assert(!prepared.matchesPending(*beforeCommitPending, original));
            assert(prepared.tryWrite(9u, &original) == Result::Written);
            original.rollback();
            assert(prepared.value() == 5u && !prepared.matches(*beforeCommit));
            prepared.invalidate();
            assert(
                !prepared.canPrepare(original) && !prepared.prepare(original) && original.empty());
        }
        {
            core::MonotonicIdSequence identities(UINT64_MAX - 2u);
            assert(identities.hasRemaining(0u) && identities.hasRemaining(3u)
                && !identities.hasRemaining(4u) && !identities.hasRemaining(UINT64_MAX));
            assert(identities.tryAcquire() == UINT64_MAX - 2u && identities.hasRemaining(2u)
                && !identities.hasRemaining(3u));
            assert(identities.tryAcquire() == UINT64_MAX - 1u
                && identities.tryAcquire() == UINT64_MAX);
            assert(identities.hasRemaining(0u) && !identities.hasRemaining(1u));
            core::MutationJournal journal, foreign;
            Value exact(5u, UINT64_MAX - 3u), oneLess(7u, UINT64_MAX - 2u),
                noRollback(11u, UINT64_MAX);
            const auto before = oneLess.observe();
            assert(before && journal.empty());
            assert(exact.canWriteBatch(journal, 2u) && !exact.canWriteBatch(journal, 3u));
            assert(!oneLess.canWriteBatch(journal, 2u) && oneLess.canWriteBatch(journal, 1u)
                && oneLess.matches(*before) && journal.empty());
            assert(!noRollback.canWriteBatch(journal, 0u) && noRollback.active());
            assert(!exact.canWriteBatch(journal, UINT64_MAX) && exact.prepare(journal));
            assert(!exact.canWriteBatch(foreign, 0u) && exact.canWriteBatch(journal, 2u));
            assert(exact.tryWrite(13u, &journal) == Result::Written
                && exact.tryWrite(17u, &journal) == Result::Written);
            assert(exact.canWriteBatch(journal, 0u) && !exact.canWriteBatch(journal, 1u));
            journal.rollback();
            assert(exact.value() == 5u && exact.revision() == UINT64_MAX && exact.active());
            assert(!exact.canWriteBatch(journal, 0u) && journal.empty());
        }
        Value value(7u);
        {
            Value noEpoch(61u, 1u, 0u), lastEpoch(67u, 1u, UINT64_MAX);
            core::MutationJournal journal;
            const auto initial = noEpoch.observe();
            assert(initial);
            assert(!noEpoch.canPrepare(journal) && !noEpoch.prepare(journal));
            assert(noEpoch.tryWrite(71u, &journal) == Result::EnlistmentExhausted && journal.empty()
                && noEpoch.matches(*initial));
            assert(noEpoch.tryWrite(71u) == Result::Written);
            const auto lastInitial = lastEpoch.observe();
            assert(lastInitial && lastEpoch.prepare(journal));
            const auto pending = lastEpoch.observePending(journal);
            assert(pending && lastEpoch.prepare(journal)
                && lastEpoch.matchesPending(*pending, journal));
            assert(lastEpoch.tryWrite(73u, &journal) == Result::Written);
            journal.commit();
            const auto committed = lastEpoch.observe();
            assert(committed && !lastEpoch.canPrepare(journal) && !lastEpoch.prepare(journal));
            assert(lastEpoch.tryWrite(79u, &journal) == Result::EnlistmentExhausted
                && journal.empty() && lastEpoch.matches(*committed));
            assert(!lastEpoch.matchesPending(*pending, journal)
                && lastEpoch.tryWrite(79u) == Result::Written);
            Value recycled(83u);
            std::optional<core::MutationJournal> slot{std::in_place};
            assert(recycled.prepare(*slot));
            const auto old = recycled.observePending(*slot);
            assert(old);
            slot->commit();
            slot.reset();
            slot.emplace();
            assert(recycled.prepare(*slot) && !recycled.matchesPending(*old, *slot));
            slot->commit();
        }
        core::MutationJournal notPending;
        assert(!value.observePending(notPending));
        auto first = value.observe();
        assert(first && value.matches(*first));
        assert(value.tryWrite(7u) == Result::Written && !value.matches(*first));
        first = value.observe();
        assert(first);
        {
            core::MutationJournal firstJournal, foreign;
            assert(value.tryWrite(11u, &firstJournal) == Result::Written);
            assert(!value.observe() && !value.matches(*first));
            const auto pending = value.observePending(firstJournal);
            assert(pending);
            assert(pending->value() == 11u && pending->revision() == value.revision());
            assert(value.matchesPending(*pending, firstJournal)
                && !value.matchesPending(*pending, foreign));
            assert(!value.observePending(foreign));
            Value sameFacts(7u, first->revision);
            assert(sameFacts.tryWrite(11u, &firstJournal) == Result::Written);
            assert(sameFacts.revision() == pending->revision()
                && !sameFacts.matchesPending(*pending, firstJournal));
            const auto revision = value.revision();
            assert(value.tryWrite(13u, &foreign) == Result::Conflict && foreign.empty());
            assert(value.tryWrite(13u) == Result::Conflict);
            assert(value.revision() == revision && value.value() == 11u);
            assert(value.tryWrite(17u, &firstJournal) == Result::Written
                && !value.matchesPending(*pending, firstJournal));
            const auto rewritten = value.observePending(firstJournal);
            assert(rewritten && value.matchesPending(*rewritten, firstJournal));
            firstJournal.rollback();
            assert(!value.matchesPending(*rewritten, firstJournal)
                && !value.observePending(firstJournal));
            assert(value.tryWrite(17u, &firstJournal) == Result::Written
                && !value.matchesPending(*rewritten, firstJournal));
        }
        assert(value.value() == 7u && !value.pending() && !value.matches(*first));
        const auto restored = value.observe();
        assert(restored && value.matches(*restored));
        {
            core::MutationJournal commit;
            assert(value.tryWrite(19u, &commit) == Result::Written);
            const auto pending = value.observePending(commit);
            assert(pending && !value.observe());
            commit.commit();
            assert(!value.matchesPending(*pending, commit) && !value.observePending(commit));
            assert(commit.empty() && value.value() == 19u && value.observe());
        }
        assert(value.value() == 19u && !value.matches(*restored));
        Value exhausted(23u, 0u), last(29u, UINT64_MAX), rollbackLast(31u, UINT64_MAX - 1u);
        assert(!exhausted.active() && !exhausted.observe()
            && exhausted.tryWrite(0u) == Result::Exhausted);
        const auto lastObservation = last.observe();
        assert(lastObservation);
        core::MutationJournal rejected;
        assert(last.tryWrite(37u, &rejected) == Result::Exhausted && rejected.empty()
            && last.value() == 29u);
        assert(!last.active() && !last.matches(*lastObservation));
        {
            core::MutationJournal rollback;
            assert(rollbackLast.tryWrite(41u, &rollback) == Result::Written);
            assert(rollbackLast.revision() == UINT64_MAX && rollbackLast.value() == 41u);
            const auto pending = rollbackLast.observePending(rollback);
            assert(pending);
            assert(rollbackLast.tryWrite(43u, &rollback) == Result::Exhausted);
            assert(!rollbackLast.matchesPending(*pending, rollback)
                && !rollbackLast.observePending(rollback));
        }
        assert(rollbackLast.value() == 31u && !rollbackLast.active() && !rollbackLast.observe());
        Value invalidated(43u);
        const auto originalInvalidated = invalidated.observe();
        assert(originalInvalidated);
        core::MutationJournal invalidationJournal;
        assert(invalidated.tryWrite(47u, &invalidationJournal) == Result::Written);
        const auto invalidatedPending = invalidated.observePending(invalidationJournal);
        assert(invalidatedPending);
        invalidated.invalidate();
        assert(!invalidated.active() && !invalidated.observe()
            && !invalidated.observePending(invalidationJournal));
        assert(!invalidated.matches(*originalInvalidated)
            && !invalidated.matchesPending(*invalidatedPending, invalidationJournal));
        invalidationJournal.rollback();
        assert(invalidated.value() == 43u && !invalidated.active()
            && invalidated.tryWrite(53u) == Result::Exhausted);
        invalidated.invalidate();
        assert(!invalidated.active());
        core::MutationJournal outlives;
        {
            Value destroyed(43u);
            assert(destroyed.tryWrite(47u, &outlives) == Result::Written && !outlives.empty());
        }
        assert(outlives.empty());
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        {
            auto owner = core::makeShared<Value>(53u);
            core::MutationJournal retained;
            auto retainedOwner = owner;
            assert(owner->tryWrite(59u, &retained) == Result::Written);
            owner.reset();
            assert(retainedOwner->value() == 59u && !retained.empty());
            retained.rollback();
            assert(retainedOwner->value() == 53u && retainedOwner->observe());
        }
        std::puts(
            "BINDLESS_RHI_TERMINAL_MUTATION_INVALIDATION_OK pending rollback permanent allocation0");
        std::puts(
            "BINDLESS_RHI_PENDING_MUTATION_OBSERVATION_OK exact-value journal revision rewrite commit rollback exhaustion allocation0");
        std::puts(
            "BINDLESS_RHI_VERSIONED_MUTATION_OK write same-value conflict rollback-ABA commit exhaustion owner allocation0");
        std::puts(
            "BINDLESS_CORE_PENDING_ENLISTMENT_EPOCH_OK no-write-commit-reprepare recycled-journal-address same-session-stable zero-last-exhausted refuse-before-mutation direct-write-independent allocation0");
    }
};
} // namespace arp::test
