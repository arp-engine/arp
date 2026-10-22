#pragma once
#include "original_bindless_root_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_graphics_content_visit_budget_fixture.h"
#include "original_graphics_push_data_fixture.h"
#include "renderer/bindless/bindless_pbr_geometry.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"

namespace arp::test {
using namespace renderer;

template <class T, class = void> struct OriginalBindlessSceneStreamFixture : std::false_type {};

template <class T>
struct OriginalBindlessSceneStreamFixture<T, std::void_t<decltype(T::sceneStream)>>
    : std::bool_constant<T::sceneStream> {};

// Original Content9 composition, shared by a caller-provided execution observer.
// All graph, state, readback and stream validation remains in the L2 builders.
class OriginalBindlessGraphicsStreamFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Original Graphics stream: %s\n", id);
        assert(!id);
    }

public:
    template <class Execute>
    static void execute(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice& device,
        const Execute& observe,
        const rhi::OriginalBindlessGraphicsDepthContentPlanContract::Budget* depthContent = nullptr,
        std::uint32_t completedReadContractVersion = 1u)
    {
        using namespace rhi;
        using K = CommandUsageKind;
        constexpr bool geometry = OriginalBindlessGeometryFixtureTraits<Execute>::value;
        constexpr bool scene = OriginalBindlessSceneStreamFixture<Execute>::value;
        const auto bytes = inventory->originalBudget().maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)];
        std::shared_ptr<const OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
                if ((*view)->originalObject()->desc().usage == TextureUsage::RenderAttachment) {
                    assert(!target);
                    target = *view;
                }
        assert(target);
        const auto& object = *target->originalParentLease()->originalObject();
        const auto range = defaultTextureSubresourceRange(object.desc());
        const auto rootIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(root));
        const auto& snapshot = root->originalSnapshot();
        const auto& latch =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        const auto& readback =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 5u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        const char* id = nullptr;
        RenderPassDesc desc;
        RenderPassColorAttachmentDesc color;
        color.target = target->resource();
        color.format = object.desc().format;
        color.clearColor = {0.f, 0.f, 0.f, 1.f};
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        const bool depth = scene && object.desc().format == TextureFormat::D32Float;
        std::shared_ptr<const OriginalCommandBufferLease> numericSource, numericTarget;
        if constexpr (scene) {
            if (!depth) {
                const auto tableId = observe.terrain() ? 7u : 4u;
                const BindlessTableMapping* outputTable = nullptr;
                for (const auto& table : observe.artifact->tables)
                    if (table.tableId == tableId)
                        outputTable = &table;
                assert(outputTable);
                for (const auto& element : root->elements())
                    if (element.declaration.write.binding == outputTable->firstBinding) {
                        assert(!numericSource
                            && element.declaration.write.type == DescriptorType::StorageBuffer
                            && !element.declaration.bufferAccessOffset
                            && element.declaration.bufferAccessBytes == observe.resultBytes);
                        numericSource = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                            element.owner);
                    }
                numericTarget = observe.geometry->resultReadback;
                assert(numericSource && numericTarget);
            }
        }
        if (depth) {
            desc.hasDepthStencilAttachment = true;
            auto& attachment = desc.depthStencilAttachment;
            attachment.target = target->resource();
            attachment.format = object.desc().format;
            attachment.depthLoadOp = LoadOp::Clear;
            attachment.depthStoreOp = StoreOp::Store;
            attachment.stencilLoadOp = LoadOp::DontCare;
            attachment.stencilStoreOp = StoreOp::DontCare;
            attachment.depthClearValue = 1.f;
        } else
            desc.colorAttachments.push_back(color);
        auto pass = RenderPassArguments::prepareOriginal(
            desc, {1u, true, depth ? 0u : 1u, 0u, 0u, 0u, 0u, bytes}, q[0], id);
        require(id);
        assert(pass);
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
        const auto shader = scene
            ? bindlessPipelineStagesForShader(
                  std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                      root->originalPrepared())
                      ->owners()
                      ->stages())
            : PipelineStage::FragmentShader;
        const auto rw = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        const auto numericBarrier = [&](bool restore) {
            assert(numericSource);
            ResourceBarrierDesc barrier{numericSource->resource(),
                {},
                restore ? ResourceState::CopySrc : ResourceState::ShaderWrite,
                restore ? ResourceState::ShaderWrite : ResourceState::CopySrc};
            barrier.sourceStages =
                restore ? PipelineStage::Transfer : PipelineStage::FragmentShader;
            barrier.sourceAccess = restore ? ResourceAccess::TransferRead : rw;
            barrier.destinationStages =
                restore ? PipelineStage::FragmentShader : PipelineStage::Transfer;
            barrier.destinationAccess = restore ? rw : ResourceAccess::TransferRead;
            descriptors.push_back(
                {append(K::BufferBarrier,
                     {static_cast<std::uint32_t>(inventory->originalLeaseIndex(numericSource))}),
                    *ResourceDependencyArguments::capture(K::BufferBarrier, barrier)});
        };
        if constexpr (scene)
            append(K::RetainIndexedSnapshot, {rootIndex});
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
        const auto sampledInSelectedFragment = [&](std::size_t index) {
            if constexpr (scene) {
                BindlessResourceAccess access;
                std::size_t source = SIZE_MAX;
                bool active = false;
                require(root->readOriginalShaderDeclaration(
                    index, ShaderStage::Fragment, access, source, active, 4u));
                return active;
            }
            return true;
        };
        if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value) {
            for (std::size_t index = 0u; index < root->elements().size(); ++index) {
                const auto& element = root->elements()[index];
                const auto* sampled =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.owner);
                if (!sampled || !*sampled)
                    continue;
                if (!sampledInSelectedFragment(index))
                    continue;
                const auto& parent = (*sampled)->originalParentLease();
                assert(parent->originalObject()->state() == ResourceState::CopyDst);
                ResourceBarrierDesc read{parent->resource(),
                    defaultTextureSubresourceRange(parent->originalObject()->desc()),
                    ResourceState::CopyDst,
                    ResourceState::ShaderRead};
                read.sourceStages = PipelineStage::Transfer;
                read.sourceAccess = ResourceAccess::TransferWrite;
                read.destinationStages = shader;
                read.destinationAccess = ResourceAccess::ShaderRead;
                descriptors.push_back(
                    {append(K::TextureBarrier,
                         {static_cast<std::uint32_t>(inventory->originalLeaseIndex(parent))}),
                        *ResourceDependencyArguments::capture(K::TextureBarrier, read)});
            }
        }
        if (depthContent || completedReadContractVersion == 3u) {
            for (const auto& domain : root->ordinaryDomains())
                for (const auto& element : domain.source->frozenElements()) {
                    const auto* sampled =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &element.resourceOwner);
                    if (!sampled || !*sampled)
                        continue;
                    const auto& parent = (*sampled)->originalParentLease();
                    const bool depth =
                        (*sampled)->originalObject()->desc().aspect == TextureAspect::Depth;
                    const auto committed = parent->originalObject()->observeOriginalCommittedState(
                        defaultTextureSubresourceRange(parent->originalObject()->desc()));
                    assert(committed);
                    if (!depth && committed->state() == ResourceState::ShaderRead)
                        continue;
                    ResourceBarrierDesc read{parent->resource(),
                        defaultTextureSubresourceRange(parent->originalObject()->desc()),
                        committed->state(),
                        ResourceState::ShaderRead};
                    // Match the genuine producer's committed state. Canonical
                    // clear and native Compute readers have distinct origins.
                    if (depth) {
                        switch (committed->state()) {
                        case ResourceState::DepthAttachment:
                            read.sourceStages = PipelineStage::DepthStencil;
                            read.sourceAccess = ResourceAccess::DepthStencilRead
                                | ResourceAccess::DepthStencilWrite;
                            break;
                        case ResourceState::CopySrc:
                            read.sourceStages = PipelineStage::Transfer;
                            read.sourceAccess = ResourceAccess::TransferRead;
                            break;
                        case ResourceState::ShaderRead:
                            read.sourceStages =
                                PipelineStage::ComputeShader | PipelineStage::FragmentShader;
                            read.sourceAccess = ResourceAccess::ShaderRead;
                            break;
                        default:
                            assert(false && "Unsupported original depth producer state");
                            break;
                        }
                    } else {
                        assert(committed->state() == ResourceState::CopyDst);
                        read.sourceStages = PipelineStage::Transfer;
                        read.sourceAccess = ResourceAccess::TransferWrite;
                    }
                    read.destinationStages = element.declaration.stages;
                    read.destinationAccess = ResourceAccess::ShaderRead;
                    descriptors.push_back(
                        {append(K::TextureBarrier,
                             {static_cast<std::uint32_t>(inventory->originalLeaseIndex(parent))}),
                            *ResourceDependencyArguments::capture(K::TextureBarrier, read)});
                }
        }
        const auto attachmentState =
            depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        ResourceBarrierDesc b{object.id(), range, ResourceState::Undefined, attachmentState};
        b.sourceStages = PipelineStage::TopOfPipe;
        b.sourceAccess = ResourceAccess::None;
        b.destinationStages =
            depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        b.destinationAccess =
            depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        if (!scene || object.desc().initialState == ResourceState::Undefined)
            descriptors.push_back({append(K::TextureBarrier,
                                       {static_cast<std::uint32_t>(inventory->originalLeaseIndex(
                                           target->originalParentLease()))}),
                *ResourceDependencyArguments::capture(K::TextureBarrier, b)});
        const auto passScope = append(K::BeginRenderPass,
            {static_cast<std::uint32_t>(inventory->originalLeaseIndex(target))});
        descriptors.push_back({passScope, pass});
        const auto pipelineEpoch = append(K::BindPreparedGraphicsBinding, {rootIndex});
        append(K::SetViewport, {});
        append(K::SetScissor, {});
        if constexpr (OriginalBindlessPushFixtureTraits<Execute>::value)
            append(K::PushConstants, {rootIndex});
        if constexpr (geometry) {
            std::array<std::shared_ptr<const OriginalCommandBufferLease>, 2u> leases;
            if constexpr (scene)
                leases = observe.geometry->leases;
            else
                for (std::size_t i = 0u; i < leases.size(); ++i)
                    leases[i] = observe.initialized->actors[i]->originalLease();
            for (std::size_t i = 0u; i < leases.size(); ++i) {
                const auto& lease = leases[i];
                const auto source =
                    static_cast<std::uint32_t>(inventory->originalLeaseIndex(lease));
                const auto kind = i ? K::BindIndexBuffer : K::BindVertexBuffer;
                descriptors.push_back({append(kind, {source}),
                    OriginalCommandGeometryBinding{kind,
                        source,
                        rootIndex,
                        pipelineEpoch,
                        passScope,
                        lease->originalObject()->originalResourceRegistration().generation(),
                        0u,
                        snapshot->generation()}});
            }
        }
        append(geometry ? K::DrawIndexed : K::Draw, {});
        constexpr auto pushUpdates = OriginalBindlessPushUpdateFixtureTraits<Execute>::value;
        static_assert(pushUpdates == 1u || pushUpdates == 2u);
        if constexpr (OriginalBindlessPushFixtureTraits<Execute>::value && pushUpdates == 2u) {
            append(K::SetScissor, {});
            append(K::PushConstants, {rootIndex});
            append(K::Draw, {});
        }
        append(K::EndRenderPass, {});
        if (numericSource)
            numericBarrier(false);
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
        std::size_t numericCopy = SIZE_MAX;
        if (numericSource) {
            numericCopy = append(K::BufferCopy,
                {static_cast<std::uint32_t>(inventory->originalLeaseIndex(numericSource)),
                    static_cast<std::uint32_t>(inventory->originalLeaseIndex(numericTarget))});
            descriptors.push_back({numericCopy,
                *TransferCopyArguments::capture(BufferCopyDesc{numericSource->resource(),
                    numericTarget->resource(),
                    0u,
                    0u,
                    numericSource->layout().sizeBytes})});
        }
        append(K::EndTransferPass, {});
        if (numericSource)
            numericBarrier(true);
        bufferBarrier(ResourceState::CopySrc,
            ResourceState::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead,
            shader,
            rw);
        append(K::Finish, {});
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        using PushBlock = std::
            conditional_t<scene, BindlessPbrGeometryDraw16, OriginalGraphicsPushDataFixture::Block>;
        std::array<PushBlock, pushUpdates> pushes{};
        if constexpr (OriginalBindlessPushFixtureTraits<Execute>::value) {
            for (std::uint32_t i = 0u; i < pushUpdates; ++i) {
                if constexpr (scene)
                    pushes[i] = observe.draw;
                else
                    pushes[i] = OriginalGraphicsPushDataFixture::block(observe.fault, i);
            }
        }
        std::uint32_t pushIndex = 0u, scissorIndex = 0u;
        constexpr auto pushStages = scene
            ? ShaderStage::Vertex
            : static_cast<ShaderStage>(
                  std::uint32_t(ShaderStage::Vertex) | std::uint32_t(ShaderStage::Fragment));
        for (const auto& c : commands) {
            const auto scalar = c.kind == K::PushConstants
                ? CommandScalarArguments::capture(
                      c.kind, CommandScalarArguments::Push{pushStages, 0u, sizeof(pushes[0])})
                : c.kind == K::BindVertexBuffer
                ? CommandScalarArguments::capture(
                      c.kind, CommandScalarArguments::VertexBinding{0u, scene ? 0u : 16u})
                : c.kind == K::BindIndexBuffer
                ? CommandScalarArguments::capture(c.kind,
                      CommandScalarArguments::IndexBinding{
                          scene ? IndexFormat::Uint32 : IndexFormat::Uint16, scene ? 0u : 2u})
                : c.kind == K::DrawIndexed
                ? CommandScalarArguments::capture(
                      c.kind, CommandScalarArguments::DrawIndexed{3u, 1u, 0u, 0, scene ? 0u : 1u})
                : c.kind == K::Draw ? CommandScalarArguments::capture(
                                          c.kind, CommandScalarArguments::Draw{3u, 1u, 0u, 0u})
                : c.kind == K::SetViewport ? CommandScalarArguments::capture(c.kind,
                                                 ViewportState{0.f, 0.f, 2.f, 2.f, 0.f, 1.f})
                : c.kind == K::SetScissor
                ? CommandScalarArguments::capture(c.kind,
                      ScissorState{static_cast<std::int32_t>(scissorIndex++),
                          0,
                          pushUpdates == 2u ? 1u : 2u,
                          2u})
                : CommandScalarArguments::capture(c.kind, CommandScalarArguments::None{});
            assert(scalar);
            fields.push_back({*scalar});
            if (c.kind == K::PushConstants) {
                assert(pushIndex < pushes.size());
                fields.back().bytes = reinterpret_cast<const std::uint8_t*>(&pushes[pushIndex++]);
                fields.back().byteCount = sizeof(pushes[0]);
            }
        }
        OriginalCommandInputsBudget inputBudget{1u, false};
        if constexpr (geometry) {
            inputBudget = {7u, true, 1u};
            inputBudget.maxVertexBindings = inputBudget.maxIndexBindings = 1u;
        }
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            descriptors.data(),
            descriptors.size(),
            inputBudget,
            q[0],
            id);
        require(id);
        assert(inputs);
        // The original argument store, rather than caller memory, owns the bytes.
        std::memset(pushes.data(), 0xCD, sizeof(pushes));
        OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 64u, 16u, 256u, bytes};
        if constexpr (scene)
            state.maxStateObservations = inventory->originalBudget().maxStateObservations;
        state.maxRetainedOwners =
            std::max(state.maxRetainedOwners, inventory->originalBudget().maxGraphOwners);
        state.maxTextureRanges = inventory->originalBudget().maxTextureRanges;
        state.bindlessRoots = {geometry ? 5u : 4u,
            true,
            2u + pushUpdates,
            pushUpdates * root->size() + 1u,
            8192u,
            1u,
            1u};
        std::uint64_t geometryAccesses = 0u;
        if constexpr (geometry) {
            state.geometryUsage = {1u, true, commands.size(), 2u, 6u, 1u, true, 1u, 2u, 64u};
            const auto projection = inputs->visitGeometryAccesses(
                state.geometryUsage, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            require(projection.diagnostic);
            state.geometryUsage.maxBindingScans = projection.bindingScans;
            geometryAccesses = projection.bindings + projection.drawAccesses;
        }
        if (numericSource)
            state.bindlessRoots.directResultCopies = {1u,
                true,
                1u,
                inventory->sources().size()
                    * (1u
                        + commands.size()
                            * (1u + state.bindlessRoots.maxAccesses + geometryAccesses + 2u))};
        if constexpr (OriginalBindlessPushFixtureTraits<Execute>::value)
            state.bindlessRoots.pushConsumption = {1u, true, pushUpdates, sizeof(pushes), 1u, true};
        const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
            inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        if constexpr (scene) {
            if (device.backend() == Backend::Null && projected.diagnostic) {
                // A Null pipeline cannot issue native member-memory or push
                // recording proof. Keep the real typed inputs and test refusal.
                observe.preparedSceneInputs(root,
                    inventory,
                    inputs,
                    std::shared_ptr<const OriginalCommandResourceStates>{},
                    projected.diagnostic,
                    state.bindlessRoots,
                    device);
                return;
            }
        }
        require(projected.diagnostic);
        state.bindlessRoots.maxSourceVisits = projected.sourceVisits;
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[1], id);
        if constexpr (scene) {
            if (device.backend() == Backend::Null && !graph) {
                observe.preparedSceneInputs(
                    root, inventory, inputs, graph, id, state.bindlessRoots, device);
                // Independently inspect CPU correspondence with no recording
                // request. This test-only graph cannot grant native authority.
                state.bindlessRoots.pushConsumption.recordingContractVersion = 0u;
                state.bindlessRoots.pushConsumption.recordingOptIn = false;
                graph =
                    OriginalCommandResourceStates::prepareOriginal(inputs, device, state, q[1], id);
            }
        }
        require(id);
        assert(graph);
        OriginalCommandStateSequenceBudget sequence{1u,
            true,
            static_cast<std::uint32_t>(commands.size()),
            0u,
            256u,
            256u,
            projected.accesses + geometryAccesses,
            geometry ? 5u : 4u,
            true};
        const auto accesses = common::OriginalCommandAccessValidation::inspect(graph, sequence);
        require(accesses.diagnostic);
        sequence.maxStateCells = accesses.stateCells;
        sequence.maxSteps = accesses.steps;
        if constexpr (scene) {
            if (device.backend() == Backend::Null && numericSource) {
                assert(accesses.dependencyVisits);
                auto scratchGraph = core::makeShared<core::CpuMemoryQuota>(bytes);
                auto limited = state;
                limited.bindlessRoots.directResultCopies.maxDependencyVisits =
                    accesses.dependencyVisits - 1u;
                auto denied = OriginalCommandResourceStates::prepareOriginal(
                    inputs, device, limited, scratchGraph, id);
                require(id);
                const auto capacity =
                    common::OriginalCommandAccessValidation::inspect(denied, sequence);
                assert(capacity.diagnostic
                    && core::StringView(capacity.diagnostic)
                        == "RHI-ORIGINAL-BINDLESS-DEPENDENCY-VISIT-CAPACITY");
                denied.reset();
                assert(!scratchGraph->liveBytes());
                bool tested = false;
                const auto& frozen = *inputs->originalScalars()->originalArguments();
                for (std::size_t i = 0u; i < fields.size(); ++i)
                    if (fields[i].byteCount)
                        fields[i].bytes = frozen.bytes(i);
                for (auto& descriptor : descriptors) {
                    if (commands[descriptor.commandIndex].kind != K::BufferBarrier)
                        continue;
                    auto* dependency =
                        std::get_if<ResourceDependencyArguments>(&descriptor.arguments);
                    ResourceBarrierDesc barrier;
                    if (!dependency || !dependency->write(K::BufferBarrier, barrier)
                        || barrier.resource != numericSource->resource()
                        || barrier.after != ResourceState::CopySrc)
                        continue;
                    const auto saved = descriptor.arguments;
                    barrier.sourceStages = PipelineStage::VertexShader;
                    barrier.sourceAccess = ResourceAccess::ShaderRead;
                    descriptor.arguments =
                        *ResourceDependencyArguments::capture(K::BufferBarrier, barrier);
                    // Retain the original render-pass quota domain while
                    // rebuilding only the dependency arguments for this probe.
                    const auto inputBaseline = q[0]->liveBytes();
                    auto wrong =
                        OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
                            commands.data(),
                            commands.size(),
                            refs.data(),
                            refs.size(),
                            fields.data(),
                            descriptors.data(),
                            descriptors.size(),
                            inputBudget,
                            q[0],
                            id);
                    descriptor.arguments = saved;
                    require(id);
                    denied = OriginalCommandResourceStates::prepareOriginal(
                        wrong, device, state, scratchGraph, id);
                    require(id);
                    const auto hazard =
                        common::OriginalCommandAccessValidation::inspect(denied, sequence);
                    assert(hazard.diagnostic
                        && core::StringView(hazard.diagnostic)
                            == "RHI-RECORDED-RESOURCE-DEPENDENCY-SOURCE");
                    denied.reset();
                    wrong.reset();
                    assert(q[0]->liveBytes() == inputBaseline && !scratchGraph->liveBytes());
                    tested = true;
                    break;
                }
                assert(tested);
            }
        }
        OriginalCommandStreamBudget sb;
        sb.contractVersion = 1u;
        sb.prototypeOptIn = true;
        sb.maxCommands = commands.size();
        sb.maxReferences = refs.size();
        sb.maxDescriptors = descriptors.size();
        sb.maxRenderPasses = sb.maxAttachmentReferences = 1u;
        sb.maxPipelineBinds = 1u;
        sb.maxCopyElements = 1u + bool(numericSource);
        sb.maxConsumptions = pushUpdates;
        sb.maxImplicitAccesses = projected.accesses + geometryAccesses;
        sb.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
        sb.maxCpuMetadataBytes = bytes;
        sb.stateSequence = sequence;
        sb.bindlessRoots = state.bindlessRoots;
        sb.geometryUsage = state.geometryUsage;
        auto stream = OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, q[2], id);
        require(id);
        assert(stream);
        std::shared_ptr<OriginalCommandStateTransaction> tx;
        if (!scene || device.backend() != Backend::Null) {
            OriginalFiniteGraphicsContentPlanBudget content{
                9u, true, 0u, 1u, 1u, 0u, 2u, 512u, 8192u, 0u, false};
            content.bindlessGeometryContractVersion = geometry ? 1u : 0u;
            content.bindlessGeometryOptIn = geometry;
            if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value) {
                for (std::size_t index = 0u; index < root->elements().size(); ++index) {
                    const auto& element = root->elements()[index];
                    if (!sampledInSelectedFragment(index))
                        continue;
                    if (std::holds_alternative<
                            std::shared_ptr<const OriginalCommandTextureViewSource>>(element.owner))
                        ++content.maxCompletedControls;
                }
                if (completedReadContractVersion == 3u)
                    for (const auto& domain : root->ordinaryDomains())
                        for (const auto& element : domain.source->frozenElements())
                            if (std::holds_alternative<
                                    std::shared_ptr<const OriginalCommandTextureViewSource>>(
                                    element.resourceOwner))
                                ++content.maxCompletedControls;
                content.maxSampledSteps = content.maxCompletedControls;
                content.bindlessCompletedReadContractVersion = scene
                    ? (device.backend() == Backend::WebGPU ? 4u : 0u)
                    : completedReadContractVersion;
                OriginalGraphicsContentVisitBudgetFixture::configure(content,
                    commands.size(),
                    graph->textureEntries().size(),
                    content.maxCompletedControls,
                    desc.colorAttachments.size());
                for (const bool controls : {true, false}) {
                    auto invalid = content;
                    if (controls)
                        --invalid.maxCompletedControls;
                    else
                        --invalid.maxSampledSteps;
                    assert(
                        !OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                            graph, stream, device, state, sequence, invalid, q[3], id)
                        && id && !q[3]->liveBytes() && !inventory->originalStateTransactionCount());
                }
            }
            std::uint32_t transactionOwners = 0u, registrations = 0u;
            require(OriginalCommandResourceSources::registeredOwnerUnion(inventory,
                inputs->retainedOwnerCount() + 2u,
                state.maxRetainedOwners,
                state.maxResourceRegistrations,
                transactionOwners,
                registrations));
            std::printf(
                "Original Graphics content owners: transaction%u controls%llu state%u source%u\n",
                transactionOwners,
                static_cast<unsigned long long>(
                    content.maxCompletedControls + content.maxAttachmentControls),
                state.maxRetainedOwners,
                inventory->originalBudget().maxGraphOwners);
            OriginalBindlessGraphicsDepthContentPlanContract::Budget combined;
            if (depthContent)
                combined = *depthContent;
            const auto prepare = [&](const auto& limits) {
                return OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                    graph, stream, device, state, sequence, limits, q[3], id);
            };
            if (depthContent) {
                // Old /9 cannot classify the sampled depth as an attachment.
                assert(!prepare(combined.graphics) && id && !q[3]->liveBytes()
                    && !inventory->originalStateTransactionCount());
                auto probe = prepare(combined);
                require(id);
                assert(probe && probe->originalGraphicsDepthContentCounts());
                const auto actual = *probe->originalGraphicsDepthContentCounts();
                combined.graphics.maxCompletedControls = actual.graphics.completedControls;
                combined.graphics.maxAttachmentControls = actual.graphics.attachmentControls;
                combined.graphics.maxAttachmentCells = actual.graphics.attachmentCells;
                combined.graphics.maxSampledSteps = actual.graphics.drawReads;
                combined.graphics.maxAttachmentSteps = actual.graphics.attachmentSteps;
                combined.graphics.maxCommandVisits =
                    actual.graphics.commandVisits + actual.depthCommandVisits;
                combined.graphics.maxProjectionVisits =
                    actual.graphics.projectionVisits + actual.depthProjectionVisits;
                combined.maxDepthControls = actual.depthControls;
                combined.maxDepthCells = actual.depthCells;
                combined.maxDepthReadSteps = actual.depthReadSteps;
                probe.reset();
                assert(!q[3]->liveBytes() && !inventory->originalStateTransactionCount());
                for (unsigned field = 0u; field < 5u; ++field) {
                    auto less = combined;
                    if (field == 0u)
                        --less.maxDepthControls;
                    else if (field == 1u)
                        --less.maxDepthCells;
                    else if (field == 2u)
                        --less.maxDepthReadSteps;
                    else if (field == 3u)
                        --less.graphics.maxCommandVisits;
                    else
                        --less.graphics.maxProjectionVisits;
                    assert(!prepare(less) && id && !q[3]->liveBytes()
                        && !inventory->originalStateTransactionCount()
                        && !OriginalCommandBindlessRootSource::validateOriginal(root, device));
                }
            }
            tx = depthContent ? prepare(combined) : prepare(content);
            require(id);
            assert(tx);
        }
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
            {scene             ? 6u
                    : geometry ? 3u
                               : 2u,
                true,
                OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                commands.size(),
                bytes},
            q[0],
            id);
        require(id);
        assert(binding);
        if constexpr (scene) {
            std::array<std::shared_ptr<const OriginalCommandBufferReadbackBinding>, 2u> results{
                binding};
            if (numericSource) {
                const auto size = numericSource->layout().sizeBytes;
                auto numericRequest =
                    BufferReadbackRequestDeclaration::prepareOriginal(numericTarget,
                        device,
                        {2u, 1u, snapshot->generation()},
                        0u,
                        size,
                        {1u, true, size, bytes},
                        q[0],
                        id);
                require(id);
                auto numericResult =
                    OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                        numericRequest, q[4], id);
                require(id);
                results[1u] = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                    stream,
                    numericRequest,
                    numericResult,
                    numericCopy,
                    rootIndex,
                    device,
                    {6u,
                        true,
                        OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                        commands.size() + root->elements().size(),
                        bytes},
                    q[0],
                    id);
                require(id);
                assert(results[1u] && !results[1u]->isOriginalSnapshotFaultReadback());
            }
            const auto count = 1u + bool(numericSource);
            const auto resultBytes = 4u + (numericSource ? numericSource->layout().sizeBytes : 0u);
            const OriginalBindlessRootReadbackSequence::Input readbacks{results.data(),
                count,
                4096u,
                {1u, true, count, count, count, resultBytes, resultBytes, bytes}};
            require(OriginalBindlessRootReadbackSequence::inspectInput(binding, readbacks));
            for (std::size_t i = 0u; i < count; ++i) {
                const auto& result = results[i];
                assert(OriginalBindlessRootReadbackSequence::containsCopyDestination(
                    binding, readbacks, result->commandIndex(), result->originalReadbackSource()));
                assert(!OriginalBindlessRootReadbackSequence::containsCopyDestination(
                    binding, readbacks, result->commandIndex(), result->originalCopySource()));
                assert(!OriginalBindlessRootReadbackSequence::containsCopyDestination(
                    binding, readbacks, SIZE_MAX, result->originalReadbackSource()));
                if (count == 2u)
                    assert(!OriginalBindlessRootReadbackSequence::containsCopyDestination(binding,
                        readbacks,
                        result->commandIndex(),
                        results[1u - i]->originalReadbackSource()));
            }
            observe(root, inventory, binding, tx, device, readbacks);
        } else
            observe(root, inventory, binding, tx, device);
    }
};
} // namespace arp::test
