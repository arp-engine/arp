#pragma once
#include "original_general_finite_state_stream_fixture.h"
#include "rhi/frontend/command_usage_projection.h"

namespace arp::test {
class OriginalGeneralFiniteUsageFixture final {
    using P = rhi::common::OriginalTypedUsageProjection;
    using D = rhi::common::OriginalTypedDependencyProjection;
    using C = rhi::OriginalTypedUsageProjectionContract;
    using S = rhi::semantics::TypedShaderUsageScopeContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General Usage6/Dependency6: %s\n", id);
        assert(!id);
    }

public:
    static P::Budget budget() noexcept
    {
        P::Budget value;
        value.contractVersion = 6u;
        value.prototypeOptIn = true;
        value.shaderConsumption = OriginalGeneralFiniteConsumptionFixture::budget();
        value.renderBindings = OriginalGeneralFiniteConsumptionFixture::renderBudget(9u);
        value.maxCommands = 9u;
        value.maxContributions = 3u;
        value.maxTextureAccesses = value.maxTextureCells = 2u;
        return value;
    }

    static D::Request dependencyBudget() noexcept
    {
        // Three contributions: one outer pass, three pair passes and two
        // neighborhood passes per genuine barrier. Sampler is nonmemory.
        return {6u, true, {6u, true, budget()}, 3u, 10u, 30u, 3u, 3u, 2u};
    }

