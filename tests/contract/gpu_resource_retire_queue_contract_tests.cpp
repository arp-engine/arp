// Contract test for GpuResourceRetireQueue<T> (Phase 3 of
// docs/render-thread-double-buffer-plan.md).
//
// Guards the deferred-deletion queue's FIFO + serial-gating invariants so the
// render-thread / logic-thread split can rely on them. Uses a trivial
// CountingResource as T so the test stays in the core layer (does not depend
// on WoCC / RHI types). The WoCC instantiation (GpuResourceRetireQueue<
// ChunkGpuSlot>) is exercised end-to-end by the desktop smoke run
// (arp_world_of_claudecraft --hidden --frames N) which streams terrain chunks
// in/out and exercises the retire/reap path on the logic thread.
//
// Invariants under test:
//   1. FIFO + serial gating: retire(A, S=5) then retire(B, S=10); reapCompleted
//      with C <= 5 destroys nothing; C = 6 destroys A but not B; C = 11
//      destroys B. (Strictly-greater rule: reap only when C > S.)
//   2. reapAll bypasses serial gating: destroys all entries regardless of
//      serial, returns the count, leaves the queue empty.
//   3. Move semantics: retire(T&&) leaves the caller's T in a moved-from
//      state (sentinel tag); the queue owns the live resource thereafter.
//   4. Empty queue: reapCompleted / reapAll on an empty queue return 0 and do
//      not crash.
//   5. Repeat reap is idempotent: reaping up to C, then reaping up to the same
//      C again, destroys nothing the second time (entries already gone).
//   6. Out-of-order serials: the queue only ever destroys a FIFO prefix
//      (prefix-stop rule). A smaller serial retired after a larger one is not
//      retroactively reaped ahead of the earlier entry.
//   7. Destructor drains: when the queue goes out of scope, every live entry's
//      destructor runs (no leak).
//   8. Memory discipline: the internal container is core::pmr::Deque on
//      defaultCpuMemoryResource (AGENTS.md allocator constraint). This is
//      verified structurally by the cpu_memory_policy_contract_tests scanner
//      over the header; this test does not re-assert it.

#include "core/runtime/gpu_resource_retire_queue.h"
#include "rhi/frontend/memory_retirement_budget.h"

#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using arp::core::runtime::GpuResourceRetireQueue;

// Trivial resource that records its tag on a global destruction log so the
// test can observe which instances the queue destroyed and in what order,
// without depending on any engine type.
//
// Ownership model: a CountingResource is either "live" (carries a real tag)
// or "moved-from" (tag == kMovedFromSentinel). Only live instances record
// their tag on destruction; moved-from instances are silent. This lets the
// test distinguish the queue destroying a live entry (recorded) from the
// caller's moved-from shell going out of scope (silent).
struct CountingResource {
    static constexpr std::uint64_t kMovedFromSentinel = 0xFFFFFFFFFFFFFFFFull;

    // Global log of tags destroyed by LIVE instances (queue-owned entries).
    // The test resets this before each scenario.
    static std::vector<std::uint64_t> destroyedTags;

    std::uint64_t tag = 0;

    CountingResource() = default;

    explicit CountingResource(std::uint64_t t) : tag(t) {}

    ~CountingResource()
    {
        if (tag != kMovedFromSentinel && tag != 0) {
            destroyedTags.push_back(tag);
        }
    }

    CountingResource(CountingResource&& other) noexcept : tag(other.tag)
    {
        other.tag = kMovedFromSentinel; // moved-from shell stays silent on dtor
    }

    CountingResource& operator=(CountingResource&& other) noexcept
    {
        if (this != &other) {
            // If `this` currently holds a live tag, record it (the queue's
            // deque move-assigns entries during internal reallocation, which
            // would otherwise silently drop a live tag).
            if (tag != kMovedFromSentinel && tag != 0) {
                destroyedTags.push_back(tag);
            }
            tag = other.tag;
            other.tag = kMovedFromSentinel;
        }
        return *this;
    }

