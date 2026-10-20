#pragma once
#include "original_accepted_compute_pipeline_factory_fixture.h"
#include "original_command_descriptor_state_fixture.h"
#include "original_command_native_closure_typed_fixture.h"
#include "original_target_memory_fixture.h"
#include "original_whole_descriptor_write_fixture.h"

namespace arp::test {
// Two genuine logical slots on one existing original root. The actor comes
// from the Slang publication and accepted factory, never a mock pipeline.
class OriginalCommandDescriptorSlotsFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    using Source = rhi::OriginalCommandDescriptorSetSource;
    using Lease = rhi::OriginalCommandBufferLease;
    using WholeQuotas = std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>, 2u>;
    using State = OriginalCommandDescriptorStateFixture::Retained;
    using Context = OriginalAcceptedComputePipelineFactoryFixture::PreparedContext;
    inline static core::UniquePtr<Context> lossContext_;
    inline static State lossState_;
    inline static core::UniquePtr<Context> lossTypedContext_;
    inline static State lossTypedState_;
    inline static core::UniquePtr<Context> lossBoundContext_;
    inline static State lossBoundState_;
    inline static WholeQuotas lossBoundWholeQuotas_;
    inline static WholeQuotas lossTypedWholeQuotas_;
    inline static std::array<std::shared_ptr<core::CpuMemoryQuota>, 2u> lossTypedPublicationQuotas_;
    inline static WholeQuotas lossWholeQuotas_;
    inline static std::array<std::shared_ptr<core::CpuMemoryQuota>, 2u> lossPublicationQuotas_;

    // Explicit original root order authored below: Buffer0, Buffer1,
    // Pipeline, Set0, Set1. These are Source positions, never native groups.
    static auto terminalSource(const State& state)
    {
        const auto& root =
            state.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        assert(root->sources().size() == 5u);
        auto source = std::get<std::shared_ptr<const Source>>(root->sources()[4u]);
        assert(source->frozenElements().size() == 1u
            && source->frozenElements()[0].declaration.sourceIndex == 1u
            && source->frozenElements()[0].declaration.write.type
                == rhi::DescriptorType::StorageBuffer);
        return source;
    }

