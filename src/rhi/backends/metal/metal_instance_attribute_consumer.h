#pragma once
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/frontend/command_draw_validation.h"
#include "shader/vertex_location_input_proof.h"

namespace arp::rhi_metal {
// Cold selected firstInstance ABI prerequisite. No GPU producer or recording
// ticket. Exact provider/descriptor/function facts and all owners remain bound.
class MetalRhiPreparedInstanceAttributeConsumer final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiPreparedInstanceAttributeConsumer;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    MetalRhiPreparedInstanceAttributeConsumer(ConstructionKey,
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider,
        rhi::InstanceAttributeFetchIntent intent,
        shader::VertexLocationInputProof::Facts proof,
        MetalRhiOrdinaryBindingBudget budget)
        : provider_(std::move(provider)), intent_(intent), proof_(proof),
          facts_(provider_->native()->vertexInputFacts), budget_(budget)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiPreparedInstanceAttributeConsumer(
        const MetalRhiPreparedInstanceAttributeConsumer&) = delete;
    static std::shared_ptr<const MetalRhiPreparedInstanceAttributeConsumer> prepare(
        std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>,
        const rhi::InstanceAttributeFetchIntent&,
        const MetalRhiOrdinaryBindingBudget&,
        std::shared_ptr<core::CpuMemoryQuota>,
        core::pmr::String&);
    const char* validate() const noexcept;

    const char* validateForProvider(
        const std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider>& provider) const noexcept
    {
        if (!core::SharedOwnerProjection::sameOwner(provider, provider_))
            return "RHI-METAL-INSTANCE-ATTRIBUTE-PROVIDER-MISMATCH";
        return validate();
    }

    const auto& provider() const noexcept { return provider_; }

    const auto& quota() const noexcept { return provider_->quota(); }

    const auto& proof() const noexcept { return proof_; }

    const auto& facts() const noexcept { return facts_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        for (const auto& owner : MetalRhiOrdinaryGraphicsProvider::ownerProjection(provider_))
            if (owner)
                visit(owner);
    }

    std::shared_ptr<const MetalRhiOrdinaryGraphicsProvider> provider_;
    rhi::InstanceAttributeFetchIntent intent_;
    shader::VertexLocationInputProof::Facts proof_;
    MetalRhiVertexInputFacts facts_;
    MetalRhiOrdinaryBindingBudget budget_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
