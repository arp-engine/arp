#pragma once

// GPU memory budget accounting shared by backend-owned allocators. This L2
// metadata owns budget rejection/telemetry semantics, not native allocations.
// Historical core-qualified type names are retained for API stability.

#include "core/math/unsigned_range.h"
#include "core/memory/cpu_allocator.h"

#include <cstdint>
#include <limits>

namespace arp::core {

struct MemoryBudgetLedgerDesc {
    std::uint64_t memoryBudgetBytes = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
};

struct MemoryBudgetLedgerStats {
    std::uint64_t allocatedBytes = 0;
    std::uint64_t peakAllocatedBytes = 0;
    std::uint64_t budgetRejectionCount = 0;
    std::uint32_t liveResourceCount = 0;
    std::uint64_t memoryBudgetBytes = 0;
    bool backendFallbacksHiddenFromUpperLayer = true;
};

// Pure budget account. Tracks live allocations against a fixed budget with
// overflow-safe arithmetic. Does not touch native allocations or rhi types —
// backend allocators own resource creation and policy mapping, this ledger
// only answers "can I fit sizeBytes in the remaining budget?" and keeps the
// books. Thread-safety: none (backends call from their own single-threaded
// allocation path, matching the pre-existing stats_ usage).
class MemoryBudgetLedger {
public:
    MemoryBudgetLedger() = default;

    explicit MemoryBudgetLedger(MemoryBudgetLedgerDesc desc) noexcept : desc_(desc) {}

    const MemoryBudgetLedgerDesc& desc() const noexcept { return desc_; }

    // Overflow-safe addition check. Equivalent to the per-backend canAdd(lhs,rhs)
    // helper previously duplicated in vulkan_allocator.cpp, metal_allocator.cpp
    // and webgpu_allocator.cpp.
    static bool canAdd(std::uint64_t lhs, std::uint64_t rhs) noexcept
    {
        return canAddUnsigned(lhs, rhs);
    }

    // Returns true if sizeBytes fits within the remaining budget without
    // arithmetic overflow. Caller is still responsible for rejecting
    // sizeBytes == 0 separately (zero-size is a descriptor validation error,
    // not a budget error).
    bool canAllocate(std::uint64_t sizeBytes) const noexcept
    {
        if (!canAdd(allocatedBytes_, sizeBytes)) {
            return false;
        }
        return allocatedBytes_ + sizeBytes <= desc_.memoryBudgetBytes;
    }

    // Records a successful allocation. Caller must have validated sizeBytes > 0
    // and canAllocate(sizeBytes) first; this method does not re-check (matching
    // the pre-existing backend pattern where the budget check and the book
    // update are separated by policy/size assignment).
    void recordAllocation(std::uint64_t sizeBytes) noexcept
    {
        allocatedBytes_ += sizeBytes;
        if (allocatedBytes_ > peakAllocatedBytes_) {
            peakAllocatedBytes_ = allocatedBytes_;
        }
        ++liveResourceCount_;
    }

    // This is deliberately narrower than a generic allocation failure. It
    // records only a request rejected by this ledger's fixed capacity; native
    // allocation failures retain their backend-specific diagnostic and must
    // not be misrepresented as a budget rejection.
    void recordBudgetRejection() noexcept { ++budgetRejectionCount_; }

    // A caller retaining the original allocation identity may return unused
    // reservation bytes without releasing/reacquiring its live allocation.
    // The allocation count and historical reservation peak remain unchanged.
    bool shrinkAllocation(std::uint64_t previousBytes, std::uint64_t actualBytes) noexcept
    {
        if (!previousBytes || !actualBytes || actualBytes > previousBytes || !liveResourceCount_
            || previousBytes > allocatedBytes_)
            return false;
        allocatedBytes_ -= previousBytes - actualBytes;
        return true;
    }

    // Releases a previously recorded allocation. Clamps at zero so a mismatched
    // release (e.g. double-free) cannot underflow. Vulkan's
    // VulkanRhiAllocationReleaseToken destructor previously inlined this same
    // logic; Metal/WebGPU had no release path (allocations were never freed),
    // but exposing it here lets them add one without re-deriving the arithmetic.
    void releaseAllocation(std::uint64_t sizeBytes) noexcept
    {
        if (allocatedBytes_ >= sizeBytes) {
            allocatedBytes_ -= sizeBytes;
        } else {
            allocatedBytes_ = 0;
        }
        if (liveResourceCount_ > 0) {
            --liveResourceCount_;
        }
    }

    void resetBudget(std::uint64_t memoryBudgetBytes) noexcept
    {
        desc_.memoryBudgetBytes = memoryBudgetBytes;
    }

    MemoryBudgetLedgerStats stats() const noexcept
    {
        MemoryBudgetLedgerStats s;
        s.allocatedBytes = allocatedBytes_;
        s.peakAllocatedBytes = peakAllocatedBytes_;
        s.budgetRejectionCount = budgetRejectionCount_;
        s.liveResourceCount = liveResourceCount_;
        s.memoryBudgetBytes = desc_.memoryBudgetBytes;
        s.backendFallbacksHiddenFromUpperLayer = desc_.backendFallbacksHiddenFromUpperLayer;
        return s;
    }

private:
    MemoryBudgetLedgerDesc desc_;
    std::uint64_t allocatedBytes_ = 0;
    std::uint64_t peakAllocatedBytes_ = 0;
    std::uint64_t budgetRejectionCount_ = 0;
    std::uint32_t liveResourceCount_ = 0;
};

} // namespace arp::core