    CountingResource(const CountingResource&) = delete;
    CountingResource& operator=(const CountingResource&) = delete;
};

std::vector<std::uint64_t> CountingResource::destroyedTags;

void resetDestroyedLog()
{
    CountingResource::destroyedTags.clear();
}

std::size_t destroyedCount()
{
    return CountingResource::destroyedTags.size();
}

bool wasDestroyed(std::uint64_t tag)
{
    for (auto t : CountingResource::destroyedTags) {
        if (t == tag)
            return true;
    }
    return false;
}

void test_fifo_and_serial_gating()
{
    // retire(A tag=11, S=5), retire(B tag=22, S=10). reapCompleted(C):
    //   C=4  -> destroy nothing (C not > 5)
    //   C=5  -> destroy nothing (C not > 5, strict-greater rule)
    //   C=6  -> destroy A only (6 > 5, but 6 not > 10)
    //   C=10 -> destroy nothing more (10 not > 10)
    //   C=11 -> destroy B (11 > 10)
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(11);
        CountingResource b(22);
        q.retire(5, std::move(a));
        q.retire(10, std::move(b));
        assert(q.size() == 2);
        const auto admitted = q.stats();
        assert(admitted.pendingCount == 2u && admitted.peakPendingCount == 2u
            && admitted.totalRetired == 2u && admitted.totalReaped == 0u);

        assert(q.reapCompleted(4) == 0);
        assert(q.size() == 2);
        assert(destroyedCount() == 0);

        assert(q.reapCompleted(5) == 0);
        assert(q.size() == 2);
        assert(destroyedCount() == 0);

        assert(q.reapCompleted(6) == 1);
        assert(q.size() == 1);
        const auto partial = q.stats();
        assert(partial.pendingCount == 1u && partial.peakPendingCount == 2u
            && partial.totalRetired == 2u && partial.totalReaped == 1u);
        assert(destroyedCount() == 1);
        assert(wasDestroyed(11));
        assert(!wasDestroyed(22));

        assert(q.reapCompleted(10) == 0);
        assert(q.size() == 1);
        assert(destroyedCount() == 1);

        assert(q.reapCompleted(11) == 1);
        assert(q.size() == 0);
        const auto complete = q.stats();
        assert(complete.pendingCount == 0u && complete.peakPendingCount == 2u
            && complete.totalRetired == 2u && complete.totalReaped == 2u);
        assert(destroyedCount() == 2);
        assert(wasDestroyed(22));
        // `a` and `b` shells go out of scope here silently (moved-from).
    }
}

void test_reapAll_bypasses_serial_gating()
{
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(1);
        CountingResource b(2);
        CountingResource c(3);
        q.retire(100, std::move(a));
        q.retire(200, std::move(b));
        q.retire(300, std::move(c));
        assert(q.size() == 3);

        // reapCompleted with a small C destroys nothing (serial gating holds).
        assert(q.reapCompleted(50) == 0);
        assert(q.size() == 3);

        // reapAll ignores serials and destroys everything.
        const std::size_t reaped = q.reapAll();
        assert(reaped == 3);
        assert(q.size() == 0);
        assert(q.empty());
        assert(destroyedCount() == 3);
        assert(wasDestroyed(1));
        assert(wasDestroyed(2));
        assert(wasDestroyed(3));
    }
}

void test_move_semantics_retire_takes_ownership()
{
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(42);
        q.retire(7, std::move(a));

        // moved-from instance is left with the sentinel tag; the queue holds
        // the live resource. The caller's shell has not yet been destroyed.
        assert(a.tag == CountingResource::kMovedFromSentinel);
        assert(q.size() == 1);
        assert(destroyedCount() == 0);

        // reapCompleted with C > 7 destroys the retired entry (live resource).
        assert(q.reapCompleted(8) == 1);
        assert(destroyedCount() == 1);
        assert(wasDestroyed(42));
        assert(q.empty());
        // `a` shell goes out of scope here silently (moved-from).
    }
}

