#pragma once
#include "rhi/frontend/resource_device_lifetime.h"

#include <optional>

namespace arp::rhi::common {
// Embedded weak terminal checkpoint. Sole binding authority is the genuine
// original factory publisher; no second state owner or resource inventory.
class OriginalRecordedStateDomain final {
    friend class rhi::GpuDevice;

public:
    OriginalRecordedStateDomain() = default;
    OriginalRecordedStateDomain(const OriginalRecordedStateDomain&) = delete;

    const char* diagnostic() const noexcept { return ticket_ ? ticket_->diagnostic() : nullptr; }

private:
    const char* bind(const OriginalDeviceLifetime::Ticket& ticket) const noexcept
    {
        if (ticket_)
            return "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-ALREADY-BOUND";
        if (const auto* id = ticket.diagnostic())
            return id;
        ticket_.emplace(ticket);
        return nullptr;
    }

    mutable std::optional<OriginalDeviceLifetime::Ticket> ticket_;
};
} // namespace arp::rhi::common
