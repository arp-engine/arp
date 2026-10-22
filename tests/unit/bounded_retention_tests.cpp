#include "../fixtures/bounded_terminal_observers_fixture.h"
#include "../fixtures/completion_admission_transaction_fixture.h"
#include "../fixtures/cpu_quota_factory_fixture.h"
#include "core/algorithm/ordered_interval_neighborhood.h"
#include "core/algorithm/ordered_span_cursor.h"
#include "core/algorithm/preflight_replay.h"
#include "core/container/bounded_sequence.h"
#include "core/hash/sha256.h"
#include "core/io/artifact_path.h"
#include "core/math/unsigned_range.h"
#include "core/ownership/bounded_retainer.h"
#include "core/ownership/cold_owned_object_slot.h"
#include "core/ownership/external_object_slot.h"
#include "core/ownership/quota_bound_shared_identity.h"
#include "core/ownership/shared_allocation_projection.h"
#include "core/ownership/shared_owner_projection.h"
#include "core/runtime/completion_drain_consumer.h"
#include "core/runtime/completion_evidence_batch.h"
#include "core/runtime/completion_retention_pool.h"
#include "core/runtime/completion_signal.h"
#include "core/runtime/external_callback_gate.h"
#include "core/text/ascii_token_cursor.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <new>
#include <optional>

#ifdef NDEBUG
#error Bounded retention tests require assertions
#endif

