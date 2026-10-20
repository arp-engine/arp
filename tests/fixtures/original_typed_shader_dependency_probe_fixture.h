#pragma once
#include "original_accepted_compute_pipeline_factory_fixture.h"
#include "original_command_descriptor_state_fixture.h"
#include "original_target_memory_fixture.h"
#include "original_whole_descriptor_write_fixture.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "original_finite_descriptor_write_fixture.h"
#endif
#include "original_typed_cold_admission_fixture.h"
#include "original_typed_cold_command_fixture.h"
#include "original_typed_dependency_decision_fixture.h"
#include "rhi/frontend/command_recording.h"

namespace arp::test {
// Native experiment over genuine accepted actors. This is not a synchronization
// lowering/executor: the six original dependencies remain unapproved. Readback
// copies and seed writes belong solely to this independent verification probe.
class OriginalTypedShaderDependencyProbeFixture final {
    friend class OriginalTypedCallbackCustodyFixture;

public:
    template <class Factory, class Wait, class Map>
    static void verify(Factory&& create, Wait&& wait, Map&& map)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        using Sources = rhi::OriginalCommandResourceSources;
        using Source = rhi::OriginalCommandDescriptorSetSource;
        using Lease = rhi::OriginalCommandBufferLease;
        using PipelineSource = rhi::OriginalCommandComputePipelineSource;
        using Inputs = rhi::OriginalCommandInputs;
        using K = rhi::CommandUsageKind;
        using Scalar = rhi::CommandScalarArguments;
        using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
        using Projection = rhi::common::OriginalTypedUsageProjection;
        using Scope = rhi::semantics::TypedShaderUsageScopeContract;
        constexpr std::array<std::uint32_t, 4u> seed{29u, 0u, 0u, 0u};
        constexpr std::uint32_t increment = 7u; // Unchanged published Slang actor.
        const char* path = "/descriptor-state-slots/descriptor_state_slots.computeMain.entry.yaml";
        auto artifactBudget = OriginalTargetMemoryFixture::entryBudget();
        auto artifactQuota =
            core::makeShared<core::CpuMemoryQuota>(artifactBudget.maxCpuMetadataBytes);
        auto inputQuota = core::makeShared<core::CpuMemoryQuota>(fixtureMetadataBytes);
        core::pmr::String diagnostic;
        auto artifact = shader::SingleEntryShaderArtifact::load(
            path, artifactBudget, artifactQuota, diagnostic);
        assert(artifact);
        TargetMemoryRequestFixture target;
        target.prepare(artifact);
        artifactBudget.targetMemory = &target.request();
        artifact = shader::SingleEntryShaderArtifact::load(
            path, artifactBudget, artifactQuota, diagnostic);
        assert(artifact);
        auto input = rhi::OriginalSingleEntryPipelineInput::prepare(
            artifact, {1u, fixtureMetadataBytes}, inputQuota, diagnostic);
        assert(input);
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        assert(layout.descriptorLayouts.size() == slotCount);
        auto context = OriginalAcceptedComputePipelineFactoryFixture::prepareContext(
            create, input, layout, passCount + 1u, groupCount, 7u);
        context->ready();
        const char* id = nullptr;
        auto pipeline =
            rhi::OriginalRegisteredPipeline::shareAcceptedOriginal(context->publish(), id);
        assert(pipeline);
        auto sourceQuota = core::makeShared<core::CpuMemoryQuota>(fixtureMetadataBytes);
        auto graphQuota = core::makeShared<core::CpuMemoryQuota>(fixtureMetadataBytes);
        std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>, groupCount> writeQuotas;
        {
            std::array<std::shared_ptr<const Lease>, passCount + 1u> buffers;
            for (std::size_t i = 0u; i < buffers.size(); ++i) {
                const bool readback = i == passCount;
                rhi::BufferDesc desc;
                desc.sizeBytes = readback ? passCount * sizeof(std::uint32_t) : sizeof(seed);
                desc.usage = readback ? rhi::ResourceUsage::CopyDst
                                      : rhi::ResourceUsage::StorageBuffer
                        | rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
                desc.memory = readback ? rhi::MemoryUsage::GpuToCpu : rhi::MemoryUsage::GpuOnly;
                desc.initialState = rhi::ResourceState::Undefined;
                core::UniquePtr<rhi::GpuBuffer> object;
                {
                    core::CpuMemoryQuotaScope scope(context->quotas[2]);
                    object = context->device->createBuffer(desc);
                }
                assert(object);
                buffers[i] = Lease::prepareOriginal(
                    object, {1u, true, 2u, fixtureMetadataBytes}, sourceQuota, id);
                assert(buffers[i]);
            }
            auto actor = prepareActor(
                *context, pipeline, buffers, layout, sourceQuota, graphQuota, writeQuotas, false);
            const auto& admission = actor.admission;
            auto& root = actor.root;
            auto& arena = actor.arena;
            auto& sets = actor.sets;
            auto& inputs = actor.inputs;
            auto state = OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(inputs,
                *context->device,
                true,
                false,
                false,
                {1u, true, 1u, 1u, true, true, passCount, groupCount});
            const auto& stream = state.stream.program;
            assert(stream->counts().commands == admission.maxCommands
                && stream->counts().consumptions == passCount
                && stream->counts().implicitAccesses == groupCount
                && stream->counts().descriptorBinds == groupCount);
            auto loss = context->device->observeOriginalDeviceNativeLoss(id);
            assert(loss && !id);
            std::uint64_t resourceBytes = 0u;
            for (std::size_t i = 0u; i < passCount; ++i)
                resourceBytes += buffers[i]->originalObject()->allocation().estimatedSizeBytes;
            const rhi::OriginalCommandNativeClosureBudget closureBudget{2u,
                true,
                passCount,
                admission.maxNativePayloadObjects,
                fixtureOwnerCeiling,
                resourceBytes,
                fixtureMetadataBytes,
                1u,
                groupCount};
            state.nativeClosure = Closure::prepareOriginalTypedLossBound(
                state.transaction, stream, *context->device, closureBudget, *loss, id);
            if (!state.nativeClosure)
                std::fprintf(stderr, "dependency probe closure: %s\n", id ? id : "unknown");
            assert(state.nativeClosure);
            const Projection::Budget projectionBudget{1u,
                true,
                {1u, true, 1u, 1u, true, true, passCount, groupCount},
                {},
                admission.maxCommands,
                0u,
                groupCount};
            std::array<Projection::BufferPoint, groupCount> points;
            std::size_t pointCount = 0u;
            const auto projection = Projection::visit(stream,
                *context->device,
                projectionBudget,
                [&](const Projection::Point& point) noexcept -> const char* {
                    const auto* buffer = std::get_if<Projection::BufferPoint>(&point);
                    assert(buffer && pointCount < points.size());
                    points[pointCount++] = *buffer;
                    return nullptr;
                });
            assert(!projection.diagnostic && pointCount == groupCount);
            std::array<Scope::BufferUse, groupCount> uses;
            for (std::size_t i = 0u; i < uses.size(); ++i)
                uses[i] = points[i].use;
            const Scope::Budget usageBudget{
                1u, true, admission.maxCommands, groupCount, groupCount * (groupCount - 1u) / 2u};
            assert(!Scope::inspect(stream->operations(), uses, usageBudget).diagnostic);
            const Projection::StateBoundRequest boundProjection{1u, true, projectionBudget};
            assert(Projection::inspectStateBound(stream, state.graph, boundProjection)
                    .counts.matches(projection.counts));
            auto otherState =
                OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(inputs,
                    *context->device,
                    true,
                    false,
                    true,
                    {1u, true, 1u, 1u, true, true, passCount, groupCount});
            std::size_t mismatchedEmitted = 0u;
            const auto mismatched = Projection::visitStateBound(stream,
                otherState.graph,
                boundProjection,
                [&](const auto&) noexcept -> const char* {
                    ++mismatchedEmitted;
                    return nullptr;
                });
            assert(mismatched.diagnostic
                && core::StringView(mismatched.diagnostic) == "RHI-ORIGINAL-STREAM-STATE-IDENTITY"
                && !mismatchedEmitted && !mismatched.counts.contributions);
            otherState = {};
            std::puts(
                "BINDLESS_RHI_TYPED_STATE_BOUND_USAGE_ROOT_REJECT_OK genuine-different-state same-source stream-state-identity emitted0 counts-cleared");
            assert(points[0].original.bufferSourceIndex == 0u
                && points[1].original.bufferSourceIndex == 1u
                && points[2].original.bufferSourceIndex == 1u
                && points[3].original.bufferSourceIndex == 0u);
            assert(points[0].original.commandIndex == 6u && points[2].original.commandIndex == 14u);
            // Check the real original six dependency positions separately. Their
            // valid state/access values still return DEPENDENCY-NOT-FROZEN.
            std::size_t dependencies = 0u;
            for (const auto& op : stream->operations())
                if (op.kind == K::BufferBarrier || op.kind == K::BufferMemoryBarrier) {
                    const auto& dependency = std::get<rhi::ResourceDependencyArguments>(
                        *stream->originalInputs()->descriptorAt(op.commandIndex));
                    const std::array<std::size_t, dependencyCount> positions{
                        0u, 1u, 8u, 9u, 16u, 17u};
                    assert(dependencies < positions.size()
                        && op.commandIndex == positions[dependencies]);
                    assert(dependency.firstResource()
                        == buffers[dependencies % passCount]->resource());
                    const char* refusal = nullptr;
                    if (op.kind == K::BufferMemoryBarrier) {
                        rhi::ResourceMemoryBarrierDesc memory;
                        assert(dependency.write(op.kind, memory));
                        assert(memory.sourceStages == rhi::PipelineStage::ComputeShader
                            && memory.destinationStages == rhi::PipelineStage::ComputeShader);
                        refusal =
                            Scope::rejectUnfrozenMemory(memory, rhi::ResourceState::ShaderWrite);
                    } else {
                        rhi::ResourceBarrierDesc barrier;
                        assert(dependency.write(op.kind, barrier));
                        assert(barrier.before
                            == (dependencies < passCount ? rhi::ResourceState::Undefined
                                                         : rhi::ResourceState::ShaderWrite));
                        assert(barrier.after
                            == (dependencies < passCount ? rhi::ResourceState::ShaderWrite
                                                         : rhi::ResourceState::ShaderRead));
                        refusal = Scope::rejectUnfrozenTransition(barrier, barrier.before);
                    }
                    assert(refusal
                        && core::StringView(refusal)
                            == "RHI-TYPED-USAGE-SCOPE-DEPENDENCY-NOT-FROZEN");
                    ++dependencies;
                }
            assert(dependencies == dependencyCount);
            // Complete projection preflight emits zero with one-less ceilings.
            auto less = projectionBudget;
            --less.maxContributions;
            std::size_t emitted = 0u;
            assert(Projection::visit(stream,
                       *context->device,
                       less,
                       [&](const auto&) noexcept -> const char* {
                           ++emitted;
                           return nullptr;
                       })
                       .diagnostic
                && !emitted);
            std::array<std::shared_ptr<core::CpuMemoryQuota>, groupCount * 3u + 7u> pressure;
            std::size_t q = 0u;
            for (const auto& set : writeQuotas)
                for (const auto& quota : set)
                    pressure[q++] = quota;
            for (const auto& quota : std::array{sourceQuota,
                     graphQuota,
                     state.stateQuota,
                     state.recordingQuota,
                     state.stream.quota,
                     context->lossQuota,
                     context->quotas[1]})
                pressure[q++] = quota;
            assert(q == pressure.size());
            state.dependencyDecision = OriginalTypedDependencyDecisionFixture::verify(state,
                *context->device,
                state.nativeClosure,
                pressure,
                "dependent-compute",
                projectionBudget);
            const auto& frozen = state.dependencyDecision->records();
            assert(frozen.size() == 6u);
            assert(frozen[2].original.commandIndex == 8u && frozen[3].original.commandIndex == 9u
                && frozen[2].original.clause
                    == rhi::common::OriginalTypedDependencyProjection::Clause::BetweenScopes
                && frozen[2].original.preceding->scope.first == 6u
                && frozen[2].original.following->scope.first == 14u
                && state.dependencyDecision->counts().initial == 2u
                && state.dependencyDecision->counts().between == 2u
                && state.dependencyDecision->counts().final == 2u
                && state.dependencyDecision->counts().endpointContributions == 8u);
            std::puts(
                "BINDLESS_RHI_TYPED_DEPENDENCY_DECISION_COMPUTE_POSITIONS_OK original19 dependency8-9 dispatch6-14 clauses2-2-2 endpoints8 full-coverage candidate-only");

            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    pressure, [&]() noexcept -> const char* {
                        assert(!state.nativeClosure->validate()
                            && !Projection::inspect(stream, *context->device, projectionBudget)
                                .diagnostic);
                        const auto bound =
                            Projection::inspectStateBound(stream, state.graph, boundProjection);
                        assert(!bound.diagnostic && bound.counts.matches(projection.counts));
                        assert(!Scope::inspect(stream->operations(), uses, usageBudget).diagnostic);
                        return nullptr;
                    }));
            });
            // Native command experiment consumes exact original accepted sources
            // and dispatch scalar values. It issues no typed recording receipt.
            const auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
            assert(native && native->issuedContract());
            const auto device = static_cast<WGPUDevice>(state.nativeClosure->nativeDevice());
            assert(device && native->issuedContract()->nativeDevice() == device);
            std::array<std::shared_ptr<rhi_webgpu::WebGpuNativeBufferPayload>, passCount + 1u>
                payloads;
            for (std::size_t i = 0u; i < payloads.size(); ++i) {
                payloads[i] = rhi_webgpu::webGpuNativePayloadFor(*buffers[i]->originalObject());
                assert(payloads[i] && payloads[i]->ownerDevice == device);
            }
            auto queue = wgpuDeviceGetQueue(device);
            assert(queue);
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            wgpuQueueWriteBuffer(queue, payloads[0]->buffer, 0u, seed.data(), sizeof(seed));
            const std::array<std::uint32_t, 4u> zero{};
            wgpuQueueWriteBuffer(queue, payloads[1]->buffer, 0u, zero.data(), sizeof(zero));
            auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
            assert(encoder);
            for (std::size_t p = 0u; p < passCount; ++p) {
                assert(!state.nativeClosure->validate());
                auto desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                auto nativePass = wgpuCommandEncoderBeginComputePass(encoder, &desc);
                assert(nativePass);
                wgpuComputePassEncoderSetPipeline(nativePass, native->pipeline);
                for (std::size_t s = 0u; s < slotCount; ++s) {
                    const auto& point = points[p * slotCount + s].original;
                    assert(point.set == s
                        && point.descriptorSourceIndex == passCount + 1u + p * slotCount + s);
                    const auto payload = rhi_webgpu::webGpuNativePayloadFor(
                        *sets[p * slotCount + s]->originalObject());
                    const auto facts = OriginalWholeDescriptorWriteFixture::originalTarget(
                        pipeline, static_cast<std::uint32_t>(s), point.binding);
                    assert(payload && payload->containsOriginalWholeWrite()
                        && !payload->dynamicOffsetCount);
                    wgpuComputePassEncoderSetBindGroup(
                        nativePass, facts.group, payload->group, 0u, nullptr);
                }
                const auto command = points[p * slotCount].original.commandIndex;
                const auto* dispatch = stream->originalInputs()
                                           ->originalScalars()
                                           ->originalArguments()
                                           ->records()[command]
                                           .scalar.template get<Scalar::Dispatch>();
                assert(dispatch);
                wgpuComputePassEncoderDispatchWorkgroups(
                    nativePass, dispatch->x, dispatch->y, dispatch->z);
                wgpuComputePassEncoderEnd(nativePass);
                wgpuComputePassEncoderRelease(nativePass);
            }
            for (std::size_t i = 0u; i < passCount; ++i)
                wgpuCommandEncoderCopyBufferToBuffer(encoder,
                    payloads[i]->buffer,
                    0u,
                    payloads[passCount]->buffer,
                    i * sizeof(std::uint32_t),
                    sizeof(std::uint32_t));
            auto commandDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            auto command = wgpuCommandEncoderFinish(encoder, &commandDesc);
            assert(command);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                0u));
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            wgpuQueueSubmit(queue, 1u, &command);
            assert(wait(queue) && map(payloads[passCount]->buffer, payloads[passCount]->sizeBytes));
            const auto* values = static_cast<const std::uint32_t*>(wgpuBufferGetConstMappedRange(
                payloads[passCount]->buffer, 0u, payloads[passCount]->sizeBytes));
            assert(values);
            assert(
                values[1] == seed[0] + increment && values[0] == seed[0] + passCount * increment);
            std::printf(
                "BINDLESS_RHI_TYPED_SHADER_DEPENDENCY_PROBE_VALUES seed%u pass1-B%u pass2-A%u expected-B%u expected-A%u original-stream2-commands19 dependencies6 source2-groups4 bound-closure2 distinct-scopes real-completion-map probe-only sync-unfrozen native-closed\n",
                seed[0],
                values[1],
                values[0],
                seed[0] + increment,
                seed[0] + static_cast<std::uint32_t>(passCount) * increment);
            wgpuBufferUnmap(payloads[passCount]->buffer);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device),
                rhi_webgpu::WebGpuNativeErrorScopeBatch::count,
                0u));
            wgpuCommandBufferRelease(command);
            wgpuCommandEncoderRelease(encoder);
            wgpuQueueRelease(queue);
            assert(!state.nativeClosure->validate() && !state.transaction->nativeExecutionProven()
                && state.transaction->journalEmpty());
            state = {};
            actor = {};
            // Separately authored genuine Source and descriptor actors; immutable
            // metadata-only Source ceilings above are never changed or enlarged.
            auto executor = prepareActor(
                *context, pipeline, buffers, layout, sourceQuota, graphQuota, writeQuotas, true);
            OriginalTypedColdAdmissionFixture::verifyCompute(
                executor.inputs, *context->device, context->lossQuota);
            OriginalTypedColdCommandFixture::verifyCompute(
                executor.inputs, *context->device, context->lossQuota);
            executor = {};
        }
        pipeline.reset();
        context->retire();
        assert(!sourceQuota->liveBytes() && !graphQuota->liveBytes());
        for (const auto& set : writeQuotas)
            for (const auto& quota : set)
                assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_SHADER_DEPENDENCY_PROBE_RELEASE_OK original-owners-reclaimed groups4 quotas-independent metadata-hot0-query0 cpu-plan-never-completion no-new-proof-issuer");
