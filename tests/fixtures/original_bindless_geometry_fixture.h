#pragma once
#include "original_bindless_indirect_fixture.h"
#include "original_bindless_root_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_host_geometry_content_fixture.h"
#include "rhi/frontend/bindless_root_native_sequence.h"
#include "rhi/frontend/bindless_root_sequence.h"

namespace arp::test {
// Authored geometry metadata only. Contents/fetch/native execution are separate
// gates; these fixtures never submit the new RootState5 program.
struct OriginalBindlessGeometryDataFixture : OriginalBindlessRootFixtureExecution {
    static constexpr std::uint32_t geometryBufferCount = 3u;
    static constexpr bool tableBufferData = true;
    bool instanceRate = false;
    bool indirectAddresses = false;

    void appendGeometrySources(rhi::GpuDevice& device,
        core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& inputs,
        const rhi::OriginalCommandResourceLeaseBudget& budget,
        const std::shared_ptr<core::CpuMemoryQuota>& quota) const
    {
        using namespace rhi;
        for (const auto& desc :
            std::array<BufferDesc, 3u>{
                {{"root-geometry-vertices",
                     64u,
                     indirectAddresses ? ResourceUsage::VertexBuffer | ResourceUsage::IndirectBuffer
                                       : ResourceUsage::VertexBuffer,
                     MemoryUsage::CpuToGpu,
                     ResourceState::ShaderRead},
                    {"root-geometry-indices",
                        8u,
                        ResourceUsage::IndexBuffer,
                        MemoryUsage::CpuToGpu,
                        ResourceState::ShaderRead},
                    {"root-produced-result",
                        16u,
                        ResourceUsage::CopyDst,
                        MemoryUsage::GpuToCpu,
                        ResourceState::CopyDst}}}) {
            auto object = device.createBuffer(desc);
            assert(object);
            const char* id = nullptr;
            auto lease = OriginalCommandBufferLease::prepareOriginal(object, budget, quota, id);
            assert(lease && !id);
            inputs.push_back(std::move(lease));
        }
    }

    void configureBuffer(rhi::BufferDesc& desc, const rhi::BindlessTableMapping&) const
    {
        desc.usage = desc.usage | rhi::ResourceUsage::CopySrc;
    }

    // CPU metadata fixtures deliberately make no initialized-content claim.
    void initializeBuffer(rhi::GpuBuffer&, const rhi::BindlessTableMapping&, std::uint32_t) const {}

