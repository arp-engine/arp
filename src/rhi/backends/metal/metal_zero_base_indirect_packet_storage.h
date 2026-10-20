#pragma once
#include "rhi/backends/metal/metal_zero_base_indirect_pipeline.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
class MetalRhiClosedConsumerIssuance;

// Original private packet storage projection. The public factory permanently
// issues storage-only purpose; only the immutable-image root can create a fresh
// closed-consumer cold purpose. Neither grants a public plan or recording lease.
class MetalRhiZeroBaseIndirectPacketStorage final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiZeroBaseIndirectPacketStorage;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::BindlessIndirectPacketStorageContract;
    using Budget = Contract::ColdBudget;
    using Buffers = std::array<std::shared_ptr<rhi::GpuBuffer>, 3>;
    using Resources = std::array<MetalRhiRegisteredResource, 3>;

    MetalRhiZeroBaseIndirectPacketStorage(ConstructionKey,
        std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> pipeline,
        std::shared_ptr<const rhi::BindlessIndirectPacket> packet,
        Buffers buffers,
        Resources resources,
        Budget budget,
        rhi::BindlessIndirectPacket::Issuance purpose =
            rhi::BindlessIndirectPacket::Issuance::PrototypeStorageOnly)
        : pipeline_(std::move(pipeline)), packet_(std::move(packet)), buffers_(std::move(buffers)),
          resources_(std::move(resources)), budget_(budget), purpose_(purpose)
    {
        for (std::size_t i = 0u; i < resources_.size(); ++i)
            observations_[i] = MetalRhiNativeResourceObservation(resources_[i].buffer->resource);
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiZeroBaseIndirectPacketStorage(const MetalRhiZeroBaseIndirectPacketStorage&) = delete;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage> prepare(
        std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline>,
        rhi::BindlessIndirectPacketPool&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;
    // Native storage correspondence only, never execution/completion authority.
    const char* validateClosedReadbackReady() const noexcept;

    const auto& pipeline() const noexcept { return pipeline_; }

    const auto& packet() const noexcept { return packet_; }

    const auto& buffers() const noexcept { return buffers_; }

    const auto& resources() const noexcept { return resources_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    friend class MetalRhiClosedConsumerIssuance;
    const char* validateForState(rhi::ResourceState) const noexcept;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage> prepareImpl(
        std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline>,
        rhi::BindlessIndirectPacketPool&,
        Budget,
        rhi::BindlessIndirectPacket::Issuance,
        core::pmr::String&);

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(pipeline_);
        for (std::size_t i = 0u; i < pipeline_->childOwnerCount(); ++i)
            visit(pipeline_->childOwnerAt(i));
        visit(packet_);
        for (std::size_t i = 0u; i < buffers_.size(); ++i) {
            visit(buffers_[i]);
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, resources_[i].buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const MetalRhiZeroBaseIndirectPipeline> pipeline_;
    std::shared_ptr<const rhi::BindlessIndirectPacket> packet_;
    Buffers buffers_;
    Resources resources_;
    std::array<MetalRhiNativeResourceObservation, 3> observations_;
    Budget budget_;
    rhi::BindlessIndirectPacket::Issuance purpose_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
