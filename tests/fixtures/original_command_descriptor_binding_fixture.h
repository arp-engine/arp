#pragma once
#include "original_command_pipeline_binding_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/frontend/command_texture_ranges.h"

namespace arp::test {
// All roots/pipelines/descriptors are supplied by the existing native fixtures.
class OriginalCommandDescriptorBindingFixture final {
    using Inputs = rhi::OriginalCommandInputs;
    using Binding = rhi::OriginalCommandDescriptorBinding;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "descriptor positional expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    template <class Pipeline>
    static auto verify(const std::shared_ptr<const rhi::OriginalCommandResourceSources>& root,
        std::size_t pipelineIndex,
        std::size_t descriptorIndex,
        const std::shared_ptr<core::CpuMemoryQuota>& graph,
        const rhi::GpuDevice& device,
        std::size_t attachmentIndex = SIZE_MAX,
        bool report = true)
    {
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Usage = rhi::OriginalCommandUsage;
        constexpr bool graphics = std::is_same_v<Pipeline, rhi::RenderPipeline>;
        constexpr auto begin = graphics ? K::BeginRenderPass : K::BeginComputePass;
        constexpr auto pipelineBind = graphics ? K::BindGraphicsPipeline : K::BindComputePipeline;
        constexpr auto descriptorBind =
            graphics ? K::BindRenderDescriptor : K::BindComputeDescriptor;
        constexpr auto end = graphics ? K::EndRenderPass : K::EndComputePass;
        const auto& pipeline =
            std::get<std::shared_ptr<const rhi::OriginalCommandPipelineSource<Pipeline>>>(
                root->sources()[pipelineIndex]);
        const auto& descriptor =
            std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                root->sources()[descriptorIndex]);
        constexpr std::uint32_t attachment = graphics ? 1u : 0u,
                                totalReferences = 6u + 2u * attachment;
        const std::array<rhi::CommandUsage, 11u> commands{{{begin, 0u, attachment},
            {pipelineBind, attachment, 1u},
            {descriptorBind, attachment + 1u, 1u},
            {pipelineBind, attachment + 2u, 1u},
            {descriptorBind, attachment + 3u, 1u},
            {end, attachment + 4u, 0u},
            {begin, attachment + 4u, attachment},
            {pipelineBind, 2u * attachment + 4u, 1u},
            {descriptorBind, 2u * attachment + 5u, 1u},
            {end, totalReferences, 0u},
            {K::Finish, totalReferences, 0u}}};
        std::array<Usage::Reference, totalReferences> refs{};
        std::size_t cursor = 0u;
        if constexpr (graphics)
            refs[cursor++] = {attachmentIndex};
        refs[cursor++] = {pipelineIndex};
        refs[cursor++] = {descriptorIndex};
        refs[cursor++] = {pipelineIndex};
        refs[cursor++] = {descriptorIndex};
        if constexpr (graphics)
            refs[cursor++] = {attachmentIndex};
        refs[cursor++] = {pipelineIndex};
        refs[cursor++] = {descriptorIndex};
        assert(cursor == refs.size());
        const char* id = nullptr;
        auto usage = Usage::prepareOriginal(
            root, commands.data(), commands.size(), refs.data(), refs.size(), graph, id);
        assert(usage);
        std::array<std::uint32_t, 2u> offsets{};
        std::uint32_t offsetCount = 0u;
        for (const auto& declaration : descriptor->frozenFiniteDeclarations())
            if (rhi::findDescriptorSetBinding(
                    descriptor->frozenDefinition(), declaration.write.binding)
                    ->dynamicOffset)
                offsets[offsetCount++] = declaration.dynamicOffset;
        if (descriptor->sourceContractVersion() == 2u)
            for (const auto& binding : descriptor->frozenDefinition().bindings)
                if (binding.dynamicOffset)
                    offsets[offsetCount++] = 0u;
        const auto field = [&](std::size_t i) {
            if (rhi::OriginalCommandDescriptorBindingContract::isBinding(commands[i].kind))
                return rhi::CommandVariableArguments::Input{
                    *Scalar::capture(
                        descriptorBind, Scalar::DescriptorBinding{descriptor->frozenLogicalSet()}),
                    {},
                    offsets.data(),
                    offsetCount};
            return rhi::CommandVariableArguments::Input{
                *Scalar::capture(commands[i].kind, Scalar::None{}), {}};
        };
        std::array<rhi::CommandVariableArguments::Input, 11u> fields{{field(0u),
            field(1u),
            field(2u),
            field(3u),
            field(4u),
            field(5u),
            field(6u),
            field(7u),
            field(8u),
            field(9u),
            field(10u)}};
        const auto bytes = graph->limitBytes();
        const auto makeScalars = [&] {
            auto arguments = rhi::CommandVariableArguments::prepareOriginal(fields.data(),
                fields.size(),
                {1u, true, 11u, root->originalBudget().maxDynamicOffsets, 0u, 0u, bytes},
                graph,
                id);
            assert(arguments);
            auto scalars =
                rhi::OriginalCommandScalars::prepareOriginal(usage, arguments, graph, id);
            assert(scalars);
            return scalars;
        };
        auto scalars = makeScalars();
        const auto pipelineGeneration =
            pipeline->originalObject()->originalResourceRegistration().generation();
        Binding binding{descriptorBind,
            descriptorIndex,
            pipelineIndex,
            0u,
            descriptor->originalObject()->originalResourceRegistration().generation(),
            descriptor->frozenContentRevision()};
        const Inputs::OriginalDescriptor empty{SIZE_MAX, Binding{}};
        std::array<Inputs::OriginalDescriptor, 8u> records{
            {empty, empty, empty, empty, empty, empty, empty, empty}};
        std::size_t count = 0u;
        std::shared_ptr<const rhi::RenderPassArguments> pass;
        if constexpr (graphics) {
            const auto& view =
                std::get<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                    root->sources()[attachmentIndex]);
            rhi::RenderPassDesc desc;
            rhi::RenderPassColorAttachmentDesc target;
            target.target = view->resource();
            target.format = view->originalViewLease()->layout().format;
            target.loadOp = rhi::LoadOp::Clear;
            target.storeOp = rhi::StoreOp::Store;
            desc.colorAttachments.push_back(target);
            pass = rhi::RenderPassArguments::prepareOriginal(
                desc, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, graph, id);
            assert(pass);
            records[count++] = {0u, pass};
        }
        records[count++] = {1u,
            rhi::OriginalCommandPipelineBinding{
                pipelineBind, pipelineIndex, 0u, pipelineGeneration}};
        const auto firstDescriptor = count;
        records[count++] = {2u, binding};
        records[count++] = {3u,
            rhi::OriginalCommandPipelineBinding{
                pipelineBind, pipelineIndex, 0u, pipelineGeneration}};
        records[count++] = {4u, binding};
        if constexpr (graphics)
            records[count++] = {6u, pass};
        records[count++] = {7u,
            rhi::OriginalCommandPipelineBinding{
                pipelineBind, pipelineIndex, 6u, pipelineGeneration}};
        binding.scopeBeginCommandIndex = 6u;
        records[count++] = {8u, binding};
        rhi::OriginalCommandInputsBudget budget{3u, true, 3u, 3u, 0u, 3u * offsetCount};
        const auto baseline = graph->liveBytes();
        const auto factory = [&] {
            return Inputs::prepareOriginal(scalars, records.data(), count, budget, graph, id);
        };
        const auto reject = [&](const char* expected) {
            assert(!factory());
            is(id, expected);
            assert(graph->liveBytes() == baseline);
        };
        auto saved = records[firstDescriptor];
        const auto negative = [&](auto edit, const char* expected) {
            edit(std::get<Binding>(records[firstDescriptor].arguments));
            reject(expected);
            records[firstDescriptor] = saved;
        };
        negative(
            [](auto& b) {
                ++b.registrationGeneration;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-GENERATION");
        negative(
            [](auto& b) {
                ++b.contentRevision;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-REVISION");
        negative(
            [](auto& b) {
                b.sourceIndex = 0u;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SOURCE");
        negative(
            [](auto& b) {
                b.pipelineSourceIndex = 0u;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-PIPELINE");
        negative(
            [](auto& b) {
                b.scopeBeginCommandIndex = 6u;
            },
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-SCOPE");
        records[firstDescriptor].commandIndex = 3u;
        reject("RHI-ORIGINAL-INPUTS-ORDER");
        records[firstDescriptor] = saved;
        --budget.maxDescriptorBindings;
        reject("RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-CAPACITY");
        ++budget.maxDescriptorBindings;
        if (offsetCount) {
            --budget.maxFrozenDynamicOffsets;
            reject("RHI-ORIGINAL-TYPED-INPUT-OFFSET-CAPACITY");
            ++budget.maxFrozenDynamicOffsets;
        }
        auto invalid = budget;
        invalid.maxFrozenDynamicOffsets = root->originalBudget().maxDynamicOffsets + 1u;
        assert(!Inputs::prepareOriginal(scalars, records.data(), count, invalid, graph, id));
        is(id, "RHI-ORIGINAL-TYPED-INPUT-GRAPH-CAPACITY");
        assert(!Inputs::prepareOriginal(scalars, records.data(), count, {2u, true, 3u}, graph, id));
        is(id, "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING");
        assert(!Inputs::prepareOriginal(scalars, records.data(), count, graph, id));
        is(id, "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING");
        CpuQuotaFactoryFixture pressure(graph);
        const auto peak = pressure.measureSingleUse([] {}, factory);
        pressure.verify(peak, factory);
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto result = factory();
        std::pmr::set_default_resource(previous);
        if (!result)
            std::fprintf(stderr, "descriptor Inputs3 failed: %s\n", id ? id : "no diagnostic");
        assert(result);
        assert(result->retainedOwnerCount() == scalars->retainedOwnerCount() + 1u + attachment);
        std::array<Inputs::IncomingReference, 1u> incomingReference{{descriptor->originalObject()}};
        Inputs::Incoming incoming{fields[2u], std::monostate{}, incomingReference.data(), 1u};
        assert(!result->compareOriginalInput(2u, incoming));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{graph, pipeline->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    if (const auto* error = result->compareOriginalInput(2u, incoming))
                        return error;
                    return result->validate();
                }));
        });
        if (offsetCount) {
            const auto old = offsets[0u];
            ++offsets[0u];
            assert(result->compareOriginalInput(2u, incoming));
            auto wrongScalars = makeScalars();
            assert(
                !Inputs::prepareOriginal(wrongScalars, records.data(), count, budget, graph, id));
            is(id,
                descriptor->sourceContractVersion() == 3u
                    ? "RHI-ORIGINAL-TYPED-INPUT-FINITE-DYNAMICS"
                    : "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-RANGE-UNPROVEN");
            offsets[0u] = old;
            fields[2u].offsetCount = 0u;
            auto missing = makeScalars();
            assert(!Inputs::prepareOriginal(missing, records.data(), count, budget, graph, id));
            is(id, "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-OFFSETS");
            fields[2u].offsetCount = offsetCount + 1u;
            auto extra = makeScalars();
            assert(!Inputs::prepareOriginal(extra, records.data(), count, budget, graph, id));
            is(id, "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-OFFSETS");
            fields[2u].offsetCount = offsetCount;
        }
        fields[2u].scalar = *Scalar::capture(
            descriptorBind, Scalar::DescriptorBinding{descriptor->frozenLogicalSet() + 1u});
        auto wrongSlot = makeScalars();
        assert(!Inputs::prepareOriginal(wrongSlot, records.data(), count, budget, graph, id));
        is(id, "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT");
        fields[2u].scalar = field(2u).scalar;
        incomingReference[0u] = pipeline->originalObject();
        assert(result->compareOriginalInput(2u, incoming));
        incomingReference[0u] = descriptor->originalObject();
        assert(!result->validate() && !result->nativeExecutionProven()
            && result->executionDiagnostic(device.backend()));
        if (report)
            std::printf(
                "BINDLESS_RHI_DESCRIPTOR_POSITIONAL_OK %s source%u peak%llu commands11 binds3 scopes2 original-root-generation-revision frozen-dynamics cold-exact-less-exhausted incoming hot0 query0 state-native-closed\n",
                graphics ? "graphics" : "compute",
                descriptor->sourceContractVersion(),
                static_cast<unsigned long long>(peak));
        if constexpr (graphics) {
            std::array<rhi::CommandUsage, 12u> mixed{};
            for (std::size_t i = 0u; i < 10u; ++i)
                mixed[i] = commands[i];
            mixed[10u] = {K::TextureBarrier, totalReferences, 1u};
            mixed[11u] = {K::Finish, totalReferences + 1u, 0u};
            std::array<Usage::Reference, 9u> mixedRefs{};
            for (std::size_t i = 0u; i < refs.size(); ++i)
                mixedRefs[i] = refs[i];
            const auto parentIndex = attachmentIndex - 1u;
            mixedRefs.back() = {parentIndex};
            auto mixedUsage = Usage::prepareOriginal(
                root, mixed.data(), mixed.size(), mixedRefs.data(), mixedRefs.size(), graph, id);
            assert(mixedUsage);
            const std::array<rhi::CommandVariableArguments::Input, 12u> mixedFields{{fields[0u],
                fields[1u],
                fields[2u],
                fields[3u],
                fields[4u],
                fields[5u],
                fields[6u],
                fields[7u],
                fields[8u],
                fields[9u],
                {*Scalar::capture(K::TextureBarrier, Scalar::None{}), {}},
                fields[10u]}};
            auto mixedArguments = rhi::CommandVariableArguments::prepareOriginal(mixedFields.data(),
                mixedFields.size(),
                {1u, true, 12u, root->originalBudget().maxDynamicOffsets, 0u, 0u, bytes},
                graph,
                id);
            assert(mixedArguments);
            auto mixedScalars =
                rhi::OriginalCommandScalars::prepareOriginal(mixedUsage, mixedArguments, graph, id);
            assert(mixedScalars);
            const auto& parent = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                root->sources()[parentIndex]);
            rhi::ResourceBarrierDesc barrier;
            barrier.resource = parent->resource();
            barrier.textureSubresources =
                rhi::defaultTextureSubresourceRange(parent->originalObject()->desc());
            barrier.before = rhi::ResourceState::ColorAttachment;
            barrier.after = rhi::ResourceState::CopySrc;
            barrier.sourceStages = rhi::PipelineStage::ColorAttachmentOutput;
            barrier.destinationStages = rhi::PipelineStage::Transfer;
            barrier.sourceAccess = rhi::ResourceAccess::ColorAttachmentWrite;
            barrier.destinationAccess = rhi::ResourceAccess::TransferRead;
            const auto dependency =
                rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier);
            assert(dependency && count == 8u);
            const std::array<Inputs::OriginalDescriptor, 9u> mixedRecords{{records[0u],
                records[1u],
                records[2u],
                records[3u],
                records[4u],
                records[5u],
                records[6u],
                records[7u],
                {10u, *dependency}}};
            auto projected = Inputs::prepareOriginal(
                mixedScalars, mixedRecords.data(), mixedRecords.size(), budget, graph, id);
            assert(projected);
            assert(std::holds_alternative<rhi::ResourceDependencyArguments>(
                       *projected->descriptorAt(10u))
                && !projected->descriptorAt(11u));
            const auto projection = [&]() noexcept -> const char* {
                std::size_t points = 0u;
                const auto* error = rhi::common::OriginalCommandTextureRanges::visit(projected,
                    [&](const auto& point, const rhi::GpuTexture& texture) noexcept -> const char* {
                        assert(texture.id() == parent->resource());
                        assert(point.commandIndex == (points == 0u ? 0u : points == 1u ? 6u : 10u));
                        assert(point.sourceIndex == (points < 2u ? attachmentIndex : parentIndex));
                        assert(point.range == barrier.textureSubresources);
                        ++points;
                        return nullptr;
                    });
                assert(points == 3u);
                return error;
            };
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{graph, pipeline->originalMetadataQuota()}, projection));
            });
            if (report)
                std::printf(
                    "BINDLESS_RHI_TYPED_TEXTURE_PROJECTION_OK source%u commands12 records9 exact-passes2 barrier-after-typed-scalar-records original-ranges3 hot0 query0 state-native-closed\n",
                    descriptor->sourceContractVersion());
        }
        if constexpr (std::is_same_v<Pipeline, rhi::RenderPipeline>)
            if (report)
                OriginalCommandDescriptorConsumptionFixture::verify<Pipeline>(
                    result, device, false, true);
        return OriginalCommandDescriptorConsumptionFixture::verify<Pipeline>(
            result, device, report);
    }
};
} // namespace arp::test
