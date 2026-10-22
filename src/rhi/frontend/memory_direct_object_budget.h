#pragma once

#include "core/memory/atomic_capacity.h"

#include <cstdint>
#include <memory>

namespace arp::rhi::semantics {

// DirectObjectBudget/1 bounds L2 objects created directly through a device.
// The policy is workload-owned and deliberately has no backend or OS default.
inline constexpr std::uint32_t kDirectObjectBudgetContractVersion = 1u;

struct DirectObjectBudget {
    std::uint32_t contractVersion = kDirectObjectBudgetContractVersion;
    const char* policyId = nullptr;
    std::uint64_t pipelineObjectCapacity = 0u;
    std::uint64_t descriptorCapacity = 0u;

    bool configured() const noexcept
    {
        return policyId != nullptr || pipelineObjectCapacity != 0u || descriptorCapacity != 0u;
    }

    bool valid() const noexcept
    {
        return contractVersion == kDirectObjectBudgetContractVersion && policyId != nullptr
            && policyId[0] != '\0' && pipelineObjectCapacity != 0u && descriptorCapacity != 0u;
    }
};

struct DirectObjectBudgetStats {
    bool configured = false;
    std::uint64_t pipelineObjectCapacity = 0u;
    std::uint64_t currentPipelineObjects = 0u;
    std::uint64_t peakPipelineObjects = 0u;
    std::uint64_t pipelineRejectionCount = 0u;
    std::uint64_t descriptorCapacity = 0u;
    std::uint64_t currentDescriptors = 0u;
    std::uint64_t peakDescriptors = 0u;
    std::uint64_t descriptorRejectionCount = 0u;
};

enum class DirectObjectBudgetKind : std::uint8_t {
    PipelineLayout,
    RenderPipeline,
    ComputePipeline,
    DescriptorArena,
};

class DirectObjectBudgetLease final {
public:
    DirectObjectBudgetLease() noexcept = default;
    DirectObjectBudgetLease(
        std::shared_ptr<core::AtomicCapacityBudget> budget, std::uint64_t units) noexcept;
    ~DirectObjectBudgetLease();

    DirectObjectBudgetLease(const DirectObjectBudgetLease&) = delete;
    DirectObjectBudgetLease& operator=(const DirectObjectBudgetLease&) = delete;
    DirectObjectBudgetLease(DirectObjectBudgetLease&& other) noexcept;
    DirectObjectBudgetLease& operator=(DirectObjectBudgetLease&& other) noexcept;

private:
    void release() noexcept;

    std::shared_ptr<core::AtomicCapacityBudget> budget_;
    std::uint64_t units_ = 0u;
};

struct DirectObjectBudgetReservation {
    bool success = false;
    DirectObjectBudgetLease lease;
};

class DirectObjectBudgetTracker final {
public:
    explicit DirectObjectBudgetTracker(const DirectObjectBudget& budget) : budget_(budget)
    {
        if (budget_.valid()) {
            pipelineObjects_ =
                core::makeShared<core::AtomicCapacityBudget>(budget_.pipelineObjectCapacity);
            descriptors_ = core::makeShared<core::AtomicCapacityBudget>(budget_.descriptorCapacity);
        }
    }

    DirectObjectBudgetReservation reserve(
        DirectObjectBudgetKind kind, std::uint64_t descriptorCapacity = 0u) const noexcept;
    DirectObjectBudgetStats stats() const noexcept;

private:
    DirectObjectBudget budget_;
    std::shared_ptr<core::AtomicCapacityBudget> pipelineObjects_;
    std::shared_ptr<core::AtomicCapacityBudget> descriptors_;
};

inline DirectObjectBudgetLease::DirectObjectBudgetLease(
    std::shared_ptr<core::AtomicCapacityBudget> budget, const std::uint64_t units) noexcept
    : budget_(std::move(budget)), units_(units)
{}

inline DirectObjectBudgetLease::~DirectObjectBudgetLease()
{
    release();
}

inline DirectObjectBudgetLease::DirectObjectBudgetLease(DirectObjectBudgetLease&& other) noexcept
    : budget_(std::move(other.budget_)), units_(other.units_)
{
    other.units_ = 0u;
}

inline DirectObjectBudgetLease& DirectObjectBudgetLease::operator=(
    DirectObjectBudgetLease&& other) noexcept
{
    if (this != &other) {
        release();
        budget_ = std::move(other.budget_);
        units_ = other.units_;
        other.units_ = 0u;
    }
    return *this;
}

inline void DirectObjectBudgetLease::release() noexcept
{
    if (budget_ && units_ != 0u)
        budget_->release(units_);
    budget_.reset();
    units_ = 0u;
}

inline DirectObjectBudgetReservation DirectObjectBudgetTracker::reserve(
    const DirectObjectBudgetKind kind, const std::uint64_t descriptorCapacity) const noexcept
{
    if (!budget_.configured())
        return {true, {}};
    if (!budget_.valid())
        return {};
    const bool descriptor = kind == DirectObjectBudgetKind::DescriptorArena;
    const auto units = descriptor ? descriptorCapacity : 1u;
    auto budget = descriptor ? descriptors_ : pipelineObjects_;
    if (units == 0u || !budget || !budget->tryAcquire(units))
        return {};
    return {true, DirectObjectBudgetLease(std::move(budget), units)};
}

inline DirectObjectBudgetStats DirectObjectBudgetTracker::stats() const noexcept
{
    DirectObjectBudgetStats result;
    result.configured = budget_.valid();
    if (!result.configured)
        return result;
    result.pipelineObjectCapacity = pipelineObjects_->capacity();
    result.currentPipelineObjects = pipelineObjects_->used();
    result.peakPipelineObjects = pipelineObjects_->peak();
    result.pipelineRejectionCount = pipelineObjects_->rejectionCount();
    result.descriptorCapacity = descriptors_->capacity();
    result.currentDescriptors = descriptors_->used();
    result.peakDescriptors = descriptors_->peak();
    result.descriptorRejectionCount = descriptors_->rejectionCount();
    return result;
}

} // namespace arp::rhi::semantics
