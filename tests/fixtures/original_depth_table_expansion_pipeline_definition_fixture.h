#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_depth_table_expansion_pipeline_definition.h"

#include <cstdio>

namespace arp::test {
class OriginalDepthTableExpansionPipelineDefinitionFixture final {
    using Definition = rhi_webgpu::WebGpuOriginalDepthTableExpansionPipelineDefinition;

    static void is(const char* actual, core::StringView expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

    // Explicit hostile mutable-layout fixture. Its shared owner is authentic,
    // but it supplies no registration/native execution authority.
    class MutableLayout final : public rhi::PipelineLayout {
    public:
        explicit MutableLayout(const rhi::PipelineLayoutDesc& desc)
            : value(rhi::PipelineLayoutStateCopy::copy(desc))
        {}

        const rhi::PipelineLayoutDesc& desc() const override { return value; }

        std::uint32_t descriptorSetCount() const override { return value.descriptorSets.size(); }

        std::uint32_t descriptorTableCount() const override
        {
            return value.descriptorTables.size();
        }

        std::uint32_t pushConstantByteCount() const override { return 0u; }

        bool backendFallbacksHiddenFromUpperLayer() const override { return true; }

        bool nativeHandleExposedToUpperLayer() const override { return false; }

        rhi::PipelineLayoutDesc value;
    };

public:
    template <class Factory, class Desc>
    static void verify(const std::shared_ptr<const Definition::Space>& space,
        const std::shared_ptr<rhi::PipelineLayout>& layout,
        const Desc& original,
        const Definition::Preflight::Budget& preflight,
        bool nativeQueryRequested)
    {
        constexpr bool compute = std::is_same_v<Desc, rhi::ComputePipelineDesc>;
        Desc desc;
        desc = original;
        desc.label = "Original selected retained pipeline definition label";
        if constexpr (!compute) {
            desc.depthStencil.depthBiasClamp = -0.0f;
            desc.viewport.x = -0.0f;
            desc.depthStencil.stencilReadMask = 7u; // inactive authored state must survive.
        }
        Definition::Budget budget{1u, true, 65536u, desc.label.size(), preflight};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        const auto backend = nativeQueryRequested ? rhi::Backend::WebGPU : rhi::Backend::Null;
        const char* diagnostic = nullptr;
        auto factory = [&] {
            return Definition::prepareOriginal(
                backend, space, layout, desc, budget, quota, diagnostic);
        };
        auto failed = [&](const auto& authored, const auto& domain, core::StringView id) {
            const auto baseline = quota->liveBytes();
            auto result = Definition::prepareOriginal(
                backend, space, layout, desc, authored, domain, diagnostic);
            assert(!result && quota->liveBytes() == baseline);
            is(diagnostic, id);
        };
        auto invalid = budget;
        invalid.contractVersion = 0u;
        failed(invalid, quota, "RHI-ORIGINAL-TABLE-DEFINITION-VERSION");
        invalid = budget;
        invalid.prototypeOptIn = false;
        failed(invalid, quota, "RHI-ORIGINAL-TABLE-DEFINITION-OPT-IN");
        invalid = budget;
        --invalid.maxLabelBytes;
        failed(invalid, quota, "RHI-ORIGINAL-TABLE-DEFINITION-LABEL");
        invalid = budget;
        invalid.maxCpuMetadataBytes = 0u;
        failed(invalid, quota, "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA");
        invalid = budget;
        --invalid.preflight.maxBindings;
        failed(invalid, quota, "RHI-ORIGINAL-TABLE-PREFLIGHT-BUDGET");
        invalid = budget;
        invalid.preflight.prototypeOptIn = false;
        failed(invalid, quota, "RHI-ORIGINAL-TABLE-PREFLIGHT-OPT-IN");
        failed(
            budget, std::shared_ptr<core::CpuMemoryQuota>{}, "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA");
        const auto& input = [&]() -> const auto& {
            if constexpr (compute)
                return space->computeInput();
            else
                return space->graphicsInput();
        }();
        for (const auto& domain :
            std::array{space->quota(), input->quota(), input->tableExpansion()->quota()}) {
            invalid = budget;
            invalid.maxCpuMetadataBytes = domain->limitBytes();
            failed(invalid, domain, "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA-DOMAIN");
        }
        for (const auto [other, id] : std::array{
                 std::pair{rhi::Backend::Vulkan, "RHI-ORIGINAL-TABLE-PREFLIGHT-VULKAN-PAYLOAD"},
                 std::pair{rhi::Backend::Metal, "RHI-ORIGINAL-TABLE-PREFLIGHT-METAL-PAYLOAD"}}) {
            assert(
                !Definition::prepareOriginal(other, space, layout, desc, budget, quota, diagnostic)
                && !quota->liveBytes());
            is(diagnostic, id);
        }
        if (!nativeQueryRequested) {
            assert(!Factory::prepareTableExpansionDefinition(
                       space, layout, desc, budget, quota, diagnostic)
                && !quota->liveBytes());
            is(diagnostic, "RHI-ORIGINAL-TABLE-PREFLIGHT-DEVICE");
        }
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measureSingleUse([] {}, factory);
        pressure.verifySingleUse(peak, [] {}, factory);
        assert(!quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto definition = factory();
        std::pmr::set_default_resource(previous);
        assert(definition);
        assert(!Definition::validateOriginal(definition) && !definition->nativeExecutionProven());
        is(definition->executionDiagnostic(), "RHI-ORIGINAL-TABLE-DEFINITION-CREATION-MISSING");
        assert(core::SharedOwnerProjection::sameOwner(definition->space(), space)
            && core::SharedOwnerProjection::sameOwner(definition->layout(), layout));
        assert(rhi::PipelineDefinitionIdentity::matches(
            definition->layoutDefinition(), layout->desc()));
        assert(!definition->metadataDecision().creationAvailable
            && !definition->metadataDecision().executionAvailable);
        assert(definition->metadataDecision().validatorOnly == !nativeQueryRequested);
        if constexpr (compute)
            assert(rhi::PipelineDefinitionIdentity::matches(definition->computeDefinition(), desc));
        else {
            assert(
                rhi::PipelineDefinitionIdentity::matches(definition->graphicsDefinition(), desc));
            auto changed = rhi::RenderPipelineStateCopy::copy(desc);
            changed.viewport.x = 0.0f;
            assert(!rhi::PipelineDefinitionIdentity::matches(
                definition->graphicsDefinition(), changed));
            changed = rhi::RenderPipelineStateCopy::copy(desc);
            changed.depthStencil.depthBiasClamp = 0.0f;
            assert(!rhi::PipelineDefinitionIdentity::matches(
                definition->graphicsDefinition(), changed));
            changed = rhi::RenderPipelineStateCopy::copy(desc);
            ++changed.depthStencil.stencilReadMask;
            assert(!rhi::PipelineDefinitionIdentity::matches(
                definition->graphicsDefinition(), changed));
        }
        auto forged = std::shared_ptr<const Definition>(quota, definition.get());
        auto nonowning = std::shared_ptr<const Definition>(
            std::shared_ptr<const Definition>{}, definition.get());
        for (const auto& bad : std::array{forged, nonowning, std::shared_ptr<const Definition>{}})
            is(Definition::validateOriginal(bad), "RHI-ORIGINAL-TABLE-DEFINITION-OWNER");
        const auto ceiling = quota->limitBytes();
        quota->setLimitBytes(ceiling - 1u);
        is(Definition::validateOriginal(definition), "RHI-ORIGINAL-TABLE-DEFINITION-QUOTA-LIMIT");
        quota->setLimitBytes(ceiling);
        const auto upstream = space->quota()->limitBytes();
        space->quota()->setLimitBytes(upstream - 1u);
        is(Definition::validateOriginal(definition), "RHI-WEBGPU-TABLE-SPACE-QUOTA-LIMIT");
        space->quota()->setLimitBytes(upstream);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, space->quota(), input->quota(), input->tableExpansion()->quota()},
            [&]() noexcept {
                return Definition::validateOriginal(definition);
            }));
        auto retained = definition;
        definition.reset();
        assert(!Definition::validateOriginal(retained));
        forged.reset();
        nonowning.reset();
        retained.reset();
        assert(!quota->liveBytes());
        std::shared_ptr<rhi::PipelineLayout> mutableLayout =
            core::makeShared<MutableLayout>(layout->desc());
        Desc mutableDesc;
        mutableDesc = desc;
        mutableDesc.layout = mutableLayout.get();
        auto mutableDefinition = Definition::prepareOriginal(
            backend, space, mutableLayout, mutableDesc, budget, quota, diagnostic);
        assert(mutableDefinition);
        auto& mutableValue = static_cast<MutableLayout*>(mutableLayout.get())->value;
        for (std::uint32_t mutate = 0u; mutate < (compute ? 5u : 2u); ++mutate) {
            auto saved = rhi::PipelineLayoutStateCopy::copy(mutableValue);
            if (mutate == 0u)
                ++mutableValue.descriptorTables[0].capacity;
            if (mutate == 1u)
                mutableValue.descriptorTables[0].stages = rhi::ShaderStage::Vertex;
            if constexpr (compute) {
                if (mutate == 2u)
                    mutableValue.descriptorLayouts[0].bindings[0].sampledTextureFilterable = false;
                if (mutate == 3u)
                    mutableValue.descriptorLayouts[0].bindings[0].stages =
                        rhi::ShaderStage::Compute | rhi::ShaderStage::Fragment;
                if (mutate == 4u)
                    mutableValue.descriptorSets[0].layoutName = "changed";
            }
            is(Definition::validateOriginal(mutableDefinition),
                "RHI-ORIGINAL-TABLE-DEFINITION-LAYOUT");
            mutableValue = std::move(saved);
            assert(!Definition::validateOriginal(mutableDefinition));
        }
        std::weak_ptr<const Definition::Space> retainedSpace = space;
        mutableLayout.reset();
        mutableDesc = {};
        assert(!Definition::validateOriginal(mutableDefinition));
        mutableDefinition.reset();
        assert(!quota->liveBytes() && !retainedSpace.expired());
        // Drop the only external projection/layout roots. The definition must
        // retain those exact owners, then release their charges exactly once.
        core::pmr::String sourceDiagnostic{&core::defaultCpuMemoryResource()};
        Definition::Space::Budget spaceBudget{1u, true, 65536u, compute ? 4u : 3u};
        auto droppedSpaceQuota =
            core::makeShared<core::CpuMemoryQuota>(spaceBudget.maxCpuMetadataBytes);
        auto droppedSpace =
            Definition::Space::prepare(input, spaceBudget, droppedSpaceQuota, sourceDiagnostic);
        assert(droppedSpace);
        auto droppedLayout =
            core::shareUnique(rhi_webgpu::createWebGpuRhiPipelineLayout(layout->desc()));
        assert(droppedLayout);
        Desc droppedDesc;
        droppedDesc = desc;
        droppedDesc.layout = droppedLayout.get();
        std::weak_ptr<const Definition::Space> weakSpace = droppedSpace;
        std::weak_ptr<rhi::PipelineLayout> weakLayout = droppedLayout;
        auto droppedDefinition = Definition::prepareOriginal(
            backend, droppedSpace, droppedLayout, droppedDesc, budget, quota, diagnostic);
        assert(droppedDefinition);
        droppedSpace.reset();
        droppedLayout.reset();
        droppedDesc = {};
        assert(!weakSpace.expired() && !weakLayout.expired()
            && !Definition::validateOriginal(droppedDefinition));
        droppedDefinition.reset();
        assert(weakSpace.expired() && weakLayout.expired() && !quota->liveBytes());
        // Weak observers legitimately retain the allocator-owned control block.
        weakSpace.reset();
        weakLayout.reset();
        assert(!droppedSpaceQuota->liveBytes());
        if (nativeQueryRequested) {
            auto native = Factory::prepareTableExpansionDefinition(
                space, layout, desc, budget, quota, diagnostic);
            assert(native && !Definition::validateOriginal(native)
                && native->metadataDecision().preflightEligible);
        }
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_DEFINITION_OK %s independent-quota%llu exact-less-exhausted full-state signed-zero preserved original-owners mutable-layout-reject hot0 native-query%u creation-closed\n",
            compute ? "Compute" : "Graphics",
            static_cast<unsigned long long>(peak),
            nativeQueryRequested ? 1u : 0u);
    }
};
} // namespace arp::test
