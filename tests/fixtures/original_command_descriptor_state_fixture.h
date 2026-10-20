#pragma once

#include "cpu_quota_factory_fixture.h"
#include "original_command_typed_stream_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h"
#include "rhi/backends/webgpu/webgpu_original_typed_native_payload.h"
#include "rhi/frontend/command_state.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalCommandDescriptorStateFixture final {
    using Graph = rhi::OriginalCommandResourceStates;
    using Root = rhi::OriginalCommandStateTransaction;
    using Sequence = rhi::common::OriginalCommandStateSequence;
    using Access = rhi::common::OriginalCommandAccessValidation;
    using Inputs = rhi::OriginalCommandInputs;
    using Sources = rhi::OriginalCommandResourceSources;
    using K = rhi::CommandUsageKind;
    using S = rhi::ResourceState;
    using Stages = rhi::PipelineStage;
    using A = rhi::ResourceAccess;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "typed state expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

    // Explicit existing L2 access-scope rules; unsupported buffer states fail.
    static rhi::semantics::ResourceAccessScope scope(S state, Stages shaders)
    {
        switch (state) {
        case S::Undefined:
            return {state, Stages::TopOfPipe, A::None};
        case S::CopySrc:
            return {state, Stages::Transfer, A::TransferRead};
        case S::CopyDst:
            return {state, Stages::Transfer, A::TransferWrite};
        case S::ShaderRead:
            return {state, shaders, A::ShaderRead};
        case S::ShaderWrite:
            return {state, shaders, A::ShaderRead | A::ShaderWrite};
        case S::ColorAttachment:
            return {state,
                Stages::ColorAttachmentOutput,
                A::ColorAttachmentRead | A::ColorAttachmentWrite};
        default:
            assert(false);
            return {};
        }
    }

