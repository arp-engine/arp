#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_typed_dependency_decision_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalTypedUsageProjectionFixture final {
    using P = rhi::common::OriginalTypedUsageProjection;
    using C = rhi::common::OriginalCommandDescriptorConsumption;
    using Scope = rhi::semantics::TypedShaderUsageScopeContract;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(
                stderr, "usage expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static void verifyContract()
    {
        const P::Budget compute{1u, true, {1u, true, 1u, 1u, true, true, 4u, 8u}, {}, 20u, 0u, 8u};
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(compute));
        auto bad = compute;
        bad.contractVersion = 0u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-VERSION");
        bad = compute;
        bad.prototypeOptIn = false;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-OPT-IN");
        bad = compute;
        bad.renderBindings.maxCommands = 1u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-INTENT");
        bad = compute;
        bad.renderBindings = {1u, true, 1u, 1u, true, true, 20u, 3u, 6u};
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(bad));
        bad = compute;
        bad.shaderConsumption = {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u};
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-CONSUMPTION-PROFILE");
        bad = compute;
        bad.renderBindings = {2u, true, 0u, 0u, false, false, 8u, 2u, 6u, 1u, true, 6u};
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-PROFILE");
        bad = compute;
        bad.renderBindings.depthTableFootprintVersion = 1u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-INTENT");
        const P::Budget textures{2u,
            true,
            {2u, true, 0u, 0u, false, false, 1u, 3u, 1u, true, 3u},
            {2u, true, 0u, 0u, false, false, 11u, 2u, 6u, 1u, true, 6u},
            11u,
            1u,
            10u,
            9u,
            10u};
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(textures));
        auto mixed = textures;
        mixed.contractVersion = 3u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(mixed),
            "RHI-ORIGINAL-USAGE-PROJECTION-READBACK-BUDGET");
        mixed.maxReadbacks = mixed.maxReadbackMips = 1u;
        mixed.maxReadbackDeclaredBytes = 512u;
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(mixed));
        auto legacyMixed = mixed;
        legacyMixed.contractVersion = 2u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(legacyMixed),
            "RHI-ORIGINAL-USAGE-PROJECTION-READBACK-SCHEMA");
        legacyMixed = mixed;
        legacyMixed.shaderConsumption.contractVersion = 3u;
        assert(rhi::OriginalTypedUsageProjectionContract::validateBudget(legacyMixed));
        assert(!rhi::OriginalTypedStateBoundUsageContract::validateIntent({3u, true, mixed}));
        auto geometry = textures;
        geometry.contractVersion = 4u;
        geometry.shaderConsumption.contractVersion = geometry.renderBindings.contractVersion = 3u;
        geometry.geometryUsage = {1u, true, 11u, 3u, 3u};
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(geometry));
        assert(!rhi::OriginalTypedStateBoundUsageContract::validateIntent({4u, true, geometry}));
        auto general = textures;
        general.contractVersion = 5u;
        general.shaderConsumption.generalShaderResourcesVersion = 1u;
        general.shaderConsumption.generalShaderResourcesOptIn = true;
        general.renderBindings.generalShaderResourcesVersion = 1u;
        general.renderBindings.generalShaderResourcesOptIn = true;
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(general));
        assert(!rhi::OriginalTypedStateBoundUsageContract::validateIntent({5u, true, general}));
        assert(rhi::OriginalTypedUsageProjectionContract::programVersion(general) == 3u);
        auto generalGeometry = geometry;
        generalGeometry.contractVersion = 5u;
        generalGeometry.shaderConsumption.generalShaderResourcesVersion = 1u;
        generalGeometry.shaderConsumption.generalShaderResourcesOptIn = true;
        generalGeometry.renderBindings.generalShaderResourcesVersion = 1u;
        generalGeometry.renderBindings.generalShaderResourcesOptIn = true;
        assert(!rhi::OriginalTypedUsageProjectionContract::validateBudget(generalGeometry));
        assert(rhi::OriginalTypedUsageProjectionContract::programVersion(generalGeometry) == 4u);
        auto missingGeneral = general;
        missingGeneral.shaderConsumption.generalShaderResourcesOptIn = false;
        assert(rhi::OriginalTypedUsageProjectionContract::validateBudget(missingGeneral));
        missingGeneral = general;
        missingGeneral.renderBindings.generalShaderResourcesOptIn = false;
        assert(rhi::OriginalTypedUsageProjectionContract::validateBudget(missingGeneral));
        missingGeneral = general;
        missingGeneral.contractVersion = 2u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(missingGeneral),
            "RHI-ORIGINAL-USAGE-PROJECTION-GENERAL-PROFILE");
        auto invalidGeometry = geometry;
        invalidGeometry.geometryUsage = {};
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(invalidGeometry),
            "RHI-ORIGINAL-GEOMETRY-ACCESS-VERSION");
        invalidGeometry = geometry;
        invalidGeometry.contractVersion = 2u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(invalidGeometry),
            "RHI-ORIGINAL-USAGE-PROJECTION-GEOMETRY-SCHEMA");
        invalidGeometry = geometry;
        invalidGeometry.geometryUsage.prototypeOptIn = false;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(invalidGeometry),
            "RHI-ORIGINAL-GEOMETRY-ACCESS-OPT-IN");
        invalidGeometry = geometry;
        invalidGeometry.maxReadbacks = 1u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(invalidGeometry),
            "RHI-ORIGINAL-USAGE-PROJECTION-READBACK-BUDGET");
        bad = textures;
        bad.contractVersion = 1u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-SCHEMA");
        bad = textures;
        bad.maxTextureCells = 0u;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-TEXTURE-BUDGET");
        bad = textures;
        bad.shaderConsumption = compute.shaderConsumption;
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-CONSUMPTION-PROFILE");
        bad = textures;
        bad.renderBindings = {1u, true, 1u, 1u, true, true, 11u, 2u, 6u};
        is(rhi::OriginalTypedUsageProjectionContract::validateBudget(bad),
            "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-PROFILE");
        const P::StateBoundRequest textureBound{2u, true, textures};
        assert(!rhi::OriginalTypedStateBoundUsageContract::validateIntent(textureBound));
        auto wrongProfile = textureBound;
        wrongProfile.contractVersion = 1u;
        is(rhi::OriginalTypedStateBoundUsageContract::validateIntent(wrongProfile),
            "RHI-ORIGINAL-STATE-BOUND-USAGE-PROFILE");
        wrongProfile = textureBound;
        wrongProfile.projection = compute;
        is(rhi::OriginalTypedStateBoundUsageContract::validateIntent(wrongProfile),
            "RHI-ORIGINAL-STATE-BOUND-USAGE-PROFILE");
        auto device = rhi::createNullDevice();
        auto quota = core::makeShared<core::CpuMemoryQuota>(4096u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto generalDecision =
                        rhi::semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                            backend, 5u);
                    const auto generalBound =
                        rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(backend, 5u);
                    assert(generalDecision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && generalBound.projectionAvailable == generalDecision.projectionAvailable
                        && !generalDecision.executionAvailable && !generalBound.executionAvailable);
                    const auto geometryDecision =
                        rhi::semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                            backend, 4u);
                    const auto geometryBound =
                        rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(backend, 4u);
                    const auto geometryDependency =
                        rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(
                            backend, 4u);
                    assert(geometryDecision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !geometryDecision.stateAvailable && !geometryDecision.executionAvailable
                        && geometryBound.projectionAvailable == geometryDecision.projectionAvailable
                        && geometryBound.contractVersion == 4u && !geometryBound.executionAvailable
                        && geometryDependency.projectionAvailable
                            == geometryDecision.projectionAvailable
                        && !geometryDependency.executionAvailable);
                    const auto mixedDecision = arp::rhi::semantics::
                        OriginalTypedUsageProjectionLowering::projectionLowering(backend, 3u);
                    const auto mixedBound =
                        arp::rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(
                            backend, 3u);
                    const auto mixedDependency =
                        arp::rhi::semantics::OriginalTypedDependencyProjectionLowering::lowering(
                            backend, 3u);
                    assert(mixedDecision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !mixedDecision.stateAvailable && !mixedDecision.executionAvailable
                        && mixedBound.projectionAvailable == mixedDecision.projectionAvailable
                        && !mixedBound.executionAvailable && mixedBound.contractVersion == 3u
                        && mixedDependency.projectionAvailable == mixedDecision.projectionAvailable
                        && !mixedDependency.executionAvailable);
                    const auto mixedFreeze = arp::rhi::semantics::
                        OriginalTypedDependencyProjectionLowering::freezeLowering(backend, 3u);
                    assert(mixedFreeze.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !mixedFreeze.executionAvailable);
                    const auto geometryFreeze = arp::rhi::semantics::
                        OriginalTypedDependencyProjectionLowering::freezeLowering(backend, 4u);
                    assert(geometryFreeze.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !geometryFreeze.executionAvailable);
                    const auto generalFreeze =
                        rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                            backend, 6u);
                    assert(generalFreeze.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !generalFreeze.executionAvailable);
                    assert(
                        rhi::semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(
                            backend, 8u)
                            .diagnostic);
                    const auto textureDecision = arp::rhi::semantics::
                        OriginalTypedUsageProjectionLowering::projectionLowering(backend, 2u);
                    assert(textureDecision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !textureDecision.stateAvailable && !textureDecision.executionAvailable);
                    const auto textureBoundDecision =
                        arp::rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(
                            backend, 2u);
                    assert(textureBoundDecision.contractVersion == 2u
                        && !textureBoundDecision.executionAvailable
                        && textureBoundDecision.projectionAvailable
                            == (backend == rhi::Backend::WebGPU));
                    const auto d = arp::rhi::semantics::OriginalTypedUsageProjectionLowering::
                        projectionLowering(backend);
                    assert(!d.stateAvailable && !d.executionAvailable
                        && d.projectionAvailable == (backend == rhi::Backend::WebGPU));
                    const auto native =
                        arp::rhi::semantics::OriginalTypedUsageProjectionLowering::nativeLowering(
                            backend);
                    assert(native.diagnostic && !native.coldJoinAvailable
                        && !native.recordingAvailable && !native.executionAvailable);
                }
                assert(
                    arp::rhi::semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                        static_cast<rhi::Backend>(255u))
                        .diagnostic);
                assert(
                    arp::rhi::semantics::OriginalTypedUsageProjectionLowering::projectionLowering(
                        rhi::Backend::WebGPU, 7u)
                        .diagnostic);
                assert(arp::rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(
                    rhi::Backend::WebGPU, 7u)
                        .diagnostic);
                std::size_t emitted = 0u;
                auto r = P::visit({}, *device, compute, [&](const auto&) noexcept -> const char* {
                    ++emitted;
                    return nullptr;
                });
                is(r.diagnostic, "RHI-ORIGINAL-RENDER-USAGE-NULL-VALIDATOR-ONLY");
                assert(!emitted && !r.counts.contributions && !r.nativeExecutionProven()
                    && !r.synchronizationProven());
                const C::RenderBindingBudget render{1u, true, 1u, 1u, true, true, 20u, 3u, 6u};
                auto bindings = C::visitRenderBindings(
                    {}, *device, render, [&](const auto&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                assert(bindings.diagnostic && !bindings.bindings && !bindings.logicalAccesses
                    && !emitted);
                return nullptr;
            }));
        const P::StateBoundRequest stateBound{1u, true, compute};
        assert(!rhi::OriginalTypedStateBoundUsageContract::validateIntent(stateBound));
        auto invalidBound = stateBound;
        invalidBound.contractVersion = 0u;
        is(P::inspectStateBound({}, {}, invalidBound).diagnostic,
            "RHI-ORIGINAL-STATE-BOUND-USAGE-VERSION");
        invalidBound = stateBound;
        invalidBound.prototypeOptIn = false;
        is(P::inspectStateBound({}, {}, invalidBound).diagnostic,
            "RHI-ORIGINAL-STATE-BOUND-USAGE-OPT-IN");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU}) {
                    const auto decision =
                        arp::rhi::semantics::OriginalTypedStateBoundUsageLowering::lowering(
                            backend);
                    assert(decision.projectionAvailable == (backend == rhi::Backend::WebGPU)
                        && !decision.executionAvailable);
                    assert(bool(decision.diagnostic) != decision.projectionAvailable);
                }
                std::size_t emitted = 0u;
                const auto unavailable = P::visitStateBound(
                    {}, {}, stateBound, [&](const auto&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                assert(unavailable.diagnostic && !emitted && !unavailable.counts.contributions
                    && !unavailable.synchronizationProven()
                    && !unavailable.nativeExecutionProven());
                return nullptr;
            }));
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_STATE_BOUND_USAGE_CONTRACT_OK state-bound1 independent-opt-in original-committed-root no-device-reference captured-join all-four-gates-explicit no-sync-native-authority hot0");
        std::puts(
            "BINDLESS_RHI_TYPED_USAGE_PROJECTION_CONTRACT_OK projection1 independently-bounded render-and-shader-views attachments four-backends explicit-opt-in no-state-sync-native-authority hot0");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class State> static P::Budget budgetFor(const State& state)
    {
        const auto& stream = state.stream.program;
        const auto& counts = stream->counts();
        const auto& inputs = stream->originalInputs();
        const auto& sources =
            inputs->originalScalars()->originalUsage()->originalSources()->sources();
        std::uint64_t renderBindings = 0u, renderBuffers = 0u;
        for (const auto& operation : stream->operations())
            if (operation.kind == rhi::CommandUsageKind::BindRenderDescriptor) {
                const auto& binding = std::get<rhi::OriginalCommandDescriptorBinding>(
                    *inputs->descriptorAt(operation.commandIndex));
                renderBuffers +=
                    std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                        sources[binding.sourceIndex])
                        ->frozenElements()
                        .size();
                ++renderBindings;
            }
        P::Budget budget{1u,
            true,
            {1u, true, 1u, 1u, true, true, counts.consumptions, counts.implicitAccesses},
            {},
            counts.commands,
            counts.attachmentReferences,
            counts.implicitAccesses + renderBuffers + counts.attachmentReferences};
        if (counts.renderPasses)
            budget.renderBindings = {
                1u, true, 1u, 1u, true, true, counts.commands, renderBindings, renderBuffers};
        return budget;
    }

    template <class State>
    static void verifyUnavailable(
        const State& state, const rhi::GpuDevice& device, const char* reason)
    {
        const auto budget = budgetFor(state);
        std::size_t emitted = 0u;
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            const auto result = P::visit(
                state.stream.program, device, budget, [&](const auto&) noexcept -> const char* {
                    ++emitted;
                    return nullptr;
                });
            assert(result.diagnostic && !emitted && !result.counts.contributions
                && !result.counts.shaderBuffers && !result.counts.attachments);
        });
        if (std::string_view(reason) != "foreign-device") {
            const P::StateBoundRequest bound{1u, true, budget};
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                OriginalTypedDependencyDecisionFixture::verifyUnavailable(state, reason);
                const auto rejected = P::visitStateBound(state.stream.program,
                    state.graph,
                    bound,
                    [&](const auto&) noexcept -> const char* {
                        ++emitted;
                        return nullptr;
                    });
                assert(rejected.diagnostic && !emitted && !rejected.counts.contributions
                    && !rejected.counts.shaderBuffers && !rejected.counts.attachments);
            });
            std::printf(
                "BINDLESS_RHI_TYPED_STATE_BOUND_USAGE_REJECT_OK %s genuine-state2-stream2 stale-source-or-owner emitted0 counts-cleared no-device-reference query0 native-closed\n",
                reason);
        }
        std::printf(
            "BINDLESS_RHI_TYPED_USAGE_PROJECTION_REJECT_OK %s genuine-stream2 stale-or-foreign emitted0 counts-cleared query0 sync-native-closed\n",
            reason);
    }

    template <class State, std::size_t N>
    static void verify(const State& state,
        const rhi::GpuDevice& device,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, N>& quotas,
        const char* label)
    {
        const auto& stream = state.stream.program;
        assert(!stream->validate());
        const auto& counts = stream->counts();
        const auto& inputs = stream->originalInputs();
        const auto& sources =
            inputs->originalScalars()->originalUsage()->originalSources()->sources();
        const auto budget = budgetFor(state);
        const auto renderBindings = budget.renderBindings.maxBindings;
        const auto renderBuffers = budget.renderBindings.maxLogicalAccesses;
        auto inspected = P::inspect(stream, device, budget);
        if (inspected.diagnostic)
            std::fprintf(stderr, "original projection %s: %s\n", label, inspected.diagnostic);
        assert(!inspected.diagnostic && inspected.counts.renderBindings == renderBindings
            && inspected.counts.renderBindingBuffers == renderBuffers
            && inspected.counts.shaderConsumptions == counts.consumptions
            && inspected.counts.shaderBuffers == counts.implicitAccesses
            && inspected.counts.attachments == counts.attachmentReferences
            && inspected.counts.contributions == budget.maxContributions);
        core::pmr::Vector<Scope::BufferUse> bufferUses;
        bufferUses.reserve(counts.implicitAccesses + renderBuffers);
        std::uint64_t attachmentCount = 0u;
        auto projected =
            P::visit(stream, device, budget, [&](const P::Point& point) noexcept -> const char* {
                if (const auto* buffer = std::get_if<P::BufferPoint>(&point)) {
                    assert(buffer->original.commandIndex == buffer->use.commandIndex
                        && buffer->original.binding == buffer->use.binding);
                    assert(buffer->use.resource
                        == std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            sources[buffer->original.bufferSourceIndex])
                            ->resource());
                    assert(buffer->use.offsetBytes == buffer->original.offsetBytes
                        && buffer->use.sizeBytes == buffer->original.sizeBytes);
                    bufferUses.push_back(buffer->use);
                } else {
                    const auto& a = std::get<P::AttachmentPoint>(point);
                    assert(a.scope.kind == Scope::Kind::RenderPass
                        && a.scope.first == a.original.commandIndex
                        && a.access.state == rhi::ResourceState::ColorAttachment);
                    assert(a.resource
                        == rhi::common::OriginalCommandTextureRanges::originalTexture(
                            sources[a.original.sourceIndex])
                            ->id());
                    ++attachmentCount;
                }
                return nullptr;
            });
        assert(!projected.diagnostic && inspected.counts.matches(projected.counts)
            && attachmentCount == counts.attachmentReferences);
        const auto n = static_cast<std::uint64_t>(bufferUses.size());
        const auto usage = Scope::inspect(
            stream->operations(), bufferUses, {1u, true, counts.commands, n, n * (n - 1u) / 2u});
        const bool shared = std::string_view(label) == "whole-slots-shared";
        if (shared)
            is(usage.diagnostic, "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE");
        else
            assert(!usage.diagnostic);
        const auto reject = [&](auto change, const char* expected) {
            auto less = budget;
            change(less);
            std::size_t emitted = 0u;
            const auto result =
                P::visit(stream, device, less, [&](const auto&) noexcept -> const char* {
                    ++emitted;
                    return nullptr;
                });
            is(result.diagnostic, expected);
            assert(!emitted && !result.counts.contributions && !result.counts.shaderBuffers
                && !result.counts.attachments);
        };
        reject(
            [](auto& b) {
                --b.maxCommands;
            },
            "RHI-TYPED-USAGE-SCOPE-COMMAND-CAPACITY");
        reject(
            [](auto& b) {
                --b.maxContributions;
            },
            "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY");
        reject(
            [](auto& b) {
                --b.shaderConsumption.maxLogicalAccesses;
            },
            "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY");
        if (renderBindings) {
            reject(
                [](auto& b) {
                    --b.renderBindings.maxBindings;
                },
                "RHI-ORIGINAL-RENDER-USAGE-BINDING-CAPACITY");
            reject(
                [](auto& b) {
                    --b.renderBindings.maxLogicalAccesses;
                },
                "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY");
            reject(
                [](auto& b) {
                    b.renderBindings = {};
                },
                "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-INTENT");
        }
        if (counts.attachmentReferences)
            reject(
                [](auto& b) {
                    --b.maxAttachmentReferences;
                },
                "RHI-ORIGINAL-USAGE-PROJECTION-ATTACHMENT-CAPACITY");
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const rhi::OriginalCommandStreamProgram>(
                        state.recordingQuota, stream.get());
                    auto result = P::inspect(forged, device, budget);
                    is(result.diagnostic, "RHI-ORIGINAL-STREAM-OWNER");
                    assert(!result.counts.contributions);
                    std::size_t emitted = 0u;
                    result =
                        P::visit(stream, device, budget, [&](const auto&) noexcept -> const char* {
                            ++emitted;
                            return nullptr;
                        });
                    assert(emitted == budget.maxContributions
                        && result.counts.matches(inspected.counts));
                    return result.diagnostic;
                }));
        });
        const P::StateBoundRequest bound{1u, true, budget};
        const auto stateBound = P::inspectStateBound(stream, state.graph, bound);
        assert(!stateBound.diagnostic && stateBound.counts.matches(inspected.counts));
        const auto boundReject = [&](auto request, auto graph, const char* expected) {
            std::size_t emitted = 0u;
            const auto refused = P::visitStateBound(
                stream, graph, request, [&](const auto&) noexcept -> const char* {
                    ++emitted;
                    return nullptr;
                });
            is(refused.diagnostic, expected);
            assert(!emitted && !refused.counts.contributions);
        };
        auto badBound = bound;
        --badBound.projection.maxContributions;
        boundReject(badBound, state.graph, "RHI-ORIGINAL-USAGE-PROJECTION-CONTRIBUTION-CAPACITY");
        badBound = bound;
        --badBound.projection.shaderConsumption.maxLogicalAccesses;
        boundReject(badBound, state.graph, "RHI-ORIGINAL-CONSUMPTION-ACCESS-CAPACITY");
        if (renderBindings) {
            badBound = bound;
            --badBound.projection.renderBindings.maxBindings;
            boundReject(badBound, state.graph, "RHI-ORIGINAL-RENDER-USAGE-BINDING-CAPACITY");
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const rhi::OriginalCommandResourceStates>(
                        state.stateQuota, state.graph.get());
                    assert(P::inspectStateBound(stream, forged, bound).diagnostic);
                    std::size_t buffers = 0u, attachments = 0u;
                    const auto replay = P::visitStateBound(stream,
                        state.graph,
                        bound,
                        [&](const P::Point& point) noexcept -> const char* {
                            if (const auto* buffer = std::get_if<P::BufferPoint>(&point)) {
                                assert(buffers < bufferUses.size());
                                const auto& expected = bufferUses[buffers++];
                                const auto& actual = buffer->use;
                                assert(actual.resource == expected.resource
                                    && actual.bufferBytes == expected.bufferBytes
                                    && actual.offsetBytes == expected.offsetBytes
                                    && actual.sizeBytes == expected.sizeBytes
                                    && actual.commandIndex == expected.commandIndex
                                    && actual.set == expected.set
                                    && actual.binding == expected.binding
                                    && actual.scope.matches(expected.scope)
                                    && actual.contribution == expected.contribution
                                    && actual.usage == expected.usage
                                    && actual.access.state == expected.access.state
                                    && actual.access.stages == expected.access.stages
                                    && actual.access.access == expected.access.access);
                            } else
                                ++attachments;
                            return nullptr;
                        });
                    assert(!replay.diagnostic && replay.counts.matches(inspected.counts)
                        && buffers == bufferUses.size() && attachments == attachmentCount);
                    return nullptr;
                }));
        });
        std::printf(
            "BINDLESS_RHI_TYPED_STATE_BOUND_USAGE_NATIVE_OK %s contributions%llu cold-projection-equivalent full-render-bindings-and-attachments authentic-state2-stream2 forged-less emitted0 quotas-full hot0 query0 no-device-reference sync-native-closed\n",
            label,
            static_cast<unsigned long long>(stateBound.counts.contributions));
        std::printf(
            "BINDLESS_RHI_TYPED_USAGE_PROJECTION_NATIVE_OK %s bindings%llu render-buffers%llu shader-buffers%llu attachments%llu contributions%llu original-stream2-inputs3 exact-less emitted0 forged-root full-pressure hot0 query0 shared-usage%s sync-native-closed\n",
            label,
            static_cast<unsigned long long>(renderBindings),
            static_cast<unsigned long long>(renderBuffers),
            static_cast<unsigned long long>(counts.implicitAccesses),
            static_cast<unsigned long long>(counts.attachmentReferences),
            static_cast<unsigned long long>(budget.maxContributions),
            shared ? "-rejected" : "-compatible");
    }
#endif
};
} // namespace arp::test
