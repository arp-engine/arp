#pragma once

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/compute_dispatch_validation.h"
#include "rhi/frontend/resource_device_lifetime.h"
#include "rhi/frontend/resource_loss_validation.h"

namespace arp::rhi {
class GpuDevice;

struct ComputeDispatchValidationDomainBudget {
    std::uint32_t contractVersion = 0u;
    bool prototypeOptIn = false;
    std::uint64_t maxCpuMetadataBytes = 0u;
};

class ComputeDispatchValidationDomainContract final {
public:
    static constexpr const char* inspect(
        const ComputeDispatchValidationDomainBudget& budget, std::uint64_t quotaBytes) noexcept
    {
        if (budget.contractVersion != 1u)
            return "RHI-DISPATCH-DOMAIN-VERSION";
        if (!budget.prototypeOptIn)
            return "RHI-DISPATCH-DOMAIN-OPT-IN";
        return budget.maxCpuMetadataBytes && budget.maxCpuMetadataBytes == quotaBytes
            ? nullptr
            : "RHI-DISPATCH-DOMAIN-QUOTA";
    }

    // Eligibility only; neither public facts nor a decision can mint a domain.
    static constexpr const char* issuanceDiagnostic(Backend backend) noexcept
    {
        switch (backend) {
        case Backend::Null:
            return "RHI-DISPATCH-DOMAIN-VALIDATOR-ONLY";
        case Backend::Vulkan:
        case Backend::WebGPU:
            return "RHI-DISPATCH-DOMAIN-QUERIED-LIMITS";
        case Backend::Metal:
            return nullptr;
        case Backend::WebGL:
            return "RHI-DISPATCH-DOMAIN-UNSUPPORTED";
        }
        return "RHI-DISPATCH-DOMAIN-BACKEND";
    }
};

// Device-issued finite coverage evidence, not a command or a generic limit.
// Cold issuance retains the backend's successful CPU proof under its original
// quotas, plus this independently charged owner. No native payload is exposed.
// Issuance and session polling remain owner-thread operations. Retained proof
// validation permits a serialized handoff to native completion, which consumes
// only the published successful proof and original dependency revocations.
// Domain queries and lifetime/epoch changes still require external serialization.
class ComputeDispatchValidationDomain final : public core::SharedObjectIdentity {
    friend class GpuDevice;
    friend class OriginalComputeDispatchRecordingAdmission;
    using ProofDiagnostic = const char* (*)(const void*) noexcept;

    class ConstructionKey {
        friend class GpuDevice;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    ComputeDispatchValidationDomain(ConstructionKey,
        ComputeDispatchValidationRequest request,
        OriginalDeviceLifetime::Ticket lifetime,
        OriginalDeviceNativeLossObservation loss,
        ComputeDispatchValidationDomainBudget budget,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::shared_ptr<const void> proof,
        ProofDiagnostic proofDiagnostic) noexcept
        : request_(request), lifetime_(std::move(lifetime)), loss_(std::move(loss)),
          budget_(budget), quota_(std::move(quota)), proof_(std::move(proof)),
          proofDiagnostic_(proofDiagnostic)
    {}

    ComputeDispatchValidationDomain(const ComputeDispatchValidationDomain&) = delete;
    ComputeDispatchValidationDomain& operator=(const ComputeDispatchValidationDomain&) = delete;

    const char* diagnostic() const noexcept
    {
        if (failure_)
            return failure_;
        const auto* id = lifetime_.diagnostic();
        if (!id)
            id = loss_.diagnostic();
        if (!id)
            id = ComputeDispatchValidationDomainContract::inspect(budget_, quota_->limitBytes());
        if (!id)
            id = proofDiagnostic_(proof_.get());
        // Revocation is permanent after observation; restoring a limit does not
        // turn a rejected proof into a fresh device-issued domain.
        if (id)
            failure_ = id;
        return id;
    }

    bool executionAvailable() const noexcept { return false; }

private:
    const ComputeDispatchValidationRequest request_;
    const OriginalDeviceLifetime::Ticket lifetime_;
    const OriginalDeviceNativeLossObservation loss_;
    const ComputeDispatchValidationDomainBudget budget_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    const std::shared_ptr<const void> proof_;
    const ProofDiagnostic proofDiagnostic_;
    mutable const char* failure_ = nullptr;
};
} // namespace arp::rhi
