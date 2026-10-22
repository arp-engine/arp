#include "core/memory/atomic_capacity_reservation.h"
#include "rhi/frontend/bindless_workload_retention.h"

#include <cassert>
#include <cstdio>

#ifdef NDEBUG
#error Workload capacity tests require assertions
#endif

int main()
{
    using namespace arp;
    using Contract = rhi::semantics::BindlessWorkloadCapacityContract;
    for (const auto backend :
        {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
        const auto decision = Contract::lowering(backend);
        assert(decision.reservationAvailable && !decision.executionAvailable && !decision.diagnostic
            && decision.validatorOnly == (backend == rhi::Backend::Null));
        assert(Contract::lowering(backend, 2u).diagnostic);
    }
    for (const auto backend : {rhi::Backend::WebGL, static_cast<rhi::Backend>(255u)})
        assert(!Contract::lowering(backend).reservationAvailable);
    rhi::semantics::BindlessWorkloadCapacityBudget budget{
        1u, true, {2u, 3u, 40u, 8u, 5u, 64u, 32u}};
    assert(!Contract::validate(budget));
    budget.optIn = false;
    assert(Contract::validate(budget));
    budget.optIn = true;
    budget.contractVersion = 2u;
    assert(Contract::validate(budget));
    budget.contractVersion = 1u;
    budget.limits[0] = 0u;
    assert(Contract::validate(budget));
    core::AtomicCapacityBudget first(3u), second(5u), third(0u);
    using Reservation = core::AtomicCapacityReservation<3u>;
    const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
    {
        Reservation held;
        assert(held.acquire({&first, &second, &third}, {1u, 2u, 0u}));
        assert(!held.acquire({&first, &second, &third}, {1u, 1u, 0u}));
        assert(first.used() == 1u && second.used() == 2u && held.charged());
        {
            Reservation failed;
            // Late failure must roll back the first two independent counters.
            assert(!failed.acquire({&first, &second, &third}, {2u, 3u, 1u}));
            assert(!failed.charged() && first.used() == 1u && second.used() == 2u);
            assert(!failed.acquire({&first, nullptr, &third}, {1u, 1u, 0u}));
            assert(first.used() == 1u && second.used() == 2u);
            // A failed object may retry; exact remaining capacity succeeds.
            assert(failed.acquire({&first, &second, &third}, {2u, 3u, 0u}));
            assert(first.used() == 3u && second.used() == 5u);
            Reservation over;
            assert(!over.acquire({&first, &second, &third}, {1u, 0u, 0u}));
        }
        assert(first.used() == 1u && second.used() == 2u);
    }
    assert(!first.used() && !second.used() && !third.used());
    assert(first.peak() == 3u && second.peak() == 5u && third.rejectionCount() == 1u);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
    std::puts(
        "BINDLESS_WORKLOAD_CAPACITY_OK independent transactional rollback retry exact hot0 four-backend no-execution");
}