public:
    struct Retained {
        mutable std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedDependencyDecision>
            dependencyDecision;
        mutable std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedNativePayload>
            typedNativePayload;
        std::shared_ptr<const rhi_webgpu::WebGpuCanonicalNativeClosure> nativeClosure;
        std::shared_ptr<const Graph> graph;
        std::shared_ptr<Root> transaction;
        std::shared_ptr<core::CpuMemoryQuota> stateQuota, recordingQuota;
        OriginalCommandTypedStreamFixture::Retained stream;
        rhi::OriginalCommandStateAdmissionBudget transactionBudget;
        rhi::OriginalCommandStateSequenceBudget sequenceBudget;
    };

    // Rebuild the same graph/plan under the same Recording domain for genuine
    // one-shot seal/shell tests. No copied state ledger or guessed budgets.
    static void prepareRetainedPlan(
        Retained& retained, const rhi::GpuDevice& device, const char*& diagnostic)
    {
        retained.transaction = Root::prepareOriginalOwnership(retained.graph,
            device,
            retained.transactionBudget,
            retained.recordingQuota,
            diagnostic);
        assert(retained.transaction);
        diagnostic =
            retained.transaction->materializeOriginalPrototypePlan(retained.sequenceBudget);
        assert(!diagnostic);
    }

    static void verifyRetained(const Retained& retained, bool valid, bool pending = false)
    {
        assert(retained.graph && retained.transaction);
        if (retained.dependencyDecision)
            assert(bool(retained.dependencyDecision->validate()) != valid);
        if (retained.typedNativePayload)
            assert(bool(retained.typedNativePayload->validate()) != valid);
        if (retained.nativeClosure)
            assert(bool(retained.nativeClosure->validate()) != valid);
        assert(bool(retained.graph->validate()) != valid);
        assert(retained.stream.program && bool(retained.stream.program->validate()) != valid);
        assert(bool(retained.transaction->validate()) != valid);
        assert(retained.transaction->status()
                == (pending ? Root::Status::Pending : Root::Status::Prepared)
            && retained.transaction->journalEmpty() != pending
            && !retained.transaction->nativeExecutionProven());
    }

    static void verifyContract()
    {
        using B = rhi::OriginalCommandStateAdmissionBudget;
        using C = rhi::OriginalCommandStateAdmissionContract;
        B budget{2u, true, 1u, 16u, 4u, 2u, 64u, 65536u, {1u, true, 1u, 1u, true, true, 3u, 6u}};
        assert(!C::validateIntent(budget));
        auto legacy = budget;
        legacy.contractVersion = 1u;
        is(C::validateIntent(legacy), "RHI-ORIGINAL-STATE-LEGACY-SCHEMA");
        auto absent = budget;
        absent.descriptorConsumption = {};
        assert(C::validateIntent(absent));
        auto future = budget;
        future.contractVersion = 6u;
        is(C::validateIntent(future), "RHI-ORIGINAL-STATE-VERSION");
        auto geometry = budget;
        geometry.contractVersion = 4u;
        geometry.descriptorConsumption = {3u, true, 0u, 0u, false, false, 3u, 6u, 1u, true, 6u};
        geometry.renderDescriptorUsage = {
            3u, true, 0u, 0u, false, false, 20u, 3u, 6u, 1u, true, 6u};
        geometry.geometryUsage = {1u, true, 20u, 3u, 3u};
        assert(!C::validateIntent(geometry));
        auto missingGeometry = geometry;
        missingGeometry.geometryUsage = {};
        is(C::validateIntent(missingGeometry), "RHI-ORIGINAL-GEOMETRY-ACCESS-VERSION");
        missingGeometry = geometry;
        missingGeometry.geometryUsage.prototypeOptIn = false;
        is(C::validateIntent(missingGeometry), "RHI-ORIGINAL-GEOMETRY-ACCESS-OPT-IN");
        auto oldGeometry = geometry;
        oldGeometry.contractVersion = 3u;
        is(C::validateIntent(oldGeometry), "RHI-ORIGINAL-STATE-GEOMETRY-LEGACY-SCHEMA");
        rhi::OriginalCommandStateSequenceBudget sequence{2u, true, 20u, 0u, 4u, 14u, 6u};
        assert(!rhi::OriginalCommandStateSequenceContract::validateIntent(sequence));
        sequence.contractVersion = 1u;
        is(rhi::OriginalCommandStateSequenceContract::validateIntent(sequence),
            "RHI-ORIGINAL-SEQUENCE-LEGACY-SCHEMA");
        sequence.contractVersion = 2u;
        sequence.maxImplicitAccesses = 0u;
        is(rhi::OriginalCommandStateSequenceContract::validateIntent(sequence),
            "RHI-ORIGINAL-SEQUENCE-IMPLICIT-BUDGET");
        auto device = rhi::createNullDevice();
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const char* id = nullptr;
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(backend);
            assert(decision.contractVersion == 2u && !decision.executionAvailable);
            assert(decision.stateAvailable == (backend == rhi::Backend::WebGPU));
            assert(decision.projectionAvailable == decision.stateAvailable
                && bool(decision.diagnostic) != decision.stateAvailable);
            assert(!arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)
                    .executionAvailable);
            const auto geometryDecision =
                arp::rhi::semantics::OriginalCommandStateAdmissionLowering::typedLowering(
                    backend, 4u);
            assert(geometryDecision.contractVersion == 4u && !geometryDecision.executionAvailable
                && geometryDecision.stateAvailable == (backend == rhi::Backend::WebGPU));
            const auto geometryStream =
                arp::rhi::semantics::OriginalCommandStreamLowering::typedLowering(backend, 4u);
            assert(geometryStream.contractVersion == 4u && !geometryStream.nativeExecutionAvailable
                && geometryStream.diagnostic);
        }
        assert(!Graph::prepareOriginal({}, *device, budget, quota, id));
        is(id, "RHI-ORIGINAL-CONSUMPTION-NULL-VALIDATOR-ONLY");
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_DESCRIPTOR_STATE_CONTRACT_OK state2 sequence2 transaction2 independent-footprint old-schema-closed backends4 snapshot-only-enlistment-closed native-closed");
    }

    template <class Pipeline>
    static Retained verify(const std::shared_ptr<const Inputs>& original,
        const rhi::GpuDevice& device,
        bool retain = false,
        bool exerciseContractCases = true,
        bool streamOnly = false,
        rhi::OriginalCommandDescriptorConsumptionContract::Budget footprint = {
            1u, true, 1u, 1u, true, true, 3u, 6u})
    {
        constexpr bool graphics = std::is_same_v<Pipeline, rhi::RenderPipeline>;
        const auto& root = original->originalScalars()->originalUsage()->originalSources();
        const auto& graphQuota = original->originalMetadataQuota();
        const auto bytes = graphQuota->limitBytes();
        const auto& usage = *original->originalScalars()->originalUsage();
        const auto& arguments = *original->originalScalars()->originalArguments();
        const auto originalCommands = usage.commands().size();
        std::uint64_t descriptorBinds = 0u;
        for (const auto& command : usage.commands())
            if (command.kind == K::BindComputeDescriptor || command.kind == K::BindRenderDescriptor)
                ++descriptorBinds;
        const auto descriptorIndex = usage.references()[graphics ? 2u : 1u].sourceIndex;
        const auto& descriptor =
            *std::get<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                root->sources()[descriptorIndex]);
        std::array<std::size_t, 2u> buffers{};
        std::size_t bufferCount = 0u;
        for (std::size_t i = 0u; i < root->sources().size(); ++i)
            if (std::holds_alternative<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    root->sources()[i])) {
                assert(bufferCount < buffers.size());
                buffers[bufferCount++] = i;
            }
        assert(bufferCount && bufferCount <= buffers.size());
        std::array<S, 2u> expected{}, initial{};
        std::array<Stages, 2u> stages{Stages::None, Stages::None};
        bool conflict = false;
        for (std::size_t i = 0u; i < bufferCount; ++i) {
            bool found = false;
            for (const auto& source : root->sources())
                if (const auto* set =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandDescriptorSetSource>>(
                            &source))
                    for (const auto& frozen : (*set)->frozenElements())
                        if (frozen.declaration.sourceIndex == buffers[i]) {
                            const auto& e = frozen.declaration;
                            if (found && expected[i] != e.write.resourceState)
                                conflict = true;
                            else
                                expected[i] = e.write.resourceState;
                            stages[i] = stages[i] | e.stages;
                            found = true;
                        }
            assert(found);
            const auto& lease = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                root->sources()[buffers[i]]);
            const auto observation = lease->originalObject()->observeOriginalCommittedState();
            assert(observation);
            initial[i] = observation->state();
        }
        S initialTexture = S::Undefined;
        if constexpr (graphics) {
            const auto& texture = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                root->sources()[bufferCount]);
            const auto observation = texture->originalObject()->observeOriginalCommittedState(
                {0u, 1u, 0u, 1u, rhi::TextureAspect::Color});
            assert(observation);
            initialTexture = observation->state();
        }
        const char* id = nullptr;
        const auto build = [&](bool late = false) {
            core::pmr::Vector<rhi::CommandUsage> commands;
            commands.reserve(originalCommands + 4u * bufferCount + 2u);
            core::pmr::Vector<rhi::OriginalCommandUsage::Reference> references;
            references.reserve(usage.references().size() + 4u * bufferCount + 2u);
            core::pmr::Vector<rhi::CommandVariableArguments::Input> fields;
            fields.reserve(originalCommands + 4u * bufferCount + 2u);
            core::pmr::Vector<Inputs::OriginalDescriptor> records;
            records.reserve(original->descriptors().size() + 4u * bufferCount + 2u);
            core::pmr::Vector<std::size_t> positions(originalCommands);
            const auto dependencies = [&](unsigned phase) {
                for (std::size_t i = 0u; i < bufferCount; ++i) {
                    const auto& lease =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            root->sources()[buffers[i]]);
                    const auto command = commands.size(), reference = references.size();
                    references.push_back({static_cast<std::uint32_t>(buffers[i])});
                    const auto kind = phase == 1u ? K::BufferMemoryBarrier : K::BufferBarrier;
                    commands.push_back({kind, static_cast<std::uint32_t>(reference), 1u});
                    fields.push_back({*rhi::CommandScalarArguments::capture(
                                          kind, rhi::CommandScalarArguments::None{}),
                        {}});
                    if (phase == 1u) {
                        rhi::ResourceMemoryBarrierDesc memory;
                        memory.resource = lease->resource();
                        const auto bound = scope(expected[i], stages[i]);
                        memory.sourceStages = memory.destinationStages = bound.stages;
                        memory.sourceAccess = memory.destinationAccess = bound.access;
                        const auto value = rhi::ResourceDependencyArguments::capture(kind, memory);
                        assert(value);
                        records.push_back({command, *value});
                    } else {
                        rhi::ResourceBarrierDesc barrier;
                        barrier.resource = lease->resource();
                        const auto wrong =
                            expected[i] == S::ShaderRead ? S::ShaderWrite : S::ShaderRead;
                        barrier.before = phase == 0u || phase == 3u ? initial[i]
                            : phase == 4u                           ? wrong
                                                                    : expected[i];
                        barrier.after = phase == 3u      ? wrong
                            : phase == 0u || phase == 4u ? expected[i]
                                                         : S::ShaderRead;
                        const auto before = scope(barrier.before, stages[i]),
                                   after = scope(barrier.after, stages[i]);
                        barrier.sourceStages = before.stages;
                        barrier.sourceAccess = before.access;
                        barrier.destinationStages = after.stages;
                        barrier.destinationAccess = after.access;
                        const auto value = rhi::ResourceDependencyArguments::capture(kind, barrier);
                        assert(value);
                        records.push_back({command, *value});
                    }
                }
            };
            const auto textureDependency = [&](bool after) {
                if constexpr (graphics) {
                    const auto& texture =
                        std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                            root->sources()[bufferCount]);
                    const auto command = commands.size();
                    const auto reference = references.size();
                    references.push_back({static_cast<std::uint32_t>(bufferCount)});
                    commands.push_back(
                        {K::TextureBarrier, static_cast<std::uint32_t>(reference), 1u});
                    fields.push_back({*rhi::CommandScalarArguments::capture(
                                          K::TextureBarrier, rhi::CommandScalarArguments::None{}),
                        {}});
                    rhi::ResourceBarrierDesc barrier;
                    barrier.resource = texture->resource();
                    barrier.textureSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
                    barrier.before = after ? S::ColorAttachment : initialTexture;
                    barrier.after = after ? S::CopySrc : S::ColorAttachment;
                    const auto before = scope(barrier.before, Stages::None),
                               next = scope(barrier.after, Stages::None);
                    barrier.sourceStages = before.stages;
                    barrier.sourceAccess = before.access;
                    barrier.destinationStages = next.stages;
                    barrier.destinationAccess = next.access;
                    const auto value =
                        rhi::ResourceDependencyArguments::capture(K::TextureBarrier, barrier);
                    assert(value);
                    records.push_back({command, *value});
                }
            };
            dependencies(late ? 3u : 0u);
            textureDependency(false);
            unsigned passes = 0u;
            for (std::size_t i = 0u; i < usage.commands().size(); ++i) {
                const auto kind = usage.commands()[i].kind;
                if ((kind == K::BeginRenderPass || kind == K::BeginComputePass) && ++passes == 2u) {
                    if (late)
                        dependencies(4u);
                    dependencies(1u);
                }
                if (kind == K::Finish) {
                    dependencies(2u);
                    textureDependency(true);
                }
                positions[i] = commands.size();
                const auto& c = usage.commands()[i];
                commands.push_back(
                    {c.kind, static_cast<std::uint32_t>(references.size()), c.referenceCount});
                for (std::size_t operand = 0u; operand < c.referenceCount; ++operand)
                    references.push_back(usage.references()[c.firstReference + operand]);
                const auto& f = arguments.records()[i];
                fields.push_back({f.scalar,
                    arguments.text(i),
                    arguments.offsets(i),
                    static_cast<std::uint32_t>(f.offsetCount),
                    arguments.bytes(i),
                    static_cast<std::uint32_t>(f.byteCount)});
                if (const auto* value = original->descriptorAt(i)) {
                    auto record = *value;
                    if (auto* pipeline = std::get_if<rhi::OriginalCommandPipelineBinding>(&record))
                        pipeline->scopeBeginCommandIndex =
                            positions[pipeline->scopeBeginCommandIndex];
                    if (auto* set = std::get_if<rhi::OriginalCommandDescriptorBinding>(&record))
                        set->scopeBeginCommandIndex = positions[set->scopeBeginCommandIndex];
                    records.push_back({positions[i], std::move(record)});
                }
            }
            auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(root,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                fields.data(),
                records.data(),
                records.size(),
                original->originalBudget(),
                graphQuota,
                id);
            if (!inputs)
                std::fprintf(stderr, "typed state Inputs failed: %s\n", id ? id : "unknown");
            assert(inputs);
            return inputs;
        };
        auto inputs = build();
        Retained result;
        result.stateQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        result.recordingQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
        std::uint32_t owners = 0u, registrations = 0u;
        assert(!Sources::registeredOwnerUnion(
            root, inputs->retainedOwnerCount() + 1u, 64u, UINT32_MAX, owners, registrations));
        // Count exact original points/cells, including authored memory barriers.
        // Geometry is bounded by the Source's explicit state-observation ceiling.
        std::uint32_t texturePoints = 0u;
        std::uint64_t textureCells = 0u, bufferReferences = 0u;
        const auto& assembledUsage = *inputs->originalScalars()->originalUsage();
        for (const auto& reference : assembledUsage.references())
            if (std::holds_alternative<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    root->sources()[reference.sourceIndex]))
                ++bufferReferences;
        assert(!rhi::common::OriginalCommandTextureRanges::visit(
            inputs, [&](const auto& point, const rhi::GpuTexture& texture) noexcept -> const char* {
                const char* diagnostic = nullptr;
                const auto geometry = rhi::semantics::OriginalTextureStateRange::capture(
                    texture.desc(), root->originalBudget().maxStateObservations, diagnostic);
                assert(geometry && !diagnostic);
                const auto cells = geometry->rangeCellCount(point.range);
                assert(cells);
                assert(texturePoints < root->originalBudget().maxTextureRanges
                    && core::MemoryBudgetLedger::canAdd(textureCells, cells));
                ++texturePoints;
                textureCells += cells;
                return nullptr;
            }));
        assert(bufferCount <= UINT32_MAX - texturePoints);
        rhi::OriginalCommandStateAdmissionBudget state{2u,
            true,
            1u,
            registrations,
            static_cast<std::uint32_t>(bufferCount + texturePoints),
            texturePoints,
            owners,
            bytes,
            footprint};
        const auto makeGraph = [&] {
            return Graph::prepareOriginal(inputs, device, state, result.stateQuota, id);
        };
        auto graph = makeGraph();
        if (!graph)
            std::fprintf(stderr, "typed state graph failed: %s\n", id ? id : "unknown");
        assert(graph);
        assert(assembledUsage.commands().size() <= UINT32_MAX
            && core::MemoryBudgetLedger::canAdd(bufferCount, textureCells)
            && core::MemoryBudgetLedger::canAdd(bufferReferences, textureCells)
            && core::MemoryBudgetLedger::canAdd(
                bufferReferences + textureCells, footprint.maxLogicalAccesses));
        rhi::OriginalCommandStateSequenceBudget sequence{2u,
            true,
            static_cast<std::uint32_t>(assembledUsage.commands().size()),
            0u,
            bufferCount + textureCells,
            bufferReferences + textureCells + footprint.maxLogicalAccesses,
            footprint.maxLogicalAccesses};
        result.transactionBudget = state;
        result.transactionBudget.maxRetainedOwners = owners + 1u;
        result.sequenceBudget = sequence;
        auto inspect = Access::inspect(graph, sequence);
        if (conflict) {
            is(inspect.diagnostic, "RHI-ORIGINAL-SEQUENCE-SHADER-STATE");
            const auto streamBudget = OriginalCommandTypedStreamFixture::budget(
                graph, sequence, state.descriptorConsumption, 6u);
            OriginalCommandTypedStreamFixture::verifyRejected(
                graph, streamBudget, "RHI-ORIGINAL-SEQUENCE-SHADER-STATE");
            std::puts(
                "BINDLESS_RHI_DESCRIPTOR_STATE_ALIAS_CONFLICT_OK exact-original-shared-buffer incompatible-declared-state no-plan-no-journal");
            return {};
        }
        if (!exerciseContractCases) {
            // Reuse the same genuine Inputs3 builder, Graph/Sequence contracts
            // and transaction plan for independent terminal observations.
            assert(retain && !inspect.diagnostic);
            result.graph = graph;
            result.stream = OriginalCommandTypedStreamFixture::prepare(graph,
                OriginalCommandTypedStreamFixture::budget(
                    graph, sequence, state.descriptorConsumption, descriptorBinds));
            if (streamOnly)
                return result;
            auto transaction = state;
            transaction.maxRetainedOwners = owners + 1u;
            result.transaction = Root::prepareOriginalOwnership(
                graph, device, transaction, result.recordingQuota, id);
            if (!result.transaction)
                std::fprintf(
                    stderr, "retained typed transaction preparation: %s\n", id ? id : "unknown");
            assert(result.transaction);
            id = result.transaction->materializeOriginalPrototypePlan(sequence);
            if (id)
                std::fprintf(stderr, "retained typed transaction plan: %s\n", id);
            assert(!id);
            verifyRetained(result, true);
            return result;
        }
        if (inspect.diagnostic)
            std::fprintf(stderr, "typed sequence failed: %s\n", inspect.diagnostic);
        assert(!inspect.diagnostic && inspect.steps == sequence.maxSteps
            && inspect.stateCells == sequence.maxStateCells);
        auto old = sequence;
        old.contractVersion = 1u;
        old.maxImplicitAccesses = 0u;
        is(Sequence::inspect(graph, old).diagnostic, "RHI-ORIGINAL-SEQUENCE-STATE-PROFILE");
        --sequence.maxImplicitAccesses;
        is(Sequence::inspect(graph, sequence).diagnostic,
            "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY");
        ++sequence.maxImplicitAccesses;
        --sequence.maxSteps;
        is(Sequence::inspect(graph, sequence).diagnostic, "RHI-ORIGINAL-SEQUENCE-STEP-CAPACITY");
        ++sequence.maxSteps;
        --sequence.maxStateCells;
        is(Sequence::inspect(graph, sequence).diagnostic,
            sequence.maxStateCells ? "RHI-ORIGINAL-SEQUENCE-CELL-CAPACITY"
                                   : "RHI-ORIGINAL-SEQUENCE-BUDGET");
        ++sequence.maxStateCells;
        auto saved = inputs;
        inputs = build(true);
        auto late = makeGraph();
        assert(late);
        auto lateBudget = sequence;
        lateBudget.maxCommands += bufferCount;
        lateBudget.maxSteps += bufferCount;
        is(Sequence::inspect(late, lateBudget).diagnostic, "RHI-ORIGINAL-SEQUENCE-SHADER-STATE");
        OriginalCommandTypedStreamFixture::verifyRejected(late,
            OriginalCommandTypedStreamFixture::budget(
                late, lateBudget, state.descriptorConsumption, descriptorBinds),
            "RHI-ORIGINAL-SEQUENCE-SHADER-STATE");
        late.reset();
        inputs = saved;
        saved.reset();
        graph.reset();
        CpuQuotaFactoryFixture statePressure(result.stateQuota);
        const auto statePeak = statePressure.measureSingleUse([] {}, makeGraph);
        statePressure.verifySingleUse(statePeak, [] {}, makeGraph);
        auto originalState = state;
        state.contractVersion = 1u;
        state.descriptorConsumption = {};
        assert(!makeGraph() && id);
        state = originalState;
        --state.maxRetainedOwners;
        assert(!makeGraph());
        is(id, "RHI-ORIGINAL-STATE-OWNER-CAPACITY");
        ++state.maxRetainedOwners;
        --state.maxResourceRegistrations;
        assert(!makeGraph());
        is(id, "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY");
        ++state.maxResourceRegistrations;
        --state.maxStateObservations;
        assert(!makeGraph());
        is(id, "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
        ++state.maxStateObservations;
        graph = makeGraph();
        assert(graph);
        const auto streamBudget = OriginalCommandTypedStreamFixture::budget(
            graph, sequence, state.descriptorConsumption, descriptorBinds);
        auto stream = OriginalCommandTypedStreamFixture::verify(
            graph, streamBudget, graphics ? "graphics" : "compute");
        auto transaction = state;
        transaction.maxRetainedOwners = owners + 1u;
        const auto makeTransaction = [&] {
            return Root::prepareOriginalOwnership(
                graph, device, transaction, result.recordingQuota, id);
        };
        auto pending = makeTransaction();
        if (!pending)
            std::fprintf(stderr, "typed transaction failed: %s\n", id ? id : "unknown");
        assert(pending);
        is(pending->beginOriginalPendingOwnership(),
            "RHI-ORIGINAL-TYPED-TRANSACTION-PLAN-REQUIRED");
        assert(pending->journalEmpty());
        auto txSaved = transaction;
        transaction.contractVersion = 1u;
        transaction.descriptorConsumption = {};
        assert(!makeTransaction());
        is(id, "RHI-ORIGINAL-TRANSACTION-STATE-PROFILE");
        transaction = txSaved;
        --transaction.descriptorConsumption.maxLogicalAccesses;
        assert(!makeTransaction());
        is(id, "RHI-ORIGINAL-TRANSACTION-FOOTPRINT-MISMATCH");
        transaction = txSaved;
        pending.reset();
        const auto makePlan = [&] {
            auto value = makeTransaction();
            if (value && (id = value->materializeOriginalPrototypePlan(sequence)))
                value.reset();
            return value;
        };
        CpuQuotaFactoryFixture recordingPressure(result.recordingQuota);
        const auto recordingPeak = recordingPressure.measureSingleUse([] {}, makePlan);
        recordingPressure.verifySingleUse(recordingPeak, [] {}, makePlan);
        pending = makePlan();
        if (!pending)
            std::fprintf(stderr, "typed plan failed: %s\n", id ? id : "unknown");
        assert(pending);
        assert(pending->plannedAccessStepCount() == sequence.maxSteps
            && pending->plannedTransitionCount() == 2u * bufferCount + (graphics ? 2u : 0u)
            && pending->plannedFinalCellCount() == bufferCount + (graphics ? 1u : 0u)
            && pending->journalEmpty());
        const auto closedNative = [&]() noexcept -> const char* {
            assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginal(
                pending, device, {1u, true, 1u, 1u, 1u, 1u, bytes}, id));
            is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-PROFILE");
            assert(!rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginal(
                pending, device, {2u, true, 1u, 1u, 1u, 1u, bytes, 1u, 1u}, id));
            is(id, "RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-STREAM-REQUIRED");
            return nullptr;
        };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
