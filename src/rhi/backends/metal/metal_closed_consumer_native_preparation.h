#pragma once
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_indirect_preparation.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
// Original bounded cold native target/encoder owners; no GPU encoding authority.
class MetalRhiClosedConsumerNativePreparation final : public core::SharedObjectIdentity {
    friend class MetalRhiNativeCommandPayload;

    class ConstructionKey {
        friend class MetalRhiClosedConsumerNativePreparation;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Reservation = MetalRhiClosedConsumerRecordingReservation;
    using Contract = rhi::CanonicalClosedConsumerTargetContract;
    enum class EncoderSlot : std::size_t { Clear, Compute, Blit, Render, Count };
    static constexpr std::size_t encoderSlotCount = static_cast<std::size_t>(EncoderSlot::Count);
    using Slots = std::array<std::shared_ptr<core::ExternalObjectSlot>, encoderSlotCount>;

    struct Budget {
        Contract::Budget values;
        std::uint32_t maxChildOwners = 0u;
    };

    MetalRhiClosedConsumerNativePreparation(ConstructionKey,
        std::shared_ptr<const Reservation> reservation,
        std::shared_ptr<rhi::GpuTexture> target,
        MetalRhiRegisteredResource resource,
        MetalRhiNativeRenderPassPreparation native,
        std::shared_ptr<core::MutationJournal> states,
        Slots slots,
        Contract::Input input,
        Contract::Facts facts,
        Budget budget)
        : reservation_(std::move(reservation)), target_(std::move(target)),
          resource_(std::move(resource)), native_(std::move(native)), states_(std::move(states)),
          slots_(std::move(slots)), input_(input), facts_(facts), budget_(budget),
          targetObservation_(resource_.texture->resource),
          ownedBytes_(target_->allocation().estimatedSizeBytes)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiClosedConsumerNativePreparation(
        const MetalRhiClosedConsumerNativePreparation&) = delete;
    static std::shared_ptr<const MetalRhiClosedConsumerNativePreparation> prepare(
        std::shared_ptr<const Reservation>,
        rhi::GpuDevice&,
        Contract::Input,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    const auto& reservation() const noexcept { return reservation_; }

    const auto& target() const noexcept { return target_; }

    const auto& resource() const noexcept { return resource_; }

    const auto& native() const noexcept { return native_; }

    const auto& states() const noexcept { return states_; }

    const auto& slots() const noexcept { return slots_; }

    const auto& input() const noexcept { return input_; }

    const auto& facts() const noexcept { return facts_; }

    std::uint64_t ownedBytes() const noexcept { return ownedBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CLOSED-CONSUMER-NATIVE-PREPARATION-ONLY";
    }

private:
    const char* validateRecorded(bool finished) const noexcept;
    const char* validateStorage(bool recorded, bool finished) const noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(reservation_);
        for (std::size_t i = 0u; i < reservation_->childOwnerCount(); ++i)
            visit(reservation_->childOwnerAt(i));
        visit(target_);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::TextureView, {}, resource_.texture, {}, {}))
            if (owner)
                visit(owner);
        visit(native_.descriptor);
        visit(states_);
        for (const auto& slot : slots_)
            visit(slot);
    }

    std::shared_ptr<const Reservation> reservation_;
    std::shared_ptr<rhi::GpuTexture> target_;
    MetalRhiRegisteredResource resource_;
    MetalRhiNativeRenderPassPreparation native_;
    std::shared_ptr<core::MutationJournal> states_;
    Slots slots_;
    Contract::Input input_;
    Contract::Facts facts_;
    Budget budget_;
    MetalRhiNativeResourceObservation targetObservation_;
    std::uint64_t ownedBytes_ = 0u;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
