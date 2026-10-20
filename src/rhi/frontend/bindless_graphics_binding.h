#pragma once

#include "rhi/frontend/bindless_binding_preparation.h"

namespace arp::rhi {

struct BindlessGraphicsBindingDesc {
    bool optIn = false;
    std::shared_ptr<const BindlessSnapshot> snapshot;
    std::shared_ptr<const RenderPipeline> pipeline;
    BindlessLoweringPlan plan;
    std::uint64_t generation = 0;
};

// PreparedGraphicsBinding/1 retains one exact device-issued authorization,
// snapshot generation and selected-stage pipeline. GraphicsPassFaultScope/1
// consumes the same immutable owner and ordered accesses before BeginRenderPass;
// real completion (or unsubmitted cancellation) releases its budget leases.
// Backend capability publication is independent from Root/2 execution support.

class ARP_API BindlessGraphicsBindingOwners final : public core::SharedObjectIdentity {
    friend class BindlessGraphicsBindingPool;

    class ConstructionKey {
        friend class BindlessGraphicsBindingPool;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    BindlessGraphicsBindingOwners(ConstructionKey,
        BindlessGraphicsBindingDesc desc,
        std::shared_ptr<const BindlessAuthorization> authorization,
        std::shared_ptr<core::AtomicCapacityLease> count,
        std::shared_ptr<core::AtomicCapacityLease> bytes)
        : count_(std::move(count)), bytes_(std::move(bytes)), desc_(std::move(desc)),
          authorization_(std::move(authorization)), stages_(desc_.pipeline->bindlessShaderStages())
    {}

    ValidationResult validate() const;

    const BindlessGraphicsBindingDesc& desc() const noexcept { return desc_; }

    const std::shared_ptr<const BindlessAuthorization>& authorization() const noexcept
    {
        return authorization_;
    }

    ShaderStage stages() const noexcept { return stages_; }

    std::shared_ptr<const core::AtomicCapacityLease> originalCountLease() const noexcept
    {
        return count_;
    }

    std::shared_ptr<const core::AtomicCapacityLease> originalByteLease() const noexcept
    {
        return bytes_;
    }

private:
    std::shared_ptr<core::AtomicCapacityLease> count_, bytes_;
    BindlessGraphicsBindingDesc desc_;
    std::shared_ptr<const BindlessAuthorization> authorization_;
    ShaderStage stages_ = ShaderStage::None;
};

class ARP_API BindlessPreparedGraphicsBinding : public BindlessPreparedBinding {
public:
    virtual ~BindlessPreparedGraphicsBinding() = default;

    const std::shared_ptr<const BindlessGraphicsBindingOwners>& owners() const noexcept
    {
        return owners_;
    }

    ValidationResult validateForRecord(
        const RenderPipeline&, const BindlessAccessDeclarations&) const;

    ValidationResult validateOwners() const final
    {
        if (!owners_)
            return {false, "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISSING"};
        return owners_->sharedOwnerMatches(owners_)
            ? owners_->validate()
            : ValidationResult{false, "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH"};
    }

    std::shared_ptr<const BindlessAuthorization> authorization() const noexcept final
    {
        return owners_ ? owners_->authorization() : nullptr;
    }

protected:
    explicit BindlessPreparedGraphicsBinding(
        std::shared_ptr<const BindlessGraphicsBindingOwners> owners)
        : owners_(std::move(owners))
    {}

private:
    std::shared_ptr<const BindlessGraphicsBindingOwners> owners_;
};

class ARP_API BindlessGraphicsBindingPool final {
public:
    BindlessGraphicsBindingPool(const BindlessCrossExtension&, BindlessGraphicsBindingBudget);
    BindlessGraphicsBindingPool(const BindlessGraphicsBindingPool&) = delete;
    BindlessGraphicsBindingPool& operator=(const BindlessGraphicsBindingPool&) = delete;
    std::shared_ptr<const BindlessPreparedGraphicsBinding> prepare(
        BindlessGraphicsBindingDesc, core::pmr::String& diagnostic);

    std::uint64_t liveBindings() const noexcept { return preparation_.liveBindings(); }

    std::uint64_t chargedBytes() const noexcept { return preparation_.chargedBytes(); }

private:
    const BindlessCrossExtension* issuer_;
    BindlessBindingPreparationPool preparation_;
};

} // namespace arp::rhi
