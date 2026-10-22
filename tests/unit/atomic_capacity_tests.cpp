#include "core/memory/atomic_capacity.h"

#include <array>
#include <cassert>
#include <thread>

int main()
{
    using namespace arp::core;
    AtomicCapacityBudget empty(0u);
    assert(!empty.canAcquire(0u) && !empty.canAcquire(1u) && !empty.used() && !empty.peak()
        && !empty.rejectionCount());
    assert(!empty.tryAcquire(1u));
    assert(empty.peak() == 0u && empty.rejectionCount() == 1u);
    AtomicCapacityBudget large(UINT64_MAX);
    assert(large.canAcquire(UINT64_MAX) && !large.canAcquire(0u) && !large.used() && !large.peak()
        && !large.rejectionCount());
    assert(large.tryAcquire(UINT64_MAX));
    assert(!large.canAcquire(1u) && !large.canAcquire(UINT64_MAX));
    assert(!large.tryAcquire(1u));
    assert(large.peak() == UINT64_MAX && large.rejectionCount() == 0u);
    large.release(UINT64_MAX);
    assert(large.used() == 0u);
    auto budget = makeShared<AtomicCapacityBudget>(4u);
    const auto before = defaultCpuAllocator().stats().allocationCount;
    assert(budget->canAcquire(4u) && !budget->canAcquire(5u) && !budget->used()
        && !budget->rejectionCount());
    assert(defaultCpuAllocator().stats().allocationCount == before);
    auto first = makeShared<AtomicCapacityLease>(budget, 2u);
    auto second = makeShared<AtomicCapacityLease>(budget, 2u);
    auto rejected = makeShared<AtomicCapacityLease>(budget, 1u);
    assert(first->charged() && second->charged() && !rejected->charged());
    assert(budget->used() == 4u);
    assert(budget->peak() == 4u && budget->rejectionCount() == 1u);
    first->release();
    first->release();
    first.reset();
    assert(budget->used() == 2u);
    assert(budget->canAcquire(2u) && !budget->canAcquire(3u) && budget->used() == 2u
        && budget->rejectionCount() == 1u);
    auto recovered = makeShared<AtomicCapacityLease>(budget, 2u);
    assert(recovered->charged() && budget->used() == 4u);
    second.reset();
    recovered.reset();
    assert(budget->used() == 0u);

    // The CPU byte quota keeps its established unlimited/zero-size rules.
    CpuMemoryQuota unlimited;
    assert(unlimited.tryReserve(0u) && unlimited.liveBytes() == 1u);
    unlimited.release(0u);
    assert(unlimited.liveBytes() == 0u && unlimited.peakBytes() == 1u);
    CpuMemoryQuota bytes(4u);
    assert(bytes.tryReserve(4u) && !bytes.tryReserve(1u));
    bytes.release(4u);
    assert(bytes.liveBytes() == 0u && bytes.peakBytes() == 4u);

#if !defined(__EMSCRIPTEN__)
    std::array<std::thread, 4> workers;
    for (auto& worker : workers) {
        worker = std::thread([budget] {
            for (unsigned i = 0; i < 10000u; ++i) {
                if (budget->tryAcquire(2u)) {
                    assert(budget->used() <= budget->capacity());
                    budget->release(2u);
                }
            }
        });
    }
    for (auto& worker : workers)
        worker.join();
    assert(budget->used() == 0u);
    auto sharedLease = makeShared<AtomicCapacityLease>(budget, 4u);
    for (auto& worker : workers)
        worker = std::thread([sharedLease] {
            sharedLease->release();
        });
    for (auto& worker : workers)
        worker.join();
    assert(budget->used() == 0u);
#endif
}
