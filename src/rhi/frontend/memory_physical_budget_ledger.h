#pragma once

#include "rhi/gpu_common.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace arp::rhi::semantics {

// PhysicalBudgetLedger/2 is the backend-neutral observation boundary for
// bounded GPU-adjacent ownership. It reports only scalar accounting. Native
// allocations, queues, descriptor pools, and completion handles remain
// backend-private.
inline constexpr std::uint32_t kPhysicalBudgetLedgerContractVersion = 2u;

enum class PhysicalBudgetCategory : std::uint8_t {
    ResourceAllocation,
    UploadStaging,
    TextureReadback,
    Descriptor,
    Command,
    PipelineCache,
    DirectPipelineObjects,
    DirectDescriptorArenas,
    Retirement,
    Count,
};

enum class PhysicalBudgetUnit : std::uint8_t {
    Bytes,
    Descriptors,
    CommandBuffers,
    PipelineEntries,
    RetainedObjects,
};

// Zero is not a statement about a category's budget. Consumers must inspect
// this state before reading scalar fields. Backends may only use Observed when
// capacity/current/peak are all sourced from the owner that enforces them.
enum class PhysicalBudgetObservationState : std::uint8_t {
    Unknown,
    Observed,
    Unsupported,
    DiagnosticFailure,
};

// Categories are not implicitly additive. A bounded diagnostic/readback
// budget can retain a GPU buffer already charged by ResourceAllocation while
// also accounting for a CPU-side copy. Consumers must preserve this relation
// instead of summing categories into a synthetic total.
enum class PhysicalBudgetChargeRelation : std::uint8_t {
    Independent,
    OverlapsResourceAllocation,
};

struct PhysicalBudgetObservation {
    PhysicalBudgetObservationState state = PhysicalBudgetObservationState::Unknown;
    PhysicalBudgetUnit unit = PhysicalBudgetUnit::Bytes;
    std::uint64_t capacity = 0u;
    std::uint64_t current = 0u;
    std::uint64_t peak = 0u;
    std::uint64_t rejectionCount = 0u;
    std::uint64_t liveCount = 0u;
    std::uint64_t retiredCount = 0u;
    PhysicalBudgetChargeRelation chargeRelation = PhysicalBudgetChargeRelation::Independent;
    const char* diagnosticId = nullptr;

    bool observed() const noexcept { return state == PhysicalBudgetObservationState::Observed; }
};

inline constexpr std::size_t physicalBudgetCategoryCount =
    static_cast<std::size_t>(PhysicalBudgetCategory::Count);

inline constexpr std::size_t physicalBudgetCategoryIndex(
    const PhysicalBudgetCategory category) noexcept
{
    return static_cast<std::size_t>(category);
}

inline constexpr bool validPhysicalBudgetObservation(
    const PhysicalBudgetObservation& observation) noexcept
{
    if (observation.state == PhysicalBudgetObservationState::Observed) {
        return observation.capacity > 0u && observation.current <= observation.peak
            && observation.peak <= observation.capacity && observation.diagnosticId == nullptr;
    }
    if (observation.state == PhysicalBudgetObservationState::Unknown) {
        return observation.capacity == 0u && observation.current == 0u && observation.peak == 0u
            && observation.rejectionCount == 0u && observation.liveCount == 0u
            && observation.retiredCount == 0u && observation.diagnosticId == nullptr;
    }
    return observation.capacity == 0u && observation.current == 0u && observation.peak == 0u
        && observation.rejectionCount == 0u && observation.liveCount == 0u
        && observation.retiredCount == 0u && observation.diagnosticId != nullptr;
}

struct PhysicalBudgetLedgerReport {
    std::uint32_t contractVersion = kPhysicalBudgetLedgerContractVersion;
    rhi::Backend backend = rhi::Backend::Null;
    std::array<PhysicalBudgetObservation, physicalBudgetCategoryCount> categories{};

    const PhysicalBudgetObservation& category(const PhysicalBudgetCategory kind) const noexcept
    {
        return categories[physicalBudgetCategoryIndex(kind)];
    }

    PhysicalBudgetObservation& category(const PhysicalBudgetCategory kind) noexcept
    {
        return categories[physicalBudgetCategoryIndex(kind)];
    }

    bool valid() const noexcept
    {
        if (contractVersion != kPhysicalBudgetLedgerContractVersion) {
            return false;
        }
        for (const auto& observation : categories) {
            if (!validPhysicalBudgetObservation(observation)) {
                return false;
            }
        }
        return true;
    }
};

inline constexpr PhysicalBudgetObservation observedPhysicalBudgetObservation(
    const PhysicalBudgetUnit unit,
    const std::uint64_t capacity,
    const std::uint64_t current,
    const std::uint64_t peak,
    const std::uint64_t rejectionCount,
    const std::uint64_t liveCount,
    const std::uint64_t retiredCount = 0u,
    const PhysicalBudgetChargeRelation chargeRelation =
        PhysicalBudgetChargeRelation::Independent) noexcept
{
    return {
        PhysicalBudgetObservationState::Observed,
        unit,
        capacity,
        current,
        peak,
        rejectionCount,
        liveCount,
        retiredCount,
        chargeRelation,
        nullptr,
    };
}

inline constexpr PhysicalBudgetObservation unavailablePhysicalBudgetObservation(
    const PhysicalBudgetUnit unit, const char* diagnosticId) noexcept
{
    return {
        PhysicalBudgetObservationState::Unsupported,
        unit,
        0u,
        0u,
        0u,
        0u,
        0u,
        0u,
        PhysicalBudgetChargeRelation::Independent,
        diagnosticId,
    };
}

} // namespace arp::rhi::semantics
