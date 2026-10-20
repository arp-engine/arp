#include "core/partitioned_budget_ledger.h"
#include "tests/fixtures/cpu_quota_factory_fixture.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <thread>

namespace {
using namespace arp::core;

struct Counters {
    std::uint64_t a{}, b{}, c{}, d{}, e{}, f{}, g{}, h{}, i{};
};

using Ledger = PartitionedBudgetLedger<Counters, 9u, 4u>;
constexpr Ledger::Fields fields{&Counters::a,
    &Counters::b,
    &Counters::c,
    &Counters::d,
    &Counters::e,
    &Counters::f,
    &Counters::g,
    &Counters::h,
    &Counters::i};

struct Value {
    explicit Value(unsigned& destroyed) noexcept : destroyed(destroyed) {}

    ~Value() { ++destroyed; }

    unsigned& destroyed;
};
} // namespace

int main()
{
    const char* diagnostic = nullptr;
    constexpr std::uint64_t bytes = 65536u;
    auto quota = makeShared<CpuMemoryQuota>(bytes);
    Counters capacity;
    for (auto field : fields)
        capacity.*field = 2u;
    auto ledger = Ledger::prepareCold(capacity, fields, 3u, quota, diagnostic);
    assert(ledger && !diagnostic && ledger->retainedStorageBytes());
    auto foreign = Ledger::prepareCold(capacity, fields, 3u, quota, diagnostic);
    assert(foreign && !diagnostic);
    unsigned cases = 0u;
    for (auto field : fields) {
        for (std::size_t partition = 0u; partition < 4u; ++partition) {
            Counters charge;
            charge.*field = 2u;
            auto lease = ledger->reserve(charge, partition, diagnostic);
            assert(lease && !diagnostic);
            const auto before = ledger->snapshot();
            assert(!before.diagnostic && before.total.*field == 2u);
            assert(before.usage[partition].*field == 2u);
            assert(!ledger->reserve(charge, partition, diagnostic) && diagnostic);
            assert(ledger->amend(lease, charge));
            assert(ledger->snapshot().total.*field == 2u);
            assert(foreign->transfer(lease, partition, (partition + 1u) % 4u));
            assert(ledger->transfer(lease, (partition + 1u) % 4u, partition));
            assert(!ledger->transfer(lease, partition, (partition + 1u) % 4u));
            const auto after = ledger->snapshot();
            assert(after.total.*field == 2u && after.usage[partition].*field == 0u);
            assert(after.usage[(partition + 1u) % 4u].*field == 2u);
            lease.reset();
            assert(!ledger->snapshot().liveOwners);
            ++cases;
        }
    }
    assert(!ledger->reserve({}, 0u, diagnostic) && diagnostic);
    Counters one;
    one.a = 1u;
    assert(!ledger->reserve(one, 4u, diagnostic) && diagnostic);
    // A projection observes exactly one original generation, without release
    // authority or an allocation-owner reference of its own.
    Ledger::Identity stale;
    {
        auto original = ledger->reserve(one, 0u, diagnostic);
        auto identity = ledger->identity(original, diagnostic);
        assert(!diagnostic && !ledger->validatePartition(identity, 0u));
        assert(ledger->validatePartition(identity, 1u));
        assert(foreign->validatePartition(identity, 0u));
        auto copy = identity;
        identity = {};
        assert(ledger->snapshot().liveOwners == 1u);
        stale = copy;
        assert(!ledger->transfer(original, 0u, 2u));
        assert(ledger->validatePartition(copy, 0u));
        assert(!ledger->validatePartition(copy, 2u));
        const char* id = nullptr;
        std::thread observer([&] {
            id = ledger->validatePartition(copy, 2u);
        });
        observer.join();
        assert(id);
    }
    assert(!ledger->snapshot().liveOwners && ledger->validatePartition(stale, 2u));
    {
        auto replacement = ledger->reserve(one, 2u, diagnostic);
        assert(replacement && ledger->validatePartition(stale, 2u));
        assert(ledger->snapshot().liveOwners == 1u);
    }
    // Allocation notification, rather than object destruction, releases a slot.
    unsigned destroyed = 0u;
    auto lease = ledger->reserve(one, 1u, diagnostic);
    std::shared_ptr<Value> object;
    {
        CpuMemoryQuotaScope scope(quota);
        object = ledger->makeSharedTracked<Value>(lease, destroyed);
    }
    lease.detach();
    assert(ledger->snapshot().liveOwners == 1u);
    std::weak_ptr<Value> weak = object;
    const char* wrongThread = nullptr;
    std::thread observer([&] {
        wrongThread = ledger->snapshot().diagnostic;
        const char* id = nullptr;
        assert(!ledger->reserve(one, 0u, id) && id);
        object.reset();
    });
    observer.join();
    assert(wrongThread && destroyed == 1u && ledger->snapshot().liveOwners == 1u);
    assert(weak.expired());
    // A weak control block still owns the tracked CPU allocation, even after
    // the object's last strong reference has gone.
    std::thread lastWeak([&] {
        weak.reset();
    });
    lastWeak.join();
    assert(!ledger->snapshot().liveOwners);
    // Marker generations prevent old allocator/lease notifications from
    // releasing a subsequent occupant of the same fixed slot.
    auto first = ledger->reserve(one, 0u, diagnostic);
    {
        CpuMemoryQuotaScope scope(quota);
        object = ledger->makeSharedTracked<Value>(first, destroyed);
    }
    bool duplicate = false;
    try {
        ledger->makeSharedTracked<Value>(first, destroyed);
    } catch (const DiagnosticError&) {
        duplicate = true;
    }
    assert(duplicate && ledger->snapshot().liveOwners == 1u);
    object.reset();
    assert(!ledger->snapshot().liveOwners);
    assert(ledger->transfer(first, 0u, 1u));
    auto next = ledger->reserve(one, 0u, diagnostic);
    first.reset();
    assert(ledger->snapshot().liveOwners == 1u);
    next.reset();
    assert(!ledger->snapshot().liveOwners);
    assert(!arp::test::CpuQuotaFactoryFixture::verifyHotNoAllocation<1>({quota}, [&]() noexcept {
        for (unsigned n = 0u; n < 10000u; ++n) {
            auto item = ledger->reserve(one, 1u, diagnostic);
            if (!item || ledger->transfer(item, 1u, 0u))
                return "hot reserve/transfer failed";
            auto identity = ledger->identity(item, diagnostic);
            if (diagnostic || ledger->validatePartition(identity, 0u))
                return "hot identity failed";
            item.reset();
            if (ledger->snapshot().liveOwners || !ledger->validatePartition(identity, 0u))
                return "hot release failed";
        }
        return static_cast<const char*>(nullptr);
    }));
    // Simultaneous remote releases must never expose a fraction of a record.
    // Each field of both records has the same charge, in distinct partitions.
    Counters whole;
    for (auto field : fields)
        whole.*field = 1u;
    auto left = ledger->reserve(whole, 0u, diagnostic);
    auto right = ledger->reserve(whole, 3u, diagnostic);
    assert(left && right);
    std::atomic<bool> go{false};
    std::thread releaseLeft([item = std::move(left), &go]() mutable {
        while (!go.load(std::memory_order_acquire)) {
        }
        item.reset();
    });
    std::thread releaseRight([item = std::move(right), &go]() mutable {
        while (!go.load(std::memory_order_acquire)) {
        }
        item.reset();
    });
    go.store(true, std::memory_order_release);
    for (unsigned n = 0u; n < 10000u; ++n) {
        const auto seen = ledger->snapshot();
        assert(!seen.diagnostic && seen.total.a <= 2u);
        for (auto field : fields)
            assert(seen.total.*field == seen.total.a);
    }
    releaseLeft.join();
    releaseRight.join();
    assert(!ledger->snapshot().liveOwners);
    auto badFields = fields;
    badFields[1] = fields[0];
    assert(!Ledger::prepareCold(capacity, badFields, 1u, quota, diagnostic));
    badFields = fields;
    badFields[2] = nullptr;
    assert(!Ledger::prepareCold(capacity, badFields, 1u, quota, diagnostic));
    Counters max;
    max.a = std::numeric_limits<std::uint64_t>::max();
    auto wide = Ledger::prepareCold(max, fields, 2u, quota, diagnostic);
    auto all = wide->reserve(max, 0u, diagnostic);
    assert(all && !diagnostic && wide->amend(all, one));
    assert(wide->snapshot().total.a == max.a);
    all.reset();
    auto closed = ledger->reserve(one, 0u, diagnostic);
    auto closedIdentity = ledger->identity(closed, diagnostic);
    ledger->close();
    assert(ledger->validatePartition(closedIdentity, 0u));
    assert(!ledger->reserve(one, 0u, diagnostic) && diagnostic);
    closed.reset();
    assert(!ledger->snapshot().liveOwners);
    // Cold allocation failure must retain no failed ledger storage.
    const auto baseline = quota->liveBytes();
    arp::test::CpuQuotaFactoryFixture pressure(quota);
    const auto factory = [&] {
        return Ledger::prepareCold(capacity, fields, 2u, quota, diagnostic);
    };
    const auto exact = pressure.measureSingleUse([] {}, factory);
    for (std::uint64_t available = 0u; available <= exact; ++available) {
        {
            CpuMemoryQuotaLease fill(quota, bytes - baseline - available);
            assert(fill.charged());
            auto candidate = factory();
            assert(bool(candidate) == (available == exact));
        }
        assert(quota->liveBytes() == baseline);
    }
    std::printf("partitioned ledger: %u dimension/partition cases; cold=%llu; 10000 hot cycles\n",
        cases,
        static_cast<unsigned long long>(exact));
}
