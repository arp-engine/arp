#include "core/algorithm/stable_vector_erase.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {

struct Observation {
    std::array<unsigned, 8u> destroyed{};
    unsigned moves = 0u;
};

class TrackedValue final {
    struct Owner {
        Owner(unsigned id, Observation& observation) : id(id), observation(observation) {}

        ~Owner() { ++observation.destroyed[id]; }

        unsigned id;
        Observation& observation;
    };

public:
    TrackedValue(unsigned id, Observation& observation)
        : owner_(arp::core::makeUnique<Owner>(id, observation))
    {}

    TrackedValue(TrackedValue&& other) noexcept : owner_(std::move(other.owner_))
    {
        assert(owner_);
        ++owner_->observation.moves;
    }

    TrackedValue& operator=(TrackedValue&&) = delete;

    unsigned id() const
    {
        assert(owner_);
        return owner_->id;
    }

private:
    arp::core::UniquePtr<Owner> owner_;
};

struct PredicateFailure {};

class AllocatorValue final {
public:
    AllocatorValue(std::pmr::memory_resource* resource, int id) : payload(resource)
    {
        payload.push_back(id);
    }

    AllocatorValue(AllocatorValue&&) noexcept = default;
    AllocatorValue& operator=(AllocatorValue&&) = default;

    arp::core::pmr::Vector<int> payload;
};

static_assert(std::is_nothrow_move_constructible_v<AllocatorValue>);
static_assert(!std::is_nothrow_move_assignable_v<AllocatorValue>);

void preserveAllocatorIdentity(bool failSecond)
{
    using namespace arp::core;
    alignas(int) std::array<std::byte, sizeof(int)> firstStorage{};
    alignas(int) std::array<std::byte, sizeof(int)> secondStorage{};
    std::pmr::monotonic_buffer_resource firstResource(
        firstStorage.data(), firstStorage.size(), std::pmr::null_memory_resource());
    std::pmr::monotonic_buffer_resource secondResource(
        secondStorage.data(), secondStorage.size(), std::pmr::null_memory_resource());
    pmr::Vector<AllocatorValue> values;
    values.reserve(2u);
    values.emplace_back(&firstResource, 11);
    values.emplace_back(&secondResource, 22);
    const auto data = values.data();
    const auto capacity = values.capacity();
    const auto secondData = values[1].payload.data();
    auto quota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    bool caught = false;
    unsigned calls = 0u;
    {
        CpuMemoryQuotaScope scope(quota);
        replaceCpuObjectByMove(values[0], values[0]);
        assert(values[0].payload[0] == 11);
        try {
            const auto erased = StableVectorErase::eraseIf(values, [&](AllocatorValue& value) {
                ++calls;
                if (value.payload[0] == 11)
                    return true;
                if (failSecond)
                    throw PredicateFailure{};
                return false;
            });
            assert(erased == 1u);
        } catch (const PredicateFailure&) {
            caught = true;
        }
    }
    assert(caught == failSecond && calls == 2u);
    assert(values.size() == 1u && values.data() == data && values.capacity() == capacity);
    assert(values[0].payload[0] == 22 && values[0].payload.data() == secondData);
    assert(values[0].payload.get_allocator().resource() == &secondResource);
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    assert(quota->peakBytes() == 0u);
}

void runCase(unsigned count, unsigned mask, unsigned throwAt)
{
    using namespace arp::core;
    Observation observation;
    pmr::Vector<TrackedValue> values;
    values.reserve(count);
    for (unsigned i = 0u; i < count; ++i)
        values.emplace_back(i, observation);
    const auto capacity = values.capacity();
    const auto data = values.data();
    auto quota = makeShared<CpuMemoryQuota>(1u);
    const auto allocations = defaultCpuAllocator().stats().allocationCount;
    unsigned calls = 0u;
    bool caught = false;
    std::size_t removed = 0u;
    {
        CpuMemoryQuotaScope scope(quota);
        try {
            removed = StableVectorErase::eraseIf(values, [&](TrackedValue& value) {
                const auto id = value.id();
                ++calls;
                if (id == throwAt)
                    throw PredicateFailure{};
                return (mask & (1u << id)) != 0u;
            });
        } catch (const PredicateFailure&) {
            caught = true;
        }
    }
    assert(defaultCpuAllocator().stats().allocationCount == allocations);
    assert(quota->peakBytes() == 0u);
    assert(values.capacity() == capacity && values.data() == data);
    assert(caught == (throwAt < count));
    assert(calls == (caught ? throwAt + 1u : count));
    assert(observation.moves <= count);
    unsigned survivor = 0u;
    unsigned expectedRemoved = 0u;
    for (unsigned i = 0u; i < count; ++i) {
        const bool erased = (mask & (1u << i)) && (!caught || i < throwAt);
        assert(observation.destroyed[i] == unsigned(erased));
        if (erased)
            ++expectedRemoved;
        else
            assert(values[survivor++].id() == i);
    }
    assert(values.size() == survivor);
    assert(caught || removed == expectedRemoved);
    values.clear();
    for (unsigned i = 0u; i < count; ++i)
        assert(observation.destroyed[i] == 1u);
}

} // namespace

int main()
{
    preserveAllocatorIdentity(false);
    preserveAllocatorIdentity(true);
    unsigned cases = 0u;
    for (unsigned count = 0u; count <= 8u; ++count) {
        for (unsigned mask = 0u; mask < (1u << count); ++mask) {
            for (unsigned throwAt = 0u; throwAt <= count; ++throwAt) {
                runCase(count, mask, throwAt);
                ++cases;
            }
        }
    }
    std::printf(
        "StableVectorErase: %u cases; exact ownership/order, linear moves, no allocation\n", cases);
}
