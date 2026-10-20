#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_native_identity_fixture.h"
#include "original_typed_cold_admission_fixture.h"
#include "original_typed_native_payload_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalTypedDependencyDecisionFixture final {
    using C = rhi::OriginalTypedDependencyProjectionContract;
    using P = rhi::common::OriginalTypedDependencyProjection;
    using S = rhi::semantics::TypedShaderUsageScopeContract;
    using D = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
    using K = rhi::CommandUsageKind;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "dependency expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        const C::Request request{1u,
            true,
            {1u, true, {1u, true, {1u, true, 1u, 1u, true, true, 2u, 4u}, {}, 19u, 0u, 4u}},
            6u,
            17u,
            68u,
            6u,
            8u,
            0u};
        assert(!C::validateIntent(request));
        auto wrong = request;
        wrong.contractVersion = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VERSION");
        wrong = request;
        wrong.prototypeOptIn = false;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-OPT-IN");
        wrong = request;
        wrong.maxProjectionPasses = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-BUDGET");
        const D::Request frozen{1u, true, request, 128u, 65536u};
        assert(!C::validateFreezeIntent(frozen));
        auto bad = frozen;
        bad.contractVersion = 0u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-VERSION");
        bad = frozen;
        bad.prototypeOptIn = false;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-OPT-IN");
        bad = frozen;
        bad.maxCpuMetadataBytes = 0u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-BUDGET");
        auto textures = request;
        textures.contractVersion = 2u;
        textures.usages = {2u,
            true,
            {2u,
                true,
                {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
                {2u, true, 0u, 0u, false, false, 11u, 2u, 6u, 1u, true, 6u},
                11u,
                1u,
                10u,
                9u,
                10u}};
        textures.maxTextureCells = 10u;
        assert(!C::validateIntent(textures));
        auto general = textures;
        general.contractVersion = general.usages.contractVersion =
            general.usages.projection.contractVersion = 5u;
        general.usages.projection.shaderConsumption.generalShaderResourcesVersion = 1u;
        general.usages.projection.shaderConsumption.generalShaderResourcesOptIn = true;
        general.usages.projection.renderBindings.generalShaderResourcesVersion = 1u;
        general.usages.projection.renderBindings.generalShaderResourcesOptIn = true;
        assert(!C::validateIntent(general));
        wrong = general;
        wrong.usages.contractVersion = 4u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PROFILE");
        wrong = general;
        wrong.usages.projection.shaderConsumption.generalShaderResourcesOptIn = false;
        assert(C::validateIntent(wrong));
        wrong = general;
        wrong.maxTextureCells = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-BUDGET");
        wrong = general;
        wrong.contractVersion = 7u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-VERSION");
        auto generalFreeze = frozen;
        generalFreeze.contractVersion = 5u;
        generalFreeze.projection = general;
        is(C::validateFreezeIntent(generalFreeze), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        generalFreeze.contractVersion = 6u;
        assert(!C::validateFreezeIntent(generalFreeze));
        assert(C::closureProfileMatches(6u, 8u) && !C::closureProfileMatches(6u, 7u));
        auto invalidGeneralFreeze = generalFreeze;
        invalidGeneralFreeze.projection = textures;
        is(C::validateFreezeIntent(invalidGeneralFreeze), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        invalidGeneralFreeze = generalFreeze;
        invalidGeneralFreeze.prototypeOptIn = false;
        is(C::validateFreezeIntent(invalidGeneralFreeze), "RHI-ORIGINAL-DEPENDENCY-FREEZE-OPT-IN");
        invalidGeneralFreeze = generalFreeze;
        invalidGeneralFreeze.maxRetainedOwners = 0u;
        is(C::validateFreezeIntent(invalidGeneralFreeze), "RHI-ORIGINAL-DEPENDENCY-FREEZE-BUDGET");
        wrong = textures;
        wrong.contractVersion = 1u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PROFILE");
        wrong = textures;
        wrong.maxTextureCells = 0u;
        is(C::validateIntent(wrong), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-TEXTURE-BUDGET");
        bad = frozen;
        bad.projection = textures;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        bad.contractVersion = 2u;
        assert(!C::validateFreezeIntent(bad));
        bad.prototypeOptIn = false;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-OPT-IN");
        bad = frozen;
        bad.contractVersion = 2u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        auto mixed = textures;
        mixed.contractVersion = mixed.usages.contractVersion =
            mixed.usages.projection.contractVersion = 3u;
        mixed.usages.projection.maxReadbacks = mixed.usages.projection.maxReadbackMips = 1u;
        mixed.usages.projection.maxReadbackDeclaredBytes = 512u;
        bad = frozen;
        bad.contractVersion = 3u;
        bad.projection = mixed;
        assert(!C::validateFreezeIntent(bad));
        bad.contractVersion = 2u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        bad = frozen;
        bad.contractVersion = 4u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        auto geometry = textures;
        geometry.contractVersion = geometry.usages.contractVersion =
            geometry.usages.projection.contractVersion = 4u;
        geometry.usages.projection.shaderConsumption.contractVersion = 3u;
        geometry.usages.projection.renderBindings.contractVersion = 3u;
        geometry.usages.projection.geometryUsage = {1u, true, 11u, 2u, 1u};
        bad = frozen;
        bad.contractVersion = 4u;
        bad.projection = geometry;
        assert(!C::validateFreezeIntent(bad));
        assert(C::closureProfileMatches(4u, 6u));
        assert(!C::closureProfileMatches(4u, 5u) && !C::closureProfileMatches(4u, 4u));
        assert(!C::closureProfileMatches(0u, 1u) && !C::closureProfileMatches(5u, 6u));
        for (std::uint32_t version = 1u; version <= 3u; ++version)
            assert(C::closureProfileMatches(version, version + 1u)
                && !C::closureProfileMatches(version, 6u));
        bad.contractVersion = 8u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");
        bad.contractVersion = 9u;
        is(C::validateFreezeIntent(bad), "RHI-ORIGINAL-DEPENDENCY-FREEZE-VERSION");
        rhi::ResourceMemoryBarrierDesc memory;
        memory.resource = {1u};
        memory.sourceStages = memory.destinationStages = rhi::PipelineStage::ComputeShader;
        memory.sourceAccess = memory.destinationAccess =
            rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite;
        P::Record record;
        record.commandIndex = 5u;
        record.kind = K::BufferMemoryBarrier;
        record.resource = memory.resource;
        record.registrationGeneration = 1u;
        record.bufferBytes = 64u;
        record.before = record.after = rhi::ResourceState::ShaderWrite;
        record.original = rhi::ResourceDependencyArguments::capture(record.kind, memory);
        record.clause = P::Clause::BetweenScopes;
        record.preceding = P::Endpoint{{S::Kind::ComputeDispatch, 2u, 2u},
            {record.before, memory.sourceStages, memory.sourceAccess},
            1u};
        record.following = P::Endpoint{{S::Kind::ComputeDispatch, 8u, 8u},
            {record.after, memory.destinationStages, rhi::ResourceAccess::ShaderRead},
            1u};
        assert(!P::validateCoverage(record));
        auto changed = record;
        changed.clause = P::Clause::FinalBookkeeping;
        is(P::validateCoverage(changed), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-CLAUSE");
        changed = record;
        changed.preceding->scope.last = 5u;
        is(P::validateCoverage(changed), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-POSITION");
        changed = record;
        changed.following->access.state = rhi::ResourceState::ShaderRead;
        is(P::validateCoverage(changed), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
        changed = record;
        changed.preceding->contributions = 0u;
        is(P::validateCoverage(changed), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-POSITION");
        auto less = memory;
        less.sourceAccess = rhi::ResourceAccess::ShaderRead;
        changed = record;
        changed.original = rhi::ResourceDependencyArguments::capture(changed.kind, less);
        is(P::validateCoverage(changed), "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
        // A terminal memory barrier has no future endpoint, even when the
        // descriptor destination scope is valid. It must remain bookkeeping.
        changed = record;
        changed.following.reset();
        changed.clause = P::Clause::FinalBookkeeping;
        assert(!P::validateCoverage(changed));
        rhi::TextureDesc depth;
        depth.format = rhi::TextureFormat::D32Float;
        depth.extent = {8u, 8u, 1u};
        depth.mipLevelCount = 3u;
        depth.arrayLayerCount = 3u;
        rhi::ResourceBarrierDesc transition{{2u},
            {2u, 1u, 2u, 1u, rhi::TextureAspect::Depth},
            rhi::ResourceState::Undefined,
            rhi::ResourceState::ShaderRead};
        transition.sourceStages = rhi::PipelineStage::TopOfPipe;
        transition.sourceAccess = rhi::ResourceAccess::None;
        transition.destinationStages = rhi::PipelineStage::FragmentShader;
        transition.destinationAccess = rhi::ResourceAccess::ShaderRead;
        P::Record partial;
        partial.projectionVersion = 2u;
        partial.texture = true;
        partial.resource = {2u};
        partial.registrationGeneration = 2u;
        partial.commandIndex = 0u;
        partial.kind = K::TextureBarrier;
        partial.range = transition.textureSubresources;
        partial.textureLayout = rhi::TextureResourceLayout::capture(depth);
        partial.before = transition.before;
        partial.after = transition.after;
        partial.original = rhi::ResourceDependencyArguments::capture(partial.kind, transition);
        partial.clause = P::Clause::InitialBookkeeping;
        partial.following = P::Endpoint{{S::Kind::RenderPass, 4u, 9u},
            {transition.after, transition.destinationStages, transition.destinationAccess},
            3u};
        assert(!P::validateCoverage(partial, 9u));
        changed = partial;
        changed.projectionVersion = 1u;
        is(P::validateCoverage(changed, 9u),
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-PARTIAL-TEXTURE-RANGE");
        changed = partial;
        changed.range.baseMipLevel = 3u;
        is(P::validateCoverage(changed, 9u), "RHI-ORIGINAL-TEXTURE-RANGE-BOUNDS");
        changed = partial;
        changed.following->access.stages = rhi::PipelineStage::VertexShader;
        is(P::validateCoverage(changed, 9u),
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
        auto generalRecord = partial;
        generalRecord.projectionVersion = 5u;
        assert(!P::validateCoverage(generalRecord, 9u));
        generalRecord.following->access.stages = rhi::PipelineStage::VertexShader;
        is(P::validateCoverage(generalRecord, 9u),
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-COVERAGE");
        auto generalBuffer = record;
        generalBuffer.projectionVersion = 5u;
        assert(!P::validateCoverage(generalBuffer));
        generalBuffer.following->contributions = 0u;
        is(P::validateCoverage(generalBuffer),
            "RHI-ORIGINAL-DEPENDENCY-PROJECTION-ENDPOINT-POSITION");
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
                        const auto freeze =
                            rhi::semantics::OriginalTypedDependencyProjectionLowering::
                                freezeLowering(backend, version);
                        assert(freeze.projectionAvailable == (backend == rhi::Backend::WebGPU)
                            && !freeze.executionAvailable);
                        assert((freeze.disposition
                                   == rhi::semantics::OriginalTypedDependencyProjectionLowering::
                                       Disposition::ValidatorOnly)
                            == (backend == rhi::Backend::Null));
                    }
                    const auto textureDecision =
                        arp::rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(
                            backend, 2u);
                    assert(textureDecision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !textureDecision.executionAvailable);
                    const auto generalDecision =
                        rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(
                            backend, 5u);
                    assert(generalDecision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !generalDecision.executionAvailable);
                    assert((generalDecision.disposition
                               == rhi::semantics::OriginalTypedDependencyProjectionLowering::
                                   Disposition::ValidatorOnly)
                        == (backend == rhi::Backend::Null));
                    const auto decision =
                        arp::rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(
                            backend);
                    assert(decision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !decision.executionAvailable);
                    assert(bool(decision.diagnostic) != decision.projectionAvailable);
                    const auto gate = arp::rhi::semantics::
                        OriginalTypedDependencyProjectionLowering::nativeLowering(backend);
                    assert(gate.diagnostic && !gate.executionAvailable);
                }
                assert(arp::rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(
                    rhi::Backend::WebGPU, 7u)
                        .diagnostic);
                std::size_t emitted = 0u;
                const auto refused =
                    P::visit({}, {}, request, [&](const auto&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                assert(refused.diagnostic && !emitted && !refused.counts.dependencies
                    && !refused.synchronizationProven() && !refused.nativeExecutionProven());
                const char* diagnostic = nullptr;
                assert(!D::prepareOriginal({}, frozen, diagnostic) && diagnostic);
                assert(D::validateOriginal({}));
                return nullptr;
            }));
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_CONTRACT_OK dependency1 freeze1 explicit-opt-in complete-original-clauses endpoint-union-coverage terminal-memory-no-next-scope four-native-gates-closed hot0");
    }

    template <class State>
    static C::Request requestFor(
        const State& state, const rhi::common::OriginalTypedUsageProjection::Budget& usage)
    {
        std::uint64_t dependencies = 0u, textureCells = 0u;
        for (const auto& op : state.stream.program->operations())
            if (P::isDependency(op.kind))
                ++dependencies;
        assert(!rhi::common::OriginalCommandTextureRanges::visit(
            state.stream.program->originalInputs(),
            [&](const auto& point, const rhi::GpuTexture& texture) noexcept -> const char* {
                if (!P::isDependency(state.stream.program->operations()[point.commandIndex].kind))
                    return nullptr;
                const char* id = nullptr;
                const auto geometry =
                    rhi::semantics::OriginalTextureStateRange::capture(texture.desc(),
                        state.stream.program->originalInputs()
                            ->originalScalars()
                            ->originalUsage()
                            ->originalSources()
                            ->originalBudget()
                            .maxStateObservations,
                        id);
                assert(geometry && !id);
                textureCells += geometry->rangeCellCount(point.range);
                return nullptr;
            }));
        const auto contributions = usage.maxContributions;
        // Independently authored worst-case work ceilings from this fixture's
        // original command/usage bounds; the runtime does not infer budgets.
        return {1u,
            true,
            {1u, true, usage},
            dependencies,
            1u + contributions + 2u * dependencies,
            contributions * (1u + contributions + 2u * dependencies),
            contributions * (contributions - 1u) / 2u,
            2u * dependencies * contributions,
            textureCells};
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class State>
    static void retainForActualLoss(State& state,
        const rhi::common::OriginalTypedUsageProjection::Budget& usage,
        const char* label)
    {
        const auto& stream = state.stream.program;
        const char* diagnostic = nullptr;
        assert(state.nativeClosure && !state.nativeClosure->validate());
        const D::Request request{1u,
            true,
            requestFor(state, usage),
            state.nativeClosure->retainedOwnerCount() + 1u,
            state.recordingQuota->limitBytes()};
        state.dependencyDecision = D::prepareOriginal(state.nativeClosure, request, diagnostic);
        assert(state.dependencyDecision && !diagnostic);
        assert(!state.dependencyDecision->validate());
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        state.typedNativePayload =
            OriginalTypedNativePayloadFixture::retainForActualLoss(state.dependencyDecision, label);
#endif
        std::printf(
            "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_RETAINED_OK %s authentic-loss-closure before-pending-and-physical-loss native-closed\n",
            label);
    }

    template <class State, std::size_t Count>
    static std::shared_ptr<const D> verify(const State& state,
        const rhi::GpuDevice& device,
        const std::shared_ptr<const Closure>& closure,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas,
        const char* label,
        const rhi::common::OriginalTypedUsageProjection::Budget& usage,
        const char* expectedRefusal = nullptr)
    {
        const char* diagnostic = nullptr;
        D::Request request{1u,
            true,
            requestFor(state, usage),
            closure->retainedOwnerCount() + 1u,
            closure->originalMetadataQuota()->limitBytes()};
        const auto factory = [&] {
            return D::prepareOriginal(closure, request, diagnostic);
        };
        if (expectedRefusal) {
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(!factory());
                        is(diagnostic, expectedRefusal);
                        std::size_t emitted = 0u;
                        const auto refused = P::visit(state.stream.program,
                            state.graph,
                            request.projection,
                            [&](const auto&) noexcept -> const char* {
                                ++emitted;
                                return nullptr;
                            });
                        is(refused.diagnostic, expectedRefusal);
                        assert(!emitted && refused.counts.matches(P::Counts{})
                            && !refused.synchronizationProven()
                            && !refused.nativeExecutionProven());
                        return Closure::validateOriginal(closure);
                    }));
            });
            std::printf(
                "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_INCOMPATIBLE_REJECT_OK %s %s original-loss-closure compatible-scope-required emitted0 counts-cleared full-pressure hot0-query0 native-closed\n",
                label,
                expectedRefusal);
            return {};
        }
        auto decision = factory();
        if (!decision)
            std::fprintf(
                stderr, "dependency freeze %s: %s\n", label, diagnostic ? diagnostic : "unknown");
        assert(decision);
        assert(!decision->compareOriginalClosure(closure) && !decision->synchronizationProven()
            && !decision->nativeExecutionProven());
        is(decision->executionDiagnostic(), "RHI-WEBGPU-TYPED-DEPENDENCY-EXECUTOR-PAYLOAD-MISSING");
        const auto exact = decision->counts();
        request.projection.maxEndpointContributions = exact.endpointContributions;
        decision.reset();
        CpuQuotaFactoryFixture allocation(closure->originalMetadataQuota());
        const auto peak = allocation.measureRetained(factory);
        allocation.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(closure->originalMetadataQuota(),
                closure->originalMetadataQuota()->limitBytes()
                    - closure->originalMetadataQuota()->liveBytes());
            assert(pressure.charged() && !factory());
        }
        const auto reject = [&](auto change) {
            const auto saved = request;
            change(request);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!factory() && diagnostic
                && allocations == core::defaultCpuAllocator().stats().allocationCount);
            std::size_t emitted = 0u;
            const auto result = P::visit(state.stream.program,
                state.graph,
                request.projection,
                [&](const auto&) noexcept -> const char* {
                    ++emitted;
                    return nullptr;
                });
            if (result.diagnostic)
                assert(
                    !emitted && !result.counts.dependencies && !result.counts.contributionVisits);
            request = saved;
        };
        reject([](auto& b) {
            b.contractVersion = 0u;
        });
        reject([](auto& b) {
            b.prototypeOptIn = false;
        });
        reject([](auto& b) {
            --b.maxRetainedOwners;
        });
        reject([](auto& b) {
            --b.maxCpuMetadataBytes;
        });
        reject([](auto& b) {
            --b.projection.maxDependencies;
        });
        reject([](auto& b) {
            --b.projection.maxProjectionPasses;
        });
        reject([](auto& b) {
            --b.projection.maxContributionVisits;
        });
        reject([](auto& b) {
            --b.projection.maxPairChecks;
        });
        reject([](auto& b) {
            --b.projection.maxEndpointContributions;
        });
        if (request.projection.maxTextureCells)
            reject([](auto& b) {
                --b.projection.maxTextureCells;
            });
        decision = factory();
        assert(decision && decision->counts().matches(exact));
        const rhi::OriginalCommandNativeClosureBudget otherBudget{2u,
            true,
            static_cast<std::uint32_t>(closure->resourceBindingCount()),
            static_cast<std::uint32_t>(closure->nativePayloadCount()),
            closure->retainedOwnerCount(),
            closure->retainedResourceBytes(),
            closure->originalMetadataQuota()->limitBytes(),
            static_cast<std::uint32_t>(closure->pipelineBindingCount()),
            static_cast<std::uint32_t>(closure->descriptorBindingCount())};
        auto other = Closure::prepareOriginalTypedLossBound(state.transaction,
            state.stream.program,
            device,
            otherBudget,
            *closure->originalNativeLossObservation(),
            diagnostic);
        assert(other);
        is(decision->compareOriginalClosure(other), "RHI-WEBGPU-TYPED-DEPENDENCY-CLOSURE-IDENTITY");
        other.reset();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const D>(state.recordingQuota, decision.get());
                    is(D::validateOriginal(forged), "RHI-WEBGPU-TYPED-DEPENDENCY-OWNER");
                    auto forgedClosure =
                        std::shared_ptr<const Closure>(state.recordingQuota, closure.get());
                    assert(!D::prepareOriginal(forgedClosure, request, diagnostic));
                    const auto& sources = state.stream.program->originalInputs()
                                              ->originalScalars()
                                              ->originalUsage()
                                              ->originalSources()
                                              ->sources();
                    for (const auto& source : sources)
                        std::visit(
                            [&](const auto& value) {
                                using T = std::remove_const_t<
                                    typename std::decay_t<decltype(value)>::element_type>;
                                if constexpr (std::is_same_v<T,
                                                  rhi::OriginalCommandDescriptorSetSource>) {
                                    const auto payload = rhi_webgpu::webGpuNativePayloadFor(
                                        *value->originalObject());
                                    assert(payload);
                                    OriginalDescriptorNativeIdentityFixture::mutateActual(
                                        payload, [&] {
                                            assert(decision->validate() && !factory());
                                        });
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandComputePipelineSource>
                                    || std::is_same_v<T,
                                        rhi::OriginalCommandGraphicsPipelineSource>) {
                                    auto payload = rhi_webgpu::webGpuNativePayloadFor(
                                        *value->originalPipeline());
                                    const auto native = payload->pipeline;
                                    payload->pipeline = nullptr;
                                    assert(decision->validate() && !factory());
                                    payload->pipeline = native;
                                }
                            },
                            source);
                    return D::validateOriginal(decision);
                }));
        });
        std::printf(
            "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_FROZEN_OK %s dependencies%llu initial%llu between%llu final%llu endpoints%llu passes%llu visits%llu pairs%llu cells%llu exact-less-exhausted original-loss-closure-identity mutation-reject hot0-query0 candidate-only native-closed\n",
            label,
            static_cast<unsigned long long>(exact.dependencies),
            static_cast<unsigned long long>(exact.initial),
            static_cast<unsigned long long>(exact.between),
            static_cast<unsigned long long>(exact.final),
            static_cast<unsigned long long>(exact.endpointContributions),
            static_cast<unsigned long long>(exact.projectionPasses),
            static_cast<unsigned long long>(exact.contributionVisits),
            static_cast<unsigned long long>(exact.pairChecks),
            static_cast<unsigned long long>(exact.textureCells));
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
        state.typedNativePayload =
            OriginalTypedNativePayloadFixture::verify(decision, quotas, label);
        OriginalTypedColdAdmissionFixture::verifyMetadataRefusal(state.typedNativePayload, label);
