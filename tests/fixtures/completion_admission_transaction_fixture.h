#pragma once
#include "core/runtime/completion_retention_pool.h"
#include "core/versioned_mutation_value.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class CompletionAdmissionTransactionFixture final {
    using Value = core::VersionedMutationValue<unsigned>;

    struct Record {
        explicit Record(std::shared_ptr<Value> value) : owner(std::move(value))
        {
            assert(owner->tryWrite(17u, &journal) == Value::WriteResult::Written);
            expected = owner->observePending(journal);
            assert(expected);
        }

        bool commit() noexcept
        {
            ++calls;
            if (reject || !expected || !owner->matchesPending(*expected, journal))
                return false;
            journal.commit();
            ++publications;
            return true;
        }

        // Owner outlives pending rollback on cancelled/unsubmitted destruction.
        std::shared_ptr<Value> owner;
        core::MutationJournal journal;
        std::optional<Value::PendingObservation> expected;
        unsigned calls = 0u, publications = 0u;
        bool reject = true;
    };

    using Pool = core::runtime::CompletionRetentionPool<Record>;

public:
    static void verify()
    {
        Pool pool(2u), closed(1u), failed(2u);
        auto value = core::makeShared<Value>(11u), cancelled = core::makeShared<Value>(13u);
        auto failure = core::makeShared<Value>(19u), blocked = core::makeShared<Value>(23u);
        auto candidate = core::makeUnique<Record>(value),
             drop = core::makeUnique<Record>(cancelled);
        auto bad = core::makeUnique<Record>(failure), waiting = core::makeUnique<Record>(blocked);
        const auto one = pool.reserve(candidate), two = pool.reserve(drop);
        const auto unproven = failed.reserve(bad), blockedTicket = failed.reserve(waiting);
        assert(one && two && unproven && blockedTicket);
        auto* original = pool.get(one);
        auto* pendingFailure = failed.get(unproven);
        auto* pendingBlocked = failed.get(blockedTicket);
        const auto initialRevision = value->revision();
        const auto dropRevision = cancelled->revision();
        unsigned invocations = 0u;
        const auto commit = [&](Record& record) noexcept {
            ++invocations;
            return record.commit();
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!pool.submitWith({}, commit)
            && !pool.submitWith({one.index, one.generation + 1u}, commit));
        assert(!closed.submitWith(one, commit) && !invocations);
        assert(!pool.submitWith(one, commit) && invocations == 1u && original->calls == 1u
            && !original->publications);
        assert(value->pending() && value->revision() == initialRevision
            && value->matchesPending(*original->expected, original->journal));
        assert(!pool.resolve(one, true) && pool.get(one) == original && pool.pending() == 2u);
        original->reject = false;
        assert(pool.submitWith(one, commit) && invocations == 2u && original->publications == 1u
            && !value->pending());
        assert(value->value() == 17u && value->revision() == initialRevision && value->observe());
        assert(!pool.submitWith(one, commit) && invocations == 2u && !pool.cancel(one));
        assert(pool.cancel(two) && !cancelled->pending() && cancelled->value() == 13u
            && cancelled->revision() != dropRevision);
        assert(!pool.submitWith(two, commit) && invocations == 2u);
        // A rewritten pending value cannot be committed by older evidence.
        pendingFailure->reject = false;
        assert(failure->tryWrite(29u, &pendingFailure->journal) == Value::WriteResult::Written);
        assert(!failed.submitWith(unproven, commit) && invocations == 3u && failure->pending());
        pendingFailure->expected = failure->observePending(pendingFailure->journal);
        assert(failed.submitWith(unproven, commit) && invocations == 4u && !failure->pending());
        assert(failed.resolve(unproven, false) && failed.failed()
            && failed.get(unproven) == pendingFailure);
        pendingBlocked->reject = false;
        assert(!failed.submitWith(blockedTicket, commit) && invocations == 4u && blocked->pending()
            && !pendingBlocked->calls);
        assert(failed.cancel(blockedTicket) && !blocked->pending() && blocked->value() == 23u);
        closed.invalidate();
        assert(!closed.submitWith(one, commit) && invocations == 4u);
        failed.invalidate([](Record& record) noexcept {
            record.owner->invalidate();
        });
        assert(!failure->active() && !failed.pending() && !failed.submitWith(unproven, commit)
            && invocations == 4u);
        assert(
            pool.resolve(one, true) && !pool.pending() && value->value() == 17u && value->active());
        assert(!pool.submitWith(one, commit) && invocations == 4u);
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        std::puts(
            "BINDLESS_RHI_COMPLETION_ADMISSION_TRANSACTION_OK exact-ticket commit-once reject retry stale closed unproven rollback allocation0");
    }
};
} // namespace arp::test
