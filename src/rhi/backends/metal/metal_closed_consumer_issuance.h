#pragma once
#include "rhi/backends/metal/metal_closed_consumer_images.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_cold_bindings.h"

namespace arp::rhi_metal {
// Fresh private cold generation bound to actual immutable images. This root
// grants no public plan, recording lease, protected write or native draw.
class MetalRhiClosedConsumerIssuance final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiClosedConsumerIssuance;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Images = MetalRhiClosedConsumerImages;
    using Pipeline = MetalRhiZeroBaseIndirectPipeline;
    using Packet = MetalRhiZeroBaseIndirectPacketStorage;
    using Producer = MetalRhiZeroBaseIndirectColdBindings;
    using Cold = MetalRhiZeroBaseIndirectConsumerColdBindings;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxNewBytes = 0u;
        std::uint32_t maxChildOwners = 0u;
        Packet::Budget packet;
        Producer::Budget producer;
        Cold::Budget consumer;
    };

    static constexpr std::uint64_t minimumNewBytes = Packet::Contract::minimumBytes
        + Producer::Contract::minimumOwnedBytes + Cold::Contract::minimumOwnedBytes;

    MetalRhiClosedConsumerIssuance(ConstructionKey,
        std::shared_ptr<const Images> images,
        std::shared_ptr<const Cold> cold,
        Budget budget,
        std::uint64_t bytes)
        : images_(std::move(images)), cold_(std::move(cold)), budget_(budget), ownedBytes_(bytes),
          generations_{cold_->producer()->packet()->packet()->generation(),
              cold_->producer()->generation(),
              cold_->faultDomain()->generation()}
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiClosedConsumerIssuance(const MetalRhiClosedConsumerIssuance&) = delete;
    static std::shared_ptr<const MetalRhiClosedConsumerIssuance> prepare(
        std::shared_ptr<const Images>,
        std::shared_ptr<const Pipeline>,
        rhi::BindlessIndirectPacketPool&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;
    const char* validateClosedReadbackReady() const noexcept;

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CLOSED-CONSUMER-ISSUANCE-COLD-ONLY";
    }

    const auto& images() const noexcept { return images_; }

    const auto& cold() const noexcept { return cold_; }

    const auto& packet() const noexcept { return cold_->producer()->packet()->packet(); }

    const auto& generations() const noexcept { return generations_; }

    std::uint64_t ownedBytes() const noexcept { return ownedBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    const char* validateStorage(Cold::Contract::StoragePhase) const noexcept;
    static const char* validateBudget(Budget) noexcept;
    static const char* sumOwnedBytes(const Cold&, Budget, std::uint64_t&) noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(images_);
        for (std::size_t i = 0u; i < images_->childOwnerCount(); ++i)
            visit(images_->childOwnerAt(i));
        visit(cold_);
        for (std::size_t i = 0u; i < cold_->childOwnerCount(); ++i)
            visit(cold_->childOwnerAt(i));
    }

    std::shared_ptr<const Images> images_;
    std::shared_ptr<const Cold> cold_;
    Budget budget_;
    std::uint64_t ownedBytes_ = 0u;
    std::array<std::uint64_t, 3u> generations_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
