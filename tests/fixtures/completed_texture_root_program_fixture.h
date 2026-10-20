#pragma once
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/frontend/command_texture_ranges.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class CompletedTextureRootProgramFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Completed color program: %s\n", id);
        assert(!id);
    }

public:
    using CompletedInputs =
        core::pmr::Vector<std::pair<const rhi::GpuTexture*, rhi::GpuTexture::ContentExpectation>>;

    template <class Emit>
    static CompletedInputs appendCompletedReads(
        const rhi::OriginalCommandResourceSources& inventory,
        const rhi::GpuTexture* excluded,
        rhi::PipelineStage stages,
        Emit&& emit)
    {
        using namespace rhi;
        CompletedInputs inputs;
        for (const auto& source : inventory.sources())
            if (const auto* lease =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(&source)) {
                const auto& object = *(*lease)->originalObject();
                if (&object == excluded)
                    continue;
                const auto content = object.observeOriginalContent();
                assert(content);
                inputs.emplace_back(&object, *content);
                const auto range = defaultTextureSubresourceRange(object.desc());
                const auto state = object.observeOriginalCommittedState(range);
                assert(state);
                if (state->state() == ResourceState::ShaderRead)
                    continue;
                assert(state->state() == ResourceState::CopyDst);
                ResourceBarrierDesc barrier{
                    object.id(), range, ResourceState::CopyDst, ResourceState::ShaderRead};
                barrier.sourceStages = PipelineStage::Transfer;
                barrier.sourceAccess = ResourceAccess::TransferWrite;
                barrier.destinationStages = stages;
                barrier.destinationAccess = ResourceAccess::ShaderRead;
                emit(*lease, barrier);
            }
        return inputs;
    }

    struct Prepared {
        std::shared_ptr<const rhi::OriginalCommandInputs> inputs;
        std::shared_ptr<const rhi::OriginalCommandResourceStates> graph;
        std::shared_ptr<rhi::OriginalCommandStateTransaction> tx;
        std::shared_ptr<const rhi::OriginalCommandStreamProgram> program;
        std::shared_ptr<const rhi::BufferReadbackRequestDeclaration> request;
        std::shared_ptr<const rhi::OriginalCommandBufferReadbackResultSnapshot> result;
        std::shared_ptr<const rhi::OriginalCommandBufferReadbackBinding> binding;
        core::pmr::Vector<std::pair<const rhi::GpuTexture*, rhi::GpuTexture::ContentExpectation>>
            completedInputs;
        std::size_t textureCount = 0u, commandCount = 0u;
    };

    template <std::size_t N>
    static Prepared prepare(
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, N>& q,
        std::uint32_t dispatchX,
        std::uint64_t cpuBytes)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        const auto& snapshot = root->originalSnapshot();
        const auto& latch =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        const auto& readback =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
        const auto sourceIndex = [&](const auto& lease) {
            const auto index = inventory->originalLeaseIndex(lease);
            assert(index != SIZE_MAX);
            return static_cast<std::uint32_t>(index);
        };
        core::pmr::Vector<CommandUsage> commands;
        core::pmr::Vector<OriginalCommandUsage::Reference> refs;
        core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
        const auto append = [&](K kind, std::initializer_list<std::uint32_t> operands) {
            const auto command = commands.size();
            commands.push_back({kind,
                static_cast<std::uint32_t>(refs.size()),
                static_cast<std::uint32_t>(operands.size())});
            for (const auto index : operands)
                refs.push_back({index});
            return command;
        };
        const auto bufferBarrier = [&](ResourceState before,
                                       ResourceState after,
                                       PipelineStage src,
                                       ResourceAccess access,
                                       PipelineStage dst,
                                       ResourceAccess next) {
            ResourceBarrierDesc barrier{latch->resource(), {}, before, after};
            barrier.sourceStages = src;
            barrier.sourceAccess = access;
            barrier.destinationStages = dst;
            barrier.destinationAccess = next;
            descriptors.push_back({append(K::BufferBarrier, {sourceIndex(latch)}),
                *ResourceDependencyArguments::capture(K::BufferBarrier, barrier)});
        };
        auto completedInputs = appendCompletedReads(*inventory,
            nullptr,
            PipelineStage::ComputeShader,
            [&](const auto& parent, const auto& barrier) {
                descriptors.push_back({append(K::TextureBarrier, {sourceIndex(parent)}),
                    *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)});
            });
        const auto textureCount = completedInputs.size();
        const auto rootIndex = sourceIndex(root);
        bufferBarrier(ResourceState::ShaderWrite,
            ResourceState::CopyDst,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite);
        append(K::ClearSnapshotFault, {rootIndex});
        bufferBarrier(ResourceState::CopyDst,
            ResourceState::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
        append(K::BeginComputePass, {});
        append(K::BindPreparedComputeBinding, {rootIndex});
        const auto invocation = append(K::Dispatch, {});
        append(K::EndComputePass, {});
        bufferBarrier(ResourceState::ShaderWrite,
            ResourceState::CopySrc,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead);
        append(K::BeginTransferPass, {});
        const auto copy = append(K::BufferCopy, {sourceIndex(latch), sourceIndex(readback)});
        descriptors.push_back({copy,
            *TransferCopyArguments::capture(
                BufferCopyDesc{latch->resource(), readback->resource(), 0u, 0u, 4u})});
        append(K::EndTransferPass, {});
        bufferBarrier(ResourceState::CopySrc,
            ResourceState::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead,
            PipelineStage::ComputeShader,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
        append(K::Finish, {});
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        for (const auto& command : commands) {
            const auto scalar = command.kind == K::Dispatch
                ? CommandScalarArguments::capture(
                      K::Dispatch, CommandScalarArguments::Dispatch{dispatchX, 1u, 1u})
                : CommandScalarArguments::capture(command.kind, CommandScalarArguments::None{});
            assert(scalar);
            fields.push_back({*scalar});
        }
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
        OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 64u, 16u, 256u, cpuBytes};
        state.bindlessRoots = {4u, true, 3u, root->size() + 1u, 4096u, 1u, 1u};
        const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
            inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        require(projected.diagnostic);
        state.bindlessRoots.maxSourceVisits = projected.sourceVisits;
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, *device, state, q[1], id);
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
        const auto inspected = common::OriginalCommandAccessValidation::inspect(graph, sequence);
        require(inspected.diagnostic);
        sequence.maxSteps = inspected.steps;
        auto tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, *device, state, q[2], id);
        require(id);
        assert(tx);
        const OriginalTextureContentPlanBudget content{8u,
            true,
            static_cast<std::uint32_t>(textureCount),
            0u,
            0u,
            textureCount,
            commands.size(),
            4096u};
        require(tx->materializeOriginalPrototypePlan(sequence, content));
        OriginalCommandStreamBudget sb;
        sb.contractVersion = 1u;
        sb.prototypeOptIn = true;
        sb.maxCommands = commands.size();
        sb.maxReferences = refs.size();
        sb.maxDescriptors = descriptors.size();
        sb.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        sb.maxCpuMetadataBytes = cpuBytes;
        sb.maxPipelineBinds = sb.maxConsumptions = sb.maxCopyElements = 1u;
        sb.maxImplicitAccesses = projected.accesses;
        sb.stateSequence = sequence;
        sb.bindlessRoots = state.bindlessRoots;
        auto program = OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, q[3], id);
        require(id);
        assert(program);
        auto request = BufferReadbackRequestDeclaration::prepareOriginal(readback,
            *device,
            {1u, 1u, snapshot->generation()},
            0u,
            4u,
            {1u, true, 4u, cpuBytes},
            q[0],
            id);
        require(id);
        assert(request);
        auto result =
            OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(request, q[4], id);
        require(id);
        assert(result);
        auto binding = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
            program,
            request,
            result,
            copy,
            rootIndex,
            *device,
            {2u,
                true,
                OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                commands.size(),
                cpuBytes},
            q[0],
            id);
        require(id);
        assert(binding);
        return {std::move(inputs),
            std::move(graph),
            std::move(tx),
            std::move(program),
            std::move(request),
            std::move(result),
            std::move(binding),
            std::move(completedInputs),
            textureCount,
            commands.size()};
    }
};
} // namespace arp::test
