#include "original_typed_usage_projection_fixture.h"
#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_native_identity_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_cold_admission.h"

namespace arp::test {
// Reusable checks over the existing genuine State2/Stream2/Transaction2 bundle.
// This fixture never creates a shader actor or native command.
class OriginalCommandNativeClosureTypedFixture final {
    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
    using Budget = rhi::OriginalCommandNativeClosureBudget;

public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    template <class State, std::size_t Count>
    static std::shared_ptr<const Closure> verify(const State& state,
        const rhi::GpuDevice& device,
        Budget budget,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas,
        const char* label,
        const rhi::OriginalDeviceNativeLossObservation* loss = nullptr,
        const char* dependencyRefusal = nullptr)
    {
        assert(state.graph && state.transaction && state.stream.program
            && !state.transaction->validate());
        const char* diagnostic = nullptr;
        const auto typedFactory = [&](auto root, auto stream) {
            return loss ? Closure::prepareOriginalTypedLossBound(
                              std::move(root), std::move(stream), device, budget, *loss, diagnostic)
                        : Closure::prepareOriginalTyped(
                              std::move(root), std::move(stream), device, budget, diagnostic);
        };
        const auto factory = [&] {
            return typedFactory(state.transaction, state.stream.program);
        };
        auto closure = factory();
        if (!closure)
            std::fprintf(
                stderr, "typed closure %s: %s\n", label, diagnostic ? diagnostic : "success");
        assert(closure);
        assert(closure->closureContractVersion() == 2u
            && closure->resourceBindingCount() == budget.maxResourceBindings
            && closure->pipelineBindingCount() == budget.maxPipelineSources
            && closure->descriptorBindingCount() == budget.maxDescriptorSources);
        assert(closure->retainedResourceBytes() == budget.maxRetainedResourceBytes);
        assert(!closure->compareOriginalRoot(state.transaction) && !closure->nativeExecutionProven()
            && closure->executionDiagnostic());
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        budget.maxNativePayloadObjects = static_cast<std::uint32_t>(closure->nativePayloadCount());
        closure.reset();
        CpuQuotaFactoryFixture recording(state.recordingQuota);
        const auto peak =
            loss ? recording.measureSingleUse([] {}, factory) : recording.measure(factory);
        recording.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(state.recordingQuota,
                state.recordingQuota->limitBytes() - state.recordingQuota->liveBytes());
            assert(pressure.charged() && !factory());
        }
        const auto reject = [&](auto change) {
            const auto saved = budget;
            change(budget);
            assert(!factory() && diagnostic);
            budget = saved;
        };
        reject([](auto& b) {
            b.contractVersion = 1u;
        });
        reject([](auto& b) {
            b.prototypeOptIn = false;
        });
        reject([](auto& b) {
            --b.maxResourceBindings;
        });
        reject([](auto& b) {
            --b.maxPipelineSources;
        });
        reject([](auto& b) {
            --b.maxDescriptorSources;
        });
        reject([](auto& b) {
            --b.maxNativePayloadObjects;
        });
        reject([](auto& b) {
            --b.maxRetainedOwners;
        });
        reject([](auto& b) {
            --b.maxRetainedResourceBytes;
        });
        reject([](auto& b) {
            --b.maxCpuMetadataBytes;
        });
        closure = factory();
        assert(closure);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged =
                        std::shared_ptr<const Closure>(state.recordingQuota, closure.get());
                    assert(Closure::validateOriginal(forged));
                    auto forgedStream = std::shared_ptr<const rhi::OriginalCommandStreamProgram>(
                        state.stream.quota, state.stream.program.get());
                    assert(!typedFactory(state.transaction, forgedStream) && diagnostic);
                    auto forgedRoot = std::shared_ptr<rhi::OriginalCommandStateTransaction>(
                        state.recordingQuota, state.transaction.get());
                    assert(!typedFactory(forgedRoot, state.stream.program) && diagnostic);
                    assert(!typedFactory(state.transaction,
                               std::shared_ptr<const rhi::OriginalCommandStreamProgram>{})
                        && diagnostic);
                    const auto& sources = state.graph->originalInputs()
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
                                            assert(closure->validate());
                                            const auto count =
                                                core::defaultCpuAllocator().stats().allocationCount;
                                            assert(!factory() && diagnostic
                                                && count
                                                    == core::defaultCpuAllocator()
                                                        .stats()
                                                        .allocationCount);
                                        });
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandComputePipelineSource>
                                    || std::is_same_v<T,
                                        rhi::OriginalCommandGraphicsPipelineSource>) {
                                    const auto payload = rhi_webgpu::webGpuNativePayloadFor(
                                        *value->originalPipeline());
                                    assert(payload && payload->issuedContract());
                                    const auto original = payload->pipeline;
                                    payload->pipeline = nullptr;
                                    assert(closure->validate() && !factory());
                                    payload->pipeline = original;
                                    const auto layout = payload->layout;
                                    payload->layout = nullptr;
                                    assert(closure->validate() && !factory());
                                    payload->layout = layout;
                                }
                            },
                            source);
                    return Closure::validateOriginal(closure);
                }));
        });
        verifyLegacyColdRejected(state, closure, budget, quotas, !loss);
        if (loss)
            OriginalTypedUsageProjectionFixture::verify(state, device, quotas, label);
        if (loss)
            assert(closure->originalNativeLossObservation() && !closure->nativeLossDiagnostic()
                && !closure->originalNativeLossObservation()->compareOriginal(*loss));
        std::printf(
            "%s %s resources%zu pipelines%zu descriptors%zu payloads%zu owners%u bytes%llu peak%llu exact-less-exhausted original-stream-state-plan forged-native-mutated-reject late-failure0 hot0 query0 recording-closed\n",
            loss ? "BINDLESS_RHI_TYPED_LOSS_BOUND_CLOSURE_OK"
                 : "BINDLESS_RHI_TYPED_NATIVE_CLOSURE_OK",
            label,
            closure->resourceBindingCount(),
            closure->pipelineBindingCount(),
            closure->descriptorBindingCount(),
            closure->nativePayloadCount(),
            closure->retainedOwnerCount(),
            static_cast<unsigned long long>(closure->retainedResourceBytes()),
            static_cast<unsigned long long>(peak));
        if (loss)
            state.dependencyDecision = OriginalTypedDependencyDecisionFixture::verify(state,
                device,
                closure,
                quotas,
                label,
                OriginalTypedUsageProjectionFixture::budgetFor(state),
                dependencyRefusal);
        return closure;
    }

    template <class State, std::size_t Count>
    static void verifyLegacyColdRejected(const State& state,
        const std::shared_ptr<const Closure>& closure,
        const Budget& closureBudget,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, Count>& quotas,
        bool report = true)
    {
        using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
        using Contract = rhi::OriginalCommandNativeColdContract;
        const auto required = Admission::selectedRequirements();
        const Contract::Budget base{1u,
            true,
            static_cast<std::uint32_t>(state.stream.program->counts().commands),
            1u,
            1u,
            1u,
            required.recordingScopeCallbacks,
            required.submissionScopeCallbacks,
            required.queueCallbacks,
            required.runtimeLossCallbacks,
            required.runtimeLossRegistrations,
            closure->retainedOwnerCount(),
            closureBudget.maxCpuMetadataBytes};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(core::StringView(Admission::inspectOriginalPlan(state.transaction))
                        == "RHI-ORIGINAL-NATIVE-COLD-TYPED-PROFILE-REQUIRED");
                    for (const auto version : std::array{1u, 2u, 3u, 4u, 5u}) {
                        auto b = base;
                        b.contractVersion = version;
                        // Adversarial but valid value intents cannot adapt an
                        // actual typed root to copy/upload/readback profiles.
                        if (version == 3u) {
                            b.maxBufferUploads = b.maxNativeCopyCommands = b.maxBufferUploadBytes =
                                b.maxTotalUploadBytes = 1u;
                        } else if (version >= 4u) {
                            b.maxRenderPasses = 0u;
                            b.readback = {1u, true, 1u, 1u, 1u, 1u, 1u, 1u};
                            if (version == 5u) {
                                using Map = rhi::OriginalCommandReadbackMapContract;
                                b.readbackMap = {1u,
                                    true,
                                    1u,
                                    1u,
                                    Map::scopeCallbacksPerMap,
                                    required.capacity.callbackTickets + Map::participantsPerMap,
                                    required.capacity.callbackRegistrations
                                        + Map::participantsPerMap};
                            }
                        }
                        assert(!Contract::validateIntent(b));
                        const char* id = nullptr;
                        auto counts = required;
                        assert(core::StringView(
                                   Admission::deriveRequirements(state.transaction, b, counts))
                                == "RHI-ORIGINAL-NATIVE-COLD-TYPED-PROFILE-REQUIRED"
                            && !counts.originalCommands && !counts.encoders
                            && !counts.commandBuffers && !counts.capacity.pendingSubmissions);
                        assert(!Admission::prepareOriginal(closure, {}, b, id)
                            && core::StringView(id)
                                == "RHI-ORIGINAL-NATIVE-COLD-TYPED-PROFILE-REQUIRED");
                    }
                    return Closure::validateOriginal(closure);
                }));
        });
        if (report)
            std::puts(
                "BINDLESS_RHI_TYPED_LEGACY_COLD_REJECT_OK profiles1-5 state2 closure2 counts-cleared reservation-independent hot0 query0 recording-closed");
    }

    template <class Factory, class State>
    static void verifyLossBoundUnavailable(Factory&& create,
        const State& state,
        const rhi::GpuDevice& device,
        Budget budget,
        const rhi::PipelineLayoutDesc& layout)
    {
        const char* id = nullptr;
        rhi::OriginalDeviceNativeLossObservation unissued;
        assert(!Closure::prepareOriginalTypedLossBound(
                   state.transaction, state.stream.program, device, budget, unissued, id)
            && core::StringView(id) == "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED");
        assert(!device.observeOriginalDeviceNativeLoss(id)
            && core::StringView(id) == "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED");
        auto bound = create();
        assert(bound);
        auto domain = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto subscription = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto registryQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        assert(!bound->prepareOriginalDeviceLifetime(
            {1u, true, 1u, 2u, budget.maxCpuMetadataBytes}, domain));
        assert(!bound->prepareOriginalDeviceNativeLoss(
            {{1u, true, 1u, 1u, budget.maxCpuMetadataBytes}, 1u}, subscription));
        auto loss = bound->observeOriginalDeviceNativeLoss(id);
        assert(loss && !id);
        OriginalTypedUsageProjectionFixture::verifyUnavailable(state, *bound, "foreign-device");
        rhi::OriginalResourceRegistrationBudget registry;
        registry.contractVersion = 5u;
        registry.prototypeOptIn = true;
        registry.maxPipelineLayouts = registry.maxComputePipelines = registry.maxDescriptorArenas =
            registry.maxDescriptorSets = 1u;
        using Registration = rhi::OriginalResourceRegistrationContract;
        using Kind = rhi::OriginalRegisteredResourceKind;
        registry.maxRetainedOwners = Registration::fixedOwnerCount
            + Registration::ownerCount(Kind::PipelineLayout, 5u)
            + Registration::ownerCount(Kind::ComputePipeline, 5u)
            + Registration::ownerCount(Kind::DescriptorArena, 5u)
            + Registration::ownerCount(Kind::Descriptor, 5u);
        registry.maxCpuMetadataBytes = budget.maxCpuMetadataBytes;
        assert(!bound->prepareOriginalResourceRegistration(registry, registryQuota));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{
                    domain, subscription, registryQuota, state.recordingQuota, state.stream.quota},
                [&]() noexcept -> const char* {
                    assert(!Closure::prepareOriginalTypedLossBound(
                               state.transaction, state.stream.program, device, budget, *loss, id)
                        && core::StringView(id) == "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
                    // Authentic observation on its own original device still lacks
                    // approved typed registration joins; reject before root/allocation.
                    assert(!Closure::prepareOriginalTypedLossBound(
                               state.transaction, state.stream.program, *bound, budget, *loss, id)
                        && core::StringView(id)
                            == "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-LOSS-WEBGPU-JOIN-MISSING");
                    assert(!bound->createOriginalPipelineLayout(layout)
                        && bound->lastOriginalResourceRegistrationDiagnostic()
                            == "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING");
                    rhi::DescriptorArenaDesc arena;
                    arena.maxSets = arena.maxDescriptors = 1u;
                    assert(!bound->createOriginalDescriptorArena(arena)
                        && bound->lastOriginalResourceRegistrationDiagnostic()
                            == "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING");
                    return nullptr;
                }));
        });
        loss.reset();
        bound.reset();
        assert(!domain->liveBytes() && !subscription->liveBytes() && !registryQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_NATIVE_CLOSURE_LOSS_BOUNDARY_OK genuine-armed observation unissued-foreign typed-pipeline-descriptor-joins-missing hot0 query0 no-retrofit recording-closed");
    }
#endif
};
} // namespace arp::test
