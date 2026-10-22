#include "core/cpu_allocator.h"
#include "core/memory/budget_vector_preflight.h"

#include <cassert>
#include <cstdio>
#include <limits>

namespace {
struct Counters {
    std::uint64_t bytes = 0u;
    std::uint64_t count = 0u;

    bool operator==(const Counters& other) const noexcept
    {
        return bytes == other.bytes && count == other.count;
    }
};

using Preflight = arp::core::BudgetVectorPreflight<Counters, 2u>;
using Status = arp::core::BudgetVectorStatus;
constexpr Preflight::Fields fields{&Counters::bytes, &Counters::count};
} // namespace

int main()
{
    using namespace arp::core;
    auto quota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    std::uint64_t cases = 0u;
    {
        CpuMemoryQuotaScope scope(quota);
        // Four independent ownership partitions: an empty addition must still
        // reject a live snapshot above capacity. Compute the oracle in a wider
        // domain than these small inputs, independently of the production helper.
        for (unsigned capacity = 0u; capacity < 6u; ++capacity)
            for (unsigned a = 0u; a < 6u; ++a)
                for (unsigned b = 0u; b < 6u; ++b)
                    for (unsigned c = 0u; c < 6u; ++c)
                        for (unsigned d = 0u; d < 6u; ++d)
                            for (unsigned add = 0u; add < 6u; ++add) {
                                const std::array live{Counters{a, 1u},
                                    Counters{b, 1u},
                                    Counters{c, 1u},
                                    Counters{d, 1u}};
                                const auto original = live;
                                const auto result =
                                    Preflight::assess({capacity, 5u}, live, {add, 1u}, fields);
                                const unsigned sum = a + b + c + d;
                                const auto expected = sum > capacity ? Status::LiveExceedsCapacity
                                    : sum + add > capacity           ? Status::Capacity
                                                                     : Status::Fits;
                                assert(result.status == expected && live == original);
                                if (result.fits()) {
                                    assert(result.projected == (Counters{sum + add, 5u}));
                                    assert(result.dimension == 2u);
                                } else {
                                    assert(result.projected == Counters{});
                                    assert(result.dimension == 0u);
                                }
                                ++cases;
                            }
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t dimension = 0u; dimension < fields.size(); ++dimension) {
            const auto field = fields[dimension];
            Counters capacity{maximum, maximum};
            std::array<Counters, 4u> live{};
            live[0].*field = maximum - 1u;
            live[3].*field = 1u;
            auto result = Preflight::assess(capacity, live, {}, fields);
            assert(result.fits() && result.projected.*field == maximum);
            live[3].*field = 2u;
            result = Preflight::assess(capacity, live, {}, fields);
            assert(result.status == Status::LiveOverflow && result.dimension == dimension);
            assert(result.projected == Counters{});
            live[3].*field = 0u;
            Counters addition;
            addition.*field = 1u;
            assert(Preflight::assess(capacity, live, addition, fields).fits());
            addition.*field = 2u;
            result = Preflight::assess(capacity, live, addition, fields);
            assert(result.status == Status::Overflow && result.dimension == dimension);
            assert(result.projected == Counters{});
        }
        // Failure categories precede field order. Never report a small capacity
        // failure while another field's entire live snapshot is malformed.
        assert(Preflight::assess(
                   {0u, maximum}, std::array{Counters{1u, maximum}, Counters{0u, 1u}}, {}, fields)
                   .status
            == Status::LiveOverflow);
        assert(Preflight::assess({maximum, 0u}, std::array{Counters{maximum, 1u}}, {1u, 0u}, fields)
                   .status
            == Status::LiveExceedsCapacity);
        assert(Preflight::assess({0u, maximum}, std::array{Counters{0u, maximum}}, {1u, 1u}, fields)
                   .status
            == Status::Overflow);
        assert(Preflight::assess({}, std::array<Counters, 0u>{}, {}, fields).fits());
    }
    assert(cases == 46656u && quota->peakBytes() == 0u);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    std::printf(
        "BudgetVectorPreflight: %llu exhaustive cases, overflow/priority/no-allocation PASS\n",
        static_cast<unsigned long long>(cases));
}
