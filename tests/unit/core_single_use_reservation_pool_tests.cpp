#include "core/container/single_use_reservation_pool.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
struct Metadata {
    std::uint64_t graph = 0u;
    std::uint64_t topology = 0u;
    std::uint64_t resources = 0u;
};

using Pool = arp::core::SingleUseReservationPool<Metadata>;
using Token = Pool::Reservation;
using Stage = Pool::Stage;
constexpr std::uint64_t testMetadataBudget = 64u * 1024u;
constexpr Metadata binding{1u, 2u, 3u};

static_assert(!std::is_copy_constructible_v<Pool> && !std::is_copy_assignable_v<Pool>);
static_assert(std::is_nothrow_move_constructible_v<Pool>);
static_assert(!std::is_copy_constructible_v<Token> && !std::is_copy_assignable_v<Token>);
static_assert(
    std::is_nothrow_move_constructible_v<Token> && std::is_nothrow_move_assignable_v<Token>);
static_assert(!std::is_constructible_v<Token, arp::core::PoolHandle>);
static_assert(!std::is_constructible_v<Token, Metadata>);
static_assert(!std::is_constructible_v<Token, void*>);
static_assert(std::is_same_v<decltype(std::declval<const Token&>().metadata()), const Metadata*>);

void expect(const char* diagnostic, const char* expected)
{
    assert(diagnostic && std::string_view(diagnostic) == expected);
}

Pool prepare(std::uint32_t capacity, const std::shared_ptr<arp::core::CpuMemoryQuota>& quota)
{
    const char* diagnostic = nullptr;
    auto pool = Pool::prepareCold(capacity, quota, diagnostic);
    assert(pool && !diagnostic);
    return std::move(*pool);
}

bool matches(const Metadata& metadata) noexcept
{
    return metadata.graph == binding.graph && metadata.topology == binding.topology
        && metadata.resources == binding.resources;
}

void lifecycle()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(testMetadataBudget);
    auto pool = prepare(1u, quota);
    auto foreign = prepare(1u, quota);
    auto hotQuota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    {
        CpuMemoryQuotaScope scope(hotQuota);
        const char* diagnostic = nullptr;
        Token empty;
        unsigned calls = 0u;
        const auto validate = [&](const Metadata& metadata) noexcept {
            ++calls;
            return matches(metadata);
        };
        expect(pool.consume(empty, validate), "CORE-RESERVATION-EMPTY");
        auto token = pool.reserve(binding, diagnostic);
        assert(token && !diagnostic && token.stage() == Stage::Reserved && pool.liveCount() == 1u);
        auto full = pool.reserve(binding, diagnostic);
        assert(!full);
        expect(diagnostic, "CORE-POOL-CAPACITY");
        expect(foreign.consume(token, validate), "CORE-RESERVATION-FOREIGN");
        expect(foreign.cancel(token), "CORE-RESERVATION-FOREIGN");
        expect(foreign.eraseConsumedMetadata(token), "CORE-RESERVATION-FOREIGN");
        assert(calls == 0u && token.stage() == Stage::Reserved);
        expect(pool.eraseConsumedMetadata(token), "CORE-RESERVATION-STAGE");
        expect(pool.consume(token,
                   [&](const Metadata&) noexcept {
                       ++calls;
                       return false;
                   }),
            "CORE-RESERVATION-METADATA");
        assert(calls == 1u && pool.liveCount() == 1u && token.stage() == Stage::Reserved);
        assert(!pool.consume(token, validate) && calls == 2u && pool.retainedCount() == 1u);
        expect(pool.consume(token, validate), "CORE-RESERVATION-STAGE");
        expect(pool.cancel(token), "CORE-RESERVATION-STAGE");
        assert(calls == 2u && token.stage() == Stage::Consumed);
        assert(!pool.eraseConsumedMetadata(token) && !token && pool.liveCount() == 0u);
        expect(pool.eraseConsumedMetadata(token), "CORE-RESERVATION-EMPTY");
        token = pool.reserve(binding, diagnostic);
        assert(token && !diagnostic);
        assert(!pool.cancel(token) && !token && pool.liveCount() == 0u);
        expect(pool.cancel(token), "CORE-RESERVATION-EMPTY");
        {
            auto automatic = pool.reserve(binding, diagnostic);
            assert(automatic && pool.liveCount() == 1u);
        }
        assert(pool.liveCount() == 0u && !pool.closed());
    }
    assert(hotQuota->peakBytes() == 0u);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
}

