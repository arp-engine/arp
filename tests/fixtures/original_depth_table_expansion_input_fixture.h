#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_depth_table_expansion_pipeline_fixture.h"
#include "rhi/backends/webgpu/webgpu_binding_space_lowering.h"
#include "rhi/backends/webgpu/webgpu_original_depth_table_expansion_binding_space.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/frontend/pipeline_shader_interface_validation.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <type_traits>

namespace arp::test {
class OriginalDepthTableExpansionInputFixture final {
    using Artifact = shader::OriginalDepthTableExpansionArtifact;
    using Space = rhi_webgpu::WebGpuOriginalDepthTableExpansionBindingSpace;

    static void is(const char* actual, core::StringView expected)
    {
        assert(actual && core::StringView(actual) == expected);
    }

    template <class Input> static void verifyInput(std::shared_ptr<const Artifact> artifact)
    {
        constexpr bool compute = std::is_same_v<Input, rhi::OriginalSingleEntryPipelineInput>;
        typename Input::ExpansionBudget budget{3u, 65536u, 1u, true};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        const auto baseline = artifact->quota()->liveBytes();
        auto factory = [&] {
            return Input::prepareTableExpansion(artifact, budget, quota, diagnostic);
        };
        auto expect = [&](auto owner, auto authored, auto domain, core::StringView id) {
            const auto before = quota->liveBytes();
            auto failed = Input::prepareTableExpansion(owner, authored, domain, diagnostic);
            assert(!failed && diagnostic == id && quota->liveBytes() == before);
        };
        auto invalid = budget;
        invalid.contractVersion = 2u;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-TABLE-INPUT-VERSION");
        invalid = budget;
        invalid.expansionContractVersion = 2u;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-TABLE-INPUT-VERSION");
        invalid = budget;
        invalid.prototypeOptIn = false;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-TABLE-INPUT-OPT-IN");
        invalid = budget;
        invalid.maxCpuMetadataBytes = 0u;
        expect(artifact, invalid, quota, "RHI-ORIGINAL-TABLE-INPUT-QUOTA");
        expect(artifact,
            budget,
            std::shared_ptr<core::CpuMemoryQuota>{},
            "RHI-ORIGINAL-TABLE-INPUT-QUOTA");
        invalid = budget;
        invalid.maxCpuMetadataBytes = artifact->quota()->limitBytes();
        expect(artifact, invalid, artifact->quota(), "RHI-ORIGINAL-TABLE-INPUT-QUOTA-DOMAIN");
        for (const auto& invalidOwner :
            std::array{std::shared_ptr<const Artifact>(artifact->quota(), artifact.get()),
                std::shared_ptr<const Artifact>(std::shared_ptr<const Artifact>{}, artifact.get()),
                std::shared_ptr<const Artifact>{}})
            expect(invalidOwner, budget, quota, "SHADER-ORIGINAL-TABLE-EXPANSION-OWNER");
        using Other = std::conditional_t<compute,
            rhi::OriginalGraphicsPipelineInput,
            rhi::OriginalSingleEntryPipelineInput>;
        assert(!Other::prepareTableExpansion(artifact, budget, quota, diagnostic)
            && diagnostic == "RHI-ORIGINAL-TABLE-INPUT-PROFILE");
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measureSingleUse([] {}, factory);
        pressure.verifySingleUse(peak, [] {}, factory);
        assert(!quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto input = factory();
        std::pmr::set_default_resource(previous);
        assert(input);
        assert(!Input::validateTableExpansion(input) && !input->nativeExecutionProven()
            && !input->artifact());
        assert(core::SharedOwnerProjection::sameOwner(input->tableExpansion(), artifact));
        const auto& package = input->package();
        assert(!package.generatedReflectionHydrated && package.artifacts.wgslPath.empty());
        assert(package.reflection.sourceSha256 == artifact->originalSourceSha256());
        assert(package.entrypoints.size() == (compute ? 1u : 2u));
        assert(package.reflection.descriptorBindings.size() == (compute ? 2u : 1u));
        const auto& table = package.reflection.descriptorBindings.front();
        assert(table.arrayCount == 3u && table.depthTexture
            && table.type == rhi::DescriptorType::SampledTexture);
        assert(!table.set && !table.binding
            && table.stages == (compute ? rhi::ShaderStage::Compute : rhi::ShaderStage::Fragment));
        assert(!rhi::validateShaderPackage(package).success);
        for (std::uint32_t mutate = 0u; mutate < 7u; ++mutate) {
            auto changed = rhi::ShaderPackageStateCopy::copy(package);
            if (mutate == 0u)
                changed.reflection.descriptorBindings[0].arrayCount = 1u;
            if (mutate == 1u)
                changed.reflection.descriptorBindings[0].depthTexture = false;
            if (mutate == 2u)
                changed.reflection.descriptorBindings[0].stages = rhi::ShaderStage::Vertex;
            if (mutate == 3u)
                changed.reflection.sourceSha256 = "edited";
            if (mutate == 4u)
                changed.generatedReflectionHydrated = true;
            if (mutate == 5u)
                changed.artifacts.wgslPath = "expanded.wgsl";
            if (mutate == 6u)
                changed.entrypoints[0].entryPoint = "edited";
            is(Input::tableExpansionPackageDiagnostic(input, changed),
                "RHI-ORIGINAL-TABLE-INPUT-PACKAGE-DEFINITION");
        }
        assert(!Input::tableExpansionPackageDiagnostic(input, package));
        verifyDeclaration(input);
        auto forged = std::shared_ptr<const Input>(quota, input.get());
        auto nonowning = std::shared_ptr<const Input>(std::shared_ptr<const Input>{}, input.get());
        const auto ceiling = quota->limitBytes();
        quota->setLimitBytes(ceiling - 1u);
        is(Input::validateTableExpansion(input), "RHI-ORIGINAL-TABLE-INPUT-QUOTA-LIMIT");
        quota->setLimitBytes(ceiling);
        const auto artifactCeiling = artifact->quota()->limitBytes();
        artifact->quota()->setLimitBytes(artifactCeiling - 1u);
        is(Input::validateTableExpansion(input), "SHADER-ORIGINAL-TABLE-EXPANSION-QUOTA-LIMIT");
        artifact->quota()->setLimitBytes(artifactCeiling);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, artifact->quota()}, [&]() noexcept {
                for (const auto& owner :
                    std::array{forged, nonowning, std::shared_ptr<const Input>{}})
                    is(Input::validateTableExpansion(owner), "RHI-ORIGINAL-TABLE-INPUT-OWNER");
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    is(Input::selectTarget(input, backend).diagnostic,
                        rhi::OriginalDepthTableExpansionInputContract::ordinaryProfileDiagnostic);
                    const auto gate = arp::rhi::semantics::
                        OriginalCommandDescriptorBindingLowering::tableLowering(backend);
                    assert(!gate.executionAvailable && !gate.positionalAvailable);
                }
                return Input::validateTableExpansion(input);
            }));
        if constexpr (compute) {
            const auto ordinary = rhi_webgpu::loadOriginalWebGpuBindingSpaceLowering(input);
            assert(!ordinary.success
                && ordinary.diagnostics
                    == rhi::OriginalDepthTableExpansionInputContract::ordinaryProfileDiagnostic);
        } else {
            const auto ordinary = rhi_webgpu::loadOriginalWebGpuGraphicsBindingSpaceLowering(input);
            assert(!ordinary.success
                && ordinary.diagnostics
                    == rhi::OriginalDepthTableExpansionInputContract::ordinaryProfileDiagnostic);
        }
        assert(artifact->quota()->liveBytes() == baseline);
        verifySpace(input);
        forged.reset();
        nonowning.reset();
        input.reset();
        assert(!quota->liveBytes());
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_INPUT_OK %s original-logical3 physical-targets-separate independent-quota%llu exact-less-exhausted hot0 ordinary-native-closed\n",
            compute ? "Compute" : "Graphics",
            static_cast<unsigned long long>(peak));
    }

    template <class Input> static void verifyDeclaration(const std::shared_ptr<const Input>& input)
    {
        using Contract = rhi::PipelineShaderDeclarationContract;
        constexpr bool compute = std::is_same_v<Input, rhi::OriginalSingleEntryPipelineInput>;
        const auto kind =
            compute ? Contract::Kind::ComputePipeline : Contract::Kind::GraphicsPipeline;
        const auto& package = input->package();
        const auto count = package.entrypoints.size();
        const auto entries = [&](std::size_t i) -> const auto& {
            return package.entrypoints[i];
        };
        auto layout = declarationLayout(compute);
        const auto validate = [&](const auto& candidate) {
            return Contract::validateTableExpansion(
                {1u, true}, input, candidate, package, count, kind, entries);
        };
        assert(!validate(layout));
        is(Contract::validateTableExpansion(
               {0u, true}, input, layout, package, count, kind, entries),
            "RHI-ORIGINAL-TABLE-DECLARATION-VERSION");
        is(Contract::validateTableExpansion(
               {1u, false}, input, layout, package, count, kind, entries),
            "RHI-ORIGINAL-TABLE-DECLARATION-OPT-IN");
        is(Contract::validateTableExpansion({1u, true},
               input,
               layout,
               package,
               count,
               compute ? Contract::Kind::GraphicsPipeline : Contract::Kind::ComputePipeline,
               entries),
            "RHI-ORIGINAL-TABLE-DECLARATION-KIND");
        is(Contract::validateTableExpansion(
               {1u, true}, input, layout, package, count - 1u, kind, entries),
            "RHI-ORIGINAL-TABLE-DECLARATION-ENTRIES");
        auto forged = std::shared_ptr<const Input>(input->quota(), input.get());
        is(Contract::validateTableExpansion(
               {1u, true}, forged, layout, package, count, kind, entries),
            "RHI-ORIGINAL-TABLE-INPUT-OWNER");
        for (std::uint32_t mutate = 0u; mutate < 11u; ++mutate) {
            auto changed = rhi::PipelineLayoutStateCopy::copy(layout);
            auto& table = changed.descriptorTables.front();
            if (mutate == 0u)
                table.set = 2u;
            if (mutate == 1u)
                table.tableName = "Other";
            if (mutate == 2u)
                table.type = rhi::DescriptorType::StorageTexture;
            if (mutate == 3u)
                table.stages = rhi::ShaderStage::Vertex;
            if (mutate == 4u)
                table.capacity = 2u;
            if (mutate == 5u)
                table.capacity = 4u;
            if (mutate == 6u)
                table.depthTexture = false;
            if (mutate == 7u)
                table.textureViewDimensionConstrained = false;
            if (mutate == 8u)
                table.textureViewDimension = rhi::TextureViewDimension::TextureCube;
            if (mutate == 9u)
                changed.pushConstantRanges.push_back({rhi::ShaderStage::Compute, 0u, 16u});
            if (mutate == 10u)
                changed.descriptorTables.clear();
            assert(validate(changed));
        }
        if constexpr (compute)
            for (std::uint32_t mutate = 0u; mutate < 8u; ++mutate) {
                auto changed = rhi::PipelineLayoutStateCopy::copy(layout);
                auto& binding = changed.descriptorLayouts[0].bindings[0];
                if (mutate == 0u)
                    changed.descriptorSets[0].set = 0u;
                if (mutate == 1u)
                    binding.binding = 1u;
                if (mutate == 2u)
                    binding.type = rhi::DescriptorType::ReadOnlyStorageBuffer;
                if (mutate == 3u)
                    binding.arrayCount = 3u;
                if (mutate == 4u)
                    binding.dynamicOffset = true;
                if (mutate == 5u)
                    binding.tableBacked = true;
                if (mutate == 6u)
                    binding.stages = rhi::ShaderStage::Fragment;
                if (mutate == 7u)
                    changed.descriptorSets.clear();
                assert(validate(changed));
            }
        auto edited = package.entrypoints.front();
        edited.entryPoint = "wrong";
        const auto wrongEntries = [&](std::size_t i) -> const auto& {
            return i ? package.entrypoints[i] : edited;
        };
        assert(Contract::validateTableExpansion(
            {1u, true}, input, layout, package, count, kind, wrongEntries));
        if constexpr (compute) {
            edited = package.entrypoints.front();
            ++edited.computeWorkgroupSizeX;
            assert(Contract::validateTableExpansion(
                {1u, true}, input, layout, package, count, kind, wrongEntries));
        }
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!validate(layout));
        std::pmr::set_default_resource(previous);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto gate =
                arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::tableLowering(
                    backend);
            assert(!gate.executionAvailable && !gate.positionalAvailable);
        }
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_DECLARATION_OK %s exact-input3 table3 depth-D2 stage output entries workgroup negatives native-closed\n",
            compute ? "Compute" : "Graphics");
    }

    static rhi::PipelineLayoutDesc declarationLayout(bool compute)
    {
        auto layout = rhi::PipelineLayoutStateCopy::copy(rhi::PipelineLayoutDesc{});
        layout.descriptorTables.push_back({0u,
            core::pmr::String("Inputs", &core::defaultCpuMemoryResource()),
            rhi::DescriptorType::SampledTexture,
            compute ? rhi::ShaderStage::Compute : rhi::ShaderStage::Fragment,
            3u,
            true,
            rhi::TextureViewDimension::Texture2D,
            true});
        if (compute) {
            rhi::DescriptorLayoutDesc output{
                core::pmr::String("Output", &core::defaultCpuMemoryResource()),
                core::pmr::Vector<rhi::DescriptorBindingDesc>{&core::defaultCpuMemoryResource()}};
            rhi::DescriptorBindingDesc binding;
            binding.type = rhi::DescriptorType::StorageBuffer;
            binding.stages = rhi::ShaderStage::Compute;
            output.bindings.push_back(binding);
            layout.descriptorLayouts.push_back(std::move(output));
            layout.descriptorSets.push_back(
                {1u, core::pmr::String("Output", &core::defaultCpuMemoryResource())});
        }
        return layout;
    }

    template <class Input> static void verifySpace(std::shared_ptr<const Input> input)
    {
        constexpr bool compute = std::is_same_v<Input, rhi::OriginalSingleEntryPipelineInput>;
        Space::Budget budget{1u, true, 65536u, compute ? 4u : 3u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        auto factory = [&] {
            return Space::prepare(input, budget, quota, diagnostic);
        };
        for (const auto domain : std::array{input->quota(), input->tableExpansion()->quota()}) {
            auto authored = budget;
            authored.maxCpuMetadataBytes = domain->limitBytes();
            assert(!Space::prepare(input, authored, domain, diagnostic)
                && diagnostic == "RHI-WEBGPU-TABLE-SPACE-QUOTA-DOMAIN");
        }
        for (std::uint32_t mutate = 0u; mutate < 4u; ++mutate) {
            auto authored = budget;
            if (mutate == 0u)
                authored.contractVersion = 2u;
            if (mutate == 1u)
                authored.prototypeOptIn = false;
            if (mutate == 2u)
                --authored.maxPhysicalBindings;
            if (mutate == 3u)
                authored.maxCpuMetadataBytes = 0u;
            assert(!Space::prepare(input, authored, quota, diagnostic) && !quota->liveBytes());
        }
        assert(!Space::prepare(input, budget, std::shared_ptr<core::CpuMemoryQuota>{}, diagnostic));
        for (const auto& invalid :
            std::array{std::shared_ptr<const Input>(input->quota(), input.get()),
                std::shared_ptr<const Input>(std::shared_ptr<const Input>{}, input.get()),
                std::shared_ptr<const Input>{}})
            assert(!Space::prepare(invalid, budget, quota, diagnostic)
                && diagnostic == "RHI-ORIGINAL-TABLE-INPUT-OWNER");
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measureSingleUse([] {}, factory);
        pressure.verifySingleUse(peak, [] {}, factory);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto space = factory();
        std::pmr::set_default_resource(previous);
        if (!space)
            std::fprintf(stderr, "table projection %s\n", diagnostic.c_str());
        assert(space);
        assert(!Space::validateOriginal(space) && !space->nativeExecutionProven());
        assert(space->entryCount() == budget.maxPhysicalBindings
            && space->physicalGroupCount() == (compute ? 2u : 1u));
        for (std::uint32_t i = 0u; i < space->entryCount(); ++i) {
            const auto& entry = space->entry(i);
            assert(entry.physicalCardinality == 1u);
            assert(entry.logical.arrayCount == (i < 3u ? 3u : 1u));
            assert(entry.logical.set == (i < 3u ? 0u : 1u) && !entry.logical.binding);
            assert(entry.physicalSet == (i < 3u ? 0u : 1u)
                && entry.physicalBinding == (i < 3u ? i : 0u));
            assert(entry.group == (i < 3u ? 0u : 1u) && entry.binding == (i < 3u ? i : 0u));
            assert(
                entry.arrayElement == (i < 3u ? i : 0u) && entry.logical.depthTexture == (i < 3u));
            const auto& expectedName = i < 3u ? input->tableExpansion()->elements()[i].name
                                              : input->tableExpansion()->logicalBindings()[1u].name;
            assert(entry.physicalName == expectedName
                && entry.physicalName.get_allocator().resource()
                    == &core::defaultCpuMemoryResource());
        }
        auto forged = std::shared_ptr<const Space>(quota, space.get());
        auto nonowning = std::shared_ptr<const Space>(std::shared_ptr<const Space>{}, space.get());
        const auto ceiling = quota->limitBytes();
        quota->setLimitBytes(ceiling - 1u);
        is(Space::validateOriginal(space), "RHI-WEBGPU-TABLE-SPACE-QUOTA-LIMIT");
        quota->setLimitBytes(ceiling);
        const auto inputCeiling = input->quota()->limitBytes();
        input->quota()->setLimitBytes(inputCeiling - 1u);
        is(Space::validateOriginal(space), "RHI-ORIGINAL-TABLE-INPUT-QUOTA-LIMIT");
        input->quota()->setLimitBytes(inputCeiling);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota, input->quota(), input->tableExpansion()->quota()}, [&]() noexcept {
                for (const auto& invalid :
                    std::array{forged, nonowning, std::shared_ptr<const Space>{}})
                    is(Space::validateOriginal(invalid), "RHI-WEBGPU-TABLE-SPACE-OWNER");
                return Space::validateOriginal(space);
            }));
        OriginalDepthTableExpansionPipelineFixture::verify(
            input, space, declarationLayout(compute));
        input.reset();
        assert(!Space::validateOriginal(space)); // projection retains original input/targets.
        forged.reset();
        nonowning.reset();
        space.reset();
        assert(!quota->liveBytes());
        std::printf(
            "ORIGINAL_TABLE_EXPANSION_SPACE_OK %s ordered3 logical3 physical1 output-preserved caller-drop quota%llu hot0 native-closed\n",
            compute ? "Compute" : "Graphics",
            static_cast<unsigned long long>(peak));
    }

public:
    static void verify(const std::shared_ptr<const Artifact>& artifact)
    {
        if (artifact->profile() == Artifact::Profile::Compute)
            verifyInput<rhi::OriginalSingleEntryPipelineInput>(artifact);
        else
            verifyInput<rhi::OriginalGraphicsPipelineInput>(artifact);
    }
};
} // namespace arp::test
