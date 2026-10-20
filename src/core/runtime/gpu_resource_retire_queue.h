#pragma once

// Deferred-deletion queue for GPU resources owned by the logic thread but
// referenced by in-flight render-thread submissions. Mirrors the
// deferred-deletion pattern in Unity's GfxDevice (DeletePending) and
// Unreal's RT_DeferredDelete: the logic thread moves an owning resource
// into the queue stamped with the submission serial that references it for
// the last time; the queue reaps (destroys) entries only once the render
// thread has signaled completion of a serial strictly greater than the
// entry's stamp, proving no in-flight submission references the resource.
//
// Template parameter T is the owning resource type (e.g. ChunkGpuSlot).
// T must be MoveConstructible (the queue takes ownership via move).
//
// Thread model: single producer (logic thread: retire/enqueue), single
// consumer (logic thread: reap, after waitFrameCompleted). No render-thread
// access. Synchronization is with the SPSC double buffer's
// completedFrameSerial(), not an internal lock -- reap is called only when
// the logic thread knows the render thread is idle (between
// waitFrameCompleted(N-1) and publishWrite(N)). retire is called during
// collectAndUploadTerrainChunks (logic thread, before publishWrite), so it
// also never races the render thread.
//
// Design invariants (guarded by
// tests/contract/gpu_resource_retire_queue_contract_tests.cpp):
//   - FIFO order: entries are destroyed in the same order they were retired.
//   - Serial gating: an entry retired at serial S is destroyed only when
//     reapCompleted(C) is called with C > S (strictly greater). C is the
//     render thread's completedFrameSerial, meaning the render thread has
//     finished a frame whose serial is past S, so no in-flight submission
//     can reference the entry's resource.
//   - Move semantics: retire takes T&&; the caller's T is left in a
//     moved-from state and must not be used again (the caller erases it).
//   - reapAll bypasses serial gating and destroys everything (shutdown only,
//     after waitGpuIdle).
//   - Memory discipline: internal container is core::pmr::Deque on
//     defaultCpuMemoryResource (AGENTS.md allocator constraint).
//
// Style follows src/core/runtime/spsc_frame_double_buffer.h: header-only,
// noexcept where possible, no owned native resources, no mutex (single-
// thread access by contract).