#endif
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{result.stateQuota, result.recordingQuota}, closedNative));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        });
#endif
        for (const auto& q : std::array{result.stateQuota, result.recordingQuota}) {
            const auto limit = q->limitBytes();
            q->setLimitBytes(limit - 1u);
            assert(pending->validate());
            q->setLimitBytes(limit);
            assert(!pending->validate() && pending->journalEmpty());
        }
        const auto hot = [&]() noexcept -> const char* {
            if (const auto* error = Access::inspect(graph, sequence).diagnostic)
                return error;
            if (const auto* error = pending->applyOriginalPrototypePlan())
                return error;
            assert(pending->status() == Root::Status::Pending && !pending->journalEmpty()
                && graph->validate() && stream.program->validate());
            return pending->validate();
        };
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
#endif
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{graphQuota,
                                                                      root->originalMetadataQuota(),
                                                                      result.stateQuota,
                                                                      result.recordingQuota},
                hot));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        });
#endif
        assert(!pending->abandonOriginalPendingOwnership()
            && pending->status() == Root::Status::Abandoned && pending->journalEmpty());
        assert(graph->validate() && stream.program->validate());
        stream.program.reset();
        pending.reset();
        graph.reset();
        graph = makeGraph();
        assert(graph);
        for (std::size_t i = 0u; i < bufferCount; ++i) {
            const auto& buffer = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                root->sources()[buffers[i]]);
            assert(
                buffer->originalObject()->observeOriginalCommittedState()->state() == initial[i]);
        }
        pending = makePlan();
        assert(pending);
        stream = OriginalCommandTypedStreamFixture::prepare(graph, streamBudget);
        assert(!makeTransaction());
        is(id, "RHI-ORIGINAL-TRANSACTION-COUNT-CAPACITY");
        assert(!pending->applyOriginalPrototypePlan());
        assert(!makeTransaction());
        is(id, "RHI-ORIGINAL-BUFFER-STATE-PENDING");
        pending.reset();
        assert(graph->validate() && stream.program->validate());
        stream.program.reset();
        graph.reset();
        graph = makeGraph();
        assert(graph);
        for (std::size_t i = 0u; i < bufferCount; ++i) {
            const auto& buffer = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                root->sources()[buffers[i]]);
            assert(
                buffer->originalObject()->observeOriginalCommittedState()->state() == initial[i]);
        }
        pending = makePlan();
        assert(pending);
        stream = OriginalCommandTypedStreamFixture::prepare(graph, streamBudget);
        assert(!pending->applyOriginalPrototypePlan() && stream.program->validate());
        assert(!pending->commitOriginalPendingOwnership()
            && pending->status() == Root::Status::Committed && pending->journalEmpty());
        assert(graph->validate() && stream.program->validate());
        stream.program.reset();
        pending.reset();
        graph.reset();
        assert(!stream.quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_TYPED_STREAM_PLAN_OK same-state2-root pending explicit-cancel destructor-rollback CPU-commit stale-stream-rejected quotas-reclaimed native-closed");
        for (const auto index : buffers)
            if (index < root->sources().size()) {
                if (const auto* b =
                        std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            &root->sources()[index]))
                    assert((*b)->originalObject()->observeOriginalCommittedState()->state()
                        == S::ShaderRead);
            }
        if (retain) {
            // Rebuild authored barriers from the newly committed CPU state;
            // retain an exact Prepared plan for subsequent seal/retirement/loss.
            for (std::size_t i = 0u; i < bufferCount; ++i)
                initial[i] = S::ShaderRead;
            if constexpr (graphics)
                initialTexture = S::CopySrc;
            inputs = build();
            graph = makeGraph();
            assert(graph);
            pending = makePlan();
            assert(pending);
            result.graph = graph;
            result.transaction = pending;
            result.stream = OriginalCommandTypedStreamFixture::prepare(graph, streamBudget);
        } else {
            assert(!result.stateQuota->liveBytes() && !result.recordingQuota->liveBytes()
                && !stream.quota->liveBytes());
        }
        std::printf(
            "BINDLESS_RHI_DESCRIPTOR_STATE_OK %s source%u buffers%zu cells%llu steps%llu implicit6 state-peak%llu recording-plan-peak%llu prior-middle-after-barriers exact-less hot0 query0 rollback-commit typed-native-closed\n",
            graphics ? "graphics" : "compute",
            descriptor.sourceContractVersion(),
            bufferCount,
            static_cast<unsigned long long>(sequence.maxStateCells),
            static_cast<unsigned long long>(sequence.maxSteps),
            static_cast<unsigned long long>(statePeak),
            static_cast<unsigned long long>(recordingPeak));
        return result;
    }
};
} // namespace arp::test