void test_empty_queue_reap_is_safe()
{
    resetDestroyedLog();
    GpuResourceRetireQueue<CountingResource> q;
    assert(q.empty());
    assert(q.size() == 0);

    assert(q.reapCompleted(0) == 0);
    assert(q.reapCompleted(1000) == 0);
    assert(q.reapAll() == 0);
    assert(q.empty());
    assert(destroyedCount() == 0);
}

void test_repeat_reap_is_idempotent()
{
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(1);
        CountingResource b(2);
        q.retire(5, std::move(a));
        q.retire(10, std::move(b));

        assert(q.reapCompleted(11) == 2);
        assert(destroyedCount() == 2);

        // Reaping up to the same serial again must destroy nothing (the
        // entries are already gone).
        assert(q.reapCompleted(11) == 0);
        assert(q.reapCompleted(20) == 0);
        assert(destroyedCount() == 2);
        assert(q.empty());
    }
}

void test_out_of_order_serials_prefix_stop()
{
    // The queue assumes monotonically non-decreasing serials in practice
    // (collectAndUploadTerrainChunks stamps with the current submission
    // serial). If a caller retires with a smaller serial after a larger one,
    // reapCompleted's prefix-stop rule means the smaller-serial entry is NOT
    // reaped ahead of the earlier larger-serial entry -- the queue only
    // destroys a contiguous FIFO prefix where every entry has S < C. It stops
    // at the first entry with S >= C, even if a later entry would be safe to
    // reap. This test pins that behavior so a future refactor cannot silently
    // switch to "scan-and-reap-any" semantics that would change destruction
    // ordering. The pinned guarantee is: the queue never reaps an entry whose
    // S >= C, and it never skips a blocking front entry to reach a safer one
    // behind it (no scan-ahead). Out-of-order use can delay reaping (memory
    // held longer than strictly necessary) but can never over-reap (destroy a
    // resource still referenced by an in-flight submission).
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(11);
        CountingResource b(22);
        q.retire(10, std::move(a)); // first retired, larger serial
        q.retire(5, std::move(b));  // second retired, smaller serial

        // C=6: B (S=5) would be safe under a scan-any rule (5 < 6), but the
        // queue stops at the front A (S=10, 10 >= 6) and reaps nothing. B is
        // delayed until A becomes reapable.
        assert(q.reapCompleted(6) == 0);
        assert(q.size() == 2);
        assert(destroyedCount() == 0);

        // C=11: A (S=10) is now reapable (10 < 11), so the queue pops A. The
        // new front is B (S=5, 5 < 11), so B is also popped in the same call.
        // Both are reaped in FIFO order -- the queue reaps a contiguous
        // prefix, not a cherry-picked subset.
        assert(q.reapCompleted(11) == 2);
        assert(q.size() == 0);
        assert(destroyedCount() == 2);
        assert(wasDestroyed(11));
        assert(wasDestroyed(22));
        assert(q.empty());
    }
}

void test_destructor_drains_live_entries()
{
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(1);
        CountingResource b(2);
        CountingResource c(3);
        q.retire(5, std::move(a));
        q.retire(10, std::move(b));
        q.retire(15, std::move(c));
        assert(q.size() == 3);
        assert(destroyedCount() == 0);
        // q goes out of scope here; its pmr::Deque<Entry> destructor destroys
        // every live Entry, which destroys every live CountingResource.
    }
    assert(destroyedCount() == 3);
    assert(wasDestroyed(1));
    assert(wasDestroyed(2));
    assert(wasDestroyed(3));
}

void test_move_constructed_queue_preserves_entries()
{
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        CountingResource a(7);
        q.retire(3, std::move(a));
        assert(q.size() == 1);

        GpuResourceRetireQueue<CountingResource> moved(std::move(q));
        assert(moved.size() == 1);
        assert(q.size() == 0); // moved-from queue is empty

        // The moved-to queue still owns the resource; reaping from it works.
        assert(moved.reapCompleted(4) == 1);
        assert(destroyedCount() == 1);
        assert(wasDestroyed(7));
        assert(moved.empty());
        // `a` shell goes out of scope silently.
    }
}