#endif
        return decision;
    }
#endif
    template <class State> static void verifyUnavailable(const State& state, const char* reason)
    {
        if (!state.dependencyDecision)
            return;
        OriginalTypedNativePayloadFixture::verifyUnavailable(state.typedNativePayload, reason);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(state.dependencyDecision->validate());
        const auto& decision = state.dependencyDecision;
        if (core::StringView(reason) == "finite-physical-loss"
            || core::StringView(reason) == "whole-physical-loss") {
            is(decision->originalClosure()->nativeLossDiagnostic(),
                "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
            is(decision->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        }
        const char* diagnostic = nullptr;
        assert(!D::prepareOriginal(
                   decision->originalClosure(), decision->originalRequest(), diagnostic)
            && diagnostic);
        std::size_t emitted = 0u;
        const auto& request = decision->originalRequest().projection;
        const auto rejected = P::visit(
            state.stream.program, state.graph, request, [&](const auto&) noexcept -> const char* {
                ++emitted;
                return nullptr;
            });
        assert(rejected.diagnostic && !emitted && rejected.counts.matches(P::Counts{}));
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::printf(
            "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_REJECT_OK %s genuine-frozen-clause original-root-stale emitted0 counts-cleared native-closed\n",
            reason);
    }
};
} // namespace arp::test
