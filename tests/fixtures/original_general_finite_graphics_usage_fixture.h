#pragma once
#include "original_general_finite_graphics_state_stream_fixture.h"
#include "original_general_finite_usage_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteGraphicsUsageFixture final {
    using P = rhi::common::OriginalTypedUsageProjection;
    using D = rhi::common::OriginalTypedDependencyProjection;
    using S = rhi::semantics::TypedShaderUsageScopeContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "finite Graphics Usage6/Dependency6: %s\n", id);
        assert(!id);
    }

public:
    static P::Budget budget(bool dependencies = true, bool initialized = false) noexcept
    {
        auto value = OriginalGeneralFiniteUsageFixture::budget();
        value.maxCommands = (dependencies ? 12u : 8u) + (initialized ? 2u : 0u);
        value.renderBindings =
            OriginalGeneralFiniteConsumptionFixture::renderBudget(value.maxCommands);
        value.maxAttachmentReferences = 1u;
        value.maxContributions = 7u;
        value.maxTextureAccesses = 4u;
        value.maxTextureCells = initialized ? 7u : 5u;
        return value;
    }

    static D::Request dependencyBudget(bool dependencies = true, bool initialized = false) noexcept
    {
        // Seven memory contributions: one outer pass, seven pair passes and
        // two neighborhood passes per each of four dependencies. C(7,2)=21.
        if (initialized)
            return {6u,
                true,
                {6u, true, budget(dependencies, true)},
                dependencies ? 6u : 2u,
                dependencies ? 24u : 14u,
                dependencies ? 168u : 98u,
                21u,
                dependencies ? 14u : 5u,
                dependencies ? 7u : 3u};
        return {6u, true, {6u, true, budget(dependencies)}, 4u, 16u, 112u, 21u, 7u, 3u};
    }

    static void verifyContract()
    {
        OriginalGeneralFiniteUsageFixture::verifyContract();
        for (bool dependencies : {false, true})
            for (bool initialized : {false, true}) {
                require(rhi::OriginalTypedUsageProjectionContract::validateBudget(
                    budget(dependencies, initialized)));
                require(rhi::OriginalTypedDependencyProjectionContract::validateIntent(
                    dependencyBudget(dependencies, initialized)));
            }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_CONTRACT_OK exact-State5-Stream5-Inputs6 scope5 attachment1 bind3 draw3 sampler-nonmemory native-closed");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Retained = OriginalGeneralFiniteGraphicsStateStreamFixture::Retained;

    static void verifyEmpty(const Retained& retained, bool initialized = false)
    {
        std::uint32_t emitted = 0u;
        const auto result = D::visit(retained.program,
            retained.graph,
            dependencyBudget(false, initialized),
            [&](const auto&) noexcept -> const char* {
                ++emitted;
                return nullptr;
            });
        if (initialized) {
            require(result.diagnostic);
            assert(emitted == 2u && result.counts.dependencies == 2u && result.counts.initial == 2u
                && !result.counts.final && !result.counts.between
                && result.counts.projectionPasses == 14u && result.counts.contributionVisits == 98u
                && result.counts.pairChecks == 21u && result.counts.endpointContributions == 5u
                && result.counts.textureCells == 3u && !result.nativeExecutionProven()
                && !result.synchronizationProven());
        } else
            assert(result.diagnostic
                && core::StringView(result.diagnostic) == "RHI-ORIGINAL-DEPENDENCY-PROJECTION-EMPTY"
                && !emitted);
        std::puts(initialized
                ? "BINDLESS_RHI_GENERAL_GRAPHICS_INITIALIZED_DEPENDENCY_INITIAL_ONLY_OK genuine-ten-command-render-stream initial2 final0 passes14 visits98 pairs21 endpoints5 cells3 no-native-synchronization"
                : "BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_EMPTY_REFUSED genuine-eight-command-render-stream no-barrier-no-dependency-authority");
    }

    static void verifyActual(
        const Retained& retained, const rhi::GpuDevice& device, bool initialized = false)
    {
        const std::size_t prefix = initialized ? 2u : 0u;
        const auto& stream = retained.program;
        const auto& graph = retained.graph;
        std::array<P::Point, 7u> points;
        std::size_t emitted = 0u;
        const auto result = P::visit(stream,
            device,
            budget(true, initialized),
            [&](const P::Point& point) noexcept -> const char* {
                assert(emitted < points.size());
                points[emitted++] = point;
                return nullptr;
            });
        require(result.diagnostic);
        const auto& counts = result.counts;
        assert(emitted == 7u && counts.contributions == 7u && counts.renderBindings == 1u
            && counts.renderBindingBuffers == 1u && counts.shaderConsumptions == 1u
            && counts.shaderBuffers == 1u && counts.renderBindingTextures == 2u
            && counts.shaderTextures == 2u && counts.renderBindingSamplers == 1u
            && counts.shaderSamplers == 1u && counts.attachments == 1u
            && counts.textureCells == (initialized ? 7u : 5u) && !result.nativeExecutionProven()
            && !result.synchronizationProven());
        const auto& sources =
            stream->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto& descriptor =
            *std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                sources->sources()[9u]);
        const auto& selected = descriptor.frozenElements()[0].declaration;
        const auto& lease = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
            sources->sources()[0u]);
        rhi::DescriptorBufferBindingRangeContract::Resolved interval;
        require(
            rhi::DescriptorBufferBindingRangeContract::resolve(selected.write.bufferBindingRange,
                lease->layout().sizeBytes,
                selected.dynamicOffset,
                interval));
        for (std::size_t i = 0u; i < 6u; ++i)
            std::visit(
                [&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, P::BufferPoint>
                        || std::is_same_v<T, P::TexturePoint>) {
                        const auto& use = value.use;
                        const auto& original = value.original;
                        assert(use.commandIndex == prefix + (i < 3u ? 4u : 5u)
                            && use.scope.kind == S::Kind::RenderPass && use.scope.first == prefix
                            && use.scope.last == prefix + 6u
                            && use.contribution
                                == (i < 3u ? S::Contribution::RenderBinding
                                           : S::Contribution::ShaderConsumption)
                            && original.pipelineSourceIndex == 8u
                            && original.pipelineBindCommandIndex == prefix + 1u
                            && original.descriptorSourceIndex == 9u
                            && original.descriptorBindCommandIndex == prefix + 4u
                            && original.descriptorContentRevision
                                == descriptor.frozenContentRevision()
                            && use.access.stages == rhi::PipelineStage::FragmentShader);
                        if constexpr (std::is_same_v<T, P::BufferPoint>) {
                            assert(i % 3u == 0u && original.bufferSourceIndex == 0u
                                && use.offsetBytes == interval.offsetBytes
                                && use.sizeBytes == interval.sizeBytes
                                && use.bufferBytes == lease->layout().sizeBytes
                                && use.usage
                                    == (selected.write.type == rhi::DescriptorType::UniformBuffer
                                            ? S::BufferUsage::UniformRead
                                            : S::BufferUsage::StorageRead));
                        } else {
                            const auto parent = i % 3u == 1u ? 1u : 3u;
                            assert(original.textureParentSourceIndex == parent
                                && original.textureViewSourceIndex == parent + 1u
                                && use.range == original.textureRange
                                && use.usage
                                    == (parent == 1u ? S::TextureUsage::Storage
                                                     : S::TextureUsage::SampledRead));
                        }
                    } else
                        assert(false);
                },
                points[i]);
        const auto& attachment = std::get<P::AttachmentPoint>(points[6]);
        assert(attachment.original.commandIndex == prefix && attachment.original.sourceIndex == 7u
            && attachment.scope.kind == S::Kind::RenderPass && attachment.scope.first == prefix
            && attachment.scope.last == prefix + 6u
            && attachment.access.state == rhi::ResourceState::ColorAttachment
            && attachment.access.stages == rhi::PipelineStage::ColorAttachmentOutput
            && attachment.access.access
                == (rhi::ResourceAccess::ColorAttachmentRead
                    | rhi::ResourceAccess::ColorAttachmentWrite));
        const auto captured =
            P::inspectStateBound(stream, graph, dependencyBudget(true, initialized).usages);
        require(captured.diagnostic);
        assert(captured.counts.matches(counts));
        for (const auto member : {&P::Budget::maxCommands,
                 &P::Budget::maxContributions,
                 &P::Budget::maxTextureAccesses,
                 &P::Budget::maxTextureCells,
                 &P::Budget::maxAttachmentReferences}) {
            auto less = budget(true, initialized);
            --(less.*member);
            emitted = 0u;
            assert(P::visit(stream,
                       device,
                       less,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                       .diagnostic
                && !emitted);
        }
        std::array<D::Record, 6u> records;
        emitted = 0u;
        const auto decisions = D::visit(stream,
            graph,
            dependencyBudget(true, initialized),
            [&](const D::Record& record) noexcept -> const char* {
                assert(emitted < records.size());
                records[emitted++] = record;
                return nullptr;
            });
        require(decisions.diagnostic);
        const auto& totals = decisions.counts;
        assert(emitted == (initialized ? 6u : 4u) && totals.dependencies == emitted
            && totals.final == 4u && totals.initial == (initialized ? 2u : 0u) && !totals.between
            && totals.endpointContributions == (initialized ? 14u : 7u)
            && totals.projectionPasses == (initialized ? 24u : 16u)
            && totals.contributionVisits == (initialized ? 168u : 112u) && totals.pairChecks == 21u
            && totals.textureCells == (initialized ? 7u : 3u) && !decisions.nativeExecutionProven()
            && !decisions.synchronizationProven());
        for (std::size_t i = 0u; i < emitted; ++i) {
            const auto& record = records[i];
            if (i < prefix) {
                assert(record.projectionVersion == 6u && record.commandIndex == i
                    && record.descriptorIndex == i && record.referenceIndex == i
                    && record.sourceIndex == (i == 0u ? 3u : 6u)
                    && record.clause == D::Clause::InitialBookkeeping && !record.preceding
                    && record.following && record.following->contributions == (i == 0u ? 2u : 1u)
                    && record.following->scope.kind == S::Kind::RenderPass
                    && record.following->scope.first == 2u && record.following->scope.last == 8u
                    && record.before
                        == (i == 0u ? rhi::ResourceState::CopyDst : rhi::ResourceState::Undefined)
                    && record.after
                        == (i == 0u ? rhi::ResourceState::ShaderRead
                                    : rhi::ResourceState::ColorAttachment)
                    && record.range.mipLevelCount == (i == 0u ? 2u : 1u));
            } else {
                const auto final = i - prefix;
                assert(record.projectionVersion == 6u && record.commandIndex == prefix + 7u + final
                    && record.descriptorIndex == prefix + 3u + final
                    && record.referenceIndex == prefix + 3u + final
                    && record.sourceIndex
                        == (final == 0u       ? 0u
                                : final == 1u ? 1u
                                : final == 2u ? 3u
                                              : 6u)
                    && record.clause == D::Clause::FinalBookkeeping && record.preceding
                    && !record.following
                    && record.preceding->contributions == (final == 3u ? 1u : 2u)
                    && record.preceding->scope.kind == S::Kind::RenderPass
                    && record.preceding->scope.first == prefix
                    && record.preceding->scope.last == prefix + 6u
                    && record.before == record.after);
            }
            require(D::validateCoverage(record, initialized ? 7u : 3u));
            D::Counts exact;
            require(D::validateFrozenBinding(
                stream, graph, dependencyBudget(true, initialized), record, exact));
            for (std::uint32_t field = 0u; field < 5u; ++field) {
                auto hostile = record;
                if (field == 0u)
                    ++hostile.registrationGeneration;
                if (field == 1u)
                    ++hostile.descriptorIndex;
                if (field == 2u)
                    hostile.projectionVersion = 5u;
                if (field == 3u)
                    ++(hostile.preceding ? hostile.preceding : hostile.following)->contributions;
                if (field == 4u)
                    --(hostile.preceding ? hostile.preceding : hostile.following)->scope.last;
                D::Counts invalid;
                assert(D::validateFrozenBinding(
                    stream, graph, dependencyBudget(true, initialized), hostile, invalid));
            }
        }
        for (const auto member : {&D::Request::maxDependencies,
                 &D::Request::maxProjectionPasses,
                 &D::Request::maxContributionVisits,
                 &D::Request::maxPairChecks,
                 &D::Request::maxEndpointContributions,
                 &D::Request::maxTextureCells}) {
            auto less = dependencyBudget(true, initialized);
            --(less.*member);
            emitted = 0u;
            assert(D::visit(stream,
                       graph,
                       less,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                       .diagnostic
                && !emitted);
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    stream->originalInputs()->originalMetadataQuota(),
                    graph->originalMetadataQuota(),
                    stream->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    const auto projected = P::inspectStateBound(
                        stream, graph, dependencyBudget(true, initialized).usages);
                    return projected.diagnostic
                        ? projected.diagnostic
                        : D::inspect(stream, graph, dependencyBudget(true, initialized)).diagnostic;
                }));
        });
        if (initialized)
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_INITIALIZED_USAGE6_DEPENDENCY6_OK commands14 contributions7 texture-cells7 attachment1 bind3 draw3 samplers2 dependencies6 initial2 final4 passes24 visits168 pairs21 endpoints14 dependency-cells7 scope2..8 whole-sampled-mips2 frozen-native-closed");
        else
            std::printf(
                "BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_OK commands12 contributions7 attachment1 bind3 draw3 samplers2 barriers4 final4 passes16 visits112 pairs21 endpoints7 cells3 offset%llu bytes%llu exact-less hostile-refused preflight-no-output hot0-query0 frozen-native-closed\n",
                static_cast<unsigned long long>(interval.offsetBytes),
                static_cast<unsigned long long>(interval.sizeBytes));
    }

    static void verifyInvalidated(
        const Retained& retained, const rhi::GpuDevice& device, bool initialized = false)
    {
        verifyInvalidated(retained, &device, initialized);
    }

    static void verifyInvalidated(
        const Retained& retained, const rhi::GpuDevice* device, bool initialized = false)
    {
        std::uint32_t emitted = 0u;
        const auto visitor = [&](const auto&) noexcept -> const char* {
            ++emitted;
            return nullptr;
        };
        if (device)
            assert(
                P::visit(retained.program, *device, budget(true, initialized), visitor).diagnostic
                && !emitted);
        else
            assert(retained.program->validate() && retained.graph->validate());
        assert(P::visitStateBound(retained.program,
                   retained.graph,
                   dependencyBudget(true, initialized).usages,
                   visitor)
                   .diagnostic
            && !emitted);
        assert(
            D::visit(retained.program, retained.graph, dependencyBudget(true, initialized), visitor)
                .diagnostic
            && !emitted);
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_TERMINAL_OK original-attachment-state-stream-invalid preflight-no-output native-closed");
    }
#endif
};
} // namespace arp::test
