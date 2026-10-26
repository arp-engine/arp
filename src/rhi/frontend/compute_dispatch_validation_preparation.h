#pragma once

#include "rhi/frontend/compute_dispatch_validation_domain.h"
#include "rhi/frontend/compute_dispatch_validation_session.h"

namespace arp::rhi_metal {
class MetalDispatchValidationSession;
}

namespace arp::rhi {
class GpuDevice;

// OriginalComputeDispatchValidation/1. Backend-neutral ownership of the exact
// authenticated cold session. This is not a second executor or a command stream.
// Only the private native session can construct it; a virtual device hook cannot
// turn public progress/observations or a caller object into a successful proof.
class OriginalComputeDispatchValidation final : public core::SharedObjectIdentity {
    friend class GpuDevice;
    friend class arp::rhi_metal::MetalDispatchValidationSession;

    class ConstructionKey {
        friend class arp::rhi_metal::MetalDispatchValidationSession;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    struct Observation {
        const char* diagnostic = nullptr;
        std::uint32_t checkedRows = 0u;
        bool pending = false, verified = false;
    };

private:
    struct Operations {
        const char* (*advance)(void*);
        const char* (*cancel)(void*) noexcept;
        Observation (*observe)(const void*) noexcept;
        std::shared_ptr<const ComputeDispatchValidationDomain> (*issue)(const void*,
            GpuDevice&,
            const ComputeDispatchValidationDomainBudget&,
            std::shared_ptr<core::CpuMemoryQuota>,
            const char*&);
    };

public:
    OriginalComputeDispatchValidation(ConstructionKey,
        OriginalDeviceLifetime::Ticket lifetime,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::shared_ptr<void> session,
        Operations operations) noexcept
        : lifetime_(std::move(lifetime)), quota_(std::move(quota)), session_(std::move(session)),
          operations_(operations)
    {}

    OriginalComputeDispatchValidation(const OriginalComputeDispatchValidation&) = delete;
    OriginalComputeDispatchValidation& operator=(const OriginalComputeDispatchValidation&) = delete;

    // Serialized cold polling. It can prepare the next bounded validation row;
    // it never waits or loads/compiles shaders. Observation and cancel allocate0.
    const char* advance() { return operations_.advance(session_.get()); }

    const char* cancel() noexcept { return operations_.cancel(session_.get()); }

    Observation observe() const noexcept { return operations_.observe(session_.get()); }

private:
    const OriginalDeviceLifetime::Ticket lifetime_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    const std::shared_ptr<void> session_;
    const Operations operations_;
};
} // namespace arp::rhi
