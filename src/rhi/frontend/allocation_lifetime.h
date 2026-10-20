#pragma once

#include "rhi/frontend/resource_registration.h"

namespace arp::render_graph {
class FrameGraphResourcePool;
}

namespace arp::rhi {

// CPU accounting custody only. The original pool is the only receipt issuer.
// The weak registration observes factory identity without retaining a wrapper.
class OriginalPoolAllocationReceipt {
public:
    class ConstructionKey {
        friend class arp::render_graph::FrameGraphResourcePool;
        ConstructionKey() = default;
    };

    virtual ~OriginalPoolAllocationReceipt() = default;
    OriginalPoolAllocationReceipt(const OriginalPoolAllocationReceipt&) = delete;
    OriginalPoolAllocationReceipt& operator=(const OriginalPoolAllocationReceipt&) = delete;

    bool nativeExecutionProven() const noexcept { return false; }

protected:
    OriginalPoolAllocationReceipt(
        ConstructionKey, OriginalResourceRegistrationTicket registration, const void* pool) noexcept
        : registration_(std::move(registration)), pool_(pool)
    {}

private:
    friend class GpuDevice;
    friend class arp::render_graph::FrameGraphResourcePool;
    const OriginalResourceRegistrationTicket registration_;
    const void* const pool_;
};

// One fixed, non-rebindable owner slot in the original allocation charge token
// or logical placement lease. It grants no native execution permission.
class OriginalAllocationLifetime {
public:
    OriginalAllocationLifetime() = default;

    bool hasPoolCharge() const noexcept { return bool(poolCharge_); }

    OriginalAllocationLifetime(const OriginalAllocationLifetime&) = delete;
    OriginalAllocationLifetime& operator=(const OriginalAllocationLifetime&) = delete;

protected:
    ~OriginalAllocationLifetime() = default;

private:
    friend class GpuDevice;
    std::shared_ptr<const OriginalPoolAllocationReceipt> poolCharge_;
};

} // namespace arp::rhi