    void configureGeometryPipeline(rhi::RenderPipelineDesc& desc) const
    {
        desc.vertexBuffers = {{0u,
            16u,
            instanceRate ? rhi::VertexInputRate::Instance : rhi::VertexInputRate::Vertex,
            {{0u, 0u, rhi::VertexFormat::Float32x4}}}};
    }
};

class OriginalBindlessGeometryFixture {
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Root Geometry5: %s\n", id);
        assert(!id);
    }

public:
    std::uint32_t bindingOccurrences = 1u;

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        verify(root,
            inventory,
            device,
            [](const auto&, const auto&, const auto&, const auto&, const auto&, const auto&) {});
        if (device) {
            auto repeated = *this;
            repeated.bindingOccurrences = 5u;
            repeated.verify(root,
                inventory,
                device,
                [](const auto&, const auto&, const auto&, const auto&, const auto&, const auto&) {
                });
        }
    }

    template <class InitializedObserver>
    void verify(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device,
        InitializedObserver&& initialized) const
    {
        OriginalHostGeometryContentFixture::verify();
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        using namespace rhi;
        using K = CommandUsageKind;
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas;
        for (auto& q : quotas)
            q = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        std::uint32_t vertexIndex = UINT32_MAX, indexIndex = UINT32_MAX,
                      attachmentIndex = UINT32_MAX;
        for (std::uint32_t i = 0u; i < inventory->sources().size(); ++i) {
            const auto& source = inventory->sources()[i];
            if (const auto* buffer =
                    std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&source)) {
                if (((*buffer)->layout().usage & ResourceUsage::VertexBuffer)
                    != ResourceUsage::None)
                    vertexIndex = i;
                if (((*buffer)->layout().usage & ResourceUsage::IndexBuffer) != ResourceUsage::None)
                    indexIndex = i;
            }
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
                if (((*view)->originalViewLease()->layout().usage & TextureUsage::RenderAttachment)
                    != TextureUsage::None)
                    attachmentIndex = i;
        }
        assert(
            vertexIndex != UINT32_MAX && indexIndex != UINT32_MAX && attachmentIndex != UINT32_MAX);
        const auto rootIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(root));
        const auto& snapshot = root->originalSnapshot();
        const auto& latch =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        const auto& readback =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
        assert(latch && readback);
        const auto latchIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(latch));
        const auto readbackIndex =
            static_cast<std::uint32_t>(inventory->originalLeaseIndex(readback));
        const auto* pipeline =
            OriginalCommandGeometryBindingContract::graphicsPipeline(*inventory, rootIndex, true);
        assert(pipeline
            && !OriginalCommandGeometryBindingContract::graphicsPipeline(*inventory, rootIndex));
        core::pmr::Vector<CommandUsage> commands;
        core::pmr::Vector<OriginalCommandUsage::Reference> references;
        core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
        const auto append = [&](K kind, std::initializer_list<std::uint32_t> operands) {
            const auto ordinal = commands.size();
            commands.push_back({kind,
                static_cast<std::uint32_t>(references.size()),
                static_cast<std::uint32_t>(operands.size())});
            for (const auto index : operands)
                references.push_back({index});
            return ordinal;
        };
        const auto stages = bindlessPipelineStagesForShader(
            std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                root->originalPrepared())
                ->owners()
                ->stages());
        // Genuine Vulkan upload leaves sampled inputs in CopyDst. This cold
        // graph must declare their transition; it cannot assume ShaderRead.
        for (const auto& element : root->elements()) {
            const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &inventory->sources()[element.declaration.sourceIndex]);
            if (!view)
                continue;
            const auto& parent = (*view)->originalParentLease();
            const auto* texture = parent->originalObject();
            const auto range = defaultTextureSubresourceRange(texture->desc());
            const auto observed = texture->observeOriginalCommittedState(range);
            assert(observed);
            if (observed->state() == ResourceState::CopyDst) {
                ResourceBarrierDesc b{
                    texture->id(), range, ResourceState::CopyDst, ResourceState::ShaderRead};
                b.sourceStages = PipelineStage::Transfer;
                b.sourceAccess = ResourceAccess::TransferWrite;
                b.destinationStages = element.declaration.stages;
                b.destinationAccess = element.declaration.access;
                const auto source =
                    static_cast<std::uint32_t>(inventory->originalLeaseIndex(parent));
                descriptors.push_back({append(K::TextureBarrier, {source}),
                    *ResourceDependencyArguments::capture(K::TextureBarrier, b)});
            }
        }
        const auto barrier = [&](ResourceState before,
                                 ResourceState after,
                                 PipelineStage from,
                                 ResourceAccess reads,
                                 PipelineStage to,
                                 ResourceAccess writes) {
            ResourceBarrierDesc b;
            b.resource = latch->resource();
            b.before = before;
            b.after = after;
            b.sourceStages = from;
            b.sourceAccess = reads;
            b.destinationStages = to;
            b.destinationAccess = writes;
            descriptors.push_back({append(K::BufferBarrier, {latchIndex}),
                *ResourceDependencyArguments::capture(K::BufferBarrier, b)});
        };
        barrier(ResourceState::ShaderWrite,
            ResourceState::CopyDst,
            stages,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite);
        append(K::ClearSnapshotFault, {rootIndex});
        barrier(ResourceState::CopyDst,
            ResourceState::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            stages,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
        const auto& attachment = std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            inventory->sources()[attachmentIndex]);
        RenderPassDesc desc;
        RenderPassColorAttachmentDesc color;
        color.target = attachment->resource();
        color.format = TextureFormat::Rgba8Unorm;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        desc.colorAttachments.push_back(color);
        auto pass = RenderPassArguments::prepareOriginal(
            desc, {1u, true, 1u, 0u, 0u, 0u, 0u, bytes}, quotas[0], id);
        require(id);
        assert(pass);
        const auto* attachmentObject = attachment->originalParentLease()->originalObject();
        if (attachmentObject->desc().initialState == ResourceState::Undefined) {
            ResourceBarrierDesc b{attachmentObject->id(),
                defaultTextureSubresourceRange(attachmentObject->desc()),
                ResourceState::Undefined,
                ResourceState::ColorAttachment};
            b.sourceStages = PipelineStage::TopOfPipe;
            b.sourceAccess = ResourceAccess::None;
            b.destinationStages = PipelineStage::ColorAttachmentOutput;
            b.destinationAccess = ResourceAccess::ColorAttachmentWrite;
            const auto parent = static_cast<std::uint32_t>(
                inventory->originalLeaseIndex(attachment->originalParentLease()));
            descriptors.push_back({append(K::TextureBarrier, {parent}),
                *ResourceDependencyArguments::capture(K::TextureBarrier, b)});
        }
        const auto scope = append(K::BeginRenderPass, {attachmentIndex});
        descriptors.push_back({scope, pass});
        assert(bindingOccurrences && bindingOccurrences <= 5u);
        std::size_t epoch = SIZE_MAX;
        for (std::uint32_t i = 0u; i < bindingOccurrences; ++i)
            epoch = append(K::BindPreparedGraphicsBinding, {rootIndex});
        const auto bind = [&](K kind, std::uint32_t source) {
            const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory->sources()[source]);
            const auto ordinal = append(kind, {source});
            descriptors.push_back({ordinal,
                OriginalCommandGeometryBinding{kind,
                    source,
                    rootIndex,
                    epoch,
                    scope,
                    lease->originalObject()->originalResourceRegistration().generation(),
                    0u,
                    snapshot->generation()}});
            return ordinal;
        };
        const auto vertexCommand = bind(K::BindVertexBuffer, vertexIndex);
        const auto indexCommand = bind(K::BindIndexBuffer, indexIndex);
        const auto drawCommand = append(K::DrawIndexed, {});
        append(K::EndRenderPass, {});
        barrier(ResourceState::ShaderWrite,
            ResourceState::CopySrc,
            stages,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead);
        append(K::BeginTransferPass, {});
        const auto copy = append(K::BufferCopy, {latchIndex, readbackIndex});
        descriptors.push_back({copy,
            *TransferCopyArguments::capture(
                BufferCopyDesc{latch->resource(), readback->resource(), 0u, 0u, 4u})});
        append(K::EndTransferPass, {});
        barrier(ResourceState::CopySrc,
            ResourceState::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead,
            stages,
            ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
        append(K::Finish, {});
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        for (const auto& command : commands) {
            const auto kind = command.kind;
            const auto scalar = kind == K::BindVertexBuffer
                ? CommandScalarArguments::capture(
                      kind, CommandScalarArguments::VertexBinding{0u, 16u})
                : kind == K::BindIndexBuffer
                ? CommandScalarArguments::capture(
                      kind, CommandScalarArguments::IndexBinding{IndexFormat::Uint16, 2u})
                : kind == K::DrawIndexed
                ? CommandScalarArguments::capture(
                      kind, CommandScalarArguments::DrawIndexed{3u, 1u, 0u, 0, 1u})
                : CommandScalarArguments::capture(kind, CommandScalarArguments::None{});
            assert(scalar);
            fields.push_back({*scalar});
        }
        OriginalCommandInputsBudget ib{7u, true, bindingOccurrences};
        ib.maxVertexBindings = ib.maxIndexBindings = 1u;
        const auto assemble = [&](const auto& limits, const auto& data, const auto& authored) {
            return OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
                commands.data(),
                commands.size(),
                references.data(),
                references.size(),
                data.data(),
                authored.data(),
                authored.size(),
                limits,
                quotas[0],
                id);
        };
        for (const auto version : {1u, 5u, 6u}) {
            auto old = ib;
            old.contractVersion = version;
            assert(!assemble(old, fields, descriptors));
        }
        for (const auto member : {&OriginalCommandInputsBudget::maxVertexBindings,
                 &OriginalCommandInputsBudget::maxIndexBindings}) {
            auto less = ib;
            less.*member = 0u;
            assert(!assemble(less, fields, descriptors));
        }
        for (const auto ordinal : {vertexCommand, indexCommand}) {
            auto wrong = descriptors;
            auto position = std::find_if(wrong.begin(), wrong.end(), [&](const auto& d) {
                return d.commandIndex == ordinal;
            });
            ++std::get<OriginalCommandGeometryBinding>(position->arguments).registrationGeneration;
            assert(!assemble(ib, fields, wrong)
                && core::StringView(id) == "RHI-ORIGINAL-GEOMETRY-GENERATION");
            wrong = descriptors;
            position = std::find_if(wrong.begin(), wrong.end(), [&](const auto& d) {
                return d.commandIndex == ordinal;
            });
            ++std::get<OriginalCommandGeometryBinding>(position->arguments)
                  .pipelineBindCommandIndex;
            assert(!assemble(ib, fields, wrong)
                && core::StringView(id) == "RHI-ORIGINAL-GEOMETRY-PIPELINE-EPOCH");
            wrong = descriptors;
            position = std::find_if(wrong.begin(), wrong.end(), [&](const auto& d) {
                return d.commandIndex == ordinal;
            });
            ++std::get<OriginalCommandGeometryBinding>(position->arguments).preparedGeneration;
            assert(!assemble(ib, fields, wrong)
                && core::StringView(id) == "RHI-ORIGINAL-BINDLESS-GEOMETRY-PREPARED-GENERATION");
        }
        auto inputs = assemble(ib, fields, descriptors);
        require(id);
        assert(inputs);
        OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 128u, 32u, 512u, bytes};
        state.bindlessRoots = {5u, true, bindingOccurrences + 2u, root->size() + 1u, 8192u, 1u, 1u};
        state.geometryUsage = {1u, true, commands.size(), 2u, 6u, 1u, true, 1u, 2u, 64u};
        const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
            inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        require(projected.diagnostic);
        // State accesses count the actual draw, clear and copy. Rebinding an
        // entry five times does not manufacture five shader consumptions;
        // RootSequence below independently counts those binding occurrences.
        assert(projected.faultClears == 1u && projected.faultCopies == 1u && projected.roots == 3u);
        state.bindlessRoots.maxSourceVisits = projected.sourceVisits;
        const auto geometry = inputs->visitGeometryAccesses(
            state.geometryUsage, [&](const auto& access) noexcept -> const char* {
                if (access.binding.kind() == K::BindIndexBuffer)
                    assert(access.offsetBytes == 2u && access.sizeBytes == 6u);
                else
                    assert(access.offsetBytes == 16u && access.sizeBytes == 48u);
                if (access.drawConsumption())
                    assert(access.commandIndex == drawCommand);
                return nullptr;
            });
        require(geometry.diagnostic);
        assert(geometry.bindings == 2u && geometry.draws == 1u && geometry.drawAccesses == 2u);
        state.geometryUsage.maxBindingScans = geometry.bindingScans;
        auto legacy = state;
        legacy.bindlessRoots.contractVersion = 4u;
        assert(!OriginalCommandResourceStates::prepareOriginal(
            inputs, *device, legacy, quotas[1], id));
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, *device, state, quotas[1], id);
        require(id);
        assert(graph);
        const auto implicit = projected.accesses + geometry.bindings + geometry.drawAccesses;
        OriginalCommandStateSequenceBudget sequence{1u,
            true,
            static_cast<std::uint32_t>(commands.size()),
            0u,
            256u,
            256u,
            implicit,
            5u,
            true};
        const auto accesses = common::OriginalCommandAccessValidation::inspect(graph, sequence);
        require(accesses.diagnostic);
        sequence.maxStateCells = accesses.stateCells;
        sequence.maxSteps = accesses.steps;
        auto fewer = sequence;
        --fewer.maxImplicitAccesses;
        assert(common::OriginalCommandAccessValidation::inspect(graph, fewer).diagnostic);
        OriginalCommandStreamBudget sb;
        sb.contractVersion = 1u;
        sb.prototypeOptIn = true;
        sb.maxCommands = commands.size();
        sb.maxReferences = references.size();
        sb.maxDescriptors = descriptors.size();
        sb.maxRenderPasses = sb.maxAttachmentReferences = 1u;
        sb.maxPipelineBinds = bindingOccurrences;
        sb.maxConsumptions = sb.maxCopyElements = 1u;
        sb.maxImplicitAccesses = implicit;
        sb.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        sb.maxCpuMetadataBytes = bytes;
        sb.stateSequence = sequence;
        sb.bindlessRoots = state.bindlessRoots;
        sb.geometryUsage = state.geometryUsage;
        auto stream = OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, quotas[2], id);
        require(id);
        assert(stream);
        using RootSequence = OriginalBindlessRootSequenceContract;
        const std::array boundRoots{root, root, root, root, root};
        const RootSequence::Budget rootSequenceBudget{1u,
            true,
            root->originalSnapshot()->generation(),
            bindingOccurrences,
            stream->operations().size() + bindingOccurrences};
        const auto joined = RootSequence::inspect(
            boundRoots.data(), bindingOccurrences, inventory, stream, rootSequenceBudget);
        require(joined.diagnostic);
        assert(joined.bindings == bindingOccurrences
            && joined.graphicsBindings == bindingOccurrences && !joined.computeBindings
            && joined.resourceVisits == rootSequenceBudget.maxResourceVisits
            && !joined.nativeExecutionProven());
        auto shortSequence = rootSequenceBudget;
        --shortSequence.maxResourceVisits;
        const auto denied = RootSequence::inspect(
            boundRoots.data(), bindingOccurrences, inventory, stream, shortSequence);
        assert(denied.diagnostic && !denied.bindings && !denied.resourceVisits);
        shortSequence = rootSequenceBudget;
        ++shortSequence.generation;
        assert(RootSequence::inspect(
            boundRoots.data(), bindingOccurrences, inventory, stream, shortSequence)
                .diagnostic);
        const std::array unconsumed{root, root, root, root, root, root};
        auto widerSequence = rootSequenceBudget;
        widerSequence.maxBindings = unconsumed.size();
        assert(RootSequence::inspect(
            unconsumed.data(), unconsumed.size(), inventory, stream, widerSequence)
                .diagnostic);
        auto forgedRoot =
            std::shared_ptr<const OriginalCommandBindlessRootSource>(quotas[0], root.get());
        assert(RootSequence::inspect(&forgedRoot, 1u, inventory, stream, rootSequenceBudget)
                .diagnostic);
        require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
            return RootSequence::inspectOriginal(
                boundRoots.data(), bindingOccurrences, inventory, stream);
        }));

        // CPU-only projection of native custody: these wrappers have no native
        // payload/issuer and cannot authorize execution. Use authentic Roots and
        // inventory to exercise arbitrary storage and exact Source selection.
        {
            struct ClosureProjection {
                std::shared_ptr<const OriginalCommandBindlessRootSource> root;
                std::shared_ptr<const OriginalCommandResourceSources> inventory;

                const auto& originalRoot() const noexcept { return root; }

                const auto& originalInventory() const noexcept { return inventory; }

                std::uint32_t retainedOwnerCount() const noexcept { return 2u; }
            };

            using NativeSequence = OriginalBindlessRootNativeSequence<ClosureProjection>;
            std::shared_ptr<const ClosureProjection> projected =
                core::makeShared<ClosureProjection>(ClosureProjection{root, inventory});
            const auto second = bindingOccurrences > 1u ? projected : nullptr;
            const auto third = bindingOccurrences > 2u ? projected : nullptr;
            const std::array projectedBindings{
                projected, projected, projected, projected, projected};
            {
                std::array<typename NativeSequence::Owner, 3u> historical;
                std::size_t preparedCount = 0u;
                const auto prepare = [&](const auto&, const char*&) {
                    ++preparedCount;
                    return projected;
                };
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                require(NativeSequence::prepareHistoricalInput(
                    {root, root, root}, historical, prepare));
                assert(preparedCount == 1u && historical[0u].get() == historical[1u].get()
                    && historical[1u].get() == historical[2u].get());
                assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
                assert(NativeSequence::prepareHistoricalInput(
                    {root, root, root}, historical, prepare));
                historical = {};
                assert(NativeSequence::prepareHistoricalInput(
                    {root, nullptr, root}, historical, prepare));
                assert(!historical[0u] && !historical[1u] && !historical[2u]);
                const auto failedFactory = [&](const auto&, const char*& failure) {
                    failure = "fixture-cold-factory-failed";
                    return projected;
                };
                assert(NativeSequence::prepareHistoricalInput(
                    {root, root, root}, historical, failedFactory));
                assert(!historical[0u] && !historical[1u] && !historical[2u]);
                assert(NativeSequence::prepareHistoricalInput(
                    {forgedRoot, nullptr, nullptr}, historical, prepare));
                assert(!historical[0u] && !historical[1u] && !historical[2u]);
            }
            {
                const RootSequence::Input input{
                    boundRoots.data(), bindingOccurrences, rootSequenceBudget};
                OriginalBindlessRootNativeClosureBudget closureBudget{1u, true, 1u, 1u, 1u, 4096u};
                const std::array plans{RootSequence::Input::ClosurePlan{root, closureBudget}};
                auto planned = input;
                planned.closurePlans = plans.data();
                planned.closurePlanCount = plans.size();
                planned.maxClosurePlanVisits = 2u * bindingOccurrences;
                require(RootSequence::inspectClosurePlans(planned));
                assert(&planned.closureBudget(root, closureBudget) == &plans[0u].budget);
                assert(!planned.closureBudget(forgedRoot, closureBudget).contractVersion);
                assert(&input.closureBudget(root, closureBudget) == &closureBudget);
                const std::shared_ptr<const OriginalInitializedGeometrySource> missingActor;
                const OriginalBindlessInitializedSources legacySources{
                    nullptr, 0u, &missingActor, 1u};
                assert(input.initializedSources(root, legacySources).shader == &missingActor);
                assert(planned.initializedSources(root, legacySources).empty());
                assert(planned.initializedSources(forgedRoot, legacySources).empty());
                require(planned.validateInitializedSources(root, closureBudget, {}));
                assert(planned.validateInitializedSources(root, closureBudget, legacySources));
                auto missingActorPlan = plans;
                missingActorPlan[0u].initialized = legacySources;
                auto scopedActors = planned;
                scopedActors.closurePlans = missingActorPlan.data();
                assert(scopedActors.initializedSources(root, {}).shader == &missingActor);
                assert(RootSequence::inspectClosurePlans(scopedActors));
                const auto planAllocations = core::defaultCpuAllocator().stats().allocationCount;
                require(RootSequence::inspectClosurePlans(planned));
                auto malformedPlan = planned;
                --malformedPlan.maxClosurePlanVisits;
                assert(RootSequence::inspectClosurePlans(malformedPlan));
                malformedPlan = planned;
                malformedPlan.closurePlans = nullptr;
                assert(RootSequence::inspectClosurePlans(malformedPlan));
                const std::array duplicatePlans{plans[0u], plans[0u]};
                malformedPlan = planned;
                malformedPlan.closurePlans = duplicatePlans.data();
                malformedPlan.closurePlanCount = duplicatePlans.size();
                malformedPlan.maxClosurePlanVisits = 4u * bindingOccurrences + 1u;
                assert(RootSequence::inspectClosurePlans(malformedPlan));
                auto forgedPlan = plans;
                forgedPlan[0u].root = forgedRoot;
                malformedPlan = planned;
                malformedPlan.closurePlans = forgedPlan.data();
                assert(RootSequence::inspectClosurePlans(malformedPlan));
                auto invalidBudget = plans;
                invalidBudget[0u].budget.contractVersion = 0u;
                malformedPlan = planned;
                malformedPlan.closurePlans = invalidBudget.data();
                assert(RootSequence::inspectClosurePlans(malformedPlan));
                assert(core::defaultCpuAllocator().stats().allocationCount == planAllocations);
                require(RootSequence::inspectPublicInput(input,
                    {root, second ? root : nullptr, third ? root : nullptr},
                    inventory,
                    stream));
                assert(RootSequence::inspectPublicInput(input,
                    {forgedRoot, second ? root : nullptr, third ? root : nullptr},
                    inventory,
                    stream));
                core::pmr::Vector<typename NativeSequence::Owner> prepared{
                    &core::defaultCpuMemoryResource()};
                std::size_t factoryCalls = 0u;
                const auto prepare = [&](const auto&, const char*&) {
                    ++factoryCalls;
                    return projected;
                };
                require(NativeSequence::preparePublicInput(
                    input, {projected, second, third}, inventory, stream, prepared, prepare));
                assert(!factoryCalls && prepared.size() == bindingOccurrences);
                for (const auto& owner : prepared)
                    assert(owner.get() == projected.get());
                assert(NativeSequence::preparePublicInput(
                    input, {projected, second, third}, inventory, stream, prepared, prepare));
                prepared.clear();
                auto rejected = input;
                --rejected.budget.maxBindings;
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                assert(NativeSequence::preparePublicInput(
                    rejected, {projected, second, third}, inventory, stream, prepared, prepare));
                assert(prepared.empty() && !factoryCalls
                    && core::defaultCpuAllocator().stats().allocationCount == allocations);
            }
            NativeSequence retained(projectedBindings.data(), bindingOccurrences);
            const auto inspectProjection = [&](const auto& p) noexcept {
                return OriginalCommandBindlessRootSource::validateOriginal(p->originalRoot());
            };
            {
                auto callerBindings = projectedBindings;
                const typename NativeSequence::Input requested{
                    callerBindings.data(), bindingOccurrences, rootSequenceBudget};
                require(
                    NativeSequence::inspectInput(requested, inventory, stream, inspectProjection));
                require(NativeSequence::inspectHistorical(requested, {projected, second, third}));
                if (bindingOccurrences > 1u)
                    assert(NativeSequence::inspectHistorical(requested, {projected, {}, {}}));
                assert(NativeSequence::additionalOwnerBound(requested)
                    == (bindingOccurrences > 3u ? (bindingOccurrences - 3u) * 3u : 0u));
                NativeSequence bounded({projected, second, third}, requested);
                require(bounded.diagnostic());
                callerBindings[0u].reset();
                require(bounded.validate(inventory, stream, inspectProjection));
                require(NativeSequence::inspectInput(
                    bounded.input(), inventory, stream, inspectProjection));
                auto tooSmall = bounded.input();
                --tooSmall.budget.maxBindings;
                const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
                NativeSequence rejected({projected}, tooSmall);
                assert(rejected.diagnostic() && rejected.closures().empty()
                    && core::defaultCpuAllocator().stats().allocationCount == allocations);
                assert(
                    NativeSequence::inspectInput(tooSmall, inventory, stream, inspectProjection));
                auto malformed = bounded.input();
                malformed.closures = nullptr;
                assert(
                    NativeSequence::inspectInput(malformed, inventory, stream, inspectProjection));
                assert(NativeSequence::additionalOwnerBound(malformed) == UINT64_MAX);
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return bounded.validate(inventory, stream, inspectProjection);
                }));
            }
            require(retained.validate(inventory, stream, inspectProjection));
            const auto rootIndex = inventory->originalLeaseIndex(root);
            assert(retained.select(inventory, rootIndex) == projected.get());
            assert(!retained.select(inventory, inventory->sources().size()));
            assert(!retained.select({}, rootIndex));
            assert(!retained.atOrEmpty(projectedBindings.size()));
            const auto wrongInventory =
                std::shared_ptr<const OriginalCommandResourceSources>(quotas[0u], inventory.get());
            assert(!retained.select(wrongInventory, rootIndex));
            std::shared_ptr<const ClosureProjection> duplicate =
                core::makeShared<ClosureProjection>(ClosureProjection{root, inventory});
            NativeSequence ambiguous{projected, duplicate};
            assert(!ambiguous.select(inventory, rootIndex));
            NativeSequence hole{projected, {}, projected};
            assert(hole.diagnostic() && hole.closures().empty());
            NativeSequence empty{std::shared_ptr<const ClosureProjection>{}};
            assert(empty.diagnostic() && !empty.select(inventory, rootIndex));
            const auto nativeAllocationCount = core::defaultCpuAllocator().stats().allocationCount;
            for (std::size_t repeat = 0u; repeat < 5u; ++repeat) {
                require(retained.validate(inventory, stream, inspectProjection));
                assert(retained.select(inventory, rootIndex) == projected.get());
            }
            assert(core::defaultCpuAllocator().stats().allocationCount == nativeAllocationCount);
        }
        assert(stream->counts().geometryBinds == 2u && stream->counts().geometryDraws == 1u
            && stream->counts().geometryDrawAccesses == 2u && stream->counts().consumptions == 1u
            && stream->counts().implicitAccesses == implicit
            && stream->operations()[drawCommand].implicitAccessCount == root->size() + 1u);
        const auto& candidate = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            inventory->sources()[vertexIndex]);
        if ((candidate->layout().usage & ResourceUsage::IndirectBuffer) != ResourceUsage::None)
            OriginalBindlessIndirectFixture::verify(inventory,
                *device,
                commands,
                references,
                descriptors,
                fields,
                state,
                sb,
                quotas[0u],
                drawCommand,
                vertexIndex);
        // CPU analyzer fixtures exercise the L2 join and extrema/range checks.
        // These bytes are deliberately NOT an initialized native actor and
        // grant no recording or GPU execution authority.
        using Fetch = OriginalIndexedDrawFetchContract;
        Fetch::Budget fetch{2u, true, commands.size(), 1u, 1u, 64u, 3u};
        const auto& indexLease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            inventory->sources()[indexIndex]);
        const auto& vertexLease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
            inventory->sources()[vertexIndex]);
        std::array<std::uint8_t, 8u> indexBytes{0xffu, 0xffu, 0u, 0u, 2u, 0u, 1u, 0u};
        const auto analyze =
            [&](const Fetch::Point& point, const auto& lease, auto& out) noexcept -> const char* {
            assert(core::SharedOwnerProjection::sameOwner(lease, indexLease));
            return IndexInputConsumptionContract::analyzeLittleEndianBytes(indexBytes.data(),
                indexBytes.size(),
                point.index.format,
                point.index.offset,
                point.draw.firstIndex,
                point.draw.indices,
                point.draw.vertexOffset,
                fetch.maxAnalyzedIndices,
                out);
        };
        bool refuseVertex = false;
        const auto vertex = [&](const Fetch::Point& point,
                                const auto& binding,
                                const auto& layout,
                                const auto& fields,
                                const auto& lease) noexcept -> const char* {
            assert(core::SharedOwnerProjection::sameOwner(lease, vertexLease)
                && binding.pipelineSourceIndex == rootIndex
                && binding.preparedGeneration == snapshot->generation() && layout.slot == 0u
                && fields.offset == 16u && point.pipelineBind == epoch
                && point.scopeBegin == scope);
            return refuseVertex ? "fixture-initialized-vertex-unavailable" : nullptr;
        };
        std::size_t emitted = 0u;
        const auto observe = [&](const Fetch::Point& point) noexcept -> const char* {
            assert(point.commandIndex == drawCommand && point.pipelineSource == rootIndex
                && point.indexBind == indexCommand && point.fetch.firstVertex == 0u
                && point.fetch.vertexCount == 3u && point.vertexStreams == 1u
                && point.draw.firstInstance == 1u && !point.nativeExecutionProven());
            ++emitted;
            return nullptr;
        };
        const auto fetched =
            stream->visitInitializedIndexedDrawFetch(fetch, analyze, vertex, observe);
        require(fetched.diagnostic);
        assert(emitted == 1u && fetched.draws == 1u && fetched.streams == 1u
            && fetched.analyzedIndices == 3u);
        fetch.maxBindingScans = fetched.bindingScans;
        emitted = 0u;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas,
                   [&]() noexcept -> const char* {
                       return stream
                           ->visitInitializedIndexedDrawFetch(fetch, analyze, vertex, observe)
                           .diagnostic;
                   })
            && emitted == 1u);
        const auto refuse = [&](const auto& selected, const auto& limits, const char* expected) {
            emitted = 0u;
            const auto result = Fetch::visitInitialized(selected, limits, analyze, vertex, observe);
            assert(
                result.diagnostic && core::StringView(result.diagnostic) == expected && !emitted);
        };
        auto lessFetch = fetch;
        --lessFetch.maxCommands;
        refuse(inputs, lessFetch, "RHI-ORIGINAL-INDEXED-FETCH-COMMAND-CAPACITY");
        lessFetch = fetch;
        --lessFetch.maxStreams;
        refuse(inputs, lessFetch, "RHI-ORIGINAL-INDEXED-FETCH-STREAM-CAPACITY");
        lessFetch = fetch;
        --lessFetch.maxBindingScans;
        refuse(inputs, lessFetch, "RHI-ORIGINAL-INDEXED-FETCH-SCAN-CAPACITY");
        lessFetch = fetch;
        --lessFetch.maxAnalyzedIndices;
        refuse(inputs, lessFetch, "RHI-ORIGINAL-INDEXED-FETCH-ANALYSIS-CAPACITY");
        auto oldFetch = fetch;
        oldFetch.contractVersion = 1u;
        refuse(inputs, oldFetch, "RHI-ORIGINAL-INDEXED-FETCH-INPUTS-PROFILE");
        assert(Fetch::visit(inputs, fetch, analyze, observe).diagnostic
            && stream->visitIndexedDrawFetch(fetch, analyze, observe).diagnostic);
        refuseVertex = true;
        refuse(inputs, fetch, "fixture-initialized-vertex-unavailable");
        refuseVertex = false;
        auto changedFields = fields;
        const auto drawInput = [&](CommandScalarArguments::DrawIndexed draw) {
            changedFields = fields;
            changedFields[drawCommand].scalar =
                *CommandScalarArguments::capture(K::DrawIndexed, draw);
            auto selected = assemble(ib, changedFields, descriptors);
            require(id);
            assert(selected);
            return selected;
        };
        const auto* layouts =
            OriginalCommandGeometryBindingContract::vertexBuffers(*pipeline, true);
        assert(layouts && layouts->size() == 1u);
        const bool instanceRate = (*layouts)[0].inputRate == VertexInputRate::Instance;
        // Per-vertex fetch uses index extrema/baseVertex; instance streams use
        // firstInstance+instanceCount. Neither may silently clamp its domain.
        if (!instanceRate) {
            indexBytes[4u] = 3u;
            refuse(inputs, fetch, "RHI-VERTEX-INPUT-FETCH-RANGE");
            indexBytes[4u] = 2u;
            refuse(drawInput({3u, 1u, 0u, 1, 1u}), fetch, "RHI-VERTEX-INPUT-FETCH-RANGE");
        } else {
            refuse(drawInput({3u, 1u, 0u, 0, 3u}), fetch, "RHI-VERTEX-INPUT-FETCH-RANGE");
            refuse(drawInput({3u, 3u, 0u, 0, 1u}), fetch, "RHI-VERTEX-INPUT-FETCH-RANGE");
        }
        refuse(drawInput({3u, 1u, 0u, -1, 1u}), fetch, "RHI-INDEX-INPUT-BASE-VERTEX-NEGATIVE");
        refuse(drawInput({3u, 1u, 1u, 0, 1u}), fetch, "CORE-UNSIGNED-SPAN-RANGE-INVALID");
        emitted = 0u;
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision = Fetch::lowering(backend, 2u);
            assert(decision.diagnostic && !decision.executionAvailable
                && decision.projectionAvailable);
            const auto produced = Fetch::lowering(backend, 3u);
            assert(produced.projectionAvailable && !produced.executionAvailable
                && produced.diagnostic);
            assert(Fetch::lowering(backend, 4u).diagnostic);
        }
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ROOT_INDEXED_FETCH2 1 L2-CPU-analyzer same-epoch generation index-extrema vertex-instance-bounds dry-before-output hot0 native-initialization-and-execution-unproven");
        initialized(inputs, stream, fetch, graph, state, sequence);
        auto mismatch = sb;
        ++mismatch.geometryUsage.maxBindingScans;
        assert(!OriginalCommandStreamProgram::prepareOriginalTyped(graph, mismatch, quotas[2], id)
            && core::StringView(id) == "RHI-ORIGINAL-STREAM-GEOMETRY-FOOTPRINT-MISMATCH");
        assert(
            !CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept -> const char* {
                return stream->validate();
            }));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto lower =
                semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 5u);
            assert(lower.projectionAvailable && !lower.executionAvailable);
            assert(stream->executionDiagnostic(backend));
        }
        // The geometry tail is state/ownership only; ContentPlan9 and existing
        // lifecycle issuers must refuse this program until real fetch proof.
        auto tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, *device, state, quotas[3], id);
        require(id);
        assert(tx);
        assert(tx->journalEmpty() && !tx->nativeExecutionProven());
        OriginalFiniteGraphicsContentPlanBudget oldContent{
            9u, true, 0u, 1u, 1u, 0u, 2u, 256u, 8192u};
        assert(!OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                   graph, stream, *device, state, sequence, oldContent, quotas[3], id)
            && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-CONTENT-STREAM-PROFILE");
        auto changed = state;
        ++changed.geometryUsage.maxDrawAccesses;
        assert(!OriginalCommandStateTransaction::prepareOriginalOwnership(
                   graph, *device, changed, quotas[3], id)
            && core::StringView(id) == "RHI-ORIGINAL-TRANSACTION-GEOMETRY-FOOTPRINT-MISMATCH");
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ROOT_GEOMETRY5 1 same-prepared-pipeline exact-epoch generation vertex-index-tails draw-consumption same-state-stream execution-closed");
    }
};
} // namespace arp::test