void movesAndWrapperLifetime()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(testMetadataBudget);
    Token survivor;
    {
        auto pool = prepare(2u, quota);
        const char* diagnostic = nullptr;
        auto first = pool.reserve(binding, diagnostic);
        assert(first && !diagnostic);
        auto second = pool.reserve({4u, 5u, 6u}, diagnostic);
        assert(second && !diagnostic);
        first = std::move(first);
        assert(first && pool.liveCount() == 2u);
        survivor = std::move(first);
        assert(!first && survivor && survivor.metadata()->resources == 3u);
        second = std::move(survivor); // Cancels the overwritten Reserved slot.
        assert(!survivor && pool.liveCount() == 1u && matches(*second.metadata()));
        auto movedPool = std::move(pool);
        assert(pool.closed() && pool.liveCount() == 0u);
        auto unavailable = pool.reserve(binding, diagnostic);
        assert(!unavailable);
        expect(diagnostic, "CORE-RESERVATION-SOURCE");
        expect(pool.consume(second, matches), "CORE-RESERVATION-SOURCE");
        assert(!movedPool.consume(second, matches));
        assert(!movedPool.eraseConsumedMetadata(second));
        survivor = movedPool.reserve(binding, diagnostic);
        assert(survivor && !diagnostic);
    }
    assert(survivor && survivor.stage() == Stage::Reserved && matches(*survivor.metadata()));
    assert(quota->liveBytes() > 0u); // Original state outlives its wrapper.
    survivor.reset();
    assert(!survivor && quota->liveBytes() == 0u);

    // A consumed token also keeps its original state alive across wrapper
    // teardown. Destruction quarantines metadata; it never destroys GPU data.
    {
        auto pool = prepare(1u, quota);
        const char* diagnostic = nullptr;
        survivor = pool.reserve(binding, diagnostic);
        assert(!pool.consume(survivor, matches));
    }
    assert(survivor.stage() == Stage::Consumed && quota->liveBytes() > 0u);
    survivor.reset();
    assert(quota->liveBytes() == 0u);
}

void closeAndAbandon()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(testMetadataBudget);
    auto pool = prepare(3u, quota);
    auto hotQuota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    {
        CpuMemoryQuotaScope scope(hotQuota);
        const char* diagnostic = nullptr;
        auto consumed = pool.reserve(binding, diagnostic);
        auto stale = pool.reserve(binding, diagnostic);
        assert(!pool.consume(consumed, matches));
        pool.close();
        assert(pool.closed() && pool.liveCount() == 1u && pool.retainedCount() == 1u);
        assert(consumed.stage() == Stage::Quarantined && !stale);
        unsigned calls = 0u;
        expect(pool.consume(stale,
                   [&](const Metadata&) noexcept {
                       ++calls;
                       return true;
                   }),
            "CORE-RESERVATION-STALE");
        expect(pool.cancel(stale), "CORE-RESERVATION-STALE");
        expect(pool.cancel(consumed), "CORE-RESERVATION-STAGE");
        assert(calls == 0u);
        pool.close();
        assert(pool.liveCount() == 1u);
        assert(!pool.eraseConsumedMetadata(consumed));
        auto denied = pool.reserve(binding, diagnostic);
        assert(!denied);
        expect(diagnostic, "CORE-RESERVATION-CLOSED");
    }
    assert(hotQuota->peakBytes() == 0u);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);

    auto abandonedPool = prepare(2u, quota);
    const char* diagnostic = nullptr;
    auto token = abandonedPool.reserve(binding, diagnostic);
    auto reserved = abandonedPool.reserve(binding, diagnostic);
    assert(!abandonedPool.consume(token, matches));
    token.reset();
    assert(abandonedPool.closed() && abandonedPool.liveCount() == 2u);
    assert(abandonedPool.retainedCount() == 1u);
    unsigned calls = 0u;
    expect(abandonedPool.consume(reserved,
               [&](const Metadata&) noexcept {
                   ++calls;
                   return true;
               }),
        "CORE-RESERVATION-CLOSED");
    assert(calls == 0u && reserved.stage() == Stage::Reserved);
    assert(!abandonedPool.cancel(reserved));
    assert(abandonedPool.liveCount() == 1u && abandonedPool.retainedCount() == 1u);

    auto overwrittenPool = prepare(1u, quota);
    auto otherPool = prepare(1u, quota);
    auto overwritten = overwrittenPool.reserve(binding, diagnostic);
    auto replacement = otherPool.reserve(binding, diagnostic);
    assert(!overwrittenPool.consume(overwritten, matches));
    overwritten = std::move(overwritten);
    assert(overwritten.stage() == Stage::Consumed && !overwrittenPool.closed());
    overwritten = std::move(replacement);
    assert(!replacement && overwritten.stage() == Stage::Reserved);
    assert(overwrittenPool.closed() && overwrittenPool.retainedCount() == 1u);
    assert(!otherPool.closed() && !otherPool.consume(overwritten, matches));
    assert(!otherPool.eraseConsumedMetadata(overwritten));
}

