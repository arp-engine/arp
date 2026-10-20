#pragma once
#include "rhi/gpu_common.h"
#include "rhi/semantics/resource_lowering.h"

#include <cstdint>

namespace arp::rhi {
// OriginalDeviceLossRegistration/1: explicit Prototype lifetime subscription.
// Count ceilings cover one native promise registration and its one callback;
// C++ owner/envelope/control/pool allocations use the exact Device quota.
struct DeviceLossRegistrationBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxRegistrations = 0u, maxCallbacks = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class DeviceLossRegistrationContract final {
public:
    static const char* validateBudget(
        const DeviceLossRegistrationBudget& b, std::uint64_t deviceQuotaBytes) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-DEVICE-LOSS-REGISTRATION-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-DEVICE-LOSS-REGISTRATION-OPT-IN";
        if (!b.maxRegistrations || !b.maxCallbacks)
            return "RHI-DEVICE-LOSS-REGISTRATION-COUNT";
        if (!b.maxCpuMetadataBytes || b.maxCpuMetadataBytes != deviceQuotaBytes)
            return "RHI-DEVICE-LOSS-REGISTRATION-QUOTA";
        return nullptr;
    }

    static bool sameBudget(
        const DeviceLossRegistrationBudget& a, const DeviceLossRegistrationBudget& b) noexcept
    {
        return a.contractVersion == b.contractVersion && a.prototypeOptIn == b.prototypeOptIn
            && a.maxRegistrations == b.maxRegistrations && a.maxCallbacks == b.maxCallbacks
            && a.maxCpuMetadataBytes == b.maxCpuMetadataBytes;
    }

    struct Lowering {
        bool nativePrototype = false, validatorOnly = false;
        const char* diagnostic = "RHI-DEVICE-LOSS-REGISTRATION-BACKEND";
    };

    static constexpr Lowering lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::DeviceLossRegistration_lowering<Lowering>(backend);
    }

    static const char* validateDetachment(
        std::uint64_t outstandingGraphs, bool quarantined, bool actualLoss) noexcept
    {
        if (outstandingGraphs)
            return "RHI-DEVICE-LOSS-REGISTRATION-PENDING-GRAPHS";
        if (quarantined && !actualLoss)
            return "RHI-DEVICE-LOSS-REGISTRATION-QUARANTINED";
        return nullptr;
    }
};
} // namespace arp::rhi
