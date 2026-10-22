#pragma once
#include "core/math/half_float.h"
#include "cpu_quota_factory_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_storage_pipeline_fixture.h"
#include "original_workload_resource_inspection_fixture.h"
#include "rhi/frontend/bindless_root_fence_execution.h"

#include <chrono>
#include <thread>
#if defined(__EMSCRIPTEN__)
#include "webgpu_async_wait_fixture.h"
#endif

namespace arp::test {
template <class Backend> class OriginalStorageExecutionFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Public storage execution: %s\n", id);
        assert(!id);
    }

public:
    // 0 complete, 1 caller drop, 2 retire in-flight, 3 cancel, 4 retire before submit.
    // Callback only: 5 recording error, 6 reversed genuine receipts, 7 submit
    // validation error, 8/9 throw before/after real submit, 10 pending cancel.
    inline static unsigned terminal = 0u;
    inline static bool fault = false;
    // Authored producer values at the top-left 2x2 depth samples. Native
    // producers use bindless_vertex_buffer.slang; WebGPU uses per-mip clears.
    inline static std::array<float, 4u> mixedDepthValues{};

    static void execute(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 2u>& pair,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& base,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        run(device, snapshot, pair, base, resources);
    }

    static void executeMixed(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 3u>& entries,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& base,
        const std::shared_ptr<core::CpuMemoryQuota>& resources)
    {
        run(device, snapshot, entries, base, resources);
    }

    template <bool planningOnly = false, std::size_t Entries = 2u>
    static void run(rhi::GpuDevice& device,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, Entries>& pair,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& base,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& depthRoot = {},
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& depthInventory = {})
    {
        using namespace rhi;
        using K = CommandUsageKind;
        using S = ResourceState;
        static_assert(
            Entries == 2u || Entries == 3u, "Storage or authenticated mixed entry flow required");
        constexpr bool mixed = Entries == 3u;
        constexpr auto executionVersion =
            mixed ? (Backend::callback ? 12u : 11u) : (Backend::callback ? 10u : 9u);
        const bool hasDepth = mixed || bool(depthRoot);
        using Sources = OriginalCommandResourceSources;
        using Tx = OriginalCommandStateTransaction;
        assert(terminal <= (Backend::callback ? 10u : 4u));
        const auto parent =
            std::get<std::shared_ptr<const OriginalCommandTextureLease>>(base.front());
        const auto bytes = parent->originalMetadataQuota()->limitBytes();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9u> q;
        for (auto& quota : q)
            quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        q[0] = parent->originalMetadataQuota();
        const auto sourceBaseline = q[0]->liveBytes();
        {
            core::pmr::Vector<Sources::Source> sources = base;
            if (depthRoot) {
                assert(depthInventory && planningOnly);
                for (const auto& source : depthInventory->sources()) {
                    if (std::holds_alternative<
                            std::shared_ptr<const OriginalCommandBindlessRootSource>>(source))
                        continue;
                    sources.push_back(source);
                }
            }
            std::array<std::shared_ptr<const OriginalCommandTextureViewSource>, 2u> attachments;
            const char* id = nullptr;
            for (unsigned i = 0u; i != 2u; ++i) {
                TextureViewDesc d;
                d.format = TextureFormat::Rgba16Float;
                d.usage = TextureUsage::RenderAttachment;
                d.baseMipLevel = i + 1u;
                core::UniquePtr<GpuTextureView> object;
                {
                    core::CpuMemoryQuotaScope allocation(resources);
                    object = device.createTextureView(*parent->originalObject(), d);
                }
                assert(object);
                auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                    object, {1u, true, 2u, bytes}, parent->originalMetadataQuota(), id);
                require(id);
                assert(lease);
                attachments[i] = OriginalCommandTextureViewSource::prepareOriginal(lease,
                    parent,
                    {1u, true, OriginalCommandTextureViewSource::completeRetainedOwnerCount, bytes},
                    parent->originalMetadataQuota(),
                    id);
                require(id);
                assert(attachments[i]);
                sources.push_back(attachments[i]);
            }
            // Counts are the finite authored stream below, not device capacity defaults.
            const auto commandsLimit = 8u + 3u + 4u + 2u + 1u + 4u + 1u + 3u + 1u + 1u
                + (depthRoot ? 5u : 0u) + (mixed ? 6u : 0u);
            OriginalCommandAdmissionBudget admission;
            admission.contractVersion = 1u;
            admission.prototypeOptIn = true;
            admission.maxCommands = commandsLimit;
            admission.maxReferences = commandsLimit;
            admission.maxInputDescriptors = commandsLimit;
            admission.maxGraphOwners = 512u;
            admission.maxPassAttachmentReferences = 2u;
            admission.maxPassColorAttachments = 2u;
            admission.maxStateTransactions = admission.maxPendingSubmissions = 1u;
            admission.maxStateObservations = 64u;
            admission.maxTextureRanges = 16u;
            admission.maxNativePayloadObjects =
                Backend::nativePayloadObjects; // command + two cold Clear/Store passes
            admission.maxCpuBytes.fill(bytes);
            admission.maxCpuBytes[static_cast<std::size_t>(
                OriginalCommandAdmissionBudget::CpuDomain::Callback)] =
                Backend::callback ? bytes : 0u;
            if constexpr (Backend::callback) {
                admission.maxCallbackTickets = admission.maxCallbackRegistrations = 12u;
                admission.maxUnpublishedResultBytes =
                    4u; // The authored fault word, retained through real map drain.
            }
            for (const auto& input : sources)
                std::visit(
                    [&](const auto& source) {
                        using T = std::remove_const_t<
                            typename std::decay_t<decltype(source)>::element_type>;
                        ++admission.maxSourceOwners[static_cast<std::size_t>(T::domain)];
                    },
                    input);
            admission
                .maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] =
                hasDepth ? 3u : 2u;
            auto inventory = Sources::prepareColdOriginalTyped(sources.data(),
                sources.size(),
                admission,
                {6u, true, hasDepth ? 3u : 2u},
                q[0],
                id);
            require(id);
            assert(inventory);
            std::array<std::shared_ptr<const OriginalCommandBindlessRootSource>, Entries> roots;
            for (unsigned i = 0u; i != roots.size(); ++i) {
                roots[i] = OriginalBindlessRootAssemblyFixture::prepare(
                    snapshot, pair[i], inventory, device, q[0], id);
                require(id);
                assert(roots[i]);
                if (!i) {
                    const auto count = inventory->sources().size();
                    auto duplicate = OriginalBindlessRootAssemblyFixture::prepare(
                        snapshot, pair[i], inventory, device, q[0], id);
                    assert(duplicate && id
                        && core::StringView(id) == "RHI-ORIGINAL-SOURCES-IDENTITY-ALIAS");
                    assert(inventory->sources().size() == count && !inventory->validate());
                }
            }
            std::shared_ptr<const OriginalCommandBindlessRootSource> mixedDepthRoot;
            if (depthRoot) {
                const auto prepared =
                    std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
                        depthRoot->originalPrepared());
                mixedDepthRoot = OriginalBindlessRootAssemblyFixture::prepare(
                    depthRoot->originalSnapshot(), prepared, inventory, device, q[0], id);
                require(id);
                assert(mixedDepthRoot);
            }
            if constexpr (mixed) {
                mixedDepthRoot = roots[0u];
                const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(roots,
                    inventory,
                    {1u, true, roots[0u]->size() + roots[1u]->size() + roots[2u]->size()});
                require(flow.diagnostic);
            }
            // All authenticated roots are assembled before the first graph can
            // cache owner counts. One reservation covers their shared snapshot.
            auto workloadCapacity =
                OriginalWorkloadResourceInspectionFixture::prepareRetention(inventory, device);
            const auto sourceIndex = [&](const auto& source) {
                const auto index = inventory->originalLeaseIndex(source);
                assert(index != SIZE_MAX);
                return static_cast<std::uint32_t>(index);
            };
            core::pmr::Vector<CommandUsage> commands;
            core::pmr::Vector<OriginalCommandUsage::Reference> refs;
            core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
            const auto append = [&](K kind, std::initializer_list<std::uint32_t> operands) {
                const auto index = commands.size();
                commands.push_back({kind,
                    static_cast<std::uint32_t>(refs.size()),
                    static_cast<std::uint32_t>(operands.size())});
                for (auto operand : operands)
                    refs.push_back({operand});
                return index;
            };
            const auto barrier = [&](K kind,
                                     const auto& source,
                                     TextureSubresourceRange range,
                                     S before,
                                     S after,
                                     PipelineStage src,
                                     ResourceAccess old,
                                     PipelineStage dst,
                                     ResourceAccess next) {
                ResourceBarrierDesc b{source->resource(), range, before, after};
                b.sourceStages = src;
                b.sourceAccess = old;
                b.destinationStages = dst;
                b.destinationAccess = next;
                descriptors.push_back({append(kind, {sourceIndex(source)}),
                    *ResourceDependencyArguments::capture(kind, b)});
            };
            constexpr auto compute = PipelineStage::ComputeShader;
            const auto rw = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            for (unsigned i = 0u; i != 2u; ++i) {
                TextureSubresourceRange range{i + 1u, 1u, 0u, 1u, TextureAspect::Color};
                const auto prior = parent->originalObject()->observeOriginalCommittedState(range);
                assert(prior);
                barrier(K::TextureBarrier,
                    parent,
                    range,
                    prior->state(),
                    S::ColorAttachment,
                    prior->state() == S::Undefined ? PipelineStage::TopOfPipe : compute,
                    prior->state() == S::Undefined ? ResourceAccess::None
                                                   : ResourceAccess::ShaderRead,
                    PipelineStage::ColorAttachmentOutput,
                    ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite);
                RenderPassDesc d;
                RenderPassColorAttachmentDesc color;
                color.target = attachments[i]->resource();
                color.format = TextureFormat::Rgba16Float;
                color.loadOp = LoadOp::Clear;
                color.storeOp = StoreOp::Store;
                color.clearColor = {.0625f, .25f, .5f, 1.f};
                d.colorAttachments.push_back(color);
                auto args = RenderPassArguments::prepareOriginal(
                    d, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, q[1], id);
                require(id);
                assert(args);
                descriptors.push_back(
                    {append(K::BeginRenderPass, {sourceIndex(attachments[i])}), args});
                append(K::EndRenderPass, {});
                barrier(K::TextureBarrier,
                    parent,
                    range,
                    S::ColorAttachment,
                    S::ShaderWrite,
                    PipelineStage::ColorAttachmentOutput,
                    ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite,
                    compute,
                    ResourceAccess::ShaderWrite);
            }
            const auto latch =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
            const auto readback =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
            barrier(K::BufferBarrier,
                latch,
                {},
                S::ShaderWrite,
                S::CopyDst,
                compute,
                rw,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite);
            append(K::ClearSnapshotFault, {sourceIndex(roots[0])});
            barrier(K::BufferBarrier,
                latch,
                {},
                S::CopyDst,
                S::ShaderWrite,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite,
                compute,
                rw);
            const auto dispatch = [&](unsigned root) {
                append(K::BeginComputePass, {});
                append(K::BindPreparedComputeBinding, {sourceIndex(roots[root])});
                append(K::Dispatch, {});
                append(K::EndComputePass, {});
            };
            const auto depthBarrier = [&] {
                const auto proof = mixedDepthRoot->originalCompletedDepthAt(0u);
                assert(proof);
                const auto depth = proof->originalViewSource()->originalParentLease();
                const auto range = defaultTextureSubresourceRange(depth->originalObject()->desc());
                const auto prior = depth->originalObject()->observeOriginalCommittedState(range);
                assert(prior);
                barrier(K::TextureBarrier,
                    depth,
                    range,
                    prior->state(),
                    S::ShaderRead,
                    prior->state() == S::ShaderRead ? compute : PipelineStage::DepthStencil,
                    prior->state() == S::ShaderRead
                        ? ResourceAccess::ShaderRead
                        : ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite,
                    compute,
                    ResourceAccess::ShaderRead);
            };
            if constexpr (mixed)
                depthBarrier();
            dispatch(0u);
            for (unsigned i = 0u; i != (mixed ? 1u : 2u); ++i)
                barrier(K::TextureBarrier,
                    parent,
                    {i + 1u, 1u, 0u, 1u, TextureAspect::Color},
                    S::ShaderWrite,
                    S::ShaderRead,
                    compute,
                    ResourceAccess::ShaderWrite,
                    compute,
                    ResourceAccess::ShaderRead);
            barrier(K::BufferBarrier,
                latch,
                {},
                S::ShaderWrite,
                S::ShaderWrite,
                compute,
                rw,
                compute,
                rw);
            if (depthRoot) {
                depthBarrier();
                append(K::BeginComputePass, {});
                append(K::BindPreparedComputeBinding, {sourceIndex(mixedDepthRoot)});
                append(K::Dispatch, {});
                append(K::EndComputePass, {});
            }
            dispatch(1u);
            if constexpr (mixed) {
                barrier(K::TextureBarrier,
                    parent,
                    {2u, 1u, 0u, 1u, TextureAspect::Color},
                    S::ShaderWrite,
                    S::ShaderRead,
                    compute,
                    ResourceAccess::ShaderWrite,
                    compute,
                    ResourceAccess::ShaderRead);
                barrier(K::BufferBarrier,
                    latch,
                    {},
                    S::ShaderWrite,
                    S::ShaderWrite,
                    compute,
                    rw,
                    compute,
                    rw);
                dispatch(2u);
            }
            barrier(K::BufferBarrier,
                latch,
                {},
                S::ShaderWrite,
                S::CopySrc,
                compute,
                rw,
                PipelineStage::Transfer,
                ResourceAccess::TransferRead);
            append(K::BeginTransferPass, {});
            const auto copy = append(K::BufferCopy, {sourceIndex(latch), sourceIndex(readback)});
            descriptors.push_back({copy,
                *TransferCopyArguments::capture(
                    BufferCopyDesc{latch->resource(), readback->resource(), 0u, 0u, 4u})});
            append(K::EndTransferPass, {});
            barrier(K::BufferBarrier,
                latch,
                {},
                S::CopySrc,
                S::ShaderWrite,
                PipelineStage::Transfer,
                ResourceAccess::TransferRead,
                compute,
                rw);
            append(K::Finish, {});
            assert(commands.size() == commandsLimit && refs.size() <= admission.maxReferences);
            core::pmr::Vector<CommandVariableArguments::Input> fields;
            unsigned dispatchIndex = 0u;
            for (const auto& command : commands) {
                const auto groups = mixed && dispatchIndex < 2u ? 1u : fault ? 3u : 2u;
                fields.push_back({*(command.kind == K::Dispatch
                        ? CommandScalarArguments::capture(
                              command.kind, CommandScalarArguments::Dispatch{groups, 1u, 1u})
                        : CommandScalarArguments::capture(
                              command.kind, CommandScalarArguments::None{}))});
                if (command.kind == K::Dispatch)
                    ++dispatchIndex;
            }
            auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                descriptors.data(),
                descriptors.size(),
                {1u, false},
                q[1],
                id);
            require(id);
            assert(inputs);
            OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 64u, 16u, 512u, bytes};
            std::size_t declarations = 0u;
            for (const auto& root : roots)
                declarations += root->size();
            if (depthRoot)
                declarations += mixedDepthRoot->size();
            state.bindlessRoots = {4u, true, hasDepth ? 6u : 4u, declarations + 1u, 32768u, 1u, 1u};
            const auto accesses = common::OriginalCommandBindlessRootAccesses::visit(
                inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            require(accesses.diagnostic);
            state.bindlessRoots.maxSourceVisits = accesses.sourceVisits;
            auto graph =
                OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[2], id);
            require(id);
            assert(graph);
            OriginalCommandStateSequenceBudget sequence{1u,
                true,
                static_cast<std::uint32_t>(commands.size()),
                0u,
                512u,
                512u,
                accesses.accesses,
                4u,
                true};
            const auto inspected =
                common::OriginalCommandAccessValidation::inspect(graph, sequence);
            require(inspected.diagnostic);
            sequence.maxStateCells = inspected.stateCells;
            sequence.maxSteps = inspected.steps;
            OriginalCommandStreamBudget streamBudget;
            streamBudget.contractVersion = 1u;
            streamBudget.prototypeOptIn = true;
            streamBudget.maxCommands = commands.size();
            streamBudget.maxReferences = refs.size();
            streamBudget.maxDescriptors = descriptors.size();
            streamBudget.maxRenderPasses = streamBudget.maxAttachmentReferences = 2u;
            streamBudget.maxPipelineBinds = streamBudget.maxConsumptions = hasDepth ? 3u : 2u;
            streamBudget.maxCopyElements = 1u;
            streamBudget.maxImplicitAccesses = accesses.accesses;
            streamBudget.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
            streamBudget.maxCpuMetadataBytes = bytes;
            streamBudget.stateSequence = sequence;
            streamBudget.bindlessRoots = state.bindlessRoots;
            auto stream =
                OriginalCommandStreamProgram::prepareOriginalTyped(graph, streamBudget, q[3], id);
            require(id);
            assert(stream);
            auto tx = Tx::prepareOriginalOwnership(graph, device, state, q[4], id);
            require(id);
            assert(tx);
            const auto depthCells =
                hasDepth ? mixedDepthRoot->originalCompletedDepthAt(0u)->cells().size() : 0u;
            // One recording-order scan plus one authored-stream scan per content
            // cell. WebGPU's completed depth source has two cells, Native one.
            OriginalTextureContentPlanBudget content{
                hasDepth ? (Backend::callback ? 16u : 15u) : (Backend::callback ? 14u : 13u),
                true,
                hasDepth ? 2u : 1u,
                0u,
                3u + depthCells,
                20u,
                commands.size() * (4u + depthCells),
                32768u};
            const auto projected = tx->preflightOriginalContentCells(content);
            require(projected.diagnostic);
            content.maxContentControls = projected.controls;
            content.maxContentCells = projected.cells;
            content.maxContentSteps = projected.steps;
            content.maxCommandVisits = projected.commandVisits;
            content.maxProjectionVisits = projected.projectionVisits;
            if (hasDepth && planningOnly) {
                const auto refused = [&](auto denied) {
                    const auto baseline = q[4]->liveBytes();
                    const auto result = tx->preflightOriginalContentCells(denied);
                    assert(result.diagnostic && !result.controls && !result.cells && !result.steps
                        && !result.commandVisits && !result.projectionVisits);
                    assert(tx->materializeOriginalPrototypePlan(sequence, denied));
                    assert(q[4]->liveBytes() == baseline && tx->journalEmpty()
                        && !tx->hasOriginalPrototypePlan());
                };
                auto less = content;
                --less.maxContentControls;
                refused(less);
                less = content;
                --less.maxContentCells;
                refused(less);
                less = content;
                --less.maxContentSteps;
                refused(less);
                less = content;
                --less.maxCommandVisits;
                refused(less);
                less = content;
                --less.maxProjectionVisits;
                refused(less);
                less = content;
                less.contractVersion = Backend::callback ? 14u : 13u;
                refused(less);
                less = content;
                less.contractVersion = Backend::callback ? 15u : 16u;
                refused(less);
                assert(projected.controls == 2u && projected.cells == 3u + depthCells);
                const auto prepare = [&] {
                    auto candidate = Tx::prepareOriginalOwnership(graph, device, state, q[4], id);
                    if (candidate
                        && (id = candidate->materializeOriginalPrototypePlan(sequence, content)))
                        candidate.reset();
                    return candidate;
                };
                tx.reset();
                CpuQuotaFactoryFixture pressure(q[4]);
                const auto peak = pressure.measure(prepare);
                pressure.verify(peak, prepare);
                tx = prepare();
                require(id);
                assert(tx);
                const auto proof = mixedDepthRoot->originalCompletedDepthAt(0u);
                assert(proof);
                require(proof->validate());
                // Visible mips can have independent completed generations; preserve
                // each original cell expectation rather than inventing a uniform
                // generation for the whole image.
                const auto observations = proof->cells();
                assert(observations.size() == depthCells);
                for (const auto backend : {rhi::Backend::Null,
                         rhi::Backend::Vulkan,
                         rhi::Backend::Metal,
                         rhi::Backend::WebGPU})
                    assert(tx->executionDiagnostic(backend));
                require(
                    CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                        if (const auto* diagnostic = tx->applyOriginalPrototypePlan())
                            return diagnostic;
                        if (const auto* diagnostic = proof->validate())
                            return diagnostic;
                        if (const auto* diagnostic = tx->validate())
                            return diagnostic;
                        assert(tx->commitOriginalPendingOwnership());
                        return tx->abandonOriginalPendingOwnership();
                    }));
                require(proof->validate());
                for (const auto& observation : observations)
                    require(proof->originalViewSource()
                            ->originalParentLease()
                            ->originalObject()
                            ->validateOriginalContent(observation));
                assert(tx->journalEmpty() && tx->status() == Tx::Status::Abandoned);
                std::printf(
                    "BINDLESS_%s_MIXED_CONTENT%u_PLAN_OK controls2 cells%llu steps%llu commands%zu entries%zu exact-less quota-rollback depth-read-only hot0 no-execution\n",
                    Backend::marker,
                    content.contractVersion,
                    static_cast<unsigned long long>(projected.cells),
                    static_cast<unsigned long long>(projected.steps),
                    commands.size(),
                    Entries);
                return;
            }
            require(tx->materializeOriginalPrototypePlan(sequence, content));
            if constexpr (!planningOnly) {
                auto request = BufferReadbackRequestDeclaration::prepareOriginal(readback,
                    device,
                    {1u, 1u, snapshot->generation()},
                    0u,
                    4u,
                    {1u, true, 4u, bytes},
                    q[1],
                    id);
                require(id);
                assert(request);
                auto result = OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                    request, q[5], id);
                require(id);
                assert(result);
                auto binding = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                    stream,
                    request,
                    result,
                    copy,
                    sourceIndex(roots[0]),
                    device,
                    {2u,
                        true,
                        OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                        commands.size(),
                        bytes},
                    q[1],
                    id);
                require(id);
                assert(binding);
                auto capacity = core::makeShared<core::AtomicCapacityBudget>(2u);
                using Inputs = std::conditional_t<Backend::callback,
                    OriginalBindlessRootExecutionInputs,
                    OriginalBindlessRootFenceExecutionInputs>;
                using Budget = std::conditional_t<Backend::callback,
                    OriginalBindlessRootExecutionBudget,
                    OriginalBindlessRootFenceExecutionBudget>;
                Inputs executionInputs;
                executionInputs.root = roots[0];
                executionInputs.nextRoot = roots[1];
                executionInputs.inventory = inventory;
                if constexpr (mixed)
                    executionInputs.finalRoot = roots[2];
                executionInputs.transaction = tx;
                executionInputs.readback = binding;
                executionInputs.submissionQuota = q[7];
                if constexpr (Backend::callback) {
                    executionInputs.nativeQuota = q[6];
                    executionInputs.callbackQuota = q[8];
                } else {
                    executionInputs.closureQuota = q[6];
                    executionInputs.viewQuota = q[8];
                    executionInputs.attachmentCapacity = capacity;
                }
                const auto closureEntries = roots[0]->elements().size() + (readback ? 1u : 0u)
                    + (Backend::callback ? attachments.size() : 0u);
                OriginalBindlessRootNativeClosureBudget closure{
                    mixed ? (Backend::callback ? 15u : 14u) : (Backend::callback ? 12u : 11u),
                    true,
                    static_cast<std::uint32_t>(closureEntries),
                    256u,
                    512u,
                    bytes};
                closure.maxAttachmentViews = 2u;
                if constexpr (mixed) {
                    const auto proof = mixedDepthRoot->originalCompletedDepthAt(0u);
                    std::uint64_t texels = 0u;
                    require(semantics::TextureUploadDestinationValidation::footprint(
                        proof->originalViewSource()
                            ->originalParentLease()
                            ->originalObject()
                            ->desc(),
                        texels));
                    closure.completedDepthInputs = {Backend::callback ? 1u : 2u,
                        true,
                        depthCells,
                        texels,
                        OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
                }
                Budget budget;
                if constexpr (Backend::callback) {
                    const auto operations = stream->operations().size();
                    budget = {{executionVersion, true},
                        closure,
                        {mixed ? 5u : 4u,
                            true,
                            operations,
                            32768u,
                            512u,
                            bytes,
                            {1u, true, 1u, 1u, 1u, 4u, 4u, bytes},
                            {1u, true, 1u, 1u, 3u, 12u, 12u}},
                        {mixed ? 18u : 16u,
                            true,
                            1u,
                            12u,
                            12u,
                            512u,
                            512u,
                            512u,
                            bytes,
                            bytes,
                            bytes},
                        {3u, true, 1u, 4u, bytes, 512u},
                        {2u, true, 512u, bytes, {mixed ? 5u : 4u, true, operations, 32768u}},
                        {1u, true, 1u, 12u, 512u, bytes, bytes},
                        {1u, true, 1u, 1u, 512u, bytes, bytes, bytes}};
                } else
                    budget = {{executionVersion, true},
                        closure,
                        {mixed ? 17u : 15u,
                            true,
                            1u,
                            0u,
                            0u,
                            tx->retainedOwnerCount() + 2u,
                            4096u,
                            0u,
                            bytes,
                            bytes,
                            0u},
                        {mixed ? 9u : 8u, true, 1u, 0u, 0u},
                        bytes,
                        512u};
                budget.requireWorkloadRetention = true;
                if constexpr (mixed)
                    budget.mixedEntries = {1u, true, declarations};
                if constexpr (mixed && Backend::callback)
                    std::printf(
                        "BINDLESS_WEBGPU_PUBLIC_MIXED_EXECUTION_BEGIN terminal=%u fault=%d\n",
                        terminal,
                        fault);
                auto* extension = static_cast<BindlessCrossExtension*>(
                    device.queryInterface(kExtensionBindlessCross));
                assert(extension);
                core::pmr::String diagnostic;
                diagnostic.reserve(512u);
                {
                    std::uint32_t native = 0u, owners = 0u;
                    for (const auto& root : roots) {
                        auto c = Backend::Closure::prepareOriginal(
                            root, inventory, device, budget.closure, q[6], id);
                        require(id);
                        assert(c);
                        native = std::max(native, c->nativeOwnerCount());
                        owners = std::max(owners, c->retainedOwnerCount());
                    }
                    budget.closure.maxNativeOwners = native;
                    budget.closure.maxRetainedOwners = owners;
                }
                assert(!q[6]->liveBytes());
                unsigned refusalCase = 0u;
                const auto refuse = [&](const auto& in, const auto& b) {
                    ++refusalCase;
                    std::array<std::uint64_t, 3u> before{
                        q[6]->liveBytes(), q[7]->liveBytes(), q[8]->liveBytes()};
                    auto denied =
                        extension->prepareOriginalRootExecution(device, in, b, diagnostic);
                    if (denied || diagnostic.empty())
                        std::fprintf(stderr,
                            "Storage refusal %u accepted=%d diagnostic=%s native=%u owners=%u\n",
                            refusalCase,
                            bool(denied),
                            diagnostic.c_str(),
                            b.closure.maxNativeOwners,
                            b.closure.maxRetainedOwners);
                    assert(!denied && !diagnostic.empty());
                    assert(tx->status() == Tx::Status::Prepared && tx->journalEmpty()
                        && !result->publishedBytes() && !capacity->used());
                    for (unsigned i = 0u; i != 3u; ++i)
                        assert(q[i + 6u]->liveBytes() == before[i]);
                };
                auto wrongInputs = executionInputs;
                std::swap(wrongInputs.root, wrongInputs.nextRoot);
                refuse(wrongInputs, budget);
                wrongInputs = executionInputs;
                wrongInputs.nextRoot = wrongInputs.root;
                refuse(wrongInputs, budget);
                auto less = budget;
                --less.closure.maxResourceEntries;
                refuse(executionInputs, less);
                less = budget;
                --less.closure.maxNativeOwners;
                refuse(executionInputs, less);
                less = budget;
                --less.closure.maxRetainedOwners;
                refuse(executionInputs, less);
                less = budget;
                less.lifecycle.contractVersion = 14u;
                refuse(executionInputs, less);
                if constexpr (!Backend::callback) {
                    less = budget;
                    less.capacity.contractVersion = 7u;
                    refuse(executionInputs, less);
                } else {
                    less = budget;
                    less.command.program.contractVersion = 3u;
                    refuse(executionInputs, less);
                }
                {
                    core::CpuMemoryQuotaLease pressure(q[6], bytes);
                    assert(pressure.charged());
                    refuse(executionInputs, budget);
                }
                auto execution = extension->prepareOriginalRootExecution(
                    device, executionInputs, budget, diagnostic);
                if (!execution)
                    std::fprintf(stderr, "Storage public preparation: %s\n", diagnostic.c_str());
                assert(execution);
                if constexpr (Backend::callback)
                    Backend::beforeRecord(roots[0], terminal);
                require(OriginalBindlessRootExecution::record(execution, {executionVersion, true}));
                if constexpr (Backend::callback) {
#if defined(__EMSCRIPTEN__)
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return execution->recordingProven() || execution->executionDiagnostic();
                    }));
                    Backend::afterRecord(terminal);
                    if (terminal != 5u) {
                        if (execution->executionDiagnostic())
                            std::fprintf(
                                stderr, "Storage record: %s\n", execution->executionDiagnostic());
                        assert(execution->recordingProven());
                    }