void quotaBoundaries()
{
    using namespace arp::core;
    const char* diagnostic = nullptr;
    assert(!Pool::prepareCold(0u, {}, diagnostic));
    expect(diagnostic, "CORE-RESERVATION-CAPACITY");
    assert(!Pool::prepareCold(1u, {}, diagnostic));
    expect(diagnostic, "CORE-RESERVATION-QUOTA");
    auto disabled = makeShared<CpuMemoryQuota>(0u);
    assert(!Pool::prepareCold(1u, disabled, diagnostic));
    expect(diagnostic, "CORE-RESERVATION-QUOTA");
    assert(disabled->liveBytes() == 0u);
    auto measured = makeShared<CpuMemoryQuota>(testMetadataBudget);
    {
        auto pool = prepare(3u, measured);
        assert(pool.liveCount() == 0u);
    }
    assert(measured->liveBytes() == 0u);
    const auto required = measured->peakBytes();
    assert(required > 1u && required < testMetadataBudget);
    for (std::uint64_t limit = 1u; limit <= required; ++limit) {
        auto quota = makeShared<CpuMemoryQuota>(limit);
        auto pool = Pool::prepareCold(3u, quota, diagnostic);
        if (limit == required)
            assert(pool && !diagnostic);
        else {
            assert(!pool);
            expect(diagnostic, "CORE-RESERVATION-ALLOCATION");
        }
        pool.reset();
        assert(quota->liveBytes() == 0u && quota->peakBytes() <= limit);
    }
    std::printf("cold quota: exact %llu bytes, every lower byte boundary rolls back\n",
        static_cast<unsigned long long>(required));
}

void repeatedReuse()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(testMetadataBudget);
    auto pool = prepare(1u, quota);
    auto hotQuota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    const auto storageBytes = quota->liveBytes();
    {
        CpuMemoryQuotaScope scope(hotQuota);
        for (std::uint64_t iteration = 1u; iteration <= 10000u; ++iteration) {
            const char* diagnostic = nullptr;
            auto token = pool.reserve({iteration, 2u, 3u}, diagnostic);
            assert(token && !diagnostic && token.metadata()->graph == iteration);
            if (iteration % 2u) {
                assert(!pool.consume(token, [iteration](const Metadata& metadata) noexcept {
                    return metadata.graph == iteration;
                }));
                assert(!pool.eraseConsumedMetadata(token));
            } else
                assert(!pool.cancel(token));
            assert(!token && pool.liveCount() == 0u && quota->liveBytes() == storageBytes);
        }
    }
    assert(hotQuota->peakBytes() == 0u);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::string_view mode(argv[1]);
    if (mode == "lifecycle")
        lifecycle();
    else if (mode == "moves")
        movesAndWrapperLifetime();
    else if (mode == "close")
        closeAndAbandon();
    else if (mode == "quota")
        quotaBoundaries();
    else if (mode == "reuse")
        repeatedReuse();
    else
        return 2;
    std::puts("SingleUseReservationPool/1 CPU metadata only: PASS");
}
