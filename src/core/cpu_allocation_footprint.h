#pragma once

#include "core/cpu_allocator.h"
#include "core/memory_budget_ledger.h"

namespace arp::core {

// Cold-only inspection of distinct, still-owned allocations. The caller lists
// each allocation once; interior addresses authenticate its original charge.
class CpuAllocationFootprint final {
public:
    explicit CpuAllocationFootprint(const std::shared_ptr<CpuMemoryQuota>& quota) noexcept
        : quota_(quota)
    {}

    void add(const void* address) noexcept
    {
        if (!address || diagnostic_)
            return;
        const auto info = inspectCpuAllocationContaining(address);
        if (!info.found || !cpuAllocationOverlapsQuotaOwner(info.allocationIdentity, quota_)) {
            diagnostic_ = "CORE-FOOTPRINT-QUOTA-OWNER";
            return;
        }
        if (!MemoryBudgetLedger::canAdd(bytes_, info.sizeBytes)) {
            diagnostic_ = "CORE-FOOTPRINT-OVERFLOW";
            return;
        }
        bytes_ += info.sizeBytes;
    }

    const char* diagnostic() const noexcept { return diagnostic_; }

    std::uint64_t bytes() const noexcept { return diagnostic_ ? 0u : bytes_; }

private:
    const std::shared_ptr<CpuMemoryQuota> quota_;
    std::uint64_t bytes_ = 0u;
    const char* diagnostic_ = nullptr;
};

} // namespace arp::core
