#pragma once
#include "rhi/frontend/resource_device_native_loss.h"
#include "rhi/gpu_common.h"
#include "rhi/semantics/resource_lowering.h"

#include <cstdint>

// region original_device_lifetime
namespace arp::rhi {
struct OriginalDeviceLifetimeBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxDeviceDomains = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

// Logical CPU lifetime only; resource registration, original state controls,
// native loss observation and execution are separate mandatory prerequisites.
class OriginalDeviceLifetimeContract final {
public:
    static constexpr std::uint32_t retainedOwnerCount = 2u; // Domain + epoch state.

    static constexpr const char* validateIntent(const OriginalDeviceLifetimeBudget& b) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-DEVICE-LIFETIME-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-DEVICE-LIFETIME-OPT-IN";
        if (b.maxDeviceDomains != 1u)
            return "RHI-ORIGINAL-DEVICE-LIFETIME-DOMAIN-BUDGET";
        if (b.maxRetainedOwners < retainedOwnerCount)
            return "RHI-ORIGINAL-DEVICE-LIFETIME-OWNER-BUDGET";
        if (!b.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-DEVICE-LIFETIME-CPU-BUDGET";
        return nullptr;
    }

    struct Decision {
        std::uint32_t contractVersion = 1u;
        CapabilityMode mode = CapabilityMode::DiagnosticFailure;
        bool logicalLifetimeAvailable = false;
        bool resourceIssuerAvailable = false, nativeLossObserved = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-DEVICE-LIFETIME-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        // The existing four implementations share the logical L2 base. WebGL
        // original-lifetime issuance remains closed until its adapter exists.
        // This mapping never derives a native capability from backend identity.
        return semantics::ResourceLowering::OriginalDeviceLifetime_lowering<Decision>(backend);
    }
};

// endregion original_device_lifetime

// region original_device_native_loss_observer

// Authored before the original resource registry. This separate request does
// not enlarge OriginalDeviceNativeLossBudget/1 or authorize a second handler.
struct OriginalDeviceNativeLossObserverRequest {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxObserverSlots = 0u, maxBindingOwnedObjects = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};
enum class OriginalDeviceNativeLossTerminal { NativeLost, Shutdown, Retired };

class OriginalDeviceNativeLossObserverContract final {
public:
    using Request = OriginalDeviceNativeLossObserverRequest;

    struct Requirements {
        std::uint32_t nativeRegistrations = 0u, nativeCallbacks = 0u, observerSlots = 0u,
                      bindingOwnedObjects = 0u;
        std::uint64_t cpuMetadataBytes = 0u;
    };

    static const char* inspect(const OriginalDeviceNativeLossBudget& original,
        const Request& request,
        std::uint64_t quotaBytes,
        Requirements& output) noexcept
    {
        output = {};
        if (request.contractVersion != 1u)
            return "RHI-ORIGINAL-LOSS-OBSERVER-VERSION";
        if (!request.prototypeOptIn)
            return "RHI-ORIGINAL-LOSS-OBSERVER-OPT-IN";
        if (const auto* id = OriginalDeviceNativeLossContract::validateIntent(original, quotaBytes))
            return id;
        if (original.registration.maxRegistrations
                != OriginalDeviceNativeLossContract::registrationCount
            || original.registration.maxCallbacks
                != OriginalDeviceNativeLossContract::callbackCount)
            return "RHI-ORIGINAL-LOSS-OBSERVER-SUBSCRIPTION-COUNT";
        if (!request.maxObserverSlots)
            return "RHI-ORIGINAL-LOSS-OBSERVER-SLOT-CAPACITY";
        constexpr auto originalObjects = OriginalDeviceNativeLossContract::retainedOwnerCount;
        if (request.maxObserverSlots > UINT32_MAX - originalObjects)
            return "RHI-ORIGINAL-LOSS-OBSERVER-OWNER-OVERFLOW";
        const auto objects = originalObjects + request.maxObserverSlots;
        if (objects > request.maxBindingOwnedObjects)
            return "RHI-ORIGINAL-LOSS-OBSERVER-OWNER-CAPACITY";
        if (!request.maxCpuMetadataBytes || request.maxCpuMetadataBytes != quotaBytes)
            return "RHI-ORIGINAL-LOSS-OBSERVER-QUOTA";
        output = {OriginalDeviceNativeLossContract::registrationCount,
            OriginalDeviceNativeLossContract::callbackCount,
            request.maxObserverSlots,
            objects,
            request.maxCpuMetadataBytes};
        return nullptr;
    }

    struct Decision {
        bool validatorOnly = false, bindingAvailable = false, observerIssuanceAvailable = false,
             executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-LOSS-OBSERVER-BACKEND";
        bool typedColdObserverPreparationAvailable = false;
    };

    // Independent native binding preparation is separate from authentic typed
    // command/custody receiver issuance. Request1 alone keeps generic issuance
    // and execution closed; Custody1 opt-in may prepare one cold local receiver.
    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::OriginalDeviceNativeLossObserver_lowering<Decision>(
            backend);
    }
};
} // namespace arp::rhi

// endregion original_device_native_loss_observer
