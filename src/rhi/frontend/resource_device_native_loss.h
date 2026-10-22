#pragma once
#include "core/ownership/quota_bound_shared_identity.h"
#include "rhi/frontend/resource_device_loss_registration.h"
#include "rhi/semantics/resource_lowering.h"

namespace arp::rhi {
// NativeResultLoss/1: observation of actual native call/completion results.
// One device/source subscription, zero foreign callbacks. This separate opt-in
// does not borrow the promise/callback budget of OriginalNativeLoss/1.
// A source may consume an already-budgeted command completed handler. It creates
// no additional handler and retains no command; only genuine terminal native
// results may deliver the receiver's atomic terminal notification.
struct OriginalDeviceNativeResultLossBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint32_t maxSourceSubscriptions = 0u, maxRetainedOwners = 0u;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class OriginalDeviceNativeResultLossContract final {
public:
    static constexpr std::uint32_t sourceSubscriptionCount = 1u, retainedOwnerCount = 2u;

    static constexpr const char* validateIntent(
        const OriginalDeviceNativeResultLossBudget& b, std::uint64_t quotaBytes) noexcept
    {
        if (b.contractVersion != 1u)
            return "RHI-ORIGINAL-RESULT-LOSS-VERSION";
        if (!b.prototypeOptIn)
            return "RHI-ORIGINAL-RESULT-LOSS-OPT-IN";
        if (b.maxSourceSubscriptions != sourceSubscriptionCount)
            return "RHI-ORIGINAL-RESULT-LOSS-SOURCE-CAPACITY";
        if (b.maxRetainedOwners < retainedOwnerCount)
            return "RHI-ORIGINAL-RESULT-LOSS-OWNER-CAPACITY";
        if (!b.maxCpuMetadataBytes || b.maxCpuMetadataBytes != quotaBytes)
            return "RHI-ORIGINAL-RESULT-LOSS-QUOTA";
        return nullptr;
    }

    struct Decision {
        bool bindingAvailable = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-RESULT-LOSS-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::OriginalDeviceNativeResultLoss_lowering<Decision>(
            backend);
    }
};

struct OriginalDeviceNativeLossBudget {
    DeviceLossRegistrationBudget registration;
    std::uint32_t maxRetainedOwners = 0u;
};

class OriginalDeviceNativeLossContract final {
public:
    static constexpr std::uint32_t retainedOwnerCount = 1u, registrationCount = 1u,
                                   callbackCount = 1u;

    static const char* validateIntent(
        const OriginalDeviceNativeLossBudget& budget, std::uint64_t quotaBytes) noexcept
    {
        if (const auto* id =
                DeviceLossRegistrationContract::validateBudget(budget.registration, quotaBytes))
            return id;
        return budget.maxRetainedOwners < retainedOwnerCount
            ? "RHI-ORIGINAL-DEVICE-LOSS-OWNER-CAPACITY"
            : nullptr;
    }

    struct Decision {
        bool nativePrototype = false, validatorOnly = false, executionAvailable = false;
        const char* diagnostic = "RHI-ORIGINAL-DEVICE-LOSS-BACKEND-UNKNOWN";
    };

    static constexpr Decision lowering(Backend backend) noexcept
    {
        return semantics::ResourceLowering::OriginalDeviceNativeLoss_lowering<Decision>(backend);
    }
};

// The exact device owns this backend-private subscription through L2 diagnostics.
// No native handle/state or callback receiver is exposed to upper layers.
class OriginalDeviceNativeLossBinding : public core::QuotaBoundSharedObjectIdentity {
public:
    virtual ~OriginalDeviceNativeLossBinding() = default;
    virtual const char* diagnostic() const noexcept = 0;
    virtual const char* detach() noexcept = 0;

    // Logical terminal hooks never prove native loss or GPU completion. Legacy
    // subscriptions preserve their detach-on-shutdown behavior and layout.
    virtual void retireObservers() noexcept {}

    virtual const char* shutdown() noexcept { return detach(); }

    virtual const char* terminalDiagnostic() const noexcept { return nullptr; }
};
} // namespace arp::rhi
