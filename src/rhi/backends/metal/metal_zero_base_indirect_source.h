#pragma once
#include "rhi/backends/metal/metal_graphics_preparation.h"
#include "rhi/backends/metal/metal_sealed_buffer_upload.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"
#include "rhi/frontend/command_zero_base_indirect_processor_correspondence.h"

namespace arp::rhi_metal {
// Private cold byte/ownership prerequisite. No generation or execution ticket.
class MetalRhiZeroBaseIndirectSource final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiZeroBaseIndirectSource;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Content = rhi::ZeroBaseIndirectSourceConsumptionContract;
    using Budget = rhi::ZeroBaseIndirectSourcePreparationBudget;
    using Upload = MetalRhiSealedBufferUpload;

    MetalRhiZeroBaseIndirectSource(ConstructionKey,
        std::shared_ptr<const rhi::BindlessShaderPackage> package,
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index,
        Content&& content,
        Upload::Result source,
        Upload::Result parameters,
        Budget budget)
        : package_(std::move(package)), index_(std::move(index)),
          content_(core::makeShared<Content>(std::move(content))), source_(std::move(source)),
          parameters_(std::move(parameters)), sourceObservation_(source_.resource.buffer->resource),
          parameterObservation_(parameters_.resource.buffer->resource), budget_(budget)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiZeroBaseIndirectSource(const MetalRhiZeroBaseIndirectSource&) = delete;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectSource> prepare(
        std::shared_ptr<const rhi::BindlessShaderPackage>,
        std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding>,
        rhi::GpuDevice&,
        Content::Arguments::Source,
        rhi::ZeroBaseIndirectProducerParams,
        rhi::BindlessIndirectInstanceConsumerLayout,
        Content::Budget,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    rhi::ValidationResult validateIntent(const rhi::BindlessIndirectProcessorIntent& intent,
        rhi::ZeroBaseIndirectProcessorCorrespondence::Facts& facts) const
    {
        facts = {};
        if (const auto* id = validate())
            return {false, id};
        return rhi::ZeroBaseIndirectProcessorCorrespondence::verify(
            intent, package_, source_.buffer, *content_, facts);
    }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-ZERO-BASE-SOURCE-PREPARATION-ONLY";
    }

    const auto& package() const noexcept { return package_; }

    const auto& index() const noexcept { return index_; }

    const auto& content() const noexcept { return *content_; }

    const auto& sharedContent() const noexcept { return content_; }

    const auto& sourceWrapper() const noexcept { return source_.buffer; }

    const auto& sourceBuffer() const noexcept { return source_.resource.buffer; }

    const auto& parameterBuffer() const noexcept { return parameters_.resource.buffer; }

    const rhi::GpuBuffer& sourceResource() const noexcept { return *source_.buffer; }

    const rhi::GpuBuffer& parameterResource() const noexcept { return *parameters_.buffer; }

    const auto& quota() const noexcept { return index_->quota(); }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t ordinal) const noexcept { return owners_.at(ordinal); }

private:
    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(package_);
        visit(index_);
        visit(content_);
        for (std::size_t i = 0u; i < index_->childOwnerCount(); ++i)
            visit(index_->childOwnerAt(i));
        for (const auto* upload : {&source_, &parameters_}) {
            visit(upload->buffer);
            for (const auto& owner : MetalRhiResourceOwnerProjection::owners(
                     rhi::DescriptorResourceKind::Buffer, upload->resource.buffer, {}, {}, {}))
                if (owner)
                    visit(owner);
        }
    }

    std::shared_ptr<const rhi::BindlessShaderPackage> package_;
    std::shared_ptr<const MetalRhiOrdinaryPreparedIndexBinding> index_;
    std::shared_ptr<const Content> content_;
    Upload::Result source_, parameters_;
    MetalRhiNativeResourceObservation sourceObservation_, parameterObservation_;
    Budget budget_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
