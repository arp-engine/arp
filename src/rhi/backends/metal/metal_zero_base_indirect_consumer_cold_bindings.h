#pragma once
#include "rhi/backends/metal/metal_texture_inventory_observation.h"
#include "rhi/backends/metal/metal_zero_base_indirect_cold_bindings.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_pipeline.h"
#include "rhi/frontend/bindless_recording_validation.h"

namespace arp::rhi_metal {
// Cold correspondence only. The original mapping remains protected ShaderWrite.
// No readable descriptor, snapshot, plan, encoder or execution ticket is issued.
class MetalRhiZeroBaseIndirectConsumerColdBindings final : public core::SharedObjectIdentity {
    friend class MetalRhiCanonicalProducerRecordingPreparation;

    class ConstructionKey {
        friend class MetalRhiZeroBaseIndirectConsumerColdBindings;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::CanonicalZeroBaseIndirectConsumerColdBindingContract;
    using Consumer = Contract::Consumer;
    using Budget = Contract::Budget;
    using Producer = MetalRhiZeroBaseIndirectColdBindings;
    using Pipeline = MetalRhiZeroBaseIndirectConsumerPipeline;
    using Upload = MetalRhiSealedBufferUpload;
    using Images = std::array<MetalRhiTextureInventoryObservation, Consumer::capacity>;
    using ImageIds = std::array<rhi::GpuResourceId, Consumer::capacity>;
    using Resources = std::array<MetalRhiRegisteredResource, 3u>;

    struct BufferBinding {
        rhi::GpuResourceId resource;
        std::uint32_t set = 0u, binding = 0u;
        MetalRhiStageBufferRange range;
    };

    struct ImageBinding {
        rhi::GpuResourceId resource;
        std::uint32_t set = 0u, binding = 0u;
        MetalRhiTextureBindingFact abi;
    };

    using Buffers = std::array<BufferBinding, 3u>;
    using Textures = std::array<ImageBinding, Consumer::capacity>;

    MetalRhiZeroBaseIndirectConsumerColdBindings(ConstructionKey,
        std::shared_ptr<const Producer> producer,
        std::shared_ptr<const Pipeline> pipeline,
        std::shared_ptr<rhi::BindlessOwnedFaultDomain> fault,
        Upload::Result metadata,
        Images images,
        Resources resources,
        MetalRhiRegisteredResource readback,
        Contract::Facts facts,
        Buffers buffers,
        Textures textures,
        Budget budget,
        std::uint64_t ownedBytes)
        : producer_(std::move(producer)), pipeline_(std::move(pipeline)), fault_(std::move(fault)),
          metadata_(std::move(metadata)), images_(std::move(images)),
          resources_(std::move(resources)), readback_(std::move(readback)), facts_(facts),
          buffers_(std::move(buffers)), textures_(textures), budget_(budget),
          ownedBytes_(ownedBytes), readbackObservation_(readback_.buffer->resource)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiZeroBaseIndirectConsumerColdBindings(
        const MetalRhiZeroBaseIndirectConsumerColdBindings&) = delete;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerColdBindings> prepare(
        std::shared_ptr<const Producer>,
        std::shared_ptr<const Pipeline>,
        rhi::GpuDevice&,
        const ImageIds&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;
    const char* validateClosedReadbackReady() const noexcept;

    static constexpr const char* executionDiagnostic() noexcept
    {
        return Contract::executionDiagnostic();
    }

    const auto& producer() const noexcept { return producer_; }

    const auto& pipeline() const noexcept { return pipeline_; }

    const auto& faultDomain() const noexcept { return fault_; }

    const auto& metadata() const noexcept { return metadata_; }

    const auto& images() const noexcept { return images_; }

    const auto& resources() const noexcept { return resources_; }

    const auto& readback() const noexcept { return readback_; }

    const auto& facts() const noexcept { return facts_; }

    const auto& buffers() const noexcept { return buffers_; }

    const auto& textures() const noexcept { return textures_; }

    std::uint64_t ownedBytes() const noexcept { return ownedBytes_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    const char* validateForProducerState(rhi::ResourceState) const noexcept;
    const char* validateStorage(rhi::ResourceState, Contract::StoragePhase) const noexcept;
    static const char* validateRoots(
        const std::shared_ptr<const Producer>&, const std::shared_ptr<const Pipeline>&) noexcept;

    template <class Visitor>
    static void projectBorrowed(const std::shared_ptr<const Producer>& producer,
        const std::shared_ptr<const Pipeline>& pipeline,
        const Images& images,
        Visitor&& visit)
    {
        visit(producer);
        for (std::size_t i = 0u; i < producer->childOwnerCount(); ++i)
            visit(producer->childOwnerAt(i));
        visit(pipeline);
        for (std::size_t i = 0u; i < pipeline->childOwnerCount(); ++i)
            visit(pipeline->childOwnerAt(i));
        for (const auto& image : images)
            for (const auto& owner : image.owners())
                if (owner)
                    visit(owner);
    }

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        projectBorrowed(producer_, pipeline_, images_, visit);
        visit(fault_);
        visit(metadata_.buffer);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::Buffer, metadata_.resource.buffer, {}, {}, {}))
            if (owner)
                visit(owner);
        for (const auto* resource : {&resources_[2u], &readback_}) {
            visit(std::shared_ptr<const rhi::GpuBuffer>(fault_,
                resource == &readback_ ? fault_->faultReadbackBuffer()
                                       : fault_->faultLatchBuffer()));
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, resource->buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const Producer> producer_;
    std::shared_ptr<const Pipeline> pipeline_;
    std::shared_ptr<rhi::BindlessOwnedFaultDomain> fault_;
    Upload::Result metadata_;
    Images images_;
    Resources resources_;
    MetalRhiRegisteredResource readback_;
    Contract::Facts facts_;
    Buffers buffers_;
    Textures textures_;
    Budget budget_;
    std::uint64_t ownedBytes_;
    MetalRhiNativeResourceObservation readbackObservation_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
