#pragma once
#include "rhi/backends/metal/metal_zero_base_indirect_packet_storage.h"
#include "rhi/frontend/bindless_recording_validation.h"

namespace arp::rhi_metal {
// Exact cold native correspondence; no encoder, dispatch or writer authority.
class MetalRhiZeroBaseIndirectColdBindings final : public core::SharedObjectIdentity {
    friend class MetalRhiZeroBaseIndirectConsumerColdBindings;
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiZeroBaseIndirectColdBindings;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::CanonicalZeroBaseIndirectColdBindingContract;
    using Budget = Contract::Budget;
    using Upload = MetalRhiSealedBufferUpload;
    using Role = rhi::CanonicalZeroBaseIndirectSourceContract::Role;

    struct Binding {
        rhi::GpuResourceId resource;
        std::uint32_t set = 0u, binding = 0u;
        MetalRhiBufferBindingFact abi;
    };

    using Bindings =
        std::array<Binding, rhi::CanonicalZeroBaseIndirectSourceContract::bindingCount>;
    using Resources = std::array<MetalRhiRegisteredResource,
        rhi::CanonicalZeroBaseIndirectSourceContract::bindingCount>;

    MetalRhiZeroBaseIndirectColdBindings(ConstructionKey,
        std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage> packet,
        std::shared_ptr<rhi::BindlessOwnedFaultDomain> fault,
        std::array<Upload::Result, 3> uploads,
        Resources resources,
        MetalRhiRegisteredResource readback,
        Bindings bindings,
        Budget budget,
        std::uint64_t ownedBytes)
        : packet_(std::move(packet)), fault_(std::move(fault)), uploads_(std::move(uploads)),
          resources_(std::move(resources)), readback_(std::move(readback)), bindings_(bindings),
          budget_(budget), ownedBytes_(ownedBytes), generation_(fault_->generation()),
          readbackObservation_(readback_.buffer->resource)
    {
        for (std::size_t i = 0u; i < resources_.size(); ++i)
            observations_[i] = MetalRhiNativeResourceObservation(resources_[i].buffer->resource);
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiZeroBaseIndirectColdBindings(const MetalRhiZeroBaseIndirectColdBindings&) = delete;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectColdBindings> prepare(
        std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage>,
        rhi::GpuDevice&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;
    const char* validateClosedReadbackReady() const noexcept;

    static constexpr const char* executionDiagnostic() noexcept
    {
        return Contract::executionDiagnostic();
    }

    const auto& packet() const noexcept { return packet_; }

    const auto& faultDomain() const noexcept { return fault_; }

    const auto& uploads() const noexcept { return uploads_; }

    const auto& resources() const noexcept { return resources_; }

    const auto& readback() const noexcept { return readback_; }

    const auto& bindings() const noexcept { return bindings_; }

    std::uint64_t generation() const noexcept { return generation_; }

    std::uint64_t ownedBytes() const noexcept { return ownedBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    const char* validateForState(rhi::ResourceState) const noexcept;
    const char* validateStorage(rhi::ResourceState outputState, bool closedReady) const noexcept;
    mutable std::atomic<bool> consumed_{false};

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(packet_);
        for (std::size_t i = 0u; i < packet_->childOwnerCount(); ++i)
            visit(packet_->childOwnerAt(i));
        visit(fault_);
        for (const auto& upload : uploads_) {
            visit(upload.buffer);
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, upload.resource.buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
        for (const auto* registered :
            {&resources_[static_cast<std::size_t>(Role::FaultLatch)], &readback_}) {
            // Buffer wrappers are unique children of the original fault domain.
            visit(std::shared_ptr<const rhi::GpuBuffer>(fault_,
                registered == &readback_ ? fault_->faultReadbackBuffer()
                                         : fault_->faultLatchBuffer()));
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, registered->buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const MetalRhiZeroBaseIndirectPacketStorage> packet_;
    std::shared_ptr<rhi::BindlessOwnedFaultDomain> fault_;
    std::array<Upload::Result, 3> uploads_;
    Resources resources_;
    MetalRhiRegisteredResource readback_;
    Bindings bindings_;
    Budget budget_;
    std::uint64_t ownedBytes_, generation_;
    std::array<MetalRhiNativeResourceObservation,
        rhi::CanonicalZeroBaseIndirectSourceContract::bindingCount>
        observations_;
    MetalRhiNativeResourceObservation readbackObservation_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
