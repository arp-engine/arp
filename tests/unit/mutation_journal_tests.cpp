#include "../fixtures/shared_identity_projection_fixture.h"
#include "../fixtures/versioned_mutation_array_fixture.h"
#include "../fixtures/versioned_mutation_value_fixture.h"
#include "core/memory/cpu_allocator.h"
#include "core/ownership/mutation_journal.h"

#include <array>
#include <cassert>

int main()
{
    arp::test::VersionedMutationValueFixture::verify();
    arp::test::VersionedMutationArrayFixture::verify();
    arp::test::SharedIdentityProjectionFixture::verify();
    using namespace arp::core;
    int first = 1, second = 2;
    MutationSnapshot<int> a(first), b(second);
    MutationJournal accepted;
    assert(a.prepare(accepted));
    first = 3;
    assert(a.prepare(accepted));
    first = 4;
    MutationJournal conflicting;
    assert(!a.prepare(conflicting) && conflicting.empty());
    assert(b.prepare(accepted));
    second = 5;
    accepted.rollback();
    assert(first == 1 && second == 2 && accepted.empty());
    assert(a.prepare(accepted));
    first = 7;
    accepted.commit();
    accepted.rollback();
    assert(first == 7 && !a.pending());
    {
        MutationJournal abandoned;
        assert(a.prepare(abandoned));
        first = 8;
    }
    assert(first == 7);
    {
        int shortLived = 9;
        MutationSnapshot<int> detached(shortLived);
        assert(detached.prepare(accepted));
        shortLived = 10;
    }
    assert(accepted.empty()); // No dangling restore after the value dies.
    pmr::Vector<int> values{1, 2, 3};
    MutationSnapshot<pmr::Vector<int>> saved(values);
    assert(saved.prepare(accepted));
    values[0] = 9;
    const auto beforeRollback = defaultCpuAllocator().stats();
    accepted.rollback();
    const auto afterRollback = defaultCpuAllocator().stats();
    assert(beforeRollback.allocationCount == afterRollback.allocationCount);
    assert(beforeRollback.deallocationCount == afterRollback.deallocationCount);
    assert(values[0] == 1 && values.size() == 3u);
    assert(saved.prepare(accepted));
    const auto beforeCommit = defaultCpuAllocator().stats();
    accepted.commit();
    const auto afterCommit = defaultCpuAllocator().stats();
    assert(beforeCommit.allocationCount == afterCommit.allocationCount);
    assert(beforeCommit.deallocationCount == afterCommit.deallocationCount);
    // Intrusive enlistment and reversal require no list storage/allocation.
    std::array<int, 2> order{};

    struct Callback {
        int id;
        std::array<int, 2>& order;
        unsigned& next;
        MutationRecord record;
    };

    unsigned next = 0u;
    Callback one{1, order, next, {}}, two{2, order, next, {}};
    const auto restore = [](void* context) noexcept {
        auto& callback = *static_cast<Callback*>(context);
        callback.order[callback.next++] = callback.id;
    };
    assert(accepted.enlist(one.record, &one, restore));
    assert(accepted.enlist(two.record, &two, restore));
    accepted.rollback();
    assert(order[0] == 2 && order[1] == 1);
    // Terminal discard detaches callbacks without restoring saved values.
    next = 0u;
    order = {};
    assert(accepted.enlist(one.record, &one, restore));
    assert(accepted.enlist(two.record, &two, restore));
    const auto beforeDiscard = defaultCpuAllocator().stats().allocationCount;
    accepted.discard();
    accepted.discard();
    accepted.rollback();
    assert(accepted.empty() && !one.record.pending() && !two.record.pending() && !next);
    assert(defaultCpuAllocator().stats().allocationCount == beforeDiscard);
    {
        VersionedMutationValue<unsigned> scalar(3u);
        VersionedMutationArray<unsigned> array(2u, 5u);
        MutationJournal terminal;
        const auto before = defaultCpuAllocator().stats().allocationCount;
        assert(scalar.tryWrite(7u, &terminal)
            == VersionedMutationValue<unsigned>::WriteResult::Written);
        assert(array.writeIndices(
                   1u,
                   [](std::size_t i) noexcept {
                       return i;
                   },
                   11u,
                   &terminal)
            == VersionedMutationArray<unsigned>::WriteResult::Written);
        assert(a.prepare(terminal)); // Ordinary snapshots cannot be invalidated.
        first = 19;
        assert(!terminal.canInvalidate() && !terminal.invalidate());
        assert(scalar.active() && array.active() && a.pending() && first == 19);
        terminal.rollback();
        assert(first == 7 && scalar.value() == 3u && *array.valueAt(0u) == 5u);
        assert(scalar.tryWrite(13u, &terminal)
            == VersionedMutationValue<unsigned>::WriteResult::Written);
        assert(array.writeIndices(
                   1u,
                   [](std::size_t i) noexcept {
                       return i;
                   },
                   17u,
                   &terminal)
            == VersionedMutationArray<unsigned>::WriteResult::Written);
        {
            VersionedMutationValue<unsigned> expired(23u);
            assert(expired.prepare(terminal));
        }
        assert(terminal.canInvalidate() && terminal.invalidate() && terminal.empty());
        terminal.rollback();
        assert(!scalar.active() && !array.active() && !scalar.observe() && !array.observe());
        assert(!scalar.pending() && !array.pending() && scalar.value() == 13u
            && *array.valueAt(0u) == 17u && !scalar.prepare(terminal));
        assert(defaultCpuAllocator().stats().allocationCount == before);
    }
    // Genuine cold factory accounting: success commits all counters; empty
    // results and allocation exceptions restore them while snapshots live.
    unsigned sets = 1u, tables = 2u, descriptors = 3u;
    const auto beforeFactory = defaultCpuAllocator().stats().allocationCount;
    assert(!withMutationRollback(
        [&] {
            sets = 4u;
            tables = 5u;
            descriptors = 6u;
            return false;
        },
        sets,
        tables,
        descriptors));
    assert(sets == 1u && tables == 2u && descriptors == 3u);
    try {
        withMutationRollback(
            [&]() -> bool {
                sets = 7u;
                tables = 8u;
                descriptors = 9u;
                throw std::bad_alloc();
            },
            sets,
            tables,
            descriptors);
        assert(false);
    } catch (const std::bad_alloc&) {
    }
    assert(sets == 1u && tables == 2u && descriptors == 3u);
    assert(withMutationRollback(
        [&] {
            sets = 4u;
            tables = 5u;
            descriptors = 6u;
            return true;
        },
        sets,
        tables,
        descriptors));
    assert(sets == 4u && tables == 5u && descriptors == 6u);
    assert(defaultCpuAllocator().stats().allocationCount == beforeFactory);
}
