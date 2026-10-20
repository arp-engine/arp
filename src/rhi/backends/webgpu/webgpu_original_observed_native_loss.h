#pragma once
#include "core/runtime/bounded_terminal_observers.h"
#include "rhi/backends/webgpu/webgpu_original_device_native_loss.h"
#include "rhi/frontend/resource_device_validation.h"

namespace arp::test {
class OriginalObservedNativeLossBindingFixture;
}

namespace arp::rhi_webgpu {
// Independently admitted original subscription storage. No receiver issuance
// exists yet: only an authentic typed custody bridge may eventually attach.
class WebGpuOriginalObservedNativeLoss final : public WebGpuOriginalDeviceNativeLoss {
    friend class WebGpuCanonicalCallbackCustody;
    friend class arp::test::OriginalObservedNativeLossBindingFixture;
    using Contract = rhi::OriginalDeviceNativeLossObserverContract;
    using Terminal = rhi::OriginalDeviceNativeLossTerminal;
    using Registry = core::runtime::BoundedTerminalObservers<Terminal>;

    class ConstructionKey {
        friend class WebGpuOriginalObservedNativeLoss;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    WebGpuOriginalObservedNativeLoss(ConstructionKey,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& original,
        const Contract::Request& request)
        : WebGpuOriginalDeviceNativeLoss(
              WebGpuOriginalDeviceNativeLoss::ConstructionKey{}, receiver, original),
          request_(request), observers_(request.maxObserverSlots)
    {}

    static std::shared_ptr<rhi::OriginalDeviceNativeLossBinding> prepare(void* device,
        const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
        const rhi::OriginalDeviceNativeLossBudget& original,
        const Contract::Request& request,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        const char*& diagnostic)
    {
        Contract::Requirements required;
        if ((diagnostic = Contract::inspect(
                 original, request, quota ? quota->limitBytes() : 0u, required)))
            return {};
        if ((diagnostic = receiver.diagnostic()))
            return {};
        if (!device) {
            diagnostic = "RHI-ORIGINAL-DEVICE-LOSS-NATIVE-SOURCE-UNAVAILABLE";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            auto binding = shareOriginal(core::makeUnique<WebGpuOriginalObservedNativeLoss>(
                ConstructionKey{}, receiver, original, request));
            if (!binding->observers_.accepting()) {
                diagnostic = "RHI-ORIGINAL-LOSS-OBSERVER-IDENTITY-EXHAUSTED";
                return {};
            }
            if ((diagnostic = publishNative(device, binding)))
                return {};
            return binding;
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-LOSS-OBSERVER-ALLOCATION";
            return {};
        } catch (const std::length_error&) {
            diagnostic = "RHI-ORIGINAL-LOSS-OBSERVER-ALLOCATION";
            return {};
        }
    }

    const char* diagnostic() const noexcept override
    {
        if (const auto* id = WebGpuOriginalDeviceNativeLoss::diagnostic())
            return id;
        return !originalMetadataQuota()
                || originalMetadataQuota()->limitBytes() != request_.maxCpuMetadataBytes
            ? "RHI-ORIGINAL-LOSS-OBSERVER-QUOTA"
            : nullptr;
    }

    void retireObservers() noexcept override
    {
        const auto lease = holdNativeLease();
        if (observers_.pending()) {
            retainLogicalTerminal(Terminal::Retired);
            return;
        }
        notify(Terminal::Retired);
    }

    const char* shutdown() noexcept override
    {
        // Keep the exact binding alive through all local receiver/destructor
        // reentry. Failed native suppression deliberately keeps the self-lease.
        const auto lease = holdNativeLease();
        if (observers_.pending()) {
            retainLogicalTerminal(Terminal::Shutdown);
            return "RHI-ORIGINAL-LOSS-OBSERVER-SUBSCRIPTION-RETAINED";
        }
        if (!nativeBorrowArmed()) {
            notify(Terminal::Shutdown);
            return nullptr;
        }
        const auto* id = suppressNativeBorrow();
        if (id && !terminalDiagnostic_)
            terminalDiagnostic_ = id;
        notify(Terminal::Shutdown);
        return id;
    }

    const char* terminalDiagnostic() const noexcept override { return terminalDiagnostic_; }

private:
    void retainLogicalTerminal(Terminal terminal) const noexcept
    {
        const auto status = observers_.closeAdmission(terminal);
        if (status != Registry::Status::Complete && status != Registry::Status::Closed
            && !terminalDiagnostic_)
            terminalDiagnostic_ = "RHI-ORIGINAL-LOSS-OBSERVER-TERMINAL-INVARIANT";
    }

    // Called only after authentic local suppression has returned and released
    // its operation guard. Never suppress a still-needed physical loss borrow.
    void finishDeferredTerminal() const noexcept
    {
        if (observers_.pending() || !observers_.terminal() || observers_.delivered())
            return;
        const auto lease = holdNativeLease();
        const auto terminal = *observers_.terminal();
        if (nativeBorrowArmed())
            if (const auto* id = suppressNativeBorrow()) {
                if (!terminalDiagnostic_)
                    terminalDiagnostic_ = id;
                return;
            }
        notify(terminal);
    }

    const char* beforeDetach() const noexcept override
    {
        return observers_.pending() ? "RHI-ORIGINAL-LOSS-OBSERVER-LIVE-DETACH" : nullptr;
    }

    void afterDetach() noexcept override { notify(Terminal::Retired); }

    void afterNativeLoss() noexcept override { notify(Terminal::NativeLost); }

    void notify(Terminal terminal) const noexcept
    {
        const auto status = observers_.deliver(terminal);
        if (status != Registry::Status::Complete && status != Registry::Status::Closed
            && !terminalDiagnostic_)
            terminalDiagnostic_ = "RHI-ORIGINAL-LOSS-OBSERVER-TERMINAL-INVARIANT";
    }

    const Contract::Request request_;
    mutable Registry observers_;
    mutable const char* terminalDiagnostic_ = nullptr;
};

inline std::shared_ptr<rhi::OriginalDeviceNativeLossBinding>
WebGpuOriginalDeviceLossSource::prepareObserved(
    const rhi::OriginalDeviceLifetime::NativeLossReceiver& receiver,
    const rhi::OriginalDeviceNativeLossBudget& original,
    const rhi::OriginalDeviceNativeLossObserverRequest& request,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const char*& diagnostic) const
{
    return WebGpuOriginalObservedNativeLoss::prepare(
        device_, receiver, original, request, std::move(quota), diagnostic);
}
} // namespace arp::rhi_webgpu
