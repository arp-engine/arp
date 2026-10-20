#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_pipeline_preparation.h"

#include <cstdio>

namespace arp::test {
class OriginalDepthTableExpansionCreationSourceFixture final {
    using Contract = rhi::OriginalDepthTableExpansionCreationContract;
    using Definition = rhi_webgpu::WebGpuOriginalDepthTableExpansionPipelineDefinition;

public:
    template <class Desc>
    static void verify(const std::shared_ptr<const Definition::Space>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& desc,
        const Definition::Preflight::Budget& preflight)
    {
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        using Actor = std::conditional_t<compute,
            rhi_webgpu::WebGpuOriginalArtifactComputeCreation,
            rhi_webgpu::WebGpuOriginalArtifactGraphicsCreation>;
        using Input = std::conditional_t<compute,
            rhi::OriginalSingleEntryPipelineInput,
            rhi::OriginalGraphicsPipelineInput>;
        using Source = typename Actor::Source;
        static_assert(!std::is_copy_constructible_v<Source>);
        static_assert(sizeof(Source)
            == sizeof(typename Actor::Budget) + sizeof(std::shared_ptr<const Input>));
        Contract::Budget budget{
            1u, true, Actor::callbackCount, 64u, 256u, 65536u, 65536u, {1u, true, 65536u, 65536u}};
        assert(!Contract::validateBudget(budget) && Contract::validateBudget({}));
        auto b = budget;
        b.contractVersion = 2u;
        assert(Contract::validateBudget(b));
        b = budget;
        b.prototypeOptIn = false;
        assert(Contract::validateBudget(b));
        b = budget;
        --b.maxCallbacks;
        assert(Contract::validateBudget(b));
        for (auto field : {&Contract::Budget::maxLabelBytes,
                 &Contract::Budget::maxDiagnosticBytes,
                 &Contract::Budget::maxCpuOwnerBytes,
                 &Contract::Budget::maxCpuCallbackBytes}) {
            b = budget;
            b.*field = 0u;
            assert(Contract::validateBudget(b));
        }
        b = budget;
        b.native.contractVersion = 2u;
        assert(Contract::validateBudget(b));
        b = budget;
        b.native.prototypeOptIn = false;
        assert(Contract::validateBudget(b));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const rhi::OriginalDepthTableExpansionNativeEmissionContract::Decision emission{
                rhi::CapabilityMode::Native, false, true, false, false, nullptr};
            const auto decision = Contract::lowering(backend, emission);
            assert(!decision.executionAvailable
                && decision.creationEligible == (backend == rhi::Backend::WebGPU));
            assert(decision.validatorOnly == (backend == rhi::Backend::Null));
            const auto wrapper = Contract::wrapperLowering(backend, true, true);
            assert(wrapper.validatorOnly == (backend == rhi::Backend::Null)
                && !wrapper.executionAvailable
                && wrapper.acceptanceEligible == (backend == rhi::Backend::WebGPU));
            assert(!Contract::wrapperLowering(backend, false, true).acceptanceEligible);
            assert(!Contract::wrapperLowering(backend, true, false).acceptanceEligible);
        }
        for (std::uint32_t mutation = 0u; mutation < 4u; ++mutation) {
            rhi::OriginalDepthTableExpansionNativeEmissionContract::Decision emission{
                rhi::CapabilityMode::Native, false, true, false, false, nullptr};
            if (mutation == 0u)
                emission.mode = rhi::CapabilityMode::Equivalent;
            if (mutation == 1u)
                emission.validatorOnly = true;
            if (mutation == 2u)
                emission.emissionAvailable = false;
            if (mutation == 3u)
                emission.diagnostic = "TEST-REFUSED";
            assert(!Contract::lowering(rhi::Backend::WebGPU, emission).creationEligible);
        }
        assert(!Contract::lowering(static_cast<rhi::Backend>(255u), {}).creationEligible);
        const char* id = nullptr;
        Definition::Budget definitionBudget{1u, true, 65536u, 256u, preflight};
        auto quota = core::makeShared<core::CpuMemoryQuota>(definitionBudget.maxCpuMetadataBytes);
        auto definition = Definition::prepareOriginal(
            rhi::Backend::Null, space, layout, desc, definitionBudget, quota, id);
        assert(definition);
        std::weak_ptr<const Definition> weak = definition;
        {
            Source source(budget, definition);
            assert(source.expanded() && source.request().contractVersion == 2u);
            assert(
                core::SharedOwnerProjection::sameOwner(source.expansionDefinition(), definition));
            const auto& input = [&]() -> const auto& {
                if constexpr (compute)
                    return space->computeInput();
                else
                    return space->graphicsInput();
            }();
            assert(&source.input() == &input && !Input::validateTableExpansion(source.input()));
            typename Actor::Budget ordinary{1u,
                true,
                Actor::callbackCount,
                63u,
                255u,
                65535u,
                65534u,
                {1u, true, 65533u, 65532u}};
            Source ordinarySource(ordinary, std::shared_ptr<const Input>{});
            assert(!ordinarySource.expanded());
            assert(!ordinarySource.expansionDefinition() && !ordinarySource.input());
            const auto restored = ordinarySource.ordinaryBudget();
            assert(restored.contractVersion == ordinary.contractVersion
                && restored.prototypeOptIn == ordinary.prototypeOptIn
                && restored.maxCallbacks == ordinary.maxCallbacks
                && restored.maxLabelBytes == ordinary.maxLabelBytes
                && restored.maxDiagnosticBytes == ordinary.maxDiagnosticBytes
                && restored.maxCpuOwnerBytes == ordinary.maxCpuOwnerBytes
                && restored.maxCpuCallbackBytes == ordinary.maxCpuCallbackBytes
                && restored.native.contractVersion == ordinary.native.contractVersion
                && restored.native.prototypeOptIn == ordinary.native.prototypeOptIn
                && restored.native.maxTargetBytes == ordinary.native.maxTargetBytes
                && restored.native.maxCpuMetadataBytes == ordinary.native.maxCpuMetadataBytes);
            assert(!source.ordinaryBudget().contractVersion
                && !ordinarySource.expansionBudget().contractVersion);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota, space->quota(), input->quota(), input->tableExpansion()->quota()},
                [&]() noexcept -> const char* {
                    const auto selected = source.expansionBudget();
                    assert(selected.contractVersion == 1u
                        && selected.native.contractVersion == budget.native.contractVersion
                        && selected.maxLabelBytes == budget.maxLabelBytes
                        && &source.input() == &input);
                    return Definition::validateOriginal(source.expansionDefinition());
                }));
            definition.reset();
            assert(!weak.expired() && !Definition::validateOriginal(source.expansionDefinition()));
        }
        assert(weak.expired());
        weak.reset();
        assert(!quota->liveBytes());
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_CREATION_SOURCE_OK %s typed-request1 internal-source2 reference-input no-extra-owner-size hot0 caller-drop creation-closed\n",
            compute ? "Compute" : "Graphics");
    }
};
} // namespace arp::test
