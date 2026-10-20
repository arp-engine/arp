#pragma once
#include "rhi/gpu_common.h"

#include <array>
#include <cstdint>

namespace arp::rhi::semantics {
// Independent ceilings. Snapshot/fault bytes overlap ResourceBytes and must
// never be added to it. These are reservations, not device-memory queries.
enum class BindlessWorkloadCapacityDomain : std::uint8_t {
    Workloads,
    Snapshots,
    SnapshotBytes,
    FaultBytes,
    Resources,
    ResourceBytes,
    RetainedOwners,
    Count
};
inline constexpr auto bindlessWorkloadCapacityDomainCount =
    static_cast<std::size_t>(BindlessWorkloadCapacityDomain::Count);
using BindlessWorkloadCapacityAmounts =
    std::array<std::uint64_t, bindlessWorkloadCapacityDomainCount>;

struct BindlessWorkloadCapacityBudget {
    std::uint32_t contractVersion = 0u;
    bool optIn = false;
    BindlessWorkloadCapacityAmounts limits{};
};

class BindlessWorkloadCapacityContract final {
public:
    struct Decision {
        bool reservationAvailable = false;
        bool validatorOnly = false;
        // Reservation/inspection never grants command execution.
        bool executionAvailable = false;
        const char* diagnostic = "RHI-BINDLESS-WORKLOAD-CAPACITY-BACKEND";
    };

    static constexpr Decision lowering(Backend backend, std::uint32_t version = 1u) noexcept
    {
        if (version != 1u)
            return {false, false, false, "RHI-BINDLESS-WORKLOAD-CAPACITY-VERSION"};
        switch (backend) {
        case Backend::Null:
            return {true, true, false, nullptr};
        case Backend::Vulkan:
        case Backend::Metal:
        case Backend::WebGPU:
            return {true, false, false, nullptr};
        default:
            return {};
        }
    }

    static constexpr const char* validate(const BindlessWorkloadCapacityBudget& budget) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-BINDLESS-WORKLOAD-CAPACITY-VERSION";
        if (!budget.optIn)
            return "RHI-BINDLESS-WORKLOAD-CAPACITY-OPT-IN";
        return budget.limits[0u] ? nullptr : "RHI-BINDLESS-WORKLOAD-CAPACITY-WORKLOAD-LIMIT";
    }
};
} // namespace arp::rhi::semantics