#endif
    }

private:
    using Sources = rhi::OriginalCommandResourceSources;
    using Source = rhi::OriginalCommandDescriptorSetSource;
    using PipelineSource = rhi::OriginalCommandComputePipelineSource;
    using Inputs = rhi::OriginalCommandInputs;
    using Lease = rhi::OriginalCommandBufferLease;
    using K = rhi::CommandUsageKind;
    using Scalar = rhi::CommandScalarArguments;
    static constexpr std::size_t passCount = 2u, slotCount = 2u, groupCount = passCount * slotCount;
    static constexpr std::uint64_t fixtureMetadataBytes = 65536u;
    static constexpr std::uint32_t fixtureOwnerCeiling = 128u, dependencyCount = 3u * passCount;
    static constexpr std::array<K, 13u> kinds{{K::BeginComputePass,
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

    struct Actor {
        rhi::OriginalCommandAdmissionBudget admission;
        std::shared_ptr<Sources> root;
        std::shared_ptr<rhi::DescriptorArena> arena;
        std::array<std::shared_ptr<const Source>, groupCount> sets;
        std::shared_ptr<const Inputs> inputs;
        const char* diagnostic = nullptr;
    };

    template <bool finite = false,
        bool targetMemory = false,
        class Context,
        class Pipeline,
        class Layout>
    static Actor prepareActor(Context& contextObject,
        const std::shared_ptr<Pipeline>& pipeline,
        const std::array<std::shared_ptr<const Lease>, passCount + 1u>& buffers,
        const Layout& layout,
        const std::shared_ptr<core::CpuMemoryQuota>& sourceQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        std::array<std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>, groupCount>& writeQuotas,
        bool executor,
        std::uint32_t dispatchX = 1u)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        auto* context = &contextObject;
        const char* id = nullptr;
        auto pipelineSource = PipelineSource::prepareOriginal(pipeline,
            {1u,
                true,
                PipelineSource::kind,
                pipeline->originalResourceRegistration().generation(),
                fixtureMetadataBytes,
                PipelineSource::exclusiveOwnerCount},
            sourceQuota,
            *context->device,
            id);
        assert(pipelineSource);
        const std::array<Sources::Source, passCount> prefix{buffers[0], buffers[1]};
        rhi::OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] =
            passCount;
        admission.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Pipeline)] =
            1u;
        admission
            .maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Descriptor)] =
            groupCount;
        admission.maxCommands = kinds.size() + dependencyCount;
        admission.maxReferences = passCount + groupCount + dependencyCount;
        admission.maxInputDescriptors = admission.maxReferences;
        admission.maxGraphOwners = fixtureOwnerCeiling;
        admission.maxStateObservations = passCount;
        // The independent admission actor deliberately keeps two genuine
        // plans alive for the wrong-root reservation negative.
        admission.maxStateTransactions = executor ? 2u : 1u;
        admission.maxCpuBytes.fill(fixtureMetadataBytes);
        admission.maxNativePayloadObjects = passCount + 1u + groupCount + (executor ? 2u : 0u);
        // Explicit hypothetical shader call ceiling (encoder + base commands).
        // This does not authorize any zero-call original dependency lowering.
        admission.maxTypedNativeCommandCalls = 1u + kinds.size();
        if (executor) {
            const auto demand =
                rhi_webgpu::WebGpuOriginalTypedColdAdmission::lifecycleRequirements().capacity;
            admission.maxPendingSubmissions = demand.pendingSubmissions;
            admission.maxCallbackTickets = demand.callbackTickets;
            admission.maxCallbackRegistrations = demand.callbackRegistrations;
        }
        auto root = Sources::prepareColdOriginalTyped(prefix.data(),
            prefix.size(),
            admission,
            {targetMemory    ? 4u
                    : finite ? 3u
                             : 2u,
                true,
                1u + groupCount},
            sourceQuota,
            id);
        assert(root);
        assert(!Sources::appendOriginalTyped(root, {pipelineSource}, *context->device));
        std::shared_ptr<rhi::DescriptorArena> arena;
        {
            core::CpuMemoryQuotaScope scope(context->quotas[2]);
            rhi::DescriptorArenaDesc desc;
            desc.maxSets = groupCount;
            desc.maxDescriptors = groupCount;
            arena = core::shareUnique(context->device->createOriginalDescriptorArena(desc));
        }
        assert(arena);
        std::array<std::shared_ptr<const Source>, groupCount> sets;
        for (std::size_t i = 0u; i < sets.size(); ++i) {
            const auto pass = i / slotCount, slot = i % slotCount, buffer = pass ? 1u - slot : slot;
            assert(layout.descriptorLayouts[slot].bindings.size() == 1u);
            core::UniquePtr<rhi::DescriptorSet> object;
            {
                core::CpuMemoryQuotaScope scope(context->quotas[2]);
                rhi::DescriptorSetDesc desc;
                desc.layout = rhi::DescriptorStateCopy::copy(layout.descriptorLayouts[slot]);
                object = context->device->allocateOriginalDescriptorSet(arena, desc);
            }
            assert(object);
            Source::Element e;
            e.sourceIndex = buffer;
            e.registrationGeneration =
                buffers[buffer]->originalObject()->originalResourceRegistration().generation();
            e.write.binding = 0u;
            e.write.type = layout.descriptorLayouts[slot].bindings[0].type;
            e.write.resourceKind = rhi::DescriptorResourceKind::Buffer;
            e.write.resource = buffers[buffer]->resource();
            e.write.resourceState = rhi::ResourceState::ShaderWrite;
            e.stages = rhi::PipelineStage::ComputeShader;
            e.access = slot ? rhi::ResourceAccess::ShaderRead | rhi::ResourceAccess::ShaderWrite
                            : rhi::ResourceAccess::ShaderRead;
            e.bufferAccessBytes = buffers[buffer]->layout().sizeBytes;
            rhi::OriginalCommandDescriptorSourceBudget sourceBudget{2u,
                true,
                1u,
                1u,
                Source::canonicalExclusiveRetainedOwnerCount,
                fixtureMetadataBytes};
            if constexpr (finite) {
                const auto target = OriginalWholeDescriptorWriteFixture::originalTarget(
                    pipeline, static_cast<std::uint32_t>(slot), e.write.binding);
                assert(target.minimumBindingBytes
                    && target.minimumBindingBytes < buffers[buffer]->layout().sizeBytes);
                e.write.bufferBindingRange = {2u, 0u, target.minimumBindingBytes};
                rhi::DescriptorBufferBindingRangeContract::Resolved range;
                assert(!rhi::DescriptorBufferBindingRangeContract::resolve(
                    e.write.bufferBindingRange, buffers[buffer]->layout().sizeBytes, 0u, range));
                e.bufferAccessOffset = range.offsetBytes;
                e.bufferAccessBytes = range.sizeBytes;
                const std::array<rhi::OriginalFiniteDescriptorBufferWrite, 1u> writes{
                    {{e.write, buffers[buffer], 0u}}};
                if (const auto* refusal =
                        OriginalFiniteDescriptorWriteFixture::commitActual<targetMemory>(*context,
                            pipeline,
                            *object,
                            static_cast<std::uint32_t>(slot),
                            writes,
                            writeQuotas[i]))
                    return Actor{admission, root, arena, sets, {}, refusal};
                sourceBudget.contractVersion = targetMemory ? 4u : 3u;
                sourceBudget.maxExclusiveRetainedOwners = Source::finiteExclusiveRetainedOwnerCount;
                sourceBudget.finitePipelineSourceIndex = passCount;
                sourceBudget.finiteLogicalSet = static_cast<std::uint32_t>(slot);
                sourceBudget.finiteFootprintContractVersion = 1u;
                sourceBudget.finiteFullBoundViewOptIn = true;
            } else {
                const std::array<rhi::OriginalWholeDescriptorBufferWrite, 1u> writes{
                    {{e.write, buffers[buffer], 0u}}};
                OriginalWholeDescriptorWriteFixture::commitActual(*context,
                    pipeline,
                    *object,
                    static_cast<std::uint32_t>(slot),
                    writes,
                    writeQuotas[i]);
            }
            if constexpr (targetMemory) {
                auto wrong = sourceBudget;
                wrong.contractVersion = 3u;
                assert(!Source::prepareOriginal(
                           object, arena, root, &e, 1u, wrong, sourceQuota, *context->device, id)
                    && object);
                assert(core::StringView(id) == "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
            }
            sets[i] = Source::prepareOriginal(
                object, arena, root, &e, 1u, sourceBudget, sourceQuota, *context->device, id);
            assert(sets[i]);
            assert(!Sources::appendOriginalTyped(root, {sets[i]}, *context->device));
        }
        core::pmr::Vector<rhi::CommandUsage> commands;
        core::pmr::Vector<rhi::OriginalCommandUsage::Reference> refs;
        core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
        core::pmr::Vector<Inputs::OriginalDescriptor> records;
        commands.reserve(kinds.size());
        refs.reserve(passCount + groupCount);
        fields.reserve(kinds.size());
        records.reserve(passCount + groupCount);
        std::size_t scope = 0u, pass = 0u, slot = 0u;
        for (std::size_t i = 0u; i < kinds.size(); ++i) {
            const auto kind = kinds[i];
            commands.push_back({kind, static_cast<std::uint32_t>(refs.size()), 0u});
            if (kind == K::BeginComputePass) {
                scope = i;
                slot = 0u;
            }
            if (kind == K::BindComputePipeline) {
                refs.push_back({passCount});
                commands.back().referenceCount = 1u;
                records.push_back({i,
                    rhi::OriginalCommandPipelineBinding{kind,
                        passCount,
                        scope,
                        pipeline->originalResourceRegistration().generation()}});
            }
            if (kind == K::BindComputeDescriptor) {
                const auto index = pass * slotCount + slot, source = passCount + 1u + index;
                refs.push_back({static_cast<std::uint32_t>(source)});
                commands.back().referenceCount = 1u;
                records.push_back({i,
                    rhi::OriginalCommandDescriptorBinding{kind,
                        source,
                        passCount,
                        scope,
                        sets[index]->originalObject()->originalResourceRegistration().generation(),
                        sets[index]->frozenContentRevision()}});
                fields.push_back(
                    {*Scalar::capture(
                         kind, Scalar::DescriptorBinding{static_cast<std::uint32_t>(slot++)}),
                        {}});
            } else if (kind == K::Dispatch)
                fields.push_back({*Scalar::capture(kind, Scalar::Dispatch{dispatchX, 1u, 1u}), {}});
            else
                fields.push_back({*Scalar::capture(kind, Scalar::None{}), {}});
            if (kind == K::EndComputePass)
                ++pass;
        }
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(root,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            records.data(),
            records.size(),
            {3u, true, passCount, groupCount, 0u, 0u},
            graphQuota,
            id);
        assert(inputs);
        return Actor{admission, root, arena, sets, inputs};

#else
        (void)contextObject;
        (void)pipeline;
        (void)buffers;
        (void)layout;
        (void)sourceQuota;
        (void)graphQuota;
        (void)writeQuotas;
        (void)executor;
        return {};
#endif
    }
};
} // namespace arp::test