#include "core/atomic_capacity.h"
#include "core/cpu_allocator.h" // core::pmr::Deque, core::defaultCpuMemoryResource

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace arp::core::runtime {

// Retirement ownership counters are scalar-only and stay attached to the
// queue that owns the resources. They do not assert a workload capacity; a
// caller must first observe a complete workload before freezing one.
struct GpuResourceRetireQueueStats final {
    std::size_t pendingCount = 0u;
    std::size_t peakPendingCount = 0u;
    std::size_t totalRetired = 0u;
    std::size_t totalReaped = 0u;
};

template <typename T> class GpuResourceRetireQueue {
private:
    class CapacityLease final {
    public:
        CapacityLease() noexcept = default;

        explicit CapacityLease(std::shared_ptr<core::AtomicCapacityBudget> budget) noexcept
            : budget_(std::move(budget))
        {
            if (budget_ && budget_->tryAcquire(1u))
                acquired_ = true;
        }

        ~CapacityLease()
        {
            if (acquired_)
                budget_->release(1u);
        }

        CapacityLease(const CapacityLease&) = delete;
        CapacityLease& operator=(const CapacityLease&) = delete;

        CapacityLease(CapacityLease&& other) noexcept
            : budget_(std::move(other.budget_)), acquired_(other.acquired_)
        {
            other.acquired_ = false;
        }

        CapacityLease& operator=(CapacityLease&& other) noexcept
        {
            if (this != &other) {
                if (acquired_)
                    budget_->release(1u);
                budget_ = std::move(other.budget_);
                acquired_ = other.acquired_;
                other.acquired_ = false;
            }
            return *this;
        }

        bool acquired() const noexcept { return acquired_; }

    private:
        std::shared_ptr<core::AtomicCapacityBudget> budget_;
        bool acquired_ = false;
    };

public:
    GpuResourceRetireQueue() = default;
    ~GpuResourceRetireQueue() = default;

    GpuResourceRetireQueue(const GpuResourceRetireQueue&) = delete;
    GpuResourceRetireQueue& operator=(const GpuResourceRetireQueue&) = delete;
    GpuResourceRetireQueue(GpuResourceRetireQueue&&) noexcept = default;
    GpuResourceRetireQueue& operator=(GpuResourceRetireQueue&&) noexcept = default;

    // Installs a workload-owned, shared capacity before the queue receives
    // ownership. All queues participating in one workload use the same
    // budget so independent queue peaks cannot over-admit in aggregate.
    bool setCapacityBudget(std::shared_ptr<core::AtomicCapacityBudget> budget) noexcept
    {
        if (!entries_.empty())
            return false;
        capacityBudget_ = std::move(budget);
        return true;
    }

    // An explicit admission preflight. A rejected request is observable in
    // the shared workload ledger, while successful checks do not reserve
    // capacity or transfer ownership; the following retire() owns that
    // transaction.
    bool canRetire(const std::size_t count = 1u) noexcept
    {
        if (!capacityBudget_)
            return count != 0u;
        const auto capacity = capacityBudget_->capacity();
        const auto used = capacityBudget_->used();
        if (count != 0u && used <= capacity && count <= capacity - used)
            return true;
        // Keep the preflight non-mutating for charge and ownership, but make
        // its rejection visible through the same budget that enforces retire.
        // A malformed arithmetic request remains outside rejection telemetry,
        // matching AtomicCapacityBudget::tryAcquire().
        if (count <= std::numeric_limits<std::uint64_t>::max() - used) {
            capacityBudget_->recordCapacityRejection();
        }
        return false;
    }

    // Move `resource` into the queue, stamped with `retiredAtSerial`
    // (the submission serial that references this resource for the last
    // time). The resource will be destroyed once the render thread
    // completes a serial strictly greater than `retiredAtSerial`.
    bool retire(std::uint64_t retiredAtSerial, T&& resource)
    {
        CapacityLease lease(capacityBudget_);
        if (capacityBudget_ && !lease.acquired())
            return false;
        entries_.push_back(Entry{retiredAtSerial, std::move(lease), std::move(resource)});
        ++totalRetired_;
        peakPendingCount_ = std::max(peakPendingCount_, entries_.size());
        return true;
    }

    // Destroy all entries whose retiredAtSerial is strictly less than
    // `completedFrameSerial`. Called by the logic thread after
    // waitFrameCompleted. Returns the number of entries destroyed.
    // FIFO: only destroys a prefix of the queue (entries are retired in
    // monotonically non-decreasing serial order in practice).
    std::size_t reapCompleted(std::uint64_t completedFrameSerial)
    {
        std::size_t reaped = 0;
        while (!entries_.empty()) {
            if (entries_.front().retiredAtSerial >= completedFrameSerial) {
                // Strictly-greater rule: front not yet safe to reap. Since
                // serials are monotonically non-decreasing, no later entry
                // is safe either -- stop.
                break;
            }
            entries_.pop_front();
            ++reaped;
        }
        totalReaped_ += reaped;
        return reaped;
    }

    // Same strict serial gate as reapCompleted(), with an explicit release
    // hook for owners that maintain secondary indexes (for example, a bounded
    // GPU page-layer lease set). The hook runs while the retired resource is
    // still valid and immediately before the queue destroys it.
    template <typename ReleaseFn>
    std::size_t reapCompleted(std::uint64_t completedFrameSerial, ReleaseFn&& release)
    {
        std::size_t reaped = 0;
        while (!entries_.empty()) {
            if (entries_.front().retiredAtSerial >= completedFrameSerial) {
                break;
            }
            release(entries_.front().resource);
            entries_.pop_front();
            ++reaped;
        }
        totalReaped_ += reaped;
        return reaped;
    }

    // Destroy ALL entries immediately, ignoring serial gating. Shutdown
    // only -- caller must guarantee the render thread has been joined and
    // the GPU is idle (waitGpuIdle) before calling. Returns count destroyed.
    std::size_t reapAll()
    {
        std::size_t reaped = entries_.size();
        entries_.clear();
        totalReaped_ += reaped;
        return reaped;
    }

    // Shutdown-only equivalent of reapAll() for owners that need to release
    // secondary bookkeeping before each resource is destroyed.
    template <typename ReleaseFn> std::size_t reapAll(ReleaseFn&& release)
    {
        std::size_t reaped = 0;
        while (!entries_.empty()) {
            release(entries_.front().resource);
            entries_.pop_front();
            ++reaped;
        }
        totalReaped_ += reaped;
        return reaped;
    }

    std::size_t size() const noexcept { return entries_.size(); }

    bool empty() const noexcept { return entries_.empty(); }

    GpuResourceRetireQueueStats stats() const noexcept
    {
        return {entries_.size(), peakPendingCount_, totalRetired_, totalReaped_};
    }

private:
    struct Entry {
        std::uint64_t retiredAtSerial = 0;
        CapacityLease lease;
        T resource{};
    };

    arp::core::pmr::Deque<Entry> entries_{&arp::core::defaultCpuMemoryResource()};
    std::shared_ptr<core::AtomicCapacityBudget> capacityBudget_;
    std::size_t peakPendingCount_ = 0u;
    std::size_t totalRetired_ = 0u;
    std::size_t totalReaped_ = 0u;
};

} // namespace arp::core::runtime