#endif
                }
                if constexpr (Backend::callback) {
                    if (terminal == 5u) {
                        assert(execution->executionDiagnostic() && !execution->pendingRecords()
                            && !result->publishedBytes() && tx->journalEmpty()
                            && (tx->status() == Tx::Status::Abandoned
                                || tx->status() == Tx::Status::Invalidated));
                        execution.reset();
                    }
                }
                if (execution) {
                    assert(tx->status() == Tx::Status::Pending && !result->publishedBytes());
                    if constexpr (mixed)
                        require(mixedDepthRoot->originalCompletedDepthAt(0u)->validate());
                    if (terminal == 3u || terminal == 4u) {
                        if (terminal == 4u) {
                            device.retireOriginalDeviceLifetime();
                            assert(OriginalBindlessRootExecution::submit(
                                execution, {executionVersion, true}));
                        }
                        require(execution->cancelExecution());
                        execution.reset();
                        assert(!result->publishedBytes());
                        if (terminal == 3u)
                            assert(tx->status() == Tx::Status::Abandoned);
                    } else {
                        if constexpr (Backend::callback)
                            Backend::complete(device,
                                execution,
                                result,
                                q,
                                capacity,
                                snapshot,
                                terminal,
                                diagnostic,
                                roots[0],
                                executionVersion);
                        else
                            Backend::complete(device,
                                execution,
                                result,
                                q,
                                capacity,
                                snapshot,
                                terminal,
                                diagnostic,
                                executionVersion);
                        // The existing callback issuer cancels publication when its caller
                        // drops custody. Native fence issuers retain their completion view.
                        const bool published =
                            Backend::callback ? (terminal == 0u || terminal == 6u) : terminal != 2u;
                        assert(tx->status()
                            == (published ? Tx::Status::Committed : Tx::Status::Invalidated));
                        assert(bool(result->publishedBytes()) == published);
                        if (execution) {
                            const auto deadline =
                                std::chrono::steady_clock::now() + std::chrono::seconds(15);
                            while (execution->pendingRecords()) {
                                assert(std::chrono::steady_clock::now() < deadline);
#if defined(__EMSCRIPTEN__)
                                emscripten_sleep(1);
#else
                                std::this_thread::yield();
#endif
                            }
                            assert(execution->submissionCompleted() == published);
                        }
                        if (published) {
                            std::uint32_t bits = ~0u;
                            assert(result->publishedBytes());
                            std::memcpy(&bits, result->publishedBytes(), 4u);
                            assert(bits == std::uint32_t(fault));
                            if constexpr (!mixed) {
                                const auto& artifact =
                                    roots[1]->authorization()->package().artifact();
                                const auto table = std::find_if(artifact.tables.begin(),
                                    artifact.tables.end(),
                                    [](const auto& t) {
                                        return t.tableId == 3u;
                                    });
                                assert(table != artifact.tables.end() && table->capacity == 2u);
                                unsigned outputs = 0u;
                                for (const auto& element : roots[1]->elements()) {
                                    const auto& write = element.declaration.write;
                                    const auto index =
                                        artifact.execution == BindlessExecution::Native
                                        ? write.arrayElement
                                        : write.binding - table->firstBinding;
                                    if (write.type != DescriptorType::StorageBuffer
                                        || write.binding < table->firstBinding
                                        || index >= table->capacity
                                        || (artifact.execution == BindlessExecution::Native
                                            && write.binding != table->firstBinding))
                                        continue;
                                    assert(!(outputs & (1u << index)));
                                    outputs |= 1u << index;
                                    const auto& lease =
                                        std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                            element.owner);
                                    std::array<float, 4u> values{};
                                    assert(Backend::readBuffer(
                                        *lease->originalObject(), values, diagnostic));
                                    assert(values[0] == .75f && values[1] == .125f
                                        && values[2] == float(index) + .25f && values[3] == 1.f);
                                }
                                assert(outputs == 3u);
                            }
                            const auto packed = Backend::readPixels(*parent->originalObject());
                            if constexpr (mixed) {
                                std::array<std::uint16_t, 20u> expected;
                                for (unsigned pixel = 0u; pixel < 4u; ++pixel) {
                                    const auto nearest = core::floatToHalf(mixedDepthValues[pixel]);
                                    const auto towardZero =
                                        core::floatToHalf(mixedDepthValues[pixel],
                                            core::HalfFloatRounding::TowardZero);
                                    const auto half = packed[pixel * 4u];
                                    if (half != nearest && half != towardZero)
                                        std::fprintf(stderr,
                                            "Mixed copy[%u] actual=%u nearest=%u toward-zero=%u\n",
                                            pixel,
                                            unsigned(half),
                                            unsigned(nearest),
                                            unsigned(towardZero));
                                    assert(half == nearest || half == towardZero);
                                    for (unsigned channel = 0u; channel < 4u; ++channel)
                                        expected[pixel * 4u + channel] =
                                            channel == 3u ? 0x3c00u : half;
                                }
                                // Storage conversion permits nearest-even or toward-zero.
                                // The reduce pass must consume the exact independently read
                                // copy values; its result and eight Results lanes stay bit exact.
                                const std::array<std::uint16_t, 4u> copied{
                                    expected[0u], expected[4u], expected[8u], expected[12u]};
                                const auto range =
                                    std::minmax_element(copied.begin(), copied.end());
                                expected[16u] = expected[18u] = *range.second;
                                expected[17u] = *range.first;
                                expected[19u] = 0x3c00u;
                                if (packed != expected) {
                                    for (unsigned lane = 0u; lane < packed.size(); ++lane)
                                        std::fprintf(stderr,
                                            "Mixed half[%u] actual=%u expected=%u\n",
                                            lane,
                                            unsigned(packed[lane]),
                                            unsigned(expected[lane]));
                                }
                                assert(packed == expected);
                                const auto flow = OriginalBindlessMixedEntryFlowContract::inspect(
                                    roots, inventory, budget.mixedEntries);
                                require(flow.diagnostic);
                                const auto& output =
                                    std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                                        inventory->sources()[flow.resultsSource]);
                                std::array<float, 8u> values{};
                                assert(Backend::readBuffer(
                                    *output->originalObject(), values, diagnostic));
                                for (unsigned lane = 0u; lane < values.size(); ++lane)
                                    assert(values[lane]
                                        == core::halfToFloat(
                                            expected[(lane < 4u ? 0u : 16u) + lane % 4u]));
                                require(mixedDepthRoot->originalCompletedDepthAt(0u)->validate());
                                std::printf(
                                    "BINDLESS_%s_PUBLIC_MIXED_NUMERIC terminal=%u fault=%d half",
                                    Backend::marker,
                                    terminal,
                                    fault);
                                for (auto value : packed)
                                    std::printf(" %u", unsigned(value));
                                std::puts("");
                            } else {
                                for (unsigned i = 0u; i != 5u; ++i) {
                                    const auto expected = i == 0u
                                        ? std::array<std::uint16_t, 4u>{0x3a00u,
                                              0x3000u,
                                              0x3400u,
                                              0x3c00u}
                                        : i == 4u ? std::array<std::uint16_t, 4u>{0x3a00u,
                                                        0x3000u,
                                                        0x3d00u,
                                                        0x3c00u}
                                                  : std::array<std::uint16_t, 4u>{
                                                        0x2c00u, 0x3400u, 0x3800u, 0x3c00u};
                                    assert(std::equal(
                                        expected.begin(), expected.end(), packed.begin() + i * 4u));
                                }
                                if constexpr (Backend::callback) {
                                    std::printf(
                                        "BINDLESS_WEBGPU_PUBLIC_STORAGE_NUMERIC terminal=%u fault=%d half",
                                        terminal,
                                        fault);
                                    for (auto value : packed)
                                        std::printf(" %u", static_cast<unsigned>(value));
                                    std::puts("");
                                }
                            }
                            char name[96u];
                            std::snprintf(name,
                                sizeof(name),
                                "%s-public-%s%u-fault%d.rgba16f",
                                Backend::label,
                                mixed ? "mixed" : "storage",
                                executionVersion,
                                fault);
                            auto* file = std::fopen(name, "wb");
                            assert(file);
                            assert(std::fwrite(packed.data(), 1u, sizeof(packed), file)
                                == sizeof(packed));
                            assert(!std::fclose(file));
                            for (unsigned i = 0u; i != 3u; ++i) {
                                const auto cell = parent->originalObject()->observeOriginalContent(
                                    {i, 1u, 0u, 1u, TextureAspect::Color});
                                assert(cell);
                                assert(cell->state()
                                    == (i ? common::OriginalTextureContentControl::State::Completed
                                          : common::OriginalTextureContentControl::State::
                                                Uninitialized));
                                // Observations remain metadata; only the genuine command seal
                                // and its completion can prove execution, including after commit.
                                assert(bool(cell->contentGeneration()) == bool(i)
                                    && !cell->nativeExecutionProven());
                            }
                        }
                        execution.reset();
                    }
                }
                assert(!capacity->used() && !snapshot->originalSubmissionBudgetOwner()->used());
                std::printf(
                    "BINDLESS_%s_PUBLIC_%s_EXECUTION%u terminal=%u fault=%d clear-store2 dispatch%zu same-journal Content%u Completion%u hot-submit0 hot-completion0 retirement0\n",
                    Backend::marker,
                    mixed ? "MIXED" : "STORAGE",
                    executionVersion,
                    terminal,
                    fault,
                    Entries,
                    content.contractVersion,
                    executionVersion);
            }
        }
        for (unsigned i = 0u; i != q.size(); ++i)
            assert(q[i]->liveBytes() == (i ? 0u : sourceBaseline));
    }
};
} // namespace arp::test
