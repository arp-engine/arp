#pragma once
#include "rhi/backends/webgpu/webgpu_pipeline_preflight.h"
#include "rhi/frontend/pipeline_definition_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"

namespace arp::rhi_webgpu {
// Externally serialized cold preparation. Immutable backend-private definition custody, not a canonical candidate,
// accepted actor, pipeline receipt or command issuer.
class WebGpuOriginalDepthTableExpansionPipelineDefinition final
    : public core::QuotaBoundSharedObjectIdentity {
    class ConstructionKey {
        friend class WebGpuOriginalDepthTableExpansionPipelineDefinition;
        ConstructionKey() = default;

    public:
        ConstructionKey(const ConstructionKey&) = default;
    };

public:
    using Preflight = WebGpuOriginalDepthTableExpansionPipelinePreflight;
    using Contract = Preflight::Contract;
    using Space = Preflight::Space;
    using Facts = WebGpuArtifactCreationNativeMethods::Facts;

    struct Budget {
        std::uint32_t contractVersion = 0u;
        bool prototypeOptIn = false;
        std::uint64_t maxCpuMetadataBytes = 0u, maxLabelBytes = 0u;
        Preflight::Budget preflight;
    };

    static constexpr core::StringView contractVersion =
        "OriginalDepthTableExpansionPipelineDefinition/1";

    WebGpuOriginalDepthTableExpansionPipelineDefinition(ConstructionKey,
        Budget budget,
        rhi::Backend backend,
        std::shared_ptr<const Space> space,
        std::shared_ptr<rhi::PipelineLayout> layout,
        Contract::Requirements requirements,
        WebGpuNativeDeviceLimits limits,
        Facts facts,
        Contract::Decision decision,
        const rhi::ComputePipelineDesc& desc)
        : budget_(budget), backend_(backend), space_(std::move(space)), layout_(std::move(layout)),
          layoutDefinition_(rhi::PipelineLayoutStateCopy::copy(layout_->desc())),
          compute_(rhi::ComputePipelineStateCopy::copy(desc)), requirements_(requirements),
          limits_(limits), facts_(facts), decision_(decision)
    {}

    WebGpuOriginalDepthTableExpansionPipelineDefinition(ConstructionKey,
        Budget budget,
        rhi::Backend backend,
        std::shared_ptr<const Space> space,
        std::shared_ptr<rhi::PipelineLayout> layout,
        Contract::Requirements requirements,
        WebGpuNativeDeviceLimits limits,
        Facts facts,
        Contract::Decision decision,
        const rhi::RenderPipelineDesc& desc)
        : budget_(budget), backend_(backend), space_(std::move(space)), layout_(std::move(layout)),
          layoutDefinition_(rhi::PipelineLayoutStateCopy::copy(layout_->desc())),
          graphics_(rhi::RenderPipelineStateCopy::copy(desc)), requirements_(requirements),
          limits_(limits), facts_(facts), decision_(decision)
    {}

    WebGpuOriginalDepthTableExpansionPipelineDefinition(
        const WebGpuOriginalDepthTableExpansionPipelineDefinition&) = delete;

    template <class Desc>
    static std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>
    prepareOriginal(rhi::Backend backend,
        const std::shared_ptr<const Space>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& desc,
        const Budget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        const char*& diagnostic)
    {
        diagnostic = nullptr;
        if (budget.contractVersion != 1u) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-VERSION";
            return {};
        }
        if (!budget.prototypeOptIn) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-OPT-IN";
            return {};
        }
        if ((diagnostic = Space::validateOriginal(space)))
            return {};
        if (!quota || !budget.maxCpuMetadataBytes
            || quota->limitBytes() != budget.maxCpuMetadataBytes) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA";
            return {};
        }
        if (!budget.maxLabelBytes || desc.label.size() > budget.maxLabelBytes) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-LABEL";
            return {};
        }
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        static_assert(compute || std::is_same_v<Desc, rhi::RenderPipelineDesc>);
        const auto& input = [&]() -> const auto& {
            if constexpr (compute)
                return space->computeInput();
            else
                return space->graphicsInput();
        }();
        if (!input) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-PROFILE";
            return {};
        }
        const std::array domains{
            quota, space->quota(), input->quota(), input->tableExpansion()->quota()};
        if (!core::SharedOwnerProjection::hasIndependentOwners(
                domains.size(), [&](std::size_t i) -> const auto& {
                    return domains[i];
                })) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA-DOMAIN";
            return {};
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            Contract::Requirements required;
            if ((diagnostic =
                        Preflight::inspect(input, space, layout, desc, budget.preflight, required)))
                return {};
            const auto facts = backend == rhi::Backend::WebGPU
                ? WebGpuArtifactCreationNativeMethods::query(compute
                          ? WebGpuArtifactCreationNativeMethods::Kind::Compute
                          : WebGpuArtifactCreationNativeMethods::Kind::Graphics)
                : Facts{};
            const auto limits = backend == rhi::Backend::WebGPU ? webGpuNativeDeviceLimits()
                                                                : WebGpuNativeDeviceLimits{};
            const auto decision = Contract::lowering(backend, required, facts, limits);
            if (!decision.validatorOnly && !decision.preflightEligible) {
                diagnostic = decision.diagnostic;
                return {};
            }
            auto result = core::makeUnique<WebGpuOriginalDepthTableExpansionPipelineDefinition>(
                ConstructionKey{},
                budget,
                backend,
                space,
                layout,
                required,
                limits,
                facts,
                decision,
                desc);
            return shareOriginal(std::move(result));
        } catch (const std::bad_alloc&) {
            diagnostic = "RHI-ORIGINAL-TABLE-DEFINITION-ALLOCATION";
            return {};
        }
    }

    static const char* validateOriginal(
        const std::shared_ptr<const WebGpuOriginalDepthTableExpansionPipelineDefinition>&
            self) noexcept
    {
        if (!self || !self->hasQuotaBoundSharedOwner(self))
            return "RHI-ORIGINAL-TABLE-DEFINITION-OWNER";
        if (self->originalMetadataQuota()->limitBytes() != self->budget_.maxCpuMetadataBytes)
            return "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA-LIMIT";
        if (const auto* id = Space::validateOriginal(self->space_))
            return id;
        if (!self->layout_ || !self->layout_->sharedOwnerMatches(self->layout_)
            || !rhi::PipelineDefinitionIdentity::matches(
                self->layout_->desc(), self->layoutDefinition_))
            return "RHI-ORIGINAL-TABLE-DEFINITION-LAYOUT";
        if (const auto* id = Contract::validateBudget(self->budget_.preflight, self->requirements_))
            return id;
        const auto decision =
            Contract::lowering(self->backend_, self->requirements_, self->facts_, self->limits_);
        return decision.validatorOnly || decision.preflightEligible ? nullptr : decision.diagnostic;
    }

    const auto& space() const noexcept { return space_; }

    const auto& layout() const noexcept { return layout_; }

    const auto& layoutDefinition() const noexcept { return layoutDefinition_; }

    const auto& computeDefinition() const noexcept { return compute_; }

    const auto& graphicsDefinition() const noexcept { return graphics_; }

    const auto& requirements() const noexcept { return requirements_; }

    const auto& queriedLimits() const noexcept { return limits_; }

    const auto& metadataDecision() const noexcept { return decision_; }

    const auto& quota() const noexcept { return originalMetadataQuota(); }

    rhi::Backend backend() const noexcept { return backend_; }

    bool nativeExecutionProven() const noexcept { return false; }

    const char* executionDiagnostic() const noexcept
    {
        return "RHI-ORIGINAL-TABLE-DEFINITION-CREATION-MISSING";
    }

private:
    const Budget budget_;
    const rhi::Backend backend_;
    const std::shared_ptr<const Space> space_;
    const std::shared_ptr<rhi::PipelineLayout> layout_;
    const rhi::PipelineLayoutDesc layoutDefinition_;
    const rhi::ComputePipelineDesc compute_;
    const rhi::RenderPipelineDesc graphics_;
    const Contract::Requirements requirements_;
    const WebGpuNativeDeviceLimits limits_;
    const Facts facts_;
    const Contract::Decision decision_;
};
} // namespace arp::rhi_webgpu
