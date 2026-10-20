#pragma once
#include "rhi/backends/metal/metal_immutable_buffer_upload.h"
#include "rhi/backends/metal/metal_zero_base_indirect_consumer_pipeline.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
// Private immutable native geometry only; no packet or execution authority.
class MetalRhiClosedConsumerGeometry final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiClosedConsumerGeometry;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::CanonicalZeroBaseIndirectConsumerGeometryContract;
    using Pipeline = MetalRhiZeroBaseIndirectConsumerPipeline;
    using Upload = MetalRhiImmutableBufferUpload;

    struct Budget {
        Contract::Budget values;
        std::uint64_t maxUploadBytes = 0u;
        std::size_t maxChildOwners = 0u;
    };

    MetalRhiClosedConsumerGeometry(ConstructionKey,
        std::shared_ptr<const Pipeline> pipeline,
        Contract&& content,
        Upload::Result upload,
        MetalRhiStageBufferRange range,
        Budget budget,
        core::LifetimeEpoch::Ticket issuer)
        : pipeline_(std::move(pipeline)), content_(core::makeShared<Contract>(std::move(content))),
          upload_(std::move(upload)), range_(range), budget_(budget), issuer_(std::move(issuer))
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiClosedConsumerGeometry(const MetalRhiClosedConsumerGeometry&) = delete;
    static std::shared_ptr<const MetalRhiClosedConsumerGeometry> prepare(
        std::shared_ptr<const Pipeline>,
        rhi::GpuDevice&,
        Contract::Input,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CLOSED-GEOMETRY-PREPARATION-ONLY";
    }

    const auto& pipeline() const noexcept { return pipeline_; }

    const auto& content() const noexcept { return *content_; }

    const auto& sharedContent() const noexcept { return content_; }

    const auto& buffer() const noexcept { return upload_.buffer; }

    const auto& resource() const noexcept { return upload_.resource; }

    const auto& range() const noexcept { return range_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

private:
    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(pipeline_);
        visit(content_);
        for (std::size_t i = 0u; i < pipeline_->childOwnerCount(); ++i)
            visit(pipeline_->childOwnerAt(i));
        visit(upload_.buffer);
        for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                 rhi::DescriptorResourceKind::Buffer, upload_.resource.buffer, {}, {}, {}))
            if (owner)
                visit(owner);
    }

    std::shared_ptr<const Pipeline> pipeline_;
    std::shared_ptr<const Contract> content_;
    Upload::Result upload_;
    MetalRhiStageBufferRange range_;
    Budget budget_;
    core::LifetimeEpoch::Ticket issuer_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