    static void verifyContract()
    {
        const auto usage = budget();
        const auto dependency = dependencyBudget();
        assert(!C::validateBudget(usage) && C::programVersion(usage) == 5u
            && C::scopeVersion(usage) == 5u);
        assert(!rhi::OriginalTypedDependencyProjectionContract::validateIntent(dependency));
        for (std::uint32_t version = 0u; version <= 7u; ++version) {
            auto bad = usage;
            bad.contractVersion = version;
            assert(bool(C::validateBudget(bad)) == (version != 6u));
        }
        for (std::uint32_t field = 0u; field < 7u; ++field) {
            auto bad = usage;
            switch (field) {
            case 0u:
                bad.prototypeOptIn = false;
                break;
            case 1u:
                bad.shaderConsumption.generalShaderResourcesVersion = 1u;
                break;
            case 2u:
                bad.renderBindings.generalShaderResourcesVersion = 1u;
                break;
            case 3u:
                bad.shaderConsumption.contractVersion = 3u;
                break;
            case 4u:
                bad.geometryUsage.contractVersion = 1u;
                break;
            case 5u:
                bad.maxReadbacks = bad.maxReadbackMips = bad.maxReadbackDeclaredBytes = 1u;
                break;
            case 6u:
                bad.maxContributions = 0u;
                break;
            }
            assert(C::validateBudget(bad));
        }
        auto bad = dependency;
        bad.usages.contractVersion = 5u;
        assert(rhi::OriginalTypedDependencyProjectionContract::validateIntent(bad));
        rhi::OriginalTypedDependencyProjectionContract::FreezeRequest frozen{
            6u, true, dependency, 128u, 65536u};
        assert(rhi::OriginalTypedDependencyProjectionContract::validateFreezeIntent(frozen));
        frozen.contractVersion = 7u;
        assert(!rhi::OriginalTypedDependencyProjectionContract::validateFreezeIntent(frozen));
        assert(rhi::OriginalTypedDependencyProjectionContract::closureProfileMatches(6u, 8u));
        assert(rhi::OriginalTypedDependencyProjectionContract::closureProfileMatches(7u, 9u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto projection =
                rhi::semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                    backend, 6u);
            const auto state =
                rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(backend, 6u);
            const auto projected =
                rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(backend, 6u);
            assert(projection.projectionAvailable == (backend == rhi::Backend::WebGPU)
                && !projection.executionAvailable
                && projection.validatorOnly == (backend == rhi::Backend::Null));
            assert(state.projectionAvailable == projection.projectionAvailable
                && !state.executionAvailable);
            assert(projected.projectionAvailable == projection.projectionAvailable
                && !projected.executionAvailable);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_USAGE6_CONTRACT_OK exact-State5-Stream5-Inputs6-GeneralResources2 scope5 dry-request6 frozen-native-closed");
    }

    static void verifyUnsupported(const rhi::GpuDevice& device)
    {
        std::uint32_t emitted = 0u;
        const auto result =
            P::visit({}, device, budget(), [&](const auto&) noexcept -> const char* {
                ++emitted;
                return nullptr;
            });
        assert(result.diagnostic && !emitted && !result.nativeExecutionProven());
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Retained = OriginalGeneralFiniteStateStreamFixture::Retained;

    static void verifyEmpty(const Retained& retained)
    {
        auto request = dependencyBudget();
        request.usages.projection.maxCommands = 6u;
        request.usages.projection.renderBindings =
            OriginalGeneralFiniteConsumptionFixture::renderBudget();
        std::uint32_t emitted = 0u;
        const auto result = D::visit(
            retained.program, retained.graph, request, [&](const auto&) noexcept -> const char* {
                ++emitted;
                return nullptr;
            });
        assert(result.diagnostic
            && core::StringView(result.diagnostic) == "RHI-ORIGINAL-DEPENDENCY-PROJECTION-EMPTY"
            && !emitted);
        std::puts(
            "BINDLESS_RHI_GENERAL_USAGE6_EMPTY_REFUSED genuine-six-command-stream no-barrier-no-dependency-authority");
    }

    static void verifyActual(const Retained& retained, const rhi::GpuDevice& device)
    {
        const auto& stream = retained.program;
        const auto& graph = retained.graph;
        const auto usage = budget();
        const auto dependency = dependencyBudget();
        std::array<P::Point, 3u> points;
        std::size_t emitted = 0u;
        const auto result =
            P::visit(stream, device, usage, [&](const P::Point& point) noexcept -> const char* {
                assert(emitted < points.size());
                points[emitted++] = point;
                return nullptr;
            });
        require(result.diagnostic);
        assert(emitted == 3u && result.counts.contributions == 3u
            && result.counts.shaderBuffers == 1u && result.counts.shaderTextures == 2u
            && result.counts.shaderSamplers == 1u && result.counts.textureCells == 2u
            && !result.counts.renderBindings && !result.nativeExecutionProven()
            && !result.synchronizationProven());
        const auto& buffer = std::get<P::BufferPoint>(points[0]);
        const auto& descriptor =
            *std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                stream->originalInputs()
                    ->originalScalars()
                    ->originalUsage()
                    ->originalSources()
                    ->sources()[7u]);
        const auto& declaration = descriptor.frozenElements()[0].declaration;
        const auto type = declaration.write.type;
        assert((type == rhi::DescriptorType::UniformBuffer
                   || type == rhi::DescriptorType::ReadOnlyStorageBuffer)
            && descriptor.frozenDefinition().bindings[0].type == type);
        const auto bufferUsage = type == rhi::DescriptorType::ReadOnlyStorageBuffer
            ? S::BufferUsage::StorageRead
            : S::BufferUsage::UniformRead;
        rhi::DescriptorBufferBindingRangeContract::Resolved interval;
        const auto& lease = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
            stream->originalInputs()
                ->originalScalars()
                ->originalUsage()
                ->originalSources()
                ->sources()[0u]);
        require(
            rhi::DescriptorBufferBindingRangeContract::resolve(declaration.write.bufferBindingRange,
                lease->layout().sizeBytes,
                declaration.dynamicOffset,
                interval));
        assert(buffer.use.offsetBytes == interval.offsetBytes
            && buffer.use.sizeBytes == interval.sizeBytes
            && buffer.use.bufferBytes == lease->layout().sizeBytes
            && buffer.use.usage == bufferUsage && buffer.original.descriptorSourceIndex == 7u
            && buffer.original.bufferSourceIndex == 0u);
        for (const auto& point : points)
            std::visit(
                [](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, P::BufferPoint>
                        || std::is_same_v<T, P::TexturePoint>)
                        assert(value.use.commandIndex == 3u
                            && value.use.scope.kind == S::Kind::ComputeDispatch
                            && value.use.scope.first == 3u && value.use.scope.last == 3u
                            && value.use.contribution == S::Contribution::ShaderConsumption
                            && value.original.pipelineSourceIndex == 6u
                            && value.original.pipelineBindCommandIndex == 1u
                            && value.original.descriptorSourceIndex == 7u
                            && value.original.descriptorBindCommandIndex == 2u);
                    else
                        assert(false);
                },
                point);
        for (std::size_t i = 1u; i < 3u; ++i) {
            const auto& texture = std::get<P::TexturePoint>(points[i]);
            assert(texture.original.textureParentSourceIndex == (i == 1u ? 1u : 3u)
                && texture.original.textureViewSourceIndex == (i == 1u ? 2u : 4u)
                && texture.use.usage
                    == (i == 1u ? S::TextureUsage::Storage : S::TextureUsage::SampledRead));
        }
        const auto captured = P::inspectStateBound(stream, graph, dependency.usages);
        require(captured.diagnostic);
        assert(captured.counts.matches(result.counts));
        for (std::uint32_t field = 0u; field < 4u; ++field) {
            auto bad = usage;
            if (field == 0u)
                --bad.maxCommands;
            if (field == 1u)
                --bad.maxContributions;
            if (field == 2u)
                --bad.maxTextureAccesses;
            if (field == 3u)
                --bad.maxTextureCells;
            emitted = 0u;
            assert(P::visit(stream,
                       device,
                       bad,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                       .diagnostic
                && !emitted);
        }
        std::array<D::Record, 3u> records;
        emitted = 0u;
        const auto decisions = D::visit(
            stream, graph, dependency, [&](const D::Record& record) noexcept -> const char* {
                assert(emitted < records.size());
                records[emitted++] = record;
                return nullptr;
            });
        require(decisions.diagnostic);
        assert(emitted == 3u && decisions.counts.dependencies == 3u && decisions.counts.final == 3u
            && !decisions.counts.initial && !decisions.counts.between
            && decisions.counts.endpointContributions == 3u
            && decisions.counts.projectionPasses == 10u
            && decisions.counts.contributionVisits == 30u && decisions.counts.pairChecks == 3u
            && decisions.counts.textureCells == 2u && !decisions.synchronizationProven()
            && !decisions.nativeExecutionProven());
        for (std::size_t i = 0u; i < records.size(); ++i) {
            const auto& record = records[i];
            assert(record.projectionVersion == 6u && record.commandIndex == 5u + i
                && record.descriptorIndex == 2u + i && record.referenceIndex == 2u + i
                && record.sourceIndex
                    == (i == 0u       ? 0u
                            : i == 1u ? 1u
                                      : 3u)
                && record.clause == D::Clause::FinalBookkeeping && record.preceding
                && !record.following && record.preceding->contributions == 1u
                && record.preceding->scope.first == 3u && record.preceding->scope.last == 3u
                && record.before == record.after
                && record.before
                    == (i == 1u ? rhi::ResourceState::ShaderWrite
                                : rhi::ResourceState::ShaderRead));
            require(D::validateCoverage(record, 2u));
            D::Counts counts;
            require(D::validateFrozenBinding(stream, graph, dependency, record, counts));
            for (std::uint32_t field = 0u; field < 4u; ++field) {
                auto bad = record;
                if (field == 0u)
                    ++bad.registrationGeneration;
                if (field == 1u)
                    ++bad.descriptorIndex;
                if (field == 2u)
                    bad.projectionVersion = 5u;
                if (field == 3u)
                    ++bad.preceding->contributions;
                D::Counts hostile;
                assert(D::validateFrozenBinding(stream, graph, dependency, bad, hostile));
            }
        }
        for (const auto member : {&D::Request::maxDependencies,
                 &D::Request::maxProjectionPasses,
                 &D::Request::maxContributionVisits,
                 &D::Request::maxPairChecks,
                 &D::Request::maxEndpointContributions,
                 &D::Request::maxTextureCells}) {
            auto bad = dependency;
            --(bad.*member);
            emitted = 0u;
            assert(D::visit(stream,
                       graph,
                       bad,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                       .diagnostic
                && !emitted);
        }
        const auto& inputs = stream->originalInputs();
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sources->originalMetadataQuota(),
                    inputs->originalMetadataQuota(),
                    graph->originalMetadataQuota(),
                    stream->originalMetadataQuota()},
                [&]() noexcept -> const char* {
                    const auto projected = P::inspectStateBound(stream, graph, dependency.usages);
                    return projected.diagnostic ? projected.diagnostic
                                                : D::inspect(stream, graph, dependency).diagnostic;
                }));
        });
        std::printf(
            "BINDLESS_RHI_GENERAL_USAGE6_DEPENDENCY6_OK commands9 contributions3 sampler1 barriers3 final3 passes10 visits30 pairs3 endpoints3 cells2 offset%llu bytes%llu exact-less hostile-refused preflight-no-output hot0-query0 frozen-native-closed\n",
            static_cast<unsigned long long>(interval.offsetBytes),
            static_cast<unsigned long long>(interval.sizeBytes));
        std::printf(
            "BINDLESS_RHI_GENERAL_USAGE6_BUFFER_TYPE_OK %s actual-Source5 descriptor-type-match finite-whole-physical-range immutable-read native-closed\n",
            type == rhi::DescriptorType::ReadOnlyStorageBuffer ? "readonly-storage StorageRead"
                                                               : "uniform UniformRead");
    }

    static void verifyInvalidated(const Retained& retained, const rhi::GpuDevice& device)
    {
        std::uint32_t emitted = 0u;
        const auto visitor = [&](const auto&) noexcept -> const char* {
            ++emitted;
            return nullptr;
        };
        assert(P::visit(retained.program, device, budget(), visitor).diagnostic && !emitted);
        assert(
            P::visitStateBound(retained.program, retained.graph, dependencyBudget().usages, visitor)
                .diagnostic
            && !emitted);
        assert(D::visit(retained.program, retained.graph, dependencyBudget(), visitor).diagnostic
            && !emitted);
        std::puts(
            "BINDLESS_RHI_GENERAL_USAGE6_DEPENDENCY6_TERMINAL_OK original-state-stream-invalid preflight-no-output native-closed");
    }
#endif
};
} // namespace arp::test