void test_multiple_retire_then_interleaved_reap()
{
    // Realistic steady-state: multiple chunks retired across consecutive
    // frames, reap called every frame with the advancing completedFrameSerial.
    // Each reap should destroy exactly the entries whose serial is now past.
    resetDestroyedLog();
    {
        GpuResourceRetireQueue<CountingResource> q;
        // Frame 100: retire chunks at serial 100.
        CountingResource a(1001);
        CountingResource b(1002);
        q.retire(100, std::move(a));
        q.retire(100, std::move(b));
        // Frame 101: retire one more at serial 101.
        CountingResource c(1003);
        q.retire(101, std::move(c));

        // Render thread completes frame 100 -> completedFrameSerial=101.
        // reapCompleted(101) destroys entries with serial < 101 (both S=100).
        assert(q.reapCompleted(101) == 2);
        assert(destroyedCount() == 2);
        assert(wasDestroyed(1001));
        assert(wasDestroyed(1002));
        assert(!wasDestroyed(1003));

        // Render thread completes frame 101 -> completedFrameSerial=102.
        // reapCompleted(102) destroys S=101.
        assert(q.reapCompleted(102) == 1);
        assert(destroyedCount() == 3);
        assert(wasDestroyed(1003));
        assert(q.empty());
    }
}

void test_shared_capacity_rejects_before_ownership_transfer()
{
    resetDestroyedLog();
    auto budget = arp::core::makeShared<arp::core::AtomicCapacityBudget>(2u);
    GpuResourceRetireQueue<CountingResource> first;
    GpuResourceRetireQueue<CountingResource> second;
    assert(first.setCapacityBudget(budget));
    assert(second.setCapacityBudget(budget));

    CountingResource a(1u);
    CountingResource b(2u);
    CountingResource rejected(3u);
    assert(first.retire(5u, std::move(a)));
    assert(second.retire(5u, std::move(b)));
    assert(!first.canRetire());
    assert(!first.retire(5u, std::move(rejected)));

    // Exhaustion is an admission failure: the caller keeps ownership and the
    // two existing queues remain unchanged.
    assert(rejected.tag == 3u);
    assert(first.size() == 1u && second.size() == 1u);
    assert(budget->used() == 2u
        && budget->peak() == 2u
        // canRetire() and retire() are distinct rejected admission attempts.
        && budget->rejectionCount() == 2u);

    assert(first.reapCompleted(6u) == 1u);
    assert(budget->used() == 1u);
    assert(first.retire(6u, std::move(rejected)));
    assert(rejected.tag == CountingResource::kMovedFromSentinel);
    assert(budget->used() == 2u);
    assert(second.reapAll() == 1u);
    assert(first.reapAll() == 1u);
    assert(budget->used() == 0u);
}

void test_retirement_budget_policy_requires_explicit_versioned_owner()
{
    arp::rhi::semantics::RetirementBudget invalid;
    assert(!invalid.valid());

    const arp::rhi::semantics::RetirementBudget policy{
        arp::rhi::semantics::kRetirementBudgetContractVersion, "retirement-contract-test", 2u};
    arp::rhi::semantics::RetirementBudgetTracker tracker(policy);
    assert(tracker.configured());
    const auto stats = tracker.stats();
    assert(stats.configured && stats.capacity == 2u && stats.current == 0u && stats.peak == 0u
        && stats.rejectionCount == 0u);
}

} // namespace

int main()
{
    test_fifo_and_serial_gating();
    test_reapAll_bypasses_serial_gating();
    test_move_semantics_retire_takes_ownership();
    test_empty_queue_reap_is_safe();
    test_repeat_reap_is_idempotent();
    test_out_of_order_serials_prefix_stop();
    test_destructor_drains_live_entries();
    test_move_constructed_queue_preserves_entries();
    test_multiple_retire_then_interleaved_reap();
    test_shared_capacity_rejects_before_ownership_transfer();
    test_retirement_budget_policy_requires_explicit_versioned_owner();
    return 0;
}
