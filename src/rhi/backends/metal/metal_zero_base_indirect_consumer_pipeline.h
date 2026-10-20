#pragma once
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_prepared_function_ownership.h"
#include "rhi/backends/metal/metal_render_state_observation.h"
#include "rhi/backends/metal/metal_zero_base_indirect_source.h"
#include "rhi/frontend/bindless_image_and_indirect_validation.h"

namespace arp::rhi_metal {
// Prototype cold query only. No resource generation, snapshot or draw ticket.
class MetalRhiZeroBaseIndirectConsumerPipeline final : public core::SharedObjectIdentity {
    class ConstructionKey {
        friend class MetalRhiZeroBaseIndirectConsumerPipeline;

        ConstructionKey() {}

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Contract = rhi::CanonicalZeroBaseIndirectConsumerContract;
    using Stages = std::array<MetalRhiPreparedGraphicsStageLayout, 2u>;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint32_t maxChildOwners = 0u;
    };

    struct Facts {
        std::array<MetalRhiBufferBindingFact, 3u> buffers{};
        MetalRhiBufferBindingFact vertexStreamBuffer;
        std::array<MetalRhiTextureBindingFact, Contract::capacity> textures{};
    };

    MetalRhiZeroBaseIndirectConsumerPipeline(ConstructionKey,
        std::shared_ptr<const MetalRhiZeroBaseIndirectSource> source,
        std::shared_ptr<const rhi::BindlessShaderPackage> package,
        core::UniquePtr<rhi::ShaderPackageDesc> interface,
        core::UniquePtr<rhi::PipelineLayout> layout,
        Stages stages,
        rhi::VertexInputConsumptionContract input,
        std::shared_ptr<MetalRhiNativePipelinePayload> native,
        Facts facts,
        Budget budget)
        : source_(std::move(source)), package_(std::move(package)),
          interface_(std::move(interface)), layout_(std::move(layout)), stages_(std::move(stages)),
          input_(core::makeShared<rhi::VertexInputConsumptionContract>(std::move(input))),
          native_(std::move(native)), facts_(facts), budget_(budget),
          streams_(native_->vertexStreams), stageFacts_(native_->nativeBindingStages),
          vertexFacts_(native_->vertexInputFacts), state_(*native_)
    {
        owners_.capture([&](auto&& visit) {
            projectOwners(visit);
        });
    }

    MetalRhiZeroBaseIndirectConsumerPipeline(
        const MetalRhiZeroBaseIndirectConsumerPipeline&) = delete;
    static std::shared_ptr<const MetalRhiZeroBaseIndirectConsumerPipeline> prepare(
        std::shared_ptr<const MetalRhiZeroBaseIndirectSource>,
        std::shared_ptr<const rhi::BindlessShaderPackage>,
        const rhi::RenderPipelineDesc&,
        const rhi::semantics::NativeBackendCapabilityContract&,
        const MetalRhiOrdinaryBindingBudget&,
        Budget,
        core::pmr::String&);
    const char* validate() const noexcept;

    const auto& native() const noexcept { return native_; }

    const auto& source() const noexcept { return source_; }

    const auto& package() const noexcept { return package_; }

    const auto& stages() const noexcept { return stages_; }

    const auto& facts() const noexcept { return facts_; }

    const auto& vertexInput() const noexcept { return *input_; }

    const auto& sharedVertexInput() const noexcept { return input_; }

    std::size_t childOwnerCount() const noexcept { return owners_.size(); }

    auto childOwnerAt(std::size_t i) const noexcept { return owners_.at(i); }

    static constexpr const char* executionDiagnostic() noexcept
    {
        return "RHI-METAL-CLOSED-CONSUMER-PREPARATION-ONLY";
    }

private:
    static const char* capture(const MetalRhiZeroBaseIndirectSource&,
        const rhi::BindlessShaderPackage&,
        const Stages&,
        const rhi::VertexInputConsumptionContract&,
        const MetalRhiNativePipelinePayload&,
        Facts&) noexcept;

    template <class Visitor> void projectOwners(Visitor&& visit) const
    {
        visit(source_);
        visit(package_);
        visit(input_);
        for (std::size_t i = 0u; i < source_->childOwnerCount(); ++i)
            visit(source_->childOwnerAt(i));
        for (auto& owner : MetalRhiPreparedFunctionOwnership::pipelineOwners(native_))
            if (owner)
                visit(std::move(owner));
    }

    std::shared_ptr<const MetalRhiZeroBaseIndirectSource> source_;
    std::shared_ptr<const rhi::BindlessShaderPackage> package_;
    core::UniquePtr<rhi::ShaderPackageDesc> interface_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    Stages stages_;
    std::shared_ptr<const rhi::VertexInputConsumptionContract> input_;
    std::shared_ptr<MetalRhiNativePipelinePayload> native_;
    Facts facts_;
    Budget budget_;
    rhi::semantics::MetalVertexStreamMapping streams_;
    rhi::semantics::MetalPhysicalBindingStages stageFacts_;
    MetalRhiVertexInputFacts vertexFacts_;
    MetalRhiRenderStateObservation state_;
    core::SharedOwnerProjection owners_;
};
} // namespace arp::rhi_metal
