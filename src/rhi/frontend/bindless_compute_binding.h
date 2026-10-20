#pragma once

#include "rhi/frontend/bindless_binding_preparation.h"

namespace arp::rhi {

struct BindlessComputeBindingDesc {
    bool optIn = false;
    std::shared_ptr<const BindlessSnapshot> snapshot;
    std::shared_ptr<const ComputePipeline> pipeline;
    BindlessLoweringPlan plan;
    std::uint64_t generation = 0;
};

// Closed owner bundle. Backends may encode it but cannot replace its device,
// pipeline, plan or snapshot. Reservations survive the preparing pool.
class ARP_API BindlessComputeBindingOwners final : public core::SharedObjectIdentity {
    friend class BindlessComputeBindingPool;

    class ConstructionKey {
        friend class BindlessComputeBindingPool;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    BindlessComputeBindingOwners(ConstructionKey,
        BindlessComputeBindingDesc desc,
        std::shared_ptr<const BindlessAuthorization> authorization,
        std::shared_ptr<core::AtomicCapacityLease> count,
        std::shared_ptr<core::AtomicCapacityLease> bytes)
        : count_(std::move(count)), bytes_(std::move(bytes)), desc_(std::move(desc)),
          authorization_(std::move(authorization))
    {}

    ValidationResult validate() const;

    const BindlessComputeBindingDesc& desc() const noexcept { return desc_; }

    const std::shared_ptr<const BindlessAuthorization>& authorization() const noexcept
    {
        return authorization_;
    }

    std::shared_ptr<const core::AtomicCapacityLease> originalCountLease() const noexcept
    {
        return count_;
    }

    std::shared_ptr<const core::AtomicCapacityLease> originalByteLease() const noexcept
    {
        return bytes_;
    }

private:
    // Owners are destroyed before accounting is released.
    std::shared_ptr<core::AtomicCapacityLease> count_;
    std::shared_ptr<core::AtomicCapacityLease> bytes_;
    BindlessComputeBindingDesc desc_;
    std::shared_ptr<const BindlessAuthorization> authorization_;
};

class ARP_API BindlessPreparedComputeBinding : public BindlessPreparedBinding {
public:
    virtual ~BindlessPreparedComputeBinding() = default;

    const std::shared_ptr<const BindlessComputeBindingOwners>& owners() const noexcept
    {
        return owners_;
    }

    ValidationResult validateForRecord(
        const ComputePipeline&, const BindlessAccessDeclarations&) const;

    ValidationResult validateOwners() const final
    {
        if (!owners_)
            return {false, "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISSING"};
        return owners_->sharedOwnerMatches(owners_)
            ? owners_->validate()
            : ValidationResult{false, "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH"};
    }

    std::shared_ptr<const BindlessAuthorization> authorization() const noexcept final
    {
        return owners_ ? owners_->authorization() : nullptr;
    }

protected:
    explicit BindlessPreparedComputeBinding(
        std::shared_ptr<const BindlessComputeBindingOwners> owners)
        : owners_(std::move(owners))
    {}

private:
    std::shared_ptr<const BindlessComputeBindingOwners> owners_;
};

class ARP_API BindlessComputeBindingPool final {
public:
    BindlessComputeBindingPool(const BindlessCrossExtension&, BindlessComputeBindingBudget);
    BindlessComputeBindingPool(const BindlessComputeBindingPool&) = delete;
    BindlessComputeBindingPool& operator=(const BindlessComputeBindingPool&) = delete;
    std::shared_ptr<const BindlessPreparedComputeBinding> prepare(
        BindlessComputeBindingDesc, core::pmr::String& diagnostic);

    std::uint64_t liveBindings() const noexcept { return preparation_.liveBindings(); }

    std::uint64_t chargedBytes() const noexcept { return preparation_.chargedBytes(); }

private:
    const BindlessCrossExtension* issuer_;
    BindlessBindingPreparationPool preparation_;
};

} // namespace arp::rhi