    static void beginTerminalReplacement(Context& c, const State& state, const WholeQuotas& quotas)
    {
        const auto& root =
            state.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto pipeline = std::const_pointer_cast<rhi::ComputePipeline>(
            std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                root->sources()[2u])
                ->originalPipeline());
        const auto source = terminalSource(state);
        const auto buffer =
            std::get<std::shared_ptr<const Lease>>(source->frozenElements()[0].resourceOwner);
        const auto writes = wholeWrite(source, buffer);
        auto* object = const_cast<rhi::DescriptorSet*>(source->originalObject());
        assert(!c.device->beginOriginalWholeDescriptorWrite(
            *object, pipeline, 1u, writes.data(), writes.size(), wholeRequest(), quotas[1]));
        drainWhole(*object);
        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
            rhi_webgpu::webGpuNativePayloadFor(*object), "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
    }

    static void verifyTerminalRejected(const State& state,
        const WholeQuotas& quotas,
        const char* expected = nullptr,
        bool pendingPlan = false,
        bool report = true)
    {
        auto source = terminalSource(state);
        auto* object = const_cast<rhi::DescriptorSet*>(source->originalObject());
        const auto revision = object->contentRevision();
        assert(core::StringView(Source::validateOriginal(source))
            == "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quotas[1][0],
                    quotas[1][1],
                    quotas[1][2],
                    source->originalMetadataQuota(),
                    state.graph->originalInputs()->originalMetadataQuota(),
                    state.stateQuota,
                    state.recordingQuota,
                    state.stream.quota},
                [&]() noexcept -> const char* {
                    const auto payload = rhi_webgpu::webGpuNativePayloadFor(*object);
                    assert(payload);
                    assert(payload->diagnoseOriginalNativeIdentity({1u,
                        true,
                        rhi::OriginalDescriptorNativeIdentityRequest::Profile::WholeWrite1}));
                    const auto* diagnostic = object->finishOriginalWholeWrite();
                    assert(diagnostic);
                    if (expected)
                        assert(core::StringView(diagnostic) == expected);
                    assert(object->contentRevision() == revision);
                    assert(!object->cancelOriginalWholeWrite());
                    diagnostic = object->originalScopedContentDiagnostic();
                    assert(diagnostic);
                    if (expected)
                        assert(core::StringView(diagnostic) == expected);
                    OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                        payload, diagnostic);
                    assert(Source::validateOriginal(source)
                        && state.graph->originalInputs()->validate());
                    OriginalCommandDescriptorStateFixture::verifyRetained(
                        state, false, pendingPlan);
                    return nullptr;
                }));
        });
        if (report)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_WHOLE_PUBLIC_IDENTITY_TERMINAL_OK pending-and-committed-reject retained-hot0-query0 execution-closed");
    }

    static void releaseTerminal(State& state, Context& c, const WholeQuotas& quotas)
    {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> retainedQuotas{state.stateQuota,
            state.recordingQuota,
            state.stream.quota,
            state.graph->originalInputs()->originalMetadataQuota(),
            state.graph->originalInputs()
                ->originalScalars()
                ->originalUsage()
                ->originalSources()
                ->originalMetadataQuota()};
        state = {};
        for (const auto& quota : retainedQuotas)
            assert(!quota->liveBytes());
        for (const auto& group : quotas)
            for (const auto& quota : group)
                assert(!quota->liveBytes());
        c.retire();
    }

    static constexpr std::uint64_t fixtureBytes = 65536u;

    static rhi::OriginalWholeDescriptorWriteRequest wholeRequest()
    {
        using C = rhi::OriginalWholeDescriptorWriteContract;
        return {1u,
            true,
            1u,
            static_cast<std::uint32_t>(C::ownerSlots(1u)),
            C::callbackCount,
            {fixtureBytes, fixtureBytes, fixtureBytes}};
    }

    static std::array<rhi::OriginalWholeDescriptorBufferWrite, 1u> wholeWrite(
        const std::shared_ptr<const Source>& source, const std::shared_ptr<const Lease>& buffer)
    {
        return std::array<rhi::OriginalWholeDescriptorBufferWrite, 1u>{
            {{source->frozenElements()[0].declaration.write, buffer, 0u}}};
    }

    static void drainWhole(const rhi::DescriptorSet& object)
    {
        assert(WebGpuAsyncWaitFixture::until([&] {
            return object.originalWholeWriteScopesDrained();
        }));
    }

    template <class Context>
    static void verifySourceIdentity(Context& c,
        const std::shared_ptr<const Source>& source,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& wholeQuotas)
    {
        const auto payload = rhi_webgpu::webGpuNativePayloadFor(*source->originalObject());
        assert(payload);
        const auto& sourceQuota = source->originalMetadataQuota();
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 11u> quotas{wholeQuotas[0],
            wholeQuotas[1],
            wholeQuotas[2],
            sourceQuota,
            c.quotas[0],
            c.quotas[1],
            c.quotas[2],
            c.quotas[3],
            c.quotas[4],
            c.quotas[5],
            c.quotas[6]};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    assert(!Source::validateOriginal(source, *c.device));
                    OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
                    OriginalDescriptorNativeIdentityFixture::mutateActual(payload, [&] {
                        assert(source->originalObject()->originalScopedContentDiagnostic()
                            && Source::validateOriginal(source, *c.device));
                        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                            payload, "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY");
                        std::uint32_t group = 99u, count = 99u;
                        const std::uint32_t* offsets = nullptr;
                        assert(payload->diagnoseOriginalFiniteGroup(group, offsets, count) && !group
                            && !count && !offsets);
                    });
                    OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
                    return Source::validateOriginal(source, *c.device);
                }));
        });
        for (const auto& quota :
            std::array{wholeQuotas[0], wholeQuotas[1], wholeQuotas[2], sourceQuota}) {
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            assert(Source::validateOriginal(source));
            quota->setLimitBytes(limit);
            assert(!Source::validateOriginal(source));
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_SOURCE2_IDENTITY_OK genuine-source2 shared-mutation-driver original-source-full-pressure quota-limit-mutation restored output0 hot0 query0 public-metadata-identity");
    }

    template <class Context>
    static void verifyRetained(Context& c,
        const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const std::array<std::shared_ptr<const Lease>, 3u>& buffers,
        const std::array<std::shared_ptr<const Source>, 2u>& sets,
        const WholeQuotas& wholeQuotas,
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const OriginalCommandDescriptorStateFixture::Retained& state,
        bool report = true)
    {
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 18u> quotas{};
        std::size_t index = 0u;
        for (const auto& group : wholeQuotas)
            for (const auto& q : group)
                quotas[index++] = q;
        quotas[index++] = sets[0]->originalMetadataQuota();
        quotas[index++] = inputs->originalMetadataQuota();
        quotas[index++] = state.stateQuota;
        quotas[index++] = state.recordingQuota;
        quotas[index++] = state.stream.quota;
        for (const auto& q : c.quotas)
            quotas[index++] = q;
        assert(index == quotas.size());
        const auto check = [&](bool valid) noexcept {
            assert(bool(inputs->validate()) != valid);
            OriginalCommandDescriptorStateFixture::verifyRetained(state, valid);
        };
        for (std::size_t slot = 0u; slot < sets.size(); ++slot) {
            const auto payload = rhi_webgpu::webGpuNativePayloadFor(*sets[slot]->originalObject());
            assert(payload);
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        check(true);
                        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
                        OriginalDescriptorNativeIdentityFixture::mutateActual(payload, [&] {
                            assert(Source::validateOriginal(sets[slot]));
                            check(false);
                            OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                                payload, "RHI-ORIGINAL-FINITE-WRITE-NATIVE-IDENTITY");
                        });
                        check(true);
                        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
                        return nullptr;
                    }));
            });
            for (const auto& q : wholeQuotas[slot]) {
                const auto limit = q->limitBytes();
                q->setLimitBytes(limit - 1u);
                check(false);
                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                    OriginalTypedDependencyDecisionFixture::verifyUnavailable(state, "quota");
                });
                q->setLimitBytes(limit);
                check(true);
            }
            // The borrowed mutable pointer originates from this fixture's
            // unique descriptor publication; no caller/native authority is added.
            auto* object = const_cast<rhi::DescriptorSet*>(sets[slot]->originalObject());
            const auto writes = wholeWrite(sets[slot], buffers[slot]);
            const auto revision = object->contentRevision();
            using Scopes = WebGpuErrorScopeHandoffFixture<1005u>;
            Scopes::holdScopes();
            assert(!c.device->beginOriginalWholeDescriptorWrite(*object,
                pipeline,
                static_cast<std::uint32_t>(slot),
                writes.data(),
                writes.size(),
                wholeRequest(),
                wholeQuotas[slot]));
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(core::StringView(Source::validateOriginal(sets[slot]))
                            == "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
                        OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(
                            payload, "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
                        check(false);
                        OriginalTypedDependencyDecisionFixture::verifyUnavailable(state, "pending");
                        assert(object->finishOriginalWholeWrite()
                            && object->cancelOriginalWholeWrite());
                        return nullptr;
                    }));
            });
            Scopes::releaseScopes();
            drainWhole(*object);
            assert(!object->cancelOriginalWholeWrite());
            assert(object->contentRevision() == revision
                && rhi_webgpu::webGpuNativePayloadFor(*object) == payload);
            check(true);
            OriginalDescriptorNativeIdentityFixture::verifyWholeIdentity(payload);
        }
        const auto& sourceQuota = sets[0]->originalMetadataQuota();
        const auto limit = sourceQuota->limitBytes();
        sourceQuota->setLimitBytes(limit - 1u);
        check(false);
        sourceQuota->setLimitBytes(limit);
        check(true);
        if (report)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_RETAINED_IDENTITY_OK source2-inputs3-state2-stream2 original-source-graph-recording-full-pressure both-slot-mutations pending-replacement-rejected cancel-restores caller-drop hot0 query0 native-closed");
    }

    template <class Wait, class Map>
    static void verifyNumeric(const std::shared_ptr<rhi::ComputePipeline>& pipeline,
        const std::array<std::shared_ptr<const rhi::OriginalCommandBufferLease>, 3u>& buffers,
        const std::array<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>, 2u>& sets,
        bool shared,
        bool conflict,
        Wait&& wait,
        Map&& map)
    {
        const auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        assert(native && native->issuedContract());
        const auto device = static_cast<WGPUDevice>(native->issuedContract()->nativeDevice());
        assert(device);
        const auto source = rhi_webgpu::webGpuNativePayloadFor(*buffers[0]->originalObject());
        const auto output = rhi_webgpu::webGpuNativePayloadFor(*buffers[1]->originalObject());
        const auto readback = rhi_webgpu::webGpuNativePayloadFor(*buffers[2]->originalObject());
        assert(source && output && readback && source->ownerDevice == device
            && output->ownerDevice == device && readback->ownerDevice == device);
        auto queue = wgpuDeviceGetQueue(device);
        assert(queue);
        constexpr std::array<std::uint32_t, 4u> seed{29u, 0u, 0u, 0u};
        rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
        wgpuQueueWriteBuffer(queue, source->buffer, 0u, seed.data(), sizeof(seed));
        auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
        assert(encoder);
        auto passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        auto pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        assert(pass);
        wgpuComputePassEncoderSetPipeline(pass, native->pipeline);
        for (std::size_t slot = 0u; slot < sets.size(); ++slot) {
            assert(!rhi::OriginalCommandDescriptorSetSource::validateOriginal(sets[slot]));
            const auto payload = rhi_webgpu::webGpuNativePayloadFor(*sets[slot]->originalObject());
            assert(payload && payload->containsOriginalWholeWrite());
            const auto facts = OriginalWholeDescriptorWriteFixture::originalTarget(
                pipeline, static_cast<std::uint32_t>(slot), 0u);
            assert(!payload->dynamicOffsetCount);
            wgpuComputePassEncoderSetBindGroup(pass, facts.group, payload->group, 0u, nullptr);
        }
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1u, 1u, 1u);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (!shared)
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, output->buffer, 0u, readback->buffer, 0u, readback->sizeBytes);
        auto commandDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
        assert(command);
        // Observe recording validation before any command submission. A CPU
        // state-compatible alias cannot stand in for native usage-scope proof.
        assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
            rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
            shared ? 1u : 0u));
        if (shared) {
            std::printf(
                "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_SLOTS_ALIAS_REJECTED shared1 conflict%u real-validation-scope invalid-command-not-submitted native-closed\n",
                conflict ? 1u : 0u);
        } else {
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            wgpuQueueSubmit(queue, 1u, &command);
            assert(wait(queue) && map(readback->buffer, readback->sizeBytes));
            const auto* values = static_cast<const std::uint32_t*>(
                wgpuBufferGetConstMappedRange(readback->buffer, 0u, readback->sizeBytes));
            assert(values);
            assert(values[0] == seed[0] + 7u);
            std::printf(
                "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_SLOTS_VALUES logical-sets2 input%u output%u expected%u exact-source2-groups real-completion-map native-closed\n",
                seed[0],
                values[0],
                seed[0] + 7u);
            wgpuBufferUnmap(readback->buffer);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                0u));
        }
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueRelease(queue);
    }
