#pragma once
#include "core/ownership/lifetime_epoch.h"
#include "core/ownership/shared_owner_projection.h"
#include "rhi/frontend/resource_device_validation.h"

#include <atomic>

namespace arp::rhi {
class GpuDevice;

// Only an original L2 device can construct or invalidate this domain. Tickets
// observe it weakly and cannot retain the device or issue resource authority.
// Cold preparation, ticket observation and retirement are externally serialized.
// Native result delivery may race them: it publishes only a terminal atom and
// never mutates the externally serialized epoch or retains the device facade.
class OriginalDeviceLifetime final {
    friend class GpuDevice;

    class ConstructionKey {
        friend class GpuDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    class NativeLossReceiver final {
        friend class GpuDevice;

        explicit NativeLossReceiver(const std::shared_ptr<OriginalDeviceLifetime>& domain) noexcept
            : domain_(domain)
        {}

    public:
        NativeLossReceiver(const NativeLossReceiver&) = default;

        const char* diagnostic() const noexcept
        {
            const auto domain = domain_.lock();
            return domain ? domain->validate() : "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED";
        }

        void deliverNativeLoss() const noexcept
        {
            if (const auto domain = domain_.lock()) {
                auto expected = Terminal::None;
                domain->terminal_.compare_exchange_strong(expected,
                    Terminal::NativeLost,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire);
            }
        }

    private:
        const std::weak_ptr<OriginalDeviceLifetime> domain_;
    };

    class Ticket final {
        friend class GpuDevice;

    public:
        Ticket() = default;

        const char* diagnostic() const noexcept
        {
            const auto domain = domain_.lock();
            if (!domain)
                return "RHI-ORIGINAL-DEVICE-LIFETIME-EXPIRED";
            if (const auto* id = domain->validate())
                return id;
            return epoch_.isCurrent() ? nullptr : "RHI-ORIGINAL-DEVICE-LIFETIME-STALE";
        }

        bool nativeExecutionProven() const noexcept { return false; }

    private:
        Ticket(const std::shared_ptr<OriginalDeviceLifetime>& domain) noexcept
            : domain_(domain), epoch_(domain->epoch_.ticket())
        {}

        std::weak_ptr<OriginalDeviceLifetime> domain_;
        core::LifetimeEpoch::Ticket epoch_;
    };

    OriginalDeviceLifetime(ConstructionKey,
        const GpuDevice* device,
        OriginalDeviceLifetimeBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota)
        : device_(device), budget_(budget), quota_(std::move(quota))
    {}

    OriginalDeviceLifetime(const OriginalDeviceLifetime&) = delete;
    OriginalDeviceLifetime& operator=(const OriginalDeviceLifetime&) = delete;

    bool overlapsCpuQuota(const std::shared_ptr<core::CpuMemoryQuota>& quota) const noexcept
    {
        return core::SharedOwnerProjection::overlapsOwner(quota_, quota);
    }

private:
    enum class Terminal { None, Retired, Shutdown, NativeLost };

    const char* validate() const noexcept
    {
        switch (terminal_.load(std::memory_order_acquire)) {
        case Terminal::Retired:
            return "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED";
        case Terminal::Shutdown:
            return "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN";
        case Terminal::NativeLost:
            return "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST";
        case Terminal::None:
            break;
        }
        if (!quota_ || quota_->limitBytes() != budget_.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-DEVICE-LIFETIME-QUOTA";
        return epoch_.isActive() ? nullptr : "RHI-ORIGINAL-DEVICE-LIFETIME-EPOCH-EXHAUSTED";
    }

    void invalidate(Terminal reason) noexcept
    {
        auto expected = Terminal::None;
        if (!terminal_.compare_exchange_strong(
                expected, reason, std::memory_order_acq_rel, std::memory_order_acquire))
            return;
        epoch_.advance();
    }

    const GpuDevice* const device_;
    const OriginalDeviceLifetimeBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    core::LifetimeEpoch epoch_;
    std::atomic<Terminal> terminal_{Terminal::None};
};
} // namespace arp::rhi
