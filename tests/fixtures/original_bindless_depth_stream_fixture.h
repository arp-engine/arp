#pragma once
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/frontend/command_state.h"

namespace arp::test {
// Same inventory, graph, stream and journal as the public depth Root.
class OriginalBindlessDepthStreamFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Original depth stream: %s\n", id);
        assert(!id);
    }

public:
    // Optional depth transition plus the fixed fault/Compute/copy grammar below.
    static constexpr std::uint32_t maxCommands = 14u, maxReferences = 9u, maxDescriptors = 6u;

    template <class Execute>
    static void execute(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice& device,
        const Execute& observe,
        std::uint32_t contentVersion = 11u,
        std::uint64_t cells = 2u)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        const auto bytes = inventory->originalMetadataQuota()->limitBytes();
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            const auto rootIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(root));
            const auto& snapshot = root->originalSnapshot();
            const auto& latch =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
            const auto& readback =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
            core::pmr::Vector<CommandUsage> commands;
            core::pmr::Vector<OriginalCommandUsage::Reference> refs;
            core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
            const auto append = [&](K kind, std::initializer_list<std::uint32_t> operands) {
                const auto index = commands.size();
                commands.push_back({kind,
                    static_cast<std::uint32_t>(refs.size()),
                    static_cast<std::uint32_t>(operands.size())});
                for (const auto operand : operands)
                    refs.push_back({operand});
                return index;
            };
            const auto bufferBarrier = [&](ResourceState before,
                                           ResourceState after,
                                           PipelineStage src,
                                           ResourceAccess access,
                                           PipelineStage dst,
                                           ResourceAccess next) {
                ResourceBarrierDesc b{latch->resource(), {}, before, after};
                b.sourceStages = src;
                b.sourceAccess = access;
                b.destinationStages = dst;
                b.destinationAccess = next;
                descriptors.push_back(
                    {append(K::BufferBarrier,
                         {static_cast<std::uint32_t>(inventory->originalLeaseIndex(latch))}),
                        *ResourceDependencyArguments::capture(K::BufferBarrier, b)});
            };
            const auto& parent = std::get<std::shared_ptr<const OriginalCommandTextureLease>>(
                inventory->sources()[0]);
            const auto range = defaultTextureSubresourceRange(parent->originalObject()->desc());
            const auto captured = parent->originalObject()->observeOriginalCommittedState(range);
            assert(captured);
            if (captured->state() != ResourceState::ShaderRead) {
                ResourceBarrierDesc b{
                    parent->resource(), range, captured->state(), ResourceState::ShaderRead};
                b.sourceStages = captured->state() == ResourceState::CopySrc
                    ? PipelineStage::Transfer
                    : PipelineStage::DepthStencil;
                b.sourceAccess = captured->state() == ResourceState::CopySrc
                    ? ResourceAccess::TransferRead
                    : ResourceAccess::DepthStencilRead | ResourceAccess::DepthStencilWrite;
                b.destinationStages = PipelineStage::ComputeShader;
                b.destinationAccess = ResourceAccess::ShaderRead;
                descriptors.push_back({append(K::TextureBarrier, {0u}),
                    *ResourceDependencyArguments::capture(K::TextureBarrier, b)});
            }
            constexpr auto shader = PipelineStage::ComputeShader;
            const auto rw = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            bufferBarrier(ResourceState::ShaderWrite,
                ResourceState::CopyDst,
                shader,
                rw,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite);
            append(K::ClearSnapshotFault, {rootIndex});
            bufferBarrier(ResourceState::CopyDst,
                ResourceState::ShaderWrite,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite,
                shader,
                rw);
            append(K::BeginComputePass, {});
            append(K::BindPreparedComputeBinding, {rootIndex});
            append(K::Dispatch, {});
            append(K::EndComputePass, {});
            bufferBarrier(ResourceState::ShaderWrite,
                ResourceState::CopySrc,
                shader,
                rw,
                PipelineStage::Transfer,
                ResourceAccess::TransferRead);
            append(K::BeginTransferPass, {});
            const auto faultCopy = append(K::BufferCopy,
                {static_cast<std::uint32_t>(inventory->originalLeaseIndex(latch)),
                    static_cast<std::uint32_t>(inventory->originalLeaseIndex(readback))});
            descriptors.push_back({faultCopy,
                *TransferCopyArguments::capture(
                    BufferCopyDesc{latch->resource(), readback->resource(), 0u, 0u, 4u})});
            append(K::EndTransferPass, {});
            bufferBarrier(ResourceState::CopySrc,
                ResourceState::ShaderWrite,
                PipelineStage::Transfer,
                ResourceAccess::TransferRead,
                shader,
                rw);
            append(K::Finish, {});
            assert(commands.size() <= maxCommands && refs.size() <= maxReferences
                && descriptors.size() <= maxDescriptors);
            core::pmr::Vector<CommandVariableArguments::Input> fields;
            for (const auto& c : commands)
                fields.push_back(
                    {*(c.kind == K::Dispatch ? CommandScalarArguments::capture(c.kind,
                                                   CommandScalarArguments::Dispatch{1u, 1u, 1u})
                                             : CommandScalarArguments::capture(
                                                   c.kind, CommandScalarArguments::None{}))});
            const char* id = nullptr;
            auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
                commands.data(),
                commands.size(),
                refs.data(),
                refs.size(),
                fields.data(),
                descriptors.data(),
                descriptors.size(),
                {1u, false},
                q[0],
                id);
            require(id);
            assert(inputs);
            OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 64u, 16u, 256u, bytes};
            state.bindlessRoots = {4u, true, 3u, root->size() + 1u, 8192u, 1u, 1u};
            const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
                inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            require(projected.diagnostic);
            state.bindlessRoots.maxSourceVisits = projected.sourceVisits;
            auto graph =
                OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[1], id);
            require(id);
            assert(graph);
            OriginalCommandStateSequenceBudget sequence{1u,
                true,
                static_cast<std::uint32_t>(commands.size()),
                0u,
                256u,
                256u,
                projected.accesses,
                4u,
                true};
            const auto accesses = common::OriginalCommandAccessValidation::inspect(graph, sequence);
            require(accesses.diagnostic);
            sequence.maxStateCells = accesses.stateCells;
            sequence.maxSteps = accesses.steps;
            OriginalCommandStreamBudget sb;
            sb.contractVersion = 1u;
            sb.prototypeOptIn = true;
            sb.maxCommands = commands.size();
            sb.maxReferences = refs.size();
            sb.maxDescriptors = descriptors.size();
            sb.maxPipelineBinds = sb.maxConsumptions = sb.maxCopyElements = 1u;
            sb.maxImplicitAccesses = projected.accesses;
            sb.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
            sb.maxCpuMetadataBytes = bytes;
            sb.stateSequence = sequence;
            sb.bindlessRoots = state.bindlessRoots;
            auto stream = OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, q[2], id);
            require(id);
            assert(stream);
            auto tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
                graph, device, state, q[3], id);
            require(id);
            assert(tx);
            // One command-order visit and one traversal per authentic content cell.
            OriginalTextureContentPlanBudget content{contentVersion,
                true,
                1u,
                0u,
                cells,
                2u * cells,
                commands.size() * (1u + cells),
                8192u};
            const auto inspected = tx->preflightOriginalContentCells(content);
            require(inspected.diagnostic);
            assert(inspected.controls == 1u && inspected.cells == cells
                && inspected.steps == 2u * cells);
            content.maxContentControls = inspected.controls;
            content.maxContentCells = inspected.cells;
            content.maxContentSteps = inspected.steps;
            content.maxCommandVisits = inspected.commandVisits;
            content.maxProjectionVisits = inspected.projectionVisits;
            if (contentVersion == OriginalCompletedDepthRootReadPlanContract::nativeVersion) {
                const auto refuse = [&](const auto& denied) {
                    const auto baseline = q[3]->liveBytes();
                    const auto probe = tx->preflightOriginalContentCells(denied);
                    assert(probe.diagnostic && !probe.controls && !probe.cells && !probe.steps
                        && !probe.commandVisits && !probe.projectionVisits);
                    assert(q[3]->liveBytes() == baseline && !tx->hasOriginalPrototypePlan()
                        && tx->journalEmpty());
                };
                auto less = content;
                --less.maxContentControls;
                refuse(less);
                less = content;
                --less.maxContentCells;
                refuse(less);
                less = content;
                --less.maxContentSteps;
                refuse(less);
                less = content;
                --less.maxCommandVisits;
                refuse(less);
                less = content;
                --less.maxProjectionVisits;
                refuse(less);
                less = content;
                less.contractVersion = 11u;
                refuse(less);
                less = content;
                less.maxUploadCommands = 1u;
                refuse(less);
                less = content;
                less.maxReadbackCommands = 1u;
                refuse(less);
                const auto baseline = q[3]->liveBytes();
                {
                    core::CpuMemoryQuotaLease pressure(q[3], q[3]->limitBytes() - baseline);
                    assert(pressure.charged());
                    assert(tx->materializeOriginalPrototypePlan(sequence, content)
                        && !tx->hasOriginalPrototypePlan() && tx->journalEmpty());
                }
                assert(q[3]->liveBytes() == baseline);
            }
            {
                auto mixed = content;
                mixed.contractVersion = contentVersion == 12u ? 15u : 16u;
                mixed.maxContentControls = 2u;
                mixed.maxContentCells = std::max<std::uint64_t>(mixed.maxContentCells, 2u);
                const auto baseline = q[3]->liveBytes();
                const auto denied = tx->preflightOriginalContentCells(mixed);
                assert(denied.diagnostic
                    && core::StringView(denied.diagnostic)
                        == "RHI-MIXED-DEPTH-STORAGE-CONTENT-COVERAGE"
                    && !denied.controls && !denied.cells && !denied.steps && !denied.commandVisits
                    && !denied.projectionVisits);
                assert(q[3]->liveBytes() == baseline && tx->journalEmpty()
                    && !tx->hasOriginalPrototypePlan());
            }
            require(tx->materializeOriginalPrototypePlan(sequence, content));
            auto request = BufferReadbackRequestDeclaration::prepareOriginal(readback,
                device,
                {1u, 1u, snapshot->generation()},
                0u,
                4u,
                {1u, true, 4u, bytes},
                q[0],
                id);
            require(id);
            assert(request);
            auto result =
                OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(request, q[4], id);
            require(id);
            assert(result);
            auto binding = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                stream,
                request,
                result,
                faultCopy,
                rootIndex,
                device,
                {2u,
                    true,
                    OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                    commands.size(),
                    bytes},
                q[0],
                id);
            require(id);
            assert(binding);
            observe(root, inventory, binding, tx, device);
        }
        for (const auto& quota : q)
            assert(!quota->liveBytes());
    }
};
} // namespace arp::test