#endif

public:
    template <class Factory, class Wait, class Map>
    static void verify(Factory&& create, Wait&& wait, Map&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        using Artifact = shader::SingleEntryShaderArtifact;
        using PipelineSource = rhi::OriginalCommandComputePipelineSource;
        using Sources = rhi::OriginalCommandResourceSources;
        using Source = rhi::OriginalCommandDescriptorSetSource;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        constexpr std::uint64_t bytes = 65536u;
        const char* path = "/descriptor-state-slots/descriptor_state_slots.computeMain.entry.yaml";
        auto budget = OriginalTargetMemoryFixture::entryBudget();
        auto artifactQuota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        core::pmr::String diagnostic;
        auto artifact = Artifact::load(path, budget, artifactQuota, diagnostic);
        assert(artifact);
        TargetMemoryRequestFixture memory;
        memory.prepare(artifact);
        budget.targetMemory = &memory.request();
        artifact = Artifact::load(path, budget, artifactQuota, diagnostic);
        assert(artifact);
        auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
            artifact, {1u, bytes}, inputQuota, diagnostic);
        assert(input);
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        assert(layout.descriptorLayouts.size() == 2u
            && layout.descriptorLayouts[0].bindings.size() == 1u
            && layout.descriptorLayouts[1].bindings.size() == 1u);
        for (const unsigned mode :
            std::array{0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u}) {
            const bool shared = mode == 1u || mode == 2u || mode == 8u, conflict = mode == 2u;
            const bool bound = mode == 7u || mode == 8u || mode >= 13u;
            auto context = OriginalAcceptedComputePipelineFactoryFixture::prepareContext(
                create, input, layout, (shared ? 1u : 2u) + 1u, 2u, bound ? 7u : 5u);
            context->ready();
            const char* id = nullptr;
            auto pipeline =
                rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(context->publish(), id);
            assert(pipeline);
            assert(rhi_webgpu::webGpuNativePayloadFor(*pipeline)->issuedContract());
            auto quota = core::makeShared<core::CpuMemoryQuota>(bytes),
                 graphQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
            {
                std::array<std::shared_ptr<const rhi::OriginalCommandBufferLease>, 3u> buffers;
                for (std::size_t n = 0u; n < (shared ? 2u : 3u); ++n) {
                    const auto i = shared && n == 1u ? 2u : n;
                    rhi::BufferDesc desc;
                    desc.sizeBytes = 16u;
                    desc.usage = i == 2u ? rhi::ResourceUsage::CopyDst
                                         : rhi::ResourceUsage::StorageBuffer
                            | (i == 0u ? rhi::ResourceUsage::CopyDst : rhi::ResourceUsage::CopySrc);
                    desc.memory = i == 2u ? rhi::MemoryUsage::GpuToCpu : rhi::MemoryUsage::GpuOnly;
                    desc.initialState = rhi::ResourceState::Undefined;
                    core::UniquePtr<rhi::GpuBuffer> object;
                    {
                        core::CpuMemoryQuotaScope scope(context->quotas[2]);
                        object = context->device->createBuffer(desc);
                    }
                    assert(object);
                    buffers[i] = rhi::OriginalCommandBufferLease::prepareOriginal(
                        object, {1u, true, 2u, bytes}, quota, id);
                    assert(buffers[i]);
                }
                if (shared)
                    buffers[1] = buffers[0];
                const auto count = shared ? 1u : 2u;
                auto pipelineSource = PipelineSource::prepareOriginal(pipeline,
                    {1u,
                        true,
                        PipelineSource::kind,
                        pipeline->originalResourceRegistration().generation(),
                        bytes,
                        PipelineSource::exclusiveOwnerCount},
                    quota,
                    *context->device,
                    id);
                assert(pipelineSource);
                std::array<Sources::Source, 2u> prefix{buffers[0], buffers[1]};
                rhi::OriginalCommandAdmissionBudget admission;
                admission.contractVersion = 1u;
                admission.prototypeOptIn = true;
                admission.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Buffer)] = count;
                admission.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Pipeline)] = 1u;
                admission.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::Descriptor)] = 2u;
                admission.maxCommands = 17u + 4u * count;
                admission.maxReferences = 9u + 4u * count;
                admission.maxInputDescriptors = 9u + 4u * count;
                admission.maxGraphOwners = 64u;
                admission.maxStateObservations = count;
                admission.maxStateTransactions = 1u;
                admission.maxCpuBytes.fill(bytes);
                if (mode >= 7u) {
                    // Separate genuine native inspection admission; the existing
                    // CPU-only contexts above keep their original zero limits.
                    admission.maxNativePayloadObjects = count + 3u;
                    admission.maxTypedNativeCommandCalls = 32u;
                    admission.maxPendingSubmissions = 1u;
                    admission.maxCallbackTickets = 4u;
                    admission.maxCallbackRegistrations = 4u;
                }
                auto root = Sources::prepareColdOriginalTyped(
                    prefix.data(), count, admission, {2u, true, 3u}, quota, id);
                assert(root);
                assert(!Sources::appendOriginalTyped(root, {pipelineSource}, *context->device));
                std::shared_ptr<rhi::DescriptorArena> arena;
                {
                    core::CpuMemoryQuotaScope scope(context->quotas[2]);
                    rhi::DescriptorArenaDesc desc;
                    desc.maxSets = 2u;
                    desc.maxDescriptors = 2u;
                    arena = core::shareUnique(context->device->createOriginalDescriptorArena(desc));
                }
                assert(arena);
                std::array<std::shared_ptr<const Source>, 2u> sets;
                WholeQuotas wholeQuotas;
                for (std::size_t slot = 0u; slot < sets.size(); ++slot) {
                    core::UniquePtr<rhi::DescriptorSet> object;
                    {
                        core::CpuMemoryQuotaScope scope(context->quotas[2]);
                        rhi::DescriptorSetDesc desc;
                        desc.layout =
                            rhi::DescriptorStateCopy::copy(layout.descriptorLayouts[slot]);
                        object = context->device->allocateOriginalDescriptorSet(arena, desc);
                    }
                    assert(object);
                    Source::Element e;
                    e.sourceIndex = shared ? 0u : slot;
                    e.registrationGeneration = buffers[slot]
                                                   ->originalObject()
                                                   ->originalResourceRegistration()
                                                   .generation();
                    e.write.binding = 0u;
                    e.write.type = layout.descriptorLayouts[slot].bindings[0].type;
                    e.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
                    e.write.resource = buffers[slot]->resource();
                    e.write.resourceState = slot || (shared && !conflict)
                        ? rhi::ResourceState::ShaderWrite
                        : rhi::ResourceState::ShaderRead;
                    e.stages = rhi::PipelineStage::ComputeShader;
                    e.access = slot
                        ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
                        : rhi::ResourceAccess::ShaderRead;
                    e.bufferAccessBytes = buffers[slot]->layout().sizeBytes;
                    const std::array<rhi::OriginalWholeDescriptorBufferWrite, 1u> whole{
                        {{e.write, buffers[slot], 0u}}};
                    if (mode < 3u) {
                        (void)OriginalWholeDescriptorWriteFixture::verifyActual(*context,
                            pipeline,
                            *object,
                            static_cast<std::uint32_t>(slot),
                            whole,
                            &wholeQuotas[slot]);
                        using Scopes = WebGpuErrorScopeHandoffFixture<1005u>;
                        const auto* original = object.get();
                        Scopes::holdScopes();
                        assert(!context->device->beginOriginalWholeDescriptorWrite(*object,
                            pipeline,
                            static_cast<std::uint32_t>(slot),
                            whole.data(),
                            whole.size(),
                            wholeRequest(),
                            wholeQuotas[slot]));
                        Scopes::restoreScopes();
                        Scopes::waitHeldScopes();
                        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                std::array{quota,
                                    wholeQuotas[slot][0],
                                    wholeQuotas[slot][1],
                                    wholeQuotas[slot][2]},
                                [&]() noexcept -> const char* {
                                    assert(!Source::prepareOriginal(object,
                                               arena,
                                               root,
                                               &e,
                                               1u,
                                               {2u, true, 1u, 1u, 5u, bytes},
                                               quota,
                                               *context->device,
                                               id)
                                        && object.get() == original
                                        && core::StringView(id)
                                            == "RHI-ORIGINAL-WHOLE-WRITE-PENDING");
                                    return nullptr;
                                }));
                        });
                        Scopes::releaseScopes();
                        drainWhole(*object);
                        assert(!object->cancelOriginalWholeWrite());
                        {
                            core::CpuMemoryQuotaLease pressure(
                                quota, quota->limitBytes() - quota->liveBytes());
                            assert(pressure.charged());
                            assert(!Source::prepareOriginal(object,
                                       arena,
                                       root,
                                       &e,
                                       1u,
                                       {2u, true, 1u, 1u, 5u, bytes},
                                       quota,
                                       *context->device,
                                       id)
                                && id && object.get() == original);
                        }
                        assert(!Source::prepareOriginal(object,
                                   arena,
                                   root,
                                   &e,
                                   1u,
                                   {2u, true, 1u, 1u, 4u, bytes},
                                   quota,
                                   *context->device,
                                   id)
                            && object.get() == original && id);
                        std::puts(
                            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_SOURCE2_COLD_OK genuine-candidate pending-before-allocation source-exhausted owner-one-less unique-preserved no-publication hot0 query0");
                    } else
                        OriginalWholeDescriptorWriteFixture::commitActual(*context,
                            pipeline,
                            *object,
                            static_cast<std::uint32_t>(slot),
                            whole,
                            wholeQuotas[slot]);
                    sets[slot] = Source::prepareOriginal(object,
                        arena,
                        root,
                        &e,
                        1u,
                        {2u, true, 1u, 1u, 5u, bytes},
                        quota,
                        *context->device,
                        id);
                    if (!sets[slot])
                        std::fprintf(stderr, "two-slot Source2: %s\n", id ? id : "success");
                    assert(sets[slot]);
                    assert(!Sources::appendOriginalTyped(root, {sets[slot]}, *context->device));
                    if (mode < 3u)
                        verifySourceIdentity(*context, sets[slot], wholeQuotas[slot]);
                }
                const std::array<K, 17u> kinds{{K::BeginComputePass,
                    K::BindComputePipeline,
                    K::BindComputeDescriptor,
                    K::BindComputeDescriptor,
                    K::Dispatch,
                    K::BindComputePipeline,
                    K::BindComputeDescriptor,
                    K::BindComputeDescriptor,
                    K::Dispatch,
                    K::EndComputePass,
                    K::BeginComputePass,
                    K::BindComputePipeline,
                    K::BindComputeDescriptor,
                    K::BindComputeDescriptor,
                    K::Dispatch,
                    K::EndComputePass,
                    K::Finish}};
                const auto build = [&](std::size_t omit = SIZE_MAX, bool swappedSlots = false) {
                    core::pmr::Vector<rhi::CommandUsage> commands;
                    core::pmr::Vector<rhi::OriginalCommandUsage::Reference> refs;
                    core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
                    core::pmr::Vector<Inputs::OriginalDescriptor> records;
                    std::size_t scope = 0u;
                    unsigned slot = 0u;
                    for (std::size_t i = 0u; i < kinds.size(); ++i) {
                        auto kind = kinds[i];
                        if (i == omit)
                            kind = K::Dispatch;
                        commands.push_back({kind, static_cast<std::uint32_t>(refs.size()), 0u});
                        if (kind == K::BeginComputePass) {
                            scope = i;
                            slot = 0u;
                        }
                        if (kind == K::BindComputePipeline) {
                            slot = 0u;
                            refs.push_back({count});
                            commands.back().referenceCount = 1u;
                            records.push_back({i,
                                rhi::OriginalCommandPipelineBinding{kind,
                                    count,
                                    scope,
                                    pipeline->originalResourceRegistration().generation()}});
                        }
                        if (kind == K::BindComputeDescriptor) {
                            const auto& set = sets[slot];
                            refs.push_back({count + 1u + slot});
                            commands.back().referenceCount = 1u;
                            records.push_back({i,
                                rhi::OriginalCommandDescriptorBinding{kind,
                                    count + 1u + slot,
                                    count,
                                    scope,
                                    set->originalObject()
                                        ->originalResourceRegistration()
                                        .generation(),
                                    set->frozenContentRevision()}});
                            fields.push_back(
                                {*Scalar::capture(kind,
                                     Scalar::DescriptorBinding{swappedSlots ? 1u - slot : slot}),
                                    {}});
                            ++slot;
                        } else if (kind == K::Dispatch)
                            fields.push_back(
                                {*Scalar::capture(kind, Scalar::Dispatch{1u, 1u, 1u}), {}});
                        else
                            fields.push_back({*Scalar::capture(kind, Scalar::None{}), {}});
                    }
                    auto result = OriginalCommandUploadStreamBuilderFixture::prepareRetained(root,
                        commands.data(),
                        commands.size(),
                        refs.data(),
                        refs.size(),
                        fields.data(),
                        records.data(),
                        records.size(),
                        {3u, true, 3u, 6u, 0u, 0u},
                        graphQuota,
                        id);
                    if (swappedSlots) {
                        assert(!result
                            && core::StringView(id)
                                == "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT");
                        return result;
                    }
                    if (!result)
                        std::fprintf(stderr, "two-slot Inputs: %s\n", id ? id : "success");
                    assert(result);
                    return result;
                };
                if (mode < 3u)
                    verifyNumeric(pipeline, buffers, sets, shared, conflict, wait, map);
                auto inputs = build();
                if (mode < 3u)
                    for (const auto omit : std::array<std::size_t, 3u>{3u, 7u, 13u}) {
                        auto missing = build(omit);
                        const auto result =
                            rhi::common::OriginalCommandDescriptorConsumption::inspect(
                                missing, *context->device, {1u, true, 1u, 1u, true, true, 4u, 8u});
                        assert(result.diagnostic
                            && core::StringView(result.diagnostic)
                                == "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING");
                    }
                if (mode == 7u) {
                    // Inputs3 rejects the incompatible original logical-set
                    // layouts before a State/Stream/transaction can exist.
                    auto swapped = build(SIZE_MAX, true);
                    assert(!swapped);
                }
                auto state = OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(
                    inputs, *context->device, !conflict, mode < 3u);
                if (mode < 3u && !conflict) {
                    // Retain only the existing state/transaction/stream bundle;
                    // Source2 and Inputs3 already own their exact original root.
                    prefix = {};
                    pipelineSource.reset();
                    verifyRetained(*context, pipeline, buffers, sets, wholeQuotas, inputs, state);
                    auto* object = const_cast<rhi::DescriptorSet*>(sets[0]->originalObject());
                    auto writes = wholeWrite(sets[0], buffers[0]);
                    const auto revision = object->contentRevision();
                    assert(!context->device->beginOriginalWholeDescriptorWrite(*object,
                        pipeline,
                        0u,
                        writes.data(),
                        writes.size(),
                        wholeRequest(),
                        wholeQuotas[0]));
                    drainWhole(*object);
                    assert(!object->finishOriginalWholeWrite()
                        && object->contentRevision() == revision + 1u);
                    assert(Source::validateOriginal(sets[0]) && inputs->validate());
                    OriginalCommandDescriptorStateFixture::verifyRetained(state, false);
                    arena->reset();
                    assert(Source::validateOriginal(sets[1]) && inputs->validate());
                    OriginalCommandDescriptorStateFixture::verifyRetained(state, false);
                    auto stateQuota = state.stateQuota, recordingQuota = state.recordingQuota,
                         streamQuota = state.stream.quota;
                    state = {};
                    assert(!stateQuota->liveBytes() && !recordingQuota->liveBytes()
                        && !streamQuota->liveBytes());
                    inputs.reset();
                    sets = {};
                    root.reset();
                    arena.reset();
                    buffers = {};
                    writes = {};
                    assert(!quota->liveBytes() && !graphQuota->liveBytes());
                    for (const auto& group : wholeQuotas)
                        for (const auto& q : group)
                            assert(!q->liveBytes());
                    std::puts(
                        "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_RETAINED_REVISION_EPOCH_OK source2-inputs3-state2-stream2 replacement-revision arena-epoch rejected original-source-graph-state-recording-stream-quotas-reclaimed native-closed");
                }
                if (mode >= 7u) {
                    std::array<std::shared_ptr<core::CpuMemoryQuota>, 18u> pressure{};
                    std::size_t index = 0u;
                    for (const auto& group : wholeQuotas)
                        for (const auto& q : group)
                            pressure[index++] = q;
                    pressure[index++] = quota;
                    pressure[index++] = graphQuota;
                    pressure[index++] = state.stateQuota;
                    pressure[index++] = state.recordingQuota;
                    pressure[index++] = state.stream.quota;
                    for (const auto& q : context->quotas)
                        pressure[index++] = q;
                    assert(index == pressure.size());
                    std::uint64_t resourceBytes =
                        buffers[0]->originalObject()->allocation().estimatedSizeBytes;
                    if (count == 2u)
                        resourceBytes +=
                            buffers[1]->originalObject()->allocation().estimatedSizeBytes;
                    if (mode == 7u || mode == 8u) {
                        if (mode == 7u) {
                            using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
                            const rhi::OriginalCommandNativeClosureBudget nativeBudget{
                                2u, true, count, count + 3u, 128u, resourceBytes, bytes, 1u, 2u};
                            auto other =
                                OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(
                                    inputs, *context->device, true, false, true);
                            auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                            assert(!Closure::prepareOriginalTyped(state.transaction,
                                       other.stream.program,
                                       *context->device,
                                       nativeBudget,
                                       id)
                                && core::StringView(id) == "RHI-ORIGINAL-STREAM-STATE-IDENTITY"
                                && allocations
                                    == core::defaultCpuAllocator().stats().allocationCount);
                            other = {};
                            std::puts(
                                "BINDLESS_RHI_TYPED_NATIVE_CLOSURE_JOIN_REJECT_OK genuine-distinct-state-stream same-source preflight0 swapped-logical-sets-input-layout-reject recording-closed");
                        }
                        state.nativeClosure =
                            OriginalCommandNativeClosureTypedFixture::verify(state,
                                *context->device,
                                {2u, true, count, count + 3u, 128u, resourceBytes, bytes, 1u, 2u},
                                pressure,
                                shared ? "whole-slots-shared" : "whole-slots-distinct");
                        const auto owners = state.nativeClosure->retainedOwnerCount();
                        state.nativeClosure.reset();
                        auto loss = context->device->observeOriginalDeviceNativeLoss(id);
                        assert(loss && !id);
                        auto foreign = create();
                        auto foreignDomain = core::makeShared<core::CpuMemoryQuota>(bytes);
                        auto foreignSubscription = core::makeShared<core::CpuMemoryQuota>(bytes);
                        assert(!foreign->prepareOriginalDeviceLifetime(
                            {1u, true, 1u, 2u, bytes}, foreignDomain));
                        assert(!foreign->prepareOriginalDeviceNativeLoss(
                            {{1u, true, 1u, 1u, bytes}, 1u}, foreignSubscription));
                        auto foreignLoss = foreign->observeOriginalDeviceNativeLoss(id);
                        assert(foreignLoss && !id);
                        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                std::array{
                                    foreignDomain, foreignSubscription, state.recordingQuota},
                                [&]() noexcept -> const char* {
                                    assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::
                                               prepareOriginalTypedLossBound(state.transaction,
                                                   state.stream.program,
                                                   *context->device,
                                                   {2u,
                                                       true,
                                                       count,
                                                       count + 3u,
                                                       128u,
                                                       resourceBytes,
                                                       bytes,
                                                       1u,
                                                       2u},
                                                   *foreignLoss,
                                                   id)
                                        && core::StringView(id)
                                            == "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-FOREIGN");
                                    for (std::size_t i = 0u; i < wholeQuotas[0].size(); ++i) {
                                        auto overlap = wholeQuotas[0];
                                        overlap[i] = context->lossQuota;
                                        auto writes = wholeWrite(sets[0], buffers[0]);
                                        assert(
                                            core::StringView(
                                                context->device->beginOriginalWholeDescriptorWrite(
                                                    *const_cast<rhi::DescriptorSet*>(
                                                        sets[0]->originalObject()),
                                                    pipeline,
                                                    0u,
                                                    writes.data(),
                                                    writes.size(),
                                                    wholeRequest(),
                                                    overlap))
                                            == "RHI-ORIGINAL-REGISTRATION-QUOTA-DOMAIN");
                                    }
                                    return nullptr;
                                }));
                        });
                        foreignLoss.reset();
                        foreign.reset();
                        assert(!foreignDomain->liveBytes() && !foreignSubscription->liveBytes());
                        std::array<std::shared_ptr<core::CpuMemoryQuota>, 19u> boundPressure{};
                        std::copy(pressure.begin(), pressure.end(), boundPressure.begin());
                        boundPressure.back() = context->lossQuota;
                        state.nativeClosure = OriginalCommandNativeClosureTypedFixture::verify(
                            state,
                            *context->device,
                            {2u,
                                true,
                                count,
                                count + 3u,
                                owners + 1u,
                                resourceBytes,
                                bytes,
                                1u,
                                2u},
                            boundPressure,
                            shared ? "whole-slots-shared" : "whole-slots-distinct",
                            &*loss,
                            shared ? "RHI-TYPED-USAGE-SCOPE-INCOMPATIBLE-BUFFER-USAGE" : nullptr);
                        assert(state.nativeClosure->retainedOwnerCount() == owners + 1u);
                        loss.reset();
                        verifyRetained(
                            *context, pipeline, buffers, sets, wholeQuotas, inputs, state, false);
                        auto* object = const_cast<rhi::DescriptorSet*>(sets[0]->originalObject());
                        auto writes = wholeWrite(sets[0], buffers[0]);
                        assert(!context->device->beginOriginalWholeDescriptorWrite(*object,
                            pipeline,
                            0u,
                            writes.data(),
                            writes.size(),
                            wholeRequest(),
                            wholeQuotas[0]));
                        drainWhole(*object);
                        assert(
                            !object->finishOriginalWholeWrite() && state.nativeClosure->validate());
                        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                            OriginalTypedDependencyDecisionFixture::verifyUnavailable(
                                state, "revision");
                        });
                        arena->reset();
                        assert(state.nativeClosure->validate());
                        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                            OriginalTypedDependencyDecisionFixture::verifyUnavailable(
                                state, "arena-epoch");
                        });
                        writes = {};
                        std::puts(
                            "BINDLESS_RHI_TYPED_NATIVE_CLOSURE_RETAINED_OK both-slot-mutations pending-cancel-restores replacement-revision arena-epoch native-quota-reclamation recording-closed");
                    } else {
                        if (mode == 12u)
                            OriginalCommandNativeClosureTypedFixture::verifyLossBoundUnavailable(
                                create,
                                state,
                                *context->device,
                                {2u, true, count, count + 3u, 128u, resourceBytes, bytes, 1u, 2u},
                                layout);
                        state.nativeClosure =
                            rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalTyped(
                                state.transaction,
                                state.stream.program,
                                *context->device,
                                {2u, true, count, count + 3u, 128u, resourceBytes, bytes, 1u, 2u},
                                id);
                        assert(state.nativeClosure && !state.nativeClosure->validate());
                        if (bound) {
                            state.nativeClosure.reset();
                            auto loss = context->device->observeOriginalDeviceNativeLoss(id);
                            assert(loss && !id);
                            state.nativeClosure = rhi_webgpu::WebGpuCanonicalNativeClosure::
                                prepareOriginalTypedLossBound(state.transaction,
                                    state.stream.program,
                                    *context->device,
                                    {2u,
                                        true,
                                        count,
                                        count + 3u,
                                        128u,
                                        resourceBytes,
                                        bytes,
                                        1u,
                                        2u},
                                    *loss,
                                    id);
                            assert(state.nativeClosure && !state.nativeClosure->validate()
                                && !state.nativeClosure->nativeLossDiagnostic());
                        }
                    }
                }
                if (mode >= 3u) {
                    prefix = {};
                    pipelineSource.reset();
                    pipeline.reset();
                    inputs.reset();
                    sets = {};
                    root.reset();
                    arena.reset();
                    buffers = {};
                    // Only the existing retained State2/transaction/Stream2
                    // bundle owns the exact original root after caller drop.
                    OriginalCommandDescriptorStateFixture::verifyRetained(
                        state, mode != 7u && mode != 8u);
                    if (mode == 7u || mode == 8u) {
                        releaseTerminal(state, *context, wholeQuotas);
                    } else {
                        if (mode == 16u)
                            OriginalTypedDependencyDecisionFixture::retainForActualLoss(state,
                                OriginalTypedUsageProjectionFixture::budgetFor(state),
                                "whole-physical-loss");
                        if (mode == 6u || mode == 12u || mode == 16u)
                            assert(!state.transaction->applyOriginalPrototypePlan());
                        beginTerminalReplacement(*context, state, wholeQuotas);
                        if (mode == 3u || mode == 9u || mode == 13u) {
                            const auto source = terminalSource(state);
                            const auto lease = std::get<std::shared_ptr<const Lease>>(
                                source->frozenElements()[0].resourceOwner);
                            const_cast<rhi::GpuBuffer*>(lease->originalObject())->sealWrites();
                        } else if (mode == 4u || mode == 10u || mode == 14u)
                            context->device->retireOriginalDeviceLifetime();
                        else if (mode == 5u || mode == 11u || mode == 15u)
                            context->device.reset();
                        if (mode == 6u) {
                            lossState_ = std::move(state);
                            lossWholeQuotas_ = wholeQuotas;
                            lossPublicationQuotas_ = {artifactQuota, inputQuota};
                        } else if (mode == 12u) {
                            lossTypedState_ = std::move(state);
                            lossTypedWholeQuotas_ = wholeQuotas;
                            lossTypedPublicationQuotas_ = {artifactQuota, inputQuota};
                        } else if (mode == 16u) {
                            lossBoundState_ = std::move(state);
                            lossBoundWholeQuotas_ = wholeQuotas;
                        } else {
                            if (bound)
                                assert(state.nativeClosure->validate());
                            verifyTerminalRejected(state,
                                wholeQuotas,
                                (mode == 3u || mode == 9u || mode == 13u)
                                    ? "RHI-BUFFER-GPU-WRITES-SEALED"
                                    : nullptr,
                                false,
                                mode < 7u);
                            releaseTerminal(state, *context, wholeQuotas);
                            if (mode < 7u)
                                std::printf(
                                    "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_TERMINAL_OK %s genuine-source2-inputs3-state2-stream2 caller-drop pending-and-committed-rejected no-revision hot0 query0 all-quotas-reclaimed native-closed\n",
                                    mode == 3u       ? "gpu-write-seal"
                                        : mode == 4u ? "original-retirement"
                                                     : "device-destruction");
                            else
                                std::printf(
                                    "%s %s caller-drop pending-and-committed-reject quotas-reclaimed recording-closed\n",
                                    bound ? "BINDLESS_RHI_TYPED_LOSS_BOUND_CLOSURE_TERMINAL_OK"
                                          : "BINDLESS_RHI_TYPED_NATIVE_CLOSURE_TERMINAL_OK",
                                    (mode == 9u || mode == 13u)        ? "gpu-write-seal"
                                        : (mode == 10u || mode == 14u) ? "original-retirement"
                                                                       : "device-destruction");
                        }
                    }
                }
                if (mode < 3u && !conflict)
                    std::printf(
                        "BINDLESS_RHI_DESCRIPTOR_STATE_SLOTS_OK logical-sets2 scalar-bindings2 shared%u original-accepted-actor missing-rebind-pass-reset hot0 query0 native-closed\n",
                        shared ? 1u : 0u);
            }
            pipeline.reset();
            if (mode == 6u) {
                assert(quota->liveBytes() && graphQuota->liveBytes());
                lossContext_ = std::move(context);
            } else if (mode == 12u) {
                assert(quota->liveBytes() && graphQuota->liveBytes());
                lossTypedContext_ = std::move(context);
            } else if (mode == 16u) {
                assert(quota->liveBytes() && graphQuota->liveBytes());
                lossBoundContext_ = std::move(context);
            } else {
                assert(!quota->liveBytes() && !graphQuota->liveBytes());
                context->retire();
            }
        }
        artifact.reset();
        input.reset();
        assert(lossContext_ && artifactQuota->liveBytes() && inputQuota->liveBytes());
