#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/compute_dispatch_validation_domain.h"
#include "rhi/frontend/compute_dispatch_validation_preparation.h"
#include "rhi/frontend/compute_dispatch_validation_session.h"

namespace arp::rhi {
class GpuDevice;
}

namespace arp::test {
class MetalDispatchValidationSessionFixture;
}

namespace arp::rhi_metal {
struct MetalRhiNativeSharedEventPayload;

// Cold backend-private validation and finite L2 coverage issuance only. No
// public execution promotion. The original callback graph outlives the facade;
// the ordinary command owner may consume an issued, completed proof.
class MetalDispatchValidationSession final : public core::SharedObjectIdentity {
    friend class arp::test::MetalDispatchValidationSessionFixture;
    struct State;

    class ConstructionKey {
        friend class MetalDispatchValidationSession;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Budget = rhi::ComputeDispatchValidationSessionBudget;

    using Quotas = rhi::ComputeDispatchValidationSessionQuotas;

    struct Observation {
        const char* diagnostic = nullptr;
        std::uint32_t checkedRows = 0u;
        bool pending = false, verified = false;
        std::uint64_t retainedNativeReservationBytes = 0u, actualNativeAllocationBytes = 0u;
    };

    MetalDispatchValidationSession(ConstructionKey, std::shared_ptr<State> state) noexcept
        : state_(std::move(state))
    {}

    ~MetalDispatchValidationSession();
    static std::shared_ptr<MetalDispatchValidationSession> prepare(rhi::GpuDevice&,
        const rhi::ComputeDispatchValidationRequest&,
        const Budget&,
        Quotas,
        const char*& diagnostic);
    static std::shared_ptr<rhi::OriginalComputeDispatchValidation> prepareL2(rhi::GpuDevice&,
        const rhi::ComputeDispatchValidationRequest&,
        const Budget&,
        Quotas,
        const char*& diagnostic);
    // Serialized cold preparation/polling, never waits or performs shader IO.
    // The initial prepare loads only the backend's build-pinned publication.
    const char* advance();
    const char* cancel() noexcept;
    Observation observe() const noexcept;
    std::shared_ptr<const rhi::ComputeDispatchValidationDomain> issueDomain(rhi::GpuDevice&,
        const rhi::ComputeDispatchValidationDomainBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        const char*& diagnostic) const;

private:
    static std::shared_ptr<rhi::OriginalComputeDispatchValidation> wrapL2(
        std::shared_ptr<MetalDispatchValidationSession>, const char*& diagnostic);
    static std::shared_ptr<MetalDispatchValidationSession> prepareImpl(rhi::GpuDevice&,
        const rhi::ComputeDispatchValidationRequest&,
        const Budget&,
        Quotas,
        std::shared_ptr<MetalRhiNativeSharedEventPayload> gate,
        const char*&);
    std::weak_ptr<void> retainedGraphForTest() const noexcept;
    bool corruptCompletedOutputForTest() noexcept;
    const std::shared_ptr<State> state_;
};
} // namespace arp::rhi_metal
