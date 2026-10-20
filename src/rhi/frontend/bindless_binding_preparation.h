#pragma once
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi {

enum class BindlessBindingDomain { Compute, Graphics };
enum class BindlessBindingDiagnostic {
    Stale,
    Unavailable,
    Budget,
    OwnersMissing,
    OwnersMismatch,
    AuthorizationInvalid,
    PlanMismatch,
    Footprint,
    PoolFull,
    ResidentBudget,
    PayloadMismatch,
    PipelineMismatch,
    StageMismatch,
    CommandBudget
};

class ARP_API BindlessPreparedBinding : public core::SharedObjectIdentity {
    friend class BindlessComputeBindingPool;
    friend class BindlessGraphicsBindingPool;

public:
    virtual ~BindlessPreparedBinding() = default;
    virtual ValidationResult validateOwners() const = 0;
    virtual std::shared_ptr<const BindlessAuthorization> authorization() const noexcept = 0;
    virtual ValidationResult validatePayload() const = 0;
    virtual std::uint32_t argumentBufferCount() const noexcept = 0;
    virtual std::uint64_t argumentBufferBytes() const noexcept = 0;

    bool originalFactoryOwnerMatches(
        const std::shared_ptr<const BindlessPreparedBinding>& owner) const noexcept
    {
        const auto issued = originalFactoryOwner_.lock();
        return issued && sharedOwnerMatches(owner)
            && core::SharedOwnerProjection::sameOwner(issued, owner);
    }

private:
    void bindOriginalFactoryOwner(
        const std::shared_ptr<const BindlessPreparedBinding>& owner) const noexcept
    {
        originalFactoryOwner_ = owner;
    }

    mutable std::weak_ptr<const BindlessPreparedBinding> originalFactoryOwner_;
};

// PreparedBinding/1 recording retains the exact factory-issued owner, including
// both pool charges, through actual backend completion or unsubmitted abandonment.
// The per-command shared Compute/Graphics ceiling is maxBindlessPipelines.
// This contract does not authorize canonical Root recording/state publication;
// those operations require their own journal and completion admission.

ARP_API const char* bindlessBindingDiagnostic(
    BindlessBindingDomain, BindlessBindingDiagnostic) noexcept;
ARP_API ValidationResult validateBindlessBindingOwners(BindlessBindingDomain,
    bool optIn,
    const std::shared_ptr<const BindlessSnapshot>&,
    const BindlessLoweringPlan&,
    std::uint64_t generation,
    const std::shared_ptr<const BindlessAuthorization>& authorization,
    const std::shared_ptr<const BindlessAuthorization>& pipelineAuthorization);

struct BindlessBindingReservation {
    std::shared_ptr<const BindlessAuthorization> authorization;
    std::shared_ptr<core::AtomicCapacityLease> count, bytes;

    explicit operator bool() const noexcept { return authorization && count && bytes; }
};

class ARP_API BindlessBindingPreparationPool final {
public:
    BindlessBindingPreparationPool(
        const BindlessCrossExtension&, BindlessBindingBudget, BindlessBindingDomain);
    BindlessBindingPreparationPool(const BindlessBindingPreparationPool&) = delete;
    BindlessBindingPreparationPool& operator=(const BindlessBindingPreparationPool&) = delete;
    BindlessBindingReservation prepare(bool optIn,
        const std::shared_ptr<const BindlessSnapshot>&,
        bool pipelinePresent,
        const BindlessLoweringPlan&,
        std::uint64_t generation,
        std::shared_ptr<const BindlessAuthorization>,
        core::pmr::String& diagnostic);
    ValidationResult validatePayload(
        bool ownersMatch, std::uint32_t argumentCount, std::uint64_t argumentBytes) const;

    std::uint64_t liveBindings() const noexcept { return counts_->used(); }

    std::uint64_t chargedBytes() const noexcept { return bytes_->used(); }

    const BindlessBindingBudget& budget() const noexcept { return budget_; }

private:
    const BindlessCrossExtension* issuer_;
    core::LifetimeEpoch::Ticket lifetime_;
    BindlessBindingBudget budget_;
    BindlessBindingDomain domain_;
    std::shared_ptr<core::AtomicCapacityBudget> counts_, bytes_;
};

template <class Binding, class Pipeline>
ValidationResult validatePreparedBindlessBindingForRecord(const Binding& binding,
    const Pipeline& pipeline,
    const BindlessAccessDeclarations& accesses,
    ShaderStage stages,
    BindlessBindingDomain domain)
{
    if (!binding.owners() || binding.owners()->desc().pipeline.get() != &pipeline)
        return {
            false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::PipelineMismatch)};
    if (!binding.owners()->sharedOwnerMatches(binding.owners()))
        return {
            false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::OwnersMismatch)};
    auto valid = binding.owners()->validate();
    if (!valid.success)
        return valid;
    valid = binding.validatePayload();
    if (!valid.success)
        return valid;
    const auto& desc = binding.owners()->desc();
    return desc.snapshot->validateForRecord(desc.plan, desc.generation, accesses, stages);
}

} // namespace arp::rhi