#else
        (void)create;
        (void)wait;
        (void)map;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        assert(lossContext_ && lossState_.graph && rhi_webgpu::webGpuNativeDeviceLost());
        assert(lossBoundContext_ && lossBoundState_.nativeClosure
            && lossBoundState_.nativeClosure->originalNativeLossObservation());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return lossBoundState_.nativeClosure->nativeLossDiagnostic() != nullptr;
        }));
        assert(core::StringView(lossBoundState_.nativeClosure->nativeLossDiagnostic())
                == "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"
            && core::StringView(lossBoundState_.nativeClosure->validate())
                == "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST");
        verifyTerminalRejected(lossBoundState_,
            lossBoundWholeQuotas_,
            "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST",
            true,
            false);
        OriginalTypedUsageProjectionFixture::verifyUnavailable(
            lossBoundState_, *lossBoundContext_->device, "whole-physical-loss");
        assert(lossBoundState_.transaction->commitOriginalPendingOwnership());
        assert(!lossBoundState_.transaction->abandonOriginalPendingOwnership()
            && lossBoundState_.transaction->journalEmpty());
        const auto subscription = lossBoundContext_->lossQuota;
        releaseTerminal(lossBoundState_, *lossBoundContext_, lossBoundWholeQuotas_);
        lossBoundContext_.reset();
        lossBoundWholeQuotas_ = {};
        assert(!subscription->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_LOSS_BOUND_CLOSURE_PHYSICAL_LOSS_OK registry7 exact-original-subscription native-lost pending-and-committed-groups plan-reject CPU-cancel journal-empty full-quotas-reclaimed hot0 query0 recording-closed");
        // This original context uses the accepted creation receipt's physical
        // loss gate; it does not opt into a logical NativeLossReceiver binding.
        assert(lossContext_->receipt->status()
                == rhi_webgpu::WebGpuOriginalArtifactComputeCreation::Status::DeviceLost
            && core::StringView(lossContext_->receipt->validate())
                == "RHI-ARTIFACT-CREATION-DEVICE-LOST");
        verifyTerminalRejected(
            lossState_, lossWholeQuotas_, "RHI-ARTIFACT-CREATION-DEVICE-LOST", true);
        assert(lossState_.transaction->commitOriginalPendingOwnership());
        assert(!lossState_.transaction->abandonOriginalPendingOwnership()
            && lossState_.transaction->journalEmpty() && lossState_.transaction->validate());
        releaseTerminal(lossState_, *lossContext_, lossWholeQuotas_);
        lossContext_.reset();
        // Both independent loss contexts retain the same publication owners.
        for (const auto& quota : lossPublicationQuotas_)
            assert(quota->liveBytes());
        lossWholeQuotas_ = {};
        lossPublicationQuotas_ = {};
        assert(lossTypedContext_ && lossTypedState_.nativeClosure
            && lossTypedState_.nativeClosure->validate());
        assert(lossTypedContext_->receipt->status()
            == rhi_webgpu::WebGpuOriginalArtifactComputeCreation::Status::DeviceLost);
        verifyTerminalRejected(lossTypedState_,
            lossTypedWholeQuotas_,
            "RHI-ARTIFACT-CREATION-DEVICE-LOST",
            true,
            false);
        assert(lossTypedState_.transaction->commitOriginalPendingOwnership());
        assert(!lossTypedState_.transaction->abandonOriginalPendingOwnership()
            && lossTypedState_.transaction->journalEmpty());
        releaseTerminal(lossTypedState_, *lossTypedContext_, lossTypedWholeQuotas_);
        lossTypedContext_.reset();
        for (const auto& q : lossTypedPublicationQuotas_)
            assert(!q->liveBytes());
        lossTypedWholeQuotas_ = {};
        lossTypedPublicationQuotas_ = {};
        std::puts(
            "BINDLESS_RHI_TYPED_NATIVE_CLOSURE_LOSS_OK independent-actual-physical-loss pending-plan-reject CPU-cancel journal-empty original-native-quota-reclamation recording-closed");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_WHOLE_WRITE_LOSS_OK actual-physical-loss genuine-source2-inputs3-state2-stream2 caller-drop pending-replacement-and-plan rejected CPU-plan-cancel journal-empty all-original-source-publication-quotas-reclaimed native-closed");
#endif
    }
};
} // namespace arp::test
