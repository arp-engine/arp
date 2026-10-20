// Contract tests for core::MemoryBudgetLedger.
//
// This ledger consolidates the budget-accounting arithmetic previously
// duplicated verbatim across vulkan_allocator.cpp, metal_allocator.cpp and
// webgpu_allocator.cpp (canAdd overflow guard, budget-cap rejection,
// allocatedBytes/liveResourceCount accounting, clamp-at-zero release). These
// tests lock the overflow, budget-cap, accounting and release semantics so
// all three backend allocators inherit a single validated source of truth.

#include "core/memory_budget_ledger.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

int failures = 0;

void check(bool cond, const char* label)
{
    if (!cond) {
        std::printf("FAIL %s\n", label);
        ++failures;
    } else {
        std::printf("ok   %s\n", label);
    }
}

} // namespace

int main()
{
    using arp::core::MemoryBudgetLedger;
    using arp::core::MemoryBudgetLedgerDesc;

    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();

    // canAdd: overflow-safe addition guard (previously duplicated per backend).
    check(MemoryBudgetLedger::canAdd(0, 0), "canAdd(0,0)");
    check(MemoryBudgetLedger::canAdd(100, 200), "canAdd(100,200)");
    check(!MemoryBudgetLedger::canAdd(kMax, 1), "canAdd(max,1) overflow");
    check(!MemoryBudgetLedger::canAdd(kMax - 10, 11), "canAdd(max-10,11) overflow");
    check(MemoryBudgetLedger::canAdd(kMax - 10, 10), "canAdd(max-10,10) exact");

    // canAllocate: budget cap + overflow.
    MemoryBudgetLedger ledger({/*memoryBudgetBytes=*/1000,
        /*backendFallbacksHiddenFromUpperLayer=*/true});
    check(ledger.canAllocate(1), "canAllocate(1) within 1000 budget");
    check(ledger.canAllocate(1000), "canAllocate(1000) exact budget");
    check(!ledger.canAllocate(1001), "canAllocate(1001) over budget");
    check(!ledger.canAllocate(kMax), "canAllocate(max) overflow");

    // recordAllocation + stats accounting.
    ledger.recordAllocation(300);
    ledger.recordAllocation(200);
    auto s = ledger.stats();
    check(s.allocatedBytes == 500, "allocatedBytes == 500 after 300+200");
    check(s.peakAllocatedBytes == 500, "peakAllocatedBytes == 500 after 300+200");
    check(s.budgetRejectionCount == 0, "budgetRejectionCount initially zero");
    check(s.liveResourceCount == 2, "liveResourceCount == 2");
    check(s.memoryBudgetBytes == 1000, "memoryBudgetBytes == 1000");
    check(s.backendFallbacksHiddenFromUpperLayer, "backendFallbacksHiddenFromUpperLayer true");

    // After 500 allocated, only 500 remains.
    check(ledger.canAllocate(500), "canAllocate(500) remaining");
    check(!ledger.canAllocate(501), "canAllocate(501) over remaining");
    ledger.recordBudgetRejection();
    s = ledger.stats();
    check(s.budgetRejectionCount == 1, "budget rejection is observable");

    // releaseAllocation: clamps at zero, no underflow.
    ledger.releaseAllocation(300);
    s = ledger.stats();
    check(s.allocatedBytes == 200, "allocatedBytes == 200 after release 300");
    check(s.peakAllocatedBytes == 500, "peak remains after release");
    check(s.liveResourceCount == 1, "liveResourceCount == 1 after release");

    // Mismatched release (release more than allocated) clamps at zero.
    ledger.releaseAllocation(99999);
    s = ledger.stats();
    check(s.allocatedBytes == 0, "allocatedBytes clamped to 0 on over-release");
    check(s.liveResourceCount == 0, "liveResourceCount clamped to 0 on over-release");

    // After full release, full budget is available again.
    check(ledger.canAllocate(1000), "canAllocate(1000) after full release");

    // resetBudget updates the cap without touching live accounting.
    ledger.recordAllocation(100);
    ledger.resetBudget(200);
    check(
        !ledger.canAllocate(101), "canAllocate(101) rejected after resetBudget(200) with 100 live");
    check(
        ledger.canAllocate(100), "canAllocate(100) accepted after resetBudget(200) with 100 live");

    // Default-constructed ledger has zero budget: nothing fits.
    MemoryBudgetLedger reservation({1000, true});
    reservation.recordAllocation(1000);
    check(!reservation.shrinkAllocation(1000, 1001), "cannot grow a reservation");
    check(!reservation.shrinkAllocation(1000, 0), "zero observation rejected");
    check(!reservation.shrinkAllocation(1001, 500), "foreign reservation rejected");
    check(reservation.stats().allocatedBytes == 1000, "failed shrink preserves reservation");
    check(reservation.shrinkAllocation(1000, 300), "shrink exact original reservation");
    check(reservation.stats().allocatedBytes == 300 && reservation.stats().liveResourceCount == 1,
        "shrink keeps the live identity");
    check(reservation.stats().peakAllocatedBytes == 1000, "peak records cold reservation");
    check(reservation.canAllocate(700) && !reservation.canAllocate(701), "returned bytes reusable");
    check(reservation.shrinkAllocation(300, 300), "equal observation accepted");
    reservation.releaseAllocation(300);
    check(!reservation.stats().allocatedBytes && !reservation.stats().liveResourceCount,
        "release actual observed charge");
    check(!reservation.shrinkAllocation(300, 100), "retired reservation rejected");

    MemoryBudgetLedger empty;
    check(!empty.canAllocate(1), "default-constructed ledger rejects (zero budget)");

    if (failures != 0) {
        std::printf("%d assertion(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("all memory_budget_ledger contract tests passed\n");
    return EXIT_SUCCESS;
}
