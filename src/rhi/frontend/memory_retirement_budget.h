#pragma once

#include "core/memory/atomic_capacity.h"

#include <cstdint>
#include <memory>

namespace arp::rhi::semantics {

// RetirementBudget/1 bounds completion-gated resource ownership. The policy
// is workload-owned: it never selects a backend or implies native completion.
inline constexpr std::uint32_t kRetirementBudgetContractVersion = 1u;

struct RetirementBudget {
    std::uint32_t contractVersion = kRetirementBudgetContractVersion;
    const char* policyId = nullptr;
    std::uint64_t retainedObjectCapacity = 0u;

    bool configured() const noexcept { return policyId != nullptr || retainedObjectCapacity != 0u; }

    bool valid() const noexcept
    {
        return contractVersion == kRetirementBudgetContractVersion && policyId != nullptr
            && policyId[0] != '\0' && retainedObjectCapacity != 0u;
    }
};

struct RetirementBudgetStats {
    bool configured = false;
    std::uint64_t capacity = 0u;
    std::uint64_t current = 0u;
    std::uint64_t peak = 0u;
    std::uint64_t rejectionCount = 0u;
};

class RetirementBudgetTracker final {
public:
    explicit RetirementBudgetTracker(const RetirementBudget& budget) : budget_(budget)
    {
        if (budget_.valid()) {
            capacity_ =
                core::makeShared<core::AtomicCapacityBudget>(budget_.retainedObjectCapacity);
        }
    }

    bool configured() const noexcept { return budget_.valid(); }

    const std::shared_ptr<core::AtomicCapacityBudget>& capacityBudget() const noexcept
    {
        return capacity_;
    }

    RetirementBudgetStats stats() const noexcept
    {
        if (!capacity_)
            return {};
        return {true,
            capacity_->capacity(),
            capacity_->used(),
            capacity_->peak(),
            capacity_->rejectionCount()};
    }

private:
    RetirementBudget budget_;
    std::shared_ptr<core::AtomicCapacityBudget> capacity_;
};

} // namespace arp::rhi::semantics
