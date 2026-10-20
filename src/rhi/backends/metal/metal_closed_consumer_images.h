#pragma once
#include "rhi/backends/metal/metal_closed_consumer_geometry.h"
#include "rhi/backends/metal/metal_immutable_texture_revision.h"
#include "rhi/backends/metal/metal_texture_inventory_observation.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
// Private fresh immutable native image root; never a consumer execution ticket.
class MetalRhiClosedConsumerImages final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiClosedConsumerImages;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::CanonicalExclusiveConsumerImagesContract;
    using Geometry = MetalRhiClosedConsumerGeometry;
    using Content = Contract::Content;
    using Marker = MetalRhiImmutableTextureRevision;
    using Contents = std::array<std::shared_ptr<const Content>, Contract::imageCount>;
    using Parents = std::array<std::shared_ptr<rhi::GpuTexture>, Contract::imageCount>;
    using Views = std::array<std::shared_ptr<rhi::GpuTextureView>, Contract::imageCount>;
    using Observations = std::array<MetalRhiTextureInventoryObservation, Contract::imageCount>;
    using Markers = std::array<std::shared_ptr<const Marker>, Contract::imageCount>;

    struct Budget {
        Contract::Budget values;
        std::uint64_t maxTextureBytes = 0u;
        std::size_t maxChildOwners = 0u;
    };

    MetalRhiClosedConsumerImages(ConstructionKey,
        std::shared_ptr<const Geometry>,
        Parents,
        Views,
        Contents,
        Observations,
        Markers,
        Budget,
        std::uint64_t actualBytes,
        core::LifetimeEpoch::Ticket);
    MetalRhiClosedConsumerImages(const MetalRhiClosedConsumerImages&) = delete;
    static std::shared_ptr<const MetalRhiClosedConsumerImages> prepare(
        std::shared_ptr<const Geometry>,
        rhi::GpuDevice&,
        const Contract::Inputs&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-EXCLUSIVE-IMAGES-CONSUMER-EXECUTION-UNAVAILABLE";
    }

    const auto& geometry() const noexcept { return geometry_; }

    const auto& images() const noexcept { return observations_; }

    const auto& contents() const noexcept { return contents_; }

    const auto& markers() const noexcept { return markers_; }

    const auto& parent(std::size_t i) const noexcept { return parents_[i]; }

    const auto& view(std::size_t i) const noexcept { return views_[i]; }

    std::uint64_t ownedBytes() const noexcept { return actualBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(geometry_);
        for (std::size_t i = 0u; i < geometry_->childOwnerCount(); ++i)
            visit(geometry_->childOwnerAt(i));
        const auto& source = geometry_->pipeline()->source();
        visit(source->index()->registry());
        visit(geometry_->pipeline()->native()->nativeDevice);
        visit(source->quota());
        for (std::size_t i = 0u; i < parents_.size(); ++i) {
            visit(parents_[i]);
            visit(views_[i]);
            visit(contents_[i]);
            visit(markers_[i]);
            for (const auto& owner : observations_[i].owners())
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const Geometry> geometry_;
    Parents parents_;
    Views views_;
    Contents contents_;
    Observations observations_;
    Markers markers_;
    Budget budget_;
    std::uint64_t actualBytes_ = 0u;
    core::LifetimeEpoch::Ticket issuer_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
