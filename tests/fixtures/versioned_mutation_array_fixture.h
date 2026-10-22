#pragma once
#include "core/ownership/versioned_mutation_array.h"
#include "cpu_quota_factory_fixture.h"

#include <cstdio>

namespace arp::test {
class VersionedMutationArrayFixture final {
public:
    static void verify()
    {
        using Array = core::VersionedMutationArray<unsigned>;
        using Result = Array::WriteResult;
        static_assert(!std::is_default_constructible_v<Array::PendingObservation>);
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const auto factory = [&]() {
            try {
                core::CpuMemoryQuotaScope scope(quota);
                return core::makeUnique<Array>(4u, 7u);
            } catch (const std::bad_alloc&) {
                return core::UniquePtr<Array>{};
            }
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        auto array = factory();
        assert(array && array->size() == 4u && !array->valueAt(4u));
        const auto select = [](std::size_t i) noexcept {
            return i + 1u;
        };
        const auto invalid = [](std::size_t) noexcept {
            return SIZE_MAX;
        };
        const auto first = array->observe();
        assert(first && array->matches(*first));
        core::MutationJournal journal, foreign;
        {
            Array exact(2u, 23u, UINT64_MAX - 3u), oneLess(2u, 29u, UINT64_MAX - 2u);
            const auto before = oneLess.observe();
            assert(before && exact.canWriteBatch(journal, 2u));
            assert(
                !oneLess.canWriteBatch(journal, 2u) && oneLess.matches(*before) && journal.empty());
            const auto index = [](std::size_t) noexcept {
                return 0u;
            };
            assert(exact.writeIndices(1u, index, 31u, &journal) == Result::Written);
            assert(exact.writeIndices(1u, index, 37u, &journal) == Result::Written);
            assert(exact.canWriteBatch(journal, 0u) && !exact.canWriteBatch(foreign, 0u));
            journal.rollback();
            assert(exact.active() && exact.revision() == UINT64_MAX && *exact.valueAt(0u) == 23u);
        }
        assert(array->writeIndices(0u, select, 9u, &journal) == Result::InvalidRange
            && journal.empty());
        assert(array->writeIndices(1u, invalid, 9u, &journal) == Result::InvalidRange
            && journal.empty());
        assert(array->prepare(journal) && array->pending() && !array->observe()
            && array->revision() == first->revision);
        assert(!array->prepare(foreign) && foreign.empty());
        const auto preparedPending = array->observePending(journal);
        assert(preparedPending);
        journal.commit();
        assert(array->matches(*first));
        assert(array->prepare(journal) && !array->matchesPending(*preparedPending, journal));
        journal.commit();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            if (array->writeIndices(2u, select, 11u, &journal) != Result::Written)
                return "TEST-ARRAY-WRITE";
            if (*array->valueAt(0u) != 7u || *array->valueAt(1u) != 11u
                || *array->valueAt(2u) != 11u || *array->valueAt(3u) != 7u)
                return "TEST-ARRAY-COVERAGE";
            if (array->observe() || array->matches(*first))
                return "TEST-ARRAY-PENDING";
            const auto pending = array->observePending(journal);
            Array peer(0u, 7u);
            if (!pending || !array->matchesPending(*pending, journal)
                || array->observePending(foreign) || array->matchesPending(*pending, foreign)
                || peer.matchesPending(*pending, journal))
                return "TEST-ARRAY-PENDING-IDENTITY";
            const auto revision = array->revision();
            if (array->writeIndices(1u, select, 13u, &foreign) != Result::Conflict
                || !foreign.empty() || array->writeIndices(1u, select, 13u) != Result::Conflict
                || array->revision() != revision)
                return "TEST-ARRAY-CONFLICT";
            journal.rollback();
            if (array->observePending(journal) || array->matchesPending(*pending, journal))
                return "TEST-ARRAY-PENDING-ROLLBACK";
            if (*array->valueAt(1u) != 7u || *array->valueAt(2u) != 7u || array->matches(*first))
                return "TEST-ARRAY-ABA";
            auto restored = array->observe();
            if (!restored)
                return "TEST-ARRAY-RESTORED";
            if (array->writeIndices(1u, select, 17u, &journal) != Result::Written)
                return "TEST-ARRAY-COMMIT-WRITE";
            journal.commit();
            if (*array->valueAt(1u) != 17u || array->matches(*restored))
                return "TEST-ARRAY-COMMIT";
            if (!array->prepare(journal))
                return "TEST-ARRAY-PREPARE";
            array->invalidate();
            journal.rollback();
            return !array->active() && !array->observe()
                    && array->writeIndices(1u, select, 19u) == Result::Exhausted
                ? nullptr
                : "TEST-ARRAY-INVALIDATION";
        }));
        array.reset();
        assert(!quota->liveBytes());
        {
            Array exhausted(2u, 23u, UINT64_MAX);
            assert(exhausted.writeIndices(
                       1u,
                       [](std::size_t) noexcept {
                           return 0u;
                       },
                       29u,
                       &journal)
                == Result::Exhausted);
            assert(journal.empty() && *exhausted.valueAt(0u) == 23u && !exhausted.observe());
            Array rollback(2u, 31u, UINT64_MAX - 1u);
            assert(rollback.writeIndices(
                       1u,
                       [](std::size_t) noexcept {
                           return 0u;
                       },
                       37u,
                       &journal)
                == Result::Written);
            journal.rollback();
            assert(*rollback.valueAt(0u) == 31u && !rollback.active());
            Array destroyed(2u, 41u);
            assert(destroyed.prepare(journal));
        }
        assert(journal.empty());
        {
            Array lastEpoch(2u, 43u, 1u, UINT64_MAX), none(2u, 47u, 1u, 0u);
            const auto index = [](std::size_t) noexcept {
                return 0u;
            };
            const auto initial = none.observe();
            assert(initial && !none.prepare(journal));
            assert(none.writeIndices(1u, index, 53u, &journal) == Result::EnlistmentExhausted
                && none.matches(*initial) && journal.empty());
            assert(lastEpoch.prepare(journal));
            const auto pending = lastEpoch.observePending(journal);
            assert(pending);
            assert(lastEpoch.prepare(journal) && lastEpoch.matchesPending(*pending, journal));
            journal.commit();
            const auto committed = lastEpoch.observe();
            assert(committed && !lastEpoch.prepare(journal));
            assert(lastEpoch.writeIndices(1u, index, 59u, &journal) == Result::EnlistmentExhausted
                && lastEpoch.matches(*committed) && journal.empty());
            assert(lastEpoch.writeIndices(1u, index, 59u) == Result::Written
                && !lastEpoch.matchesPending(*pending, journal));
        }
        std::puts(
            "BINDLESS_CORE_VERSIONED_ARRAY_OK fixed-storage exact-indices preflight-conflict rollback-ABA commit terminal-exhaustion detach cold-exact-less hot-allocation0");
    }
};
} // namespace arp::test