namespace {
void testMonotoneThresholdAndHistoricalQuotaMeasurement()
{
    using namespace arp::core;
    using Search = MonotoneThresholdSearch;
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    for (const auto threshold :
        std::array<std::uint64_t, 6>{0u, 1u, 2u, UINT64_MAX / 2u, UINT64_MAX - 1u, UINT64_MAX}) {
        std::uint32_t calls = 0u;
        const auto result = Search::findMinimum(0u, UINT64_MAX, [&](std::uint64_t value) {
            ++calls;
            return value >= threshold;
        });
        assert(result.status == Search::Status::Found && result.value == threshold
            && result.evaluations == calls && calls <= 65u);
    }
    const auto absent = Search::findMinimum(7u, 11u, [](auto) {
        return false;
    });
    assert(absent.status == Search::Status::NoSatisfyingValue && absent.evaluations == 1u
        && !absent.value);
    const auto invalid = Search::findMinimum(11u, 7u, [](auto) {
        assert(false);
        return true;
    });
    assert(invalid.status == Search::Status::InvalidRange && !invalid.evaluations);
    const auto singleton = Search::findMinimum(UINT64_MAX, UINT64_MAX, [](auto) {
        return true;
    });
    assert(singleton.status == Search::Status::Found && singleton.value == UINT64_MAX
        && singleton.evaluations == 1u);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    auto quota = makeShared<CpuMemoryQuota>(64u);
    {
        CpuMemoryQuotaLease full(quota, quota->limitBytes());
        assert(full.charged());
    }
    assert(quota->peakBytes() == quota->limitBytes() && !quota->liveBytes());
    const auto prepare = [&] {
        assert(!quota->liveBytes());
    };
    const auto factory = [&]() -> UniquePtr<unsigned> {
        try {
            CpuMemoryQuotaScope scope(quota);
            return makeUnique<unsigned>(7u);
        } catch (const std::bad_alloc&) {
            return {};
        }
    };
    arp::test::CpuQuotaFactoryFixture fixture(quota);
    const auto minimum = fixture.measureSingleUse(prepare, factory);
    {
        auto object = factory();
        assert(object && quota->liveBytes() == minimum);
    }
    fixture.verifySingleUse(minimum, prepare, factory);
    assert(!quota->liveBytes());
    std::puts(
        "BINDLESS_CORE_MONOTONE_THRESHOLD_OK inclusive-u64 bounded65 overflow-safe no-allocation historical-full-quota exact-one-less-exhausted");
}

void testAllocationQuotaOwnerOverlap()
{
    using namespace arp::core;
    auto original = makeShared<CpuMemoryQuota>(4096u), other = makeShared<CpuMemoryQuota>(4096u);
    UniquePtr<unsigned> object;
    {
        CpuMemoryQuotaScope scope(original);
        object = makeUnique<unsigned>(7u);
    }
    auto controlAlias = std::shared_ptr<CpuMemoryQuota>(original, other.get());
    auto pointerAlias = std::shared_ptr<CpuMemoryQuota>(other, original.get());
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    assert(cpuAllocationOverlapsQuotaOwner(object.get(), original));
    assert(cpuAllocationOverlapsQuotaOwner(object.get(), controlAlias));
    assert(cpuAllocationOverlapsQuotaOwner(object.get(), pointerAlias));
    assert(!cpuAllocationOverlapsQuotaOwner(object.get(), other));
    assert(!cpuAllocationOverlapsQuotaOwner(nullptr, original));
    assert(!cpuAllocationOverlapsQuotaOwner(object.get(), {}));
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    object.reset();
    assert(!original->liveBytes() && !other->liveBytes());
}

void testTerminalRetentionDrain()
{
    using namespace arp::core;
    using Gate = runtime::ExternalCallbackGate;
    using Pool = runtime::CompletionRetentionPool<std::shared_ptr<unsigned>>;
    // Every subset of the seven possible issued scope/queue userdata, both
    // before and after submit. Gates model drain, never GPU success evidence.
    for (unsigned issued = 0u; issued < 128u; ++issued)
        for (bool submitted : {false, true}) {
            auto quota = makeShared<CpuMemoryQuota>(4096u);
            CpuMemoryQuotaScope scope(quota);
            Pool pool(1u);
            auto original = makeShared<unsigned>(9u);
            std::weak_ptr<unsigned> borrowed = original;
            auto graph = makeUnique<std::shared_ptr<unsigned>>(original);
            const auto ticket = pool.reserve(graph);
            assert(ticket);
            original.reset();
            std::array<Gate, 7> gates;
            for (unsigned i = 0u; i < gates.size(); ++i)
                if (issued & (1u << i))
                    assert(gates[i].arm());
            if (submitted)
                assert(pool.submit(ticket));
            assert(!pool.releaseInvalidated(ticket));
            const auto allocations = defaultCpuAllocator().stats().allocationCount;
            CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            unsigned invalidations = 0u;
            pool.invalidateAndRetain([&](auto& owner) noexcept {
                assert(pool.closed() && pool.retainedInvalidated(ticket) && owner && *owner == 9u);
                ++invalidations;
                *owner = 0u;
            });
            assert(!borrowed.expired() && pool.pending() == 1u && invalidations == 1u);
            assert(!pool.accepting() && !pool.canCancel(ticket) && !pool.cancel(ticket));
            assert(!pool.submit(ticket) && !pool.resolve(ticket, true)
                && !pool.resolve(ticket, false));
            pool.invalidateAndRetain([&](auto&) noexcept {
                ++invalidations;
            });
            pool.invalidate([&](auto&) noexcept {
                ++invalidations;
            });
            assert(invalidations == 1u && pool.get(ticket) && !borrowed.expired());
            assert(!pool.releaseInvalidated({ticket.index, ticket.generation + 1u}));
            assert(!pool.releaseInvalidated({pool.capacity(), ticket.generation})
                && !pool.releaseInvalidated({}));
            // Reverse native delivery exercises a delivering last callback: its
            // userdata stays borrowed until finishDelivery, including after loss.
            for (unsigned i = gates.size(); i--;)
                if (issued & (1u << i)) {
                    assert(gates[i].beginDelivery());
                    assert(!borrowed.expired() && pool.retainedInvalidated(ticket));
                    assert(gates[i].finishDelivery());
                }
            for (const auto& gate : gates)
                assert(
                    gate.phase() != Gate::Phase::Armed && gate.phase() != Gate::Phase::Delivering);
            assert(pool.releaseInvalidated(ticket) && borrowed.expired() && !pool.pending());
            assert(!pool.releaseInvalidated(ticket) && !pool.get(ticket));
            assert(defaultCpuAllocator().stats().allocationCount == allocations);
        }
    std::puts(
        "CORE_TERMINAL_RETENTION_DRAIN_OK subsets128 before-after-submit exact-ticket invalidation-once no-cancel-no-resolve hot-allocation0");
}

void testColdOwnedObjectSlot()
{
    using namespace arp::core;

    struct Object {
        unsigned identity;
        unsigned& destroyed;

        Object(unsigned id, unsigned& count) : identity(id), destroyed(count) {}

        virtual ~Object() { ++destroyed; }
    };

    struct Derived final : Object {
        Derived(unsigned id, unsigned& destroyed) : Object{id, destroyed} {}
    };

    using Slot = ColdOwnedObjectSlot<Object>;
    static_assert(!std::is_copy_constructible_v<Slot> && !std::is_move_constructible_v<Slot>);
    auto quota = makeShared<CpuMemoryQuota>(4096u);
    unsigned destroyed = 0u, validations = 0u;
    UniquePtr<Object> source, foreign;
    {
        CpuMemoryQuotaScope scope(quota);
        source = makeUnique<Derived>(37u, destroyed);
        foreign = makeUnique<Derived>(43u, destroyed);
    }
    const auto* exact = source.get();
    const auto validator = [&](const Object& object) noexcept {
        ++validations;
        return object.identity == 37u;
    };
    {
        Slot slot;
        UniquePtr<Object> empty;
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        const auto previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        CpuMemoryQuotaLease exhausted(quota, quota->limitBytes() - quota->liveBytes());
        assert(exhausted.charged());
        assert(!slot.bindCold(empty) && !slot.adoptOriginal(source, validator) && !validations);
        assert(slot.bindCold(source) && slot.matchesCold(*source) && !slot.originalObject());
        assert(!slot.bindCold(source) && !slot.bindCold(foreign));
        assert(!slot.adoptOriginal(foreign, validator) && foreign && !validations);
        // Equal address is insufficient: the caller rechecks stable semantics.
        source->identity = 41u;
        assert(
            !slot.adoptOriginal(source, validator) && source.get() == exact && validations == 1u);
        source->identity = 37u;
        assert(slot.adoptOriginal(source, validator) && !source && slot.originalObject() == exact
            && validations == 2u);
        assert(!slot.matchesCold(*slot.originalObject()) && !slot.adoptOriginal(source, validator)
            && !slot.adoptOriginal(foreign, validator) && validations == 2u && !destroyed);
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
    }
    assert(destroyed == 1u);
    foreign.reset();
    assert(destroyed == 2u && !quota->liveBytes());
    {
        UniquePtr<Object> abandoned;
        {
            CpuMemoryQuotaScope scope(quota);
            abandoned = makeUnique<Derived>(47u, destroyed);
        }
        Slot receipt;
        assert(receipt.bindCold(abandoned));
        abandoned.reset();
        assert(!receipt.originalObject() && !receipt.adoptOriginal(abandoned, validator)
            && destroyed == 3u && !quota->liveBytes());
    }
    std::puts(
        "CORE_COLD_OWNED_OBJECT_SLOT_OK exact-original failure-preserving stable-validator caller-death derived-deleter exhausted-hot-allocation0");
}

void testExternalCallbackGate()
{
    using Gate = arp::core::runtime::ExternalCallbackGate;
    const auto a = arp::core::defaultCpuAllocator().stats().allocationCount;
    Gate gate;
    assert(gate.phase() == Gate::Phase::Unarmed && !gate.beginDelivery() && !gate.finishDelivery());
    assert(gate.arm() && !gate.arm() && !gate.finishDelivery());
    assert(gate.beginDelivery() && !gate.beginDelivery() && !gate.detach() && !gate.arm());
    assert(gate.finishDelivery() && gate.phase() == Gate::Phase::Delivered);
    assert(!gate.finishDelivery() && !gate.detach() && !gate.arm() && !gate.beginDelivery());
    Gate detached;
    assert(detached.arm() && detached.detach() && detached.phase() == Gate::Phase::Detached);
    assert(!detached.arm() && !detached.detach() && !detached.beginDelivery()
        && !detached.finishDelivery());
    Gate unarmed;
    assert(unarmed.detach() && !unarmed.arm());
    assert(a == arp::core::defaultCpuAllocator().stats().allocationCount);
}

void testUnsignedIntervalOverlap()
{
    using arp::core::overlapsUnsignedRanges;
    static_assert(overlapsUnsignedRanges<std::uint64_t>(UINT64_MAX, 1u, UINT64_MAX, 1u));
    static_assert(!overlapsUnsignedRanges<std::uint64_t>(UINT64_MAX, 2u, UINT64_MAX, 1u));
    static_assert(!overlapsUnsignedRanges<std::uint64_t>(0u, 0u, 0u, 1u));
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    for (std::uint64_t a = 0u; a < 8u; ++a)
        for (std::uint64_t n = 0u; n < 8u; ++n)
            for (std::uint64_t b = 0u; b < 8u; ++b)
                for (std::uint64_t m = 0u; m < 8u; ++m) {
                    bool oracle = false;
                    for (std::uint64_t x = a; x < a + n; ++x)
                        for (std::uint64_t y = b; y < b + m; ++y)
                            oracle |= x == y;
                    assert(overlapsUnsignedRanges(a, n, b, m) == oracle
                        && overlapsUnsignedRanges(b, m, a, n) == oracle);
                }
    assert(!overlapsUnsignedRanges<std::uint64_t>(UINT64_MAX - 2u, 2u, UINT64_MAX, 1u));
    assert(overlapsUnsignedRanges<std::uint64_t>(UINT64_MAX - 2u, 3u, UINT64_MAX, 1u));
    assert(!overlapsUnsignedRanges<std::uint32_t>(UINT32_MAX, 2u, 0u, UINT32_MAX));
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void testOrderedIntervalNeighborhood()
{
    using N = arp::core::OrderedIntervalNeighborhood<std::uint64_t>;
    using S = N::Status;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    std::array<N::Interval, 5u> intervals{{{0u, 1u}, {2u, 4u}, {6u, 8u}, {9u, 11u}, {2u, 4u}}};
    // Every input permutation selects complete nearest scopes, including duplicates.
    std::array<unsigned, 5u> permutation{0u, 1u, 2u, 3u, 4u};
    do {
        N n(5u, 12u, intervals.size());
        for (auto i : permutation)
            assert(n.observe(intervals[i]) == S::Complete);
        const auto& r = n.result();
        assert(r.inputs == 5u && r.before->matches({2u, 4u}) && r.after->matches({6u, 8u}));
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    N empty(0u, 1u, 0u);
    assert(empty.result().status == S::Complete && !empty.result().before && !empty.result().after);
    N edge(UINT64_MAX - 1u, UINT64_MAX, 2u);
    assert(edge.observe({0u, 0u}) == S::Complete);
    assert(edge.observe({UINT64_MAX - 2u, UINT64_MAX - 2u}) == S::Complete && !edge.result().after);
    const auto cleared = [](const N& n, S status) {
        assert(n.result().status == status && !n.result().inputs && !n.result().before
            && !n.result().after);
    };
    N domain(1u, 1u, 1u);
    cleared(domain, S::InvalidDomain);
    assert(domain.observe({0u, 0u}) == S::InvalidDomain);
    N capacity(5u, 12u, 1u);
    assert(capacity.observe({2u, 4u}) == S::Complete);
    assert(capacity.observe({6u, 8u}) == S::InputCapacity);
    cleared(capacity, S::InputCapacity);
    for (const auto invalid :
        std::array<N::Interval, 3u>{{{7u, 6u}, {0u, 12u}, {0u, UINT64_MAX}}}) {
        N n(5u, 12u, 2u);
        assert(n.observe({0u, 1u}) == S::Complete);
        assert(n.observe(invalid) == S::InvalidInterval);
        cleared(n, S::InvalidInterval);
    }
    for (const auto contained : std::array<N::Interval, 3u>{{{5u, 5u}, {0u, 5u}, {5u, 11u}}}) {
        N n(5u, 12u, 2u);
        assert(n.observe({0u, 1u}) == S::Complete);
        assert(n.observe(contained) == S::ContainsPoint);
        cleared(n, S::ContainsPoint);
    }
    for (const auto after : std::array{false, true}) {
        N n(5u, 12u, 2u);
        assert(n.observe(after ? N::Interval{6u, 8u} : N::Interval{2u, 4u}) == S::Complete);
        assert(
            n.observe(after ? N::Interval{6u, 9u} : N::Interval{1u, 4u}) == S::AmbiguousBoundary);
        cleared(n, S::AmbiguousBoundary);
    }
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void testOrderedSpanCursor()
{
    using namespace arp::core;
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    OrderedSpanCursor cursor(3u, 3u), foreign(3u, 3u);
    assert(!cursor.complete() && !cursor.commit({}));
    unsigned observations = 0u;
    const auto reject = cursor.preflight({0u, 2u}, [&](std::size_t i) {
        ++observations;
        return i != 1u;
    });
    assert(!reject && observations == 2u && !cursor.nextRecord() && !cursor.nextElement());
    observations = 0u;
    assert(!cursor.preflight({1u, 1u}, [&](std::size_t) {
        ++observations;
        return true;
    }));
    assert(!cursor.preflight({0u, 4u}, [&](std::size_t) {
        ++observations;
        return true;
    }));
    assert(!observations && !cursor.matchesNext({SIZE_MAX, SIZE_MAX}));
    const auto first = cursor.preflight({0u, 2u}, [](std::size_t) {
        return true;
    });
    assert(first && !foreign.commit(first) && !foreign.nextRecord());
    assert(cursor.commit(first) && !cursor.commit(first));
    assert(cursor.nextRecord() == 1u && cursor.nextElement() == 2u && !cursor.complete());
    const auto lastElement = cursor.preflight({2u, 1u}, [](std::size_t i) {
        return i == 2u;
    });
    assert(cursor.commit(lastElement) && !cursor.complete());
    const auto final = cursor.preflight({3u, 0u}, [](std::size_t) {
        assert(false);
        return false;
    });
    assert(final && cursor.commit(final) && cursor.complete());
    assert(!cursor.preflight({3u, 0u}, [](std::size_t) {
        return true;
    }) && !cursor.commit(final));
    cursor.abort();
    assert(!cursor.complete());
    OrderedSpanCursor unconsumed(1u, 2u);
    const auto shortSpan = unconsumed.preflight({0u, 1u}, [](std::size_t) {
        return true;
    });
    assert(unconsumed.commit(shortSpan) && !unconsumed.complete());
    OrderedSpanCursor aborted(1u, 0u);
    const auto beforeAbort = aborted.preflight({0u, 0u}, [](std::size_t) {
        return true;
    });
    aborted.abort();
    assert(!aborted.commit(beforeAbort) && !aborted.complete() && !aborted.matchesNext({0u, 0u}));
    OrderedSpanCursor empty(0u, 0u), tail(0u, 1u), huge(1u, SIZE_MAX);
    assert(empty.complete() && !tail.complete() && !empty.matchesNext({0u, 0u}));
    assert(huge.matchesNext({0u, SIZE_MAX}) && !huge.matchesNext({SIZE_MAX, 1u}));
    assert(!huge.preflight({0u, SIZE_MAX}, [](std::size_t i) {
        assert(!i);
        return false;
    }));
    assert(!huge.nextElement() && !huge.nextRecord());
    std::optional<OrderedSpanCursor> reused(std::in_place, 1u, 0u);
    const auto stale = reused->preflight({0u, 0u}, [](std::size_t) {
        return true;
    });
    const auto* address = &*reused;
    reused.reset();
    reused.emplace(1u, 0u);
    assert(&*reused == address && !reused->commit(stale));
    const auto current = reused->preflight({0u, 0u}, [](std::size_t) {
        return true;
    });
    assert(reused->commit(current) && reused->complete());
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
}

void testSharedAllocationProjection()
{
    using namespace arp::core;
    using Projection = SharedAllocationProjection;
    auto chargeA = makeShared<unsigned>(1u), chargeB = makeShared<unsigned>(2u);
    auto backingA = makeShared<unsigned>(3u), backingB = makeShared<unsigned>(4u);
    std::array<Projection::Record, 4> records{
        {{chargeA, backingA, 64u}, {}, {chargeA, backingA, 64u}, {chargeB, backingB, 80u}}};
    const auto before = defaultCpuAllocator().stats().allocationCount;
    const auto inspect = [&](std::size_t capacity, std::uint64_t bytes) {
        return Projection::inspect(records.size(), capacity, bytes, [&](std::size_t i) {
            return records[i];
        });
    };
    assert(inspect(4u, 144u).status == Projection::Status::Complete
        && inspect(4u, 144u).bytes == 144u);
    assert(inspect(3u, 144u).status == Projection::Status::CountCapacity);
    assert(inspect(4u, 143u).status == Projection::Status::ByteCapacity);
    records[2].bytes = 63u;
    assert(inspect(4u, 144u).status == Projection::Status::Alias);
    records[2].bytes = 64u;
    records[2].backing = backingB;
    assert(inspect(4u, 144u).status == Projection::Status::Alias);
    records[2].backing = std::shared_ptr<const void>(chargeB, backingA.get());
    assert(inspect(4u, 144u).status == Projection::Status::Alias);
    records[2].backing = backingA;
    records[2].allocation = std::shared_ptr<const void>(chargeB, chargeA.get());
    assert(inspect(4u, 144u).status == Projection::Status::Alias);
    records[2].allocation.reset();
    assert(inspect(4u, 144u).status == Projection::Status::MissingOwner);
    records[2] = {};
    records[0].bytes = UINT64_MAX;
    assert(inspect(4u, UINT64_MAX).status == Projection::Status::ByteCapacity);
    records[3] = {};
    assert(inspect(4u, UINT64_MAX).status == Projection::Status::Complete);
    assert(Projection::inspect(0u,
               0u,
               0u,
               [](std::size_t) {
                   return Projection::Record{};
               })
               .bytes
        == 0u);
    assert(defaultCpuAllocator().stats().allocationCount == before);
}

void testCompletionSignal()
{
    using arp::core::runtime::CompletionSignal;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    CompletionSignal signal;
    assert(!signal.isComplete() && signal.canReserve());
    const auto original = signal.reserve();
    assert(original && signal.matches(original) && !signal.reserve());
    assert(!signal.complete({}) && !signal.complete({original.generation + 1u}));
    signal.reset();
    assert(!signal.matches(original) && !signal.isComplete() && !signal.canReserve());
    assert(!signal.complete(original) && !signal.release({original.generation + 1u}));
    assert(signal.release(original) && !signal.release(original));
    const auto current = signal.reserve();
    assert(current && current.generation > original.generation && signal.complete(current)
        && signal.isComplete());
    assert(!signal.complete(original) && !signal.complete(current) && signal.isComplete());
    assert(signal.isComplete(current) && !signal.isComplete(original) && !signal.isComplete({}));
    assert(signal.release(current) && !signal.canReserve());
    signal.reset();
    const auto hostMutated = signal.reserve();
    signal.signalFromHost();
    assert(signal.isComplete() && !signal.matches(hostMutated) && !signal.complete(hostMutated));
    assert(signal.release(hostMutated) && !signal.canReserve());
    signal.reset();
    assert(signal.canReserve());
    CompletionSignal exhausted(0u);
    assert(!exhausted.reserve() && !exhausted.isComplete());
    exhausted.signalFromHost();
    assert(!exhausted.isComplete());
    CompletionSignal last(UINT64_MAX);
    const auto final = last.reserve();
    assert(final.generation == UINT64_MAX && last.complete(final) && last.isComplete());
    last.reset();
    assert(
        !last.isComplete() && !last.isComplete(final) && !last.reserve() && !last.complete(final));
    assert(last.release(final));
    last.signalFromHost();
    assert(!last.isComplete() && !last.canReserve());
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void testArtifactPathsAndDigests()
{
    using namespace arp::core;
    for (const auto path :
        {"", "/a", "a/", "a//b", "a/../b", "./a", "a/./b", "a\\b", "C:/a", "a b"})
        assert(!isRelativeArtifactPath(path, 4u));
    assert(isRelativeArtifactPath("a.entry.yaml", 1u));
    assert(isRelativeArtifactPath("Vertex/a.entry.yaml", 2u));
    assert(!isRelativeArtifactPath("Vertex/a.entry.yaml", 1u));
    assert(!isRelativeArtifactPath("a", 0u));
    auto digest = Sha256::digest("abc");
    constexpr StringView expected =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    auto quota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    {
        CpuMemoryQuotaScope scope(quota);
        CpuMemoryQuotaLease pressure(quota, 1u);
        assert(pressure.charged() && Sha256::matchesRawHexDigest(digest, expected));
        assert(!Sha256::matchesRawHexDigest(digest, expected.substr(1u)));
        assert(!Sha256::matchesHexDigest("abc", expected.substr(1u)));
        digest[0] ^= 1u;
        assert(!Sha256::matchesRawHexDigest(digest, expected));
        digest[0] ^= 1u;
        assert(!Sha256::matchesRawHexDigest(
            digest, "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
    }
}

void testAsciiTokenCursor()
{
    using Cursor = arp::core::AsciiTokenCursor;
    using Kind = Cursor::Kind;
    const auto allocations = arp::core::defaultCpuAllocator().stats().allocationCount;
    Cursor cursor(" //line\r\n /* a /* b */ c */ Name_9 0 4294967295 @ /=");
    std::string_view name;
    assert(cursor.identifier(name) && name == "Name_9");
    std::uint32_t value = 99u;
    assert(cursor.decimalUint32(value) && value == 0u);
    assert(cursor.decimalUint32(value) && value == UINT32_MAX);
    for (const auto punctuation : {"@", "/", "="})
        assert(cursor.consume(punctuation));
    assert(cursor.atEnd() && cursor.atEnd() && !cursor.diagnostic());
    for (const auto source : {"4294967296", "00", "-1", "word"}) {
        Cursor invalid(source);
        value = 99u;
        assert(!invalid.decimalUint32(value) && !value);
    }
    Cursor leading("00");
    assert(leading.decimalUint32(value, false) && !value);
    for (const auto source :
        {"1u", "12name", "'literal'", "\"literal\"", "`name`", "\\", "\xc3\xa9", "/* a /* b */"}) {
        Cursor invalid(source);
        assert(invalid.next().kind == Kind::Invalid && invalid.diagnostic());
        assert(invalid.next().kind == Kind::Invalid && !invalid.atEnd());
    }
    Cursor empty({});
    assert(empty.next().kind == Kind::End);
    Cursor comment("/* nothing */ //end");
    assert(comment.atEnd());
    Cursor wrongIdentifier("1");
    name = "old";
    assert(!wrongIdentifier.identifier(name) && name.empty());
    Cursor floating("-1.0f 3.0 0 1.0f", Cursor::Profile::FixedDecimalFloat);
    assert(floating.consume("-"));
    for (const auto spelling : {"1.0f", "3.0"}) {
        const auto token = floating.next();
        assert(token.kind == Kind::FixedDecimal && token.text == spelling);
    }
    assert(floating.decimalUint32(value) && !value);
    assert(floating.consume("1.0f") && floating.atEnd());
    for (const auto source : {"1.", "1.f", "1.0F", "1.0ff", "1.0e2", "0x1", "1u"}) {
        Cursor invalid(source, Cursor::Profile::FixedDecimalFloat);
        assert(invalid.next().kind == Kind::Invalid && invalid.diagnostic());
        assert(!invalid.atEnd());
    }
    Cursor integerOnly("1.0f");
    assert(integerOnly.decimalUint32(value) && value == 1u && integerOnly.consume("."));
    assert(integerOnly.next().kind == Kind::Invalid);
    Cursor opaque(" { let value=0x1u; /* } /* ) */ */ { let n=1e-4f; } } Tail");
    assert(opaque.consume("{") && opaque.skipBalanced('{', '}') && opaque.consume("Tail")
        && opaque.atEnd());
    Cursor terminal("=1.2f /* ; */ + 3u; Tail");
    assert(terminal.skipOpaqueUntil(';') && terminal.consume("Tail") && terminal.atEnd());
    for (const auto source : {"{", "{ /* comment", "{ \"text\" }", "{ \xc3\xa9 }"}) {
        Cursor invalid(source);
        assert(invalid.consume("{") && !invalid.skipBalanced('{', '}') && invalid.diagnostic());
    }
    std::uint64_t rounded = 99u;
    assert(arp::core::roundUpUnsigned(std::uint64_t{17}, std::uint64_t{16}, rounded)
        && rounded == 32u);
    assert(
        arp::core::roundUpUnsigned(UINT64_MAX, std::uint64_t{1}, rounded) && rounded == UINT64_MAX);
    assert(!arp::core::roundUpUnsigned(UINT64_MAX, std::uint64_t{16}, rounded) && !rounded);
    assert(!arp::core::roundUpUnsigned(std::uint64_t{1}, std::uint64_t{0}, rounded) && !rounded);
    assert(arp::core::defaultCpuAllocator().stats().allocationCount == allocations);
}

void testQuotaBoundSharedIdentity()
{
    using namespace arp::core;

    class Object final : public QuotaBoundSharedObjectIdentity {};

    auto quota = makeShared<CpuMemoryQuota>(4096u);
    const auto factory = [&] {
        try {
            CpuMemoryQuotaScope scope(quota);
            return Object::shareOriginal(makeUnique<Object>());
        } catch (const std::bad_alloc&) {
            return std::shared_ptr<Object>();
        }
    };
    arp::test::CpuQuotaFactoryFixture admission(quota);
    const auto peak = admission.measure(factory);
    admission.verify(peak, factory);
    assert(!quota->liveBytes());
    UniquePtr<Object> original;
    {
        CpuMemoryQuotaScope scope(quota);
        original = makeUnique<Object>();
    }
    assert(original->originalMetadataQuota() == quota);
    auto owner = Object::shareOriginal(std::move(original));
    assert(owner && owner->hasQuotaBoundSharedOwner(owner));
    const auto alias = std::shared_ptr<Object>(quota, owner.get());
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    {
        CpuMemoryQuotaScope scope(quota);
        CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        assert(pressure.charged() && owner->hasQuotaBoundSharedOwner(owner)
            && !owner->hasQuotaBoundSharedOwner(alias));
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
    }
    owner.reset();
    assert(!quota->liveBytes());
    {
        CpuMemoryQuotaScope scope(quota);
        original = makeUnique<Object>();
    }
    auto unbudgetedShare = shareUnique(std::move(original));
    assert(!unbudgetedShare->hasQuotaBoundSharedOwner(unbudgetedShare));
    unbudgetedShare.reset();
    original = makeUnique<Object>();
    assert(!Object::shareOriginal(std::move(original)) && !quota->liveBytes());
    {
        CpuMemoryQuotaScope scope(quota);
        original = makeUnique<Object>();
    }
    bool rejected = false;
    {
        CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        try {
            Object::shareOriginal(std::move(original));
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
    }
    assert(rejected && !original && !quota->liveBytes());
}

void testCanonicalSharedObjectIdentity()
{
    using namespace arp::core;

    struct Prefix {
        virtual ~Prefix() = default;
        unsigned value = 7u;
    };

    struct Object final : Prefix, SharedObjectIdentity {
        explicit Object(unsigned& destroyed) : destroyed(destroyed) {}

        ~Object() { ++destroyed; }

        unsigned& destroyed;
    };

    unsigned destroyed = 0u;
    auto quota = makeShared<CpuMemoryQuota>(4096u);
    {
        CpuMemoryQuotaScope scope(quota);
        auto unique = makeUnique<Object>(destroyed);
        assert(!unique->sharedOwnerMatches(std::shared_ptr<Object>{}));
        auto first = shareUnique(std::move(unique));
        auto second = makeShared<Object>(destroyed);
        assert(first->sharedOwnerMatches(first) && second->sharedOwnerMatches(second));
        std::shared_ptr<SharedObjectIdentity> adjusted = first;
        assert(adjusted->sharedOwnerMatches(adjusted));
        auto wrong = std::shared_ptr<Object>(second, first.get());
        assert(!first->sharedOwnerMatches(wrong));
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
        for (unsigned i = 0u; i < 1000u; ++i)
            assert(first->sharedOwnerMatches(first) && !first->sharedOwnerMatches(wrong));
        assert(allocations == defaultCpuAllocator().stats().allocationCount);
    }
    assert(destroyed == 2u && !quota->liveBytes());
}

class OwnerReleaseProbe final {
public:
    OwnerReleaseProbe(unsigned ordinal, std::array<unsigned, 3>& released, unsigned& count)
        : ordinal_(ordinal), released_(released), count_(count)
    {}

    ~OwnerReleaseProbe() { released_[count_++] = ordinal_; }

private:
    unsigned ordinal_;
    std::array<unsigned, 3>& released_;
    unsigned& count_;
};

void testReverseOwnerRelease()
{
    using namespace arp::core;
    for (unsigned mode = 0; mode < 3; ++mode) {
        std::array<unsigned, 3> released{};
        unsigned count = 0;
        {
            BoundedRetainer<void> owners(3u);
            for (unsigned i = 0; i < 3; ++i)
                assert(owners.retain(makeShared<OwnerReleaseProbe>(i, released, count)));
            const auto allocations = defaultCpuAllocator().stats().allocationCount;
            if (mode == 0)
                owners.clear();
            if (mode == 1)
                owners = BoundedRetainer<void>{};
            assert(defaultCpuAllocator().stats().allocationCount == allocations);
        }
        assert(count == 3 && (released == std::array<unsigned, 3>{2u, 1u, 0u}));
    }
}

void testSharedOwnerProjection()
{
    using namespace arp::core;
    auto first = makeShared<unsigned>(1u), second = makeShared<unsigned>(2u);
    const std::shared_ptr<const void> aliased(first, second.get());
    assert(SharedOwnerProjection::sameControlBlock(first, aliased));
    assert(!SharedOwnerProjection::sameOwner(first, aliased));
    assert(!SharedOwnerProjection::sameControlBlock(first, second));
    // Typed/erased paths must agree for pointer and control-block identity,
    // including unrelated pointees, null aliases and non-owning aliases.
    auto unrelated = makeShared<std::array<unsigned, 2u>>();
    const std::shared_ptr<const unsigned> constFirst = first;
    const std::shared_ptr<unsigned> foreignPointer(second, first.get());
    const std::shared_ptr<unsigned> nonOwning(std::shared_ptr<unsigned>{}, first.get());
    const std::shared_ptr<unsigned> nullAlias(first, nullptr);
    const std::shared_ptr<unsigned> empty;
    const auto compare = [&](const auto& a, const auto& b) {
        const auto aCount = a.use_count(), bCount = b.use_count();
        const std::shared_ptr<const void> erasedA = a, erasedB = b;
        assert(SharedOwnerProjection::sameOwner(a, b)
            == SharedOwnerProjection::sameOwner(erasedA, erasedB));
        assert(SharedOwnerProjection::sameControlBlock(a, b)
            == SharedOwnerProjection::sameControlBlock(erasedA, erasedB));
        assert(SharedOwnerProjection::overlapsOwner(a, b)
            == SharedOwnerProjection::overlapsOwner(erasedA, erasedB));
        // The only owning increments are the explicit erased test handles.
        assert(a.use_count()
            == aCount + (aCount ? (SharedOwnerProjection::sameControlBlock(a, b) ? 2 : 1) : 0));
        assert(b.use_count()
            == bCount + (bCount ? (SharedOwnerProjection::sameControlBlock(a, b) ? 2 : 1) : 0));
    };
    {
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        compare(first, constFirst);
        compare(first, second);
        compare(first, unrelated);
        compare(first, aliased);
        compare(first, foreignPointer);
        compare(first, nonOwning);
        compare(first, nullAlias);
        compare(nullAlias, empty);
        compare(empty, empty);
        std::pmr::set_default_resource(previous);
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
    }
    std::array<std::shared_ptr<const void>, 3> source{first, second, first};
    const auto project = [&](auto&& visitor) {
        for (const auto& owner : source)
            visitor(owner);
    };
    auto quota = makeShared<CpuMemoryQuota>(4096u);
    {
        const auto at = [&](std::size_t i) noexcept -> const auto& {
            return source[i];
        };
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        CpuMemoryQuotaLease exhausted(quota, quota->limitBytes());
        assert(exhausted.charged());
        assert(SharedOwnerProjection::hasIndependentOwners(0u, at));
        assert(SharedOwnerProjection::hasIndependentOwners(2u, at));
        assert(!SharedOwnerProjection::hasIndependentOwners(3u, at));
        source[1] = aliased;
        assert(!SharedOwnerProjection::hasIndependentOwners(2u, at));
        source[1] = std::shared_ptr<const void>(second, first.get());
        assert(!SharedOwnerProjection::hasIndependentOwners(2u, at));
        source[1] = std::shared_ptr<const void>(std::shared_ptr<const void>{}, second.get());
        assert(!SharedOwnerProjection::hasIndependentOwners(2u, at));
        source[1].reset();
        assert(!SharedOwnerProjection::hasIndependentOwners(2u, at));
        source[1] = second;
        assert(SharedOwnerProjection::hasIndependentOwners(2u, at));
        assert(defaultCpuAllocator().stats().allocationCount == allocations);
    }
    {
        SharedOwnerProjection frozen;
        assert(!frozen.matches(project));
        {
            CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            assert(frozen.capture(project));
            std::pmr::set_default_resource(previous);
        }
        assert(quota->liveBytes() && frozen.size() == source.size() && frozen.at(0u) == first
            && !frozen.at(source.size()));
        assert(!frozen.capture(project));
        const auto allocations = defaultCpuAllocator().stats().allocationCount;
        {
            CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(frozen.matches(project));
            source[1] = std::shared_ptr<const void>(first, second.get());
            assert(!frozen.matches(project));
            source[1] = second;
            source[2].reset();
            assert(!frozen.matches(project));
            source[2] = first;
            assert(frozen.matches(project));
            assert(defaultCpuAllocator().stats().allocationCount == allocations);
        }
    }
    assert(!quota->liveBytes());
    auto exhausted = makeShared<CpuMemoryQuota>(1u);
    SharedOwnerProjection retry;
    try {
        CpuMemoryQuotaScope scope(exhausted);
        retry.capture(project);
        assert(false);
    } catch (const std::bad_alloc&) {
    }
    assert(!retry.size() && !exhausted->liveBytes());
    assert(retry.capture(project) && retry.matches(project));
}

void testPreflightReplay()
{
    using namespace arp::core;
    std::array<int, 3u> input{3, 7, 11}, output{};
    unsigned factories = 0u, admissions = 0u, consumed = 0u;
    auto mapper = [&] {
        ++factories;
        return [&, cursor = 0u](std::size_t index, int& value) mutable {
            assert(cursor++ == index); // Each pass receives a fresh cursor.
            value = input[index];
            return value >= 0;
        };
    };
    auto admit = [&] {
        ++admissions;
        return true;
    };
    auto consume = [&](int value) {
        output[consumed++] = value;
    };
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    input[2u] = -1;
    assert(PreflightReplay<int>::run(input.size(), mapper, admit, consume)
        == PreflightReplayStatus::PreflightRejected);
    assert(factories == 1u && !admissions && !consumed && output[0u] == 0);
    input[2u] = 11;
    assert(PreflightReplay<int>::run(input.size(), mapper, admit, consume, true)
        == PreflightReplayStatus::ValidatedOnly);
    assert(!admissions && !consumed);
    assert(PreflightReplay<int>::run(
               input.size(),
               mapper,
               [] {
                   return false;
               },
               consume)
        == PreflightReplayStatus::AdmissionRejected);
    assert(!admissions && !consumed);
    assert(PreflightReplay<int>::run(input.size(), mapper, admit, consume)
        == PreflightReplayStatus::Completed);
    assert(admissions == 1u && consumed == input.size() && output == input);
    consumed = 0u;
    output = {};
    // Deliberately violate immutable-input preconditions to test a surfaced,
    // terminal replay failure; the consumer's first output cannot be undone.
    assert(PreflightReplay<int>::run(
               input.size(),
               mapper,
               [&] {
                   input[1u] = -1;
                   return true;
               },
               consume)
        == PreflightReplayStatus::ReplayRejected);
    assert(consumed == 1u && output[0u] == 3 && output[1u] == 0);
    const auto priorFactories = factories;
    assert(
        PreflightReplay<int>::run(0u, mapper, admit, consume) == PreflightReplayStatus::Completed);
    assert(factories == priorFactories && admissions == 1u && consumed == 1u);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
}

void testMappedOwnerBatches()
{
    using namespace arp::core;
    auto first = makeShared<unsigned>(3u), second = makeShared<unsigned>(7u),
         third = makeShared<unsigned>(11u);

    struct Element {
        std::shared_ptr<const unsigned> owner;
        unsigned binding;
    };

    std::array<Element, 4u> input{{{first, 0u}, {second, 2u}, {first, 4u}, {third, 5u}}};
    auto ownerAt = [&](std::size_t index) noexcept -> const std::shared_ptr<const unsigned>& {
        return input[index].owner;
    };
    BoundedRetainer<unsigned> storage(2u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    assert(storage.canRetainMapped(0u, ownerAt) && storage.retainMapped(0u, ownerAt));
    assert(!storage.canRetainMapped(input.size(), ownerAt)
        && !storage.retainMapped(input.size(), ownerAt));
    assert(storage.values().empty());
    input.back().owner.reset();
    assert(!storage.canRetainMapped(input.size(), ownerAt)
        && !storage.retainMapped(input.size(), ownerAt));
    assert(storage.values().empty()); // Late missing owner cannot admit the earlier pair.
    input.back().owner = second;
    assert(storage.canRetainMapped(input.size(), ownerAt) && storage.values().empty());
    assert(storage.retainMapped(input.size(), ownerAt) && storage.values().size() == 2u);
    // A projection returned by value has the same identity and admission rules.
    auto copiedOwnerAt = [&](std::size_t index) noexcept {
        return input[index].owner;
    };
    assert(storage.retainMapped(input.size(), copiedOwnerAt) && storage.values().size() == 2u);
    input.back().owner = third;
    assert(!storage.canRetainMapped(input.size(), copiedOwnerAt)
        && !storage.retainMapped(input.size(), copiedOwnerAt));
    assert(storage.values().size() == 2u
        && defaultCpuAllocator().stats().allocationCount == allocations);
    std::weak_ptr<unsigned> observed = second;
    input = {};
    second.reset();
    assert(!observed.expired());
    BoundedRetainer<unsigned> completion(std::move(storage));
    assert(!storage.retainMapped(1u, [&](std::size_t) noexcept -> std::shared_ptr<const unsigned> {
        return first;
    }));
    assert(!observed.expired());
    completion.clear();
    assert(observed.expired());
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
}
} // namespace

void testCompletionEvidenceBatch()
{
    using arp::core::runtime::CompletionEvidenceBatch;
    std::array<unsigned, 4u> order{0u, 1u, 2u, 3u};
    do {
        for (unsigned failed = 0u; failed <= order.size(); ++failed) {
            CompletionEvidenceBatch batch(order.size());
            for (unsigned i = 0u; i < order.size(); ++i) {
                assert(!batch.allReceived() && !batch.proven());
                assert(batch.observe(order[i], order[i] != failed));
            }
            assert(batch.allReceived() && batch.proven() == (failed == order.size()));
            assert(!batch.observe(order.front(), true) && !batch.proven());
        }
    } while (std::next_permutation(order.begin(), order.end()));
    CompletionEvidenceBatch empty(0u), huge(UINT32_MAX),
        full(CompletionEvidenceBatch::maximumParticipants);
    assert(!empty.observe(0u, true) && !empty.proven() && !huge.allReceived());
    for (unsigned i = 0u; i < CompletionEvidenceBatch::maximumParticipants; ++i)
        assert(full.observe(i, true));
    assert(full.proven() && !full.observe(CompletionEvidenceBatch::maximumParticipants, true)
        && !full.proven());
}

int main()
{
    arp::test::BoundedTerminalObserversFixture::verify();
    testMonotoneThresholdAndHistoricalQuotaMeasurement();
    testAllocationQuotaOwnerOverlap();
    testTerminalRetentionDrain();
    {
        using Drain = arp::core::runtime::CompletionDrainConsumer;
        unsigned calls = 0u, destroyed = 0u;

        class Observer final : public Drain {
        public:
            Observer(unsigned& calls, unsigned& destroyed, std::shared_ptr<Drain>& original)
                : calls_(calls), destroyed_(destroyed), original_(original)
            {}

            ~Observer() override { ++destroyed_; }

            void consumeOriginalCallbackDrain() noexcept override
            {
                assert(!original_);
                ++calls_;
                assert(!consumeOnce(true, original_));
            }

        private:
            unsigned& calls_;
            unsigned& destroyed_;
            std::shared_ptr<Drain>& original_;
        };

        std::shared_ptr<Drain> observer;
        observer = arp::core::makeShared<Observer>(calls, destroyed, observer);
        const auto a = arp::core::defaultCpuAllocator().stats().allocationCount;
        assert(!Drain::consumeOnce(false, observer) && observer && !calls && !destroyed);
        assert(Drain::consumeOnce(true, observer) && !observer && calls == 1u && destroyed == 1u);
        assert(!Drain::consumeOnce(true, observer)
            && arp::core::defaultCpuAllocator().stats().allocationCount == a);
    }

    arp::test::CompletionAdmissionTransactionFixture::verify();
    testColdOwnedObjectSlot();
    testOrderedIntervalNeighborhood();
    testOrderedSpanCursor();
    testSharedAllocationProjection();
    testCompletionSignal();
    testExternalCallbackGate();
    testCompletionEvidenceBatch();
    testArtifactPathsAndDigests();
    testAsciiTokenCursor();
    testQuotaBoundSharedIdentity();
    testSharedOwnerProjection();
    testCanonicalSharedObjectIdentity();
    testReverseOwnerRelease();
    {
        using namespace arp::core;
        unsigned releases = 0;
        auto quota = makeShared<CpuMemoryQuota>(65536u);
        std::shared_ptr<ExternalObjectSlot> slot;
        {
            CpuMemoryQuotaScope scope(quota);
            slot = makeShared<ExternalObjectSlot>([](void* value) noexcept {
                ++*static_cast<unsigned*>(value);
            });
        }
        assert(inspectCpuAllocationContaining(slot.get()).quota == quota.get());
        assert(!slot->adopt(nullptr) && slot->adopt(&releases) && !slot->adopt(&releases));
        std::shared_ptr<void> alias(slot, slot->get());
        slot->reset();
        slot->reset();
        assert(releases == 1u);
        assert(slot->adopt(&releases));
        slot.reset();
        assert(releases == 1u && quota->liveBytes());
        alias.reset();
        assert(releases == 2u && !quota->liveBytes());
    }
    testPreflightReplay();
    testUnsignedIntervalOverlap();
    testMappedOwnerBatches();
    using namespace arp::core;
    auto first = makeShared<unsigned>(1u), second = makeShared<unsigned>(2u),
         third = makeShared<unsigned>(3u);
    std::array<std::shared_ptr<const unsigned>, 4u> batch{first, second, first, third};
    BoundedRetainer<unsigned> limited(2u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    assert(!limited.canRetainAll(batch.data(), batch.size())
        && !limited.retainAll(batch.data(), batch.size()));
    assert(limited.values().empty()); // Rejection cannot retain the first two.
    assert(limited.retainAll(batch.data(), 3u) && limited.values().size() == 2u);
    assert(limited.retainAll(batch.data(), 3u));
    assert(!limited.retainAll(nullptr, 1u) && limited.retainAll(nullptr, 0u));
    batch[1u].reset();
    assert(!limited.retainAll(batch.data(), 3u) && limited.values().size() == 2u);
    BoundedRetainer<unsigned> completion(std::move(limited));
    assert(!limited.retain(first) && completion.values().size() == 2u);
    completion.clear();
    assert(defaultCpuAllocator().stats().allocationCount == allocations);

    struct Record {
        std::shared_ptr<const unsigned> owner;
        unsigned index;
    };

    BoundedSequence<Record> records(2u), moved;
    const auto beforeRecords = defaultCpuAllocator().stats().allocationCount;
    assert(records.canAppend(2u) && !records.canAppend(3u));
    assert(records.append({first, 0u}) && records.append({second, 1u}));
    assert(!records.append({third, 2u}) && records.values().size() == 2u);
    unsigned predicates = 0u;
    assert(records.findIf([&](const auto& r) {
        ++predicates;
        return r.index == 1u;
    }) == &records.values()[1]
        && predicates == 2u);
    assert(!records.findIf([](const auto& r) {
        return r.index == 9u;
    }));
    predicates = 0u;
    assert(records.findLastIf([&](const auto&) {
        ++predicates;
        return true;
    }) == &records.values()[1u]
        && predicates == 1u);
    assert(records.findLastIf([](const auto& r) {
        return r.index == 0u;
    }) == &records.values()[0u]);
    assert(!records.findLastIf([](const auto& r) {
        return r.index == 9u;
    }));
    records.visitMutable([](Record& record) noexcept {
        record.index += 3u;
    });
    assert(records.values()[0u].index == 3u && records.values()[1u].index == 4u
        && !records.canAppend() && records.values().size() == 2u);
    moved = std::move(records);
    assert(!records.append({third, 2u}) && moved.values()[1u].index == 4u);
    assert(!records.findIf([](const auto&) {
        return true;
    }));
    assert(!records.findLastIf([](const auto&) {
        return true;
    }));
    assert(moved.findIf([](const auto& r) {
        return r.index == 4u;
    }) == &moved.values()[1u]);
    assert(moved.findLastIf([](const auto& r) {
        return r.index == 4u;
    }) == &moved.values()[1u]);
    std::weak_ptr<unsigned> observed = second;
    second.reset();
    assert(!observed.expired());
    moved.clear();
    moved.clear();
    assert(!moved.findIf([](const auto&) {
        return true;
    }));
    assert(!moved.findLastIf([](const auto&) {
        return true;
    }));
    assert(observed.expired() && moved.canAppend(2u));
    assert(defaultCpuAllocator().stats().allocationCount == beforeRecords);

    // Stable move-only owners are admitted without moving a rejected candidate.
    BoundedSequence<UniquePtr<unsigned>> owned(1u);
    auto candidate = makeUnique<unsigned>(17u);
    auto rejectedOwner = makeUnique<unsigned>(19u);
    const auto* identity = candidate.get();
    const auto beforeOwned = defaultCpuAllocator().stats().allocationCount;
    assert(owned.append(std::move(candidate)) && !candidate);
    assert(!owned.append(std::move(rejectedOwner)) && rejectedOwner && *rejectedOwner == 19u);
    BoundedSequence<UniquePtr<unsigned>> transferred(std::move(owned));
    assert(transferred.values()[0].get() == identity && *identity == 17u);
    assert(!owned.append(std::move(rejectedOwner)) && rejectedOwner);
    transferred.clear();
    assert(defaultCpuAllocator().stats().allocationCount == beforeOwned);

    // Real core allocation headers retain an explicit byte quota beyond the
    // preparation scope; no sizeof-based estimate stands in for that charge.
    auto quota = makeShared<CpuMemoryQuota>(4096u);
    BoundedSequence<Record> quotaRecords;
    {
        CpuMemoryQuotaScope scope(quota);
        quotaRecords = BoundedSequence<Record>(2u);
        assert(quota->liveBytes() != 0u && quota->liveBytes() <= quota->limitBytes());
    }
    const auto charged = quota->liveBytes();
    assert(quotaRecords.append({first, 0u}) && quota->liveBytes() == charged);
    quotaRecords.clear();
    assert(quota->liveBytes() == charged); // Reserved storage remains owned.
    quotaRecords = BoundedSequence<Record>();
    assert(quota->liveBytes() == 0u);
    auto exhausted = makeShared<CpuMemoryQuota>(1u);
    bool rejected = false;
    try {
        CpuMemoryQuotaScope scope(exhausted);
        BoundedSequence<Record> impossible(2u);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected && exhausted->liveBytes() == 0u);
}
