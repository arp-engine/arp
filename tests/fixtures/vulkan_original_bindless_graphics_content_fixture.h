#pragma once
#include "original_bindless_root_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_graphics_content_visit_budget_fixture.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h"
#include "rhi/frontend/bindless_graphics_execution.h"
#include "vulkan_original_bindless_geometry_fixture.h"
#include "vulkan_original_graphics_buffer_data_fixture.h"
#include "vulkan_original_graphics_record_data_fixture.h"
#include "vulkan_original_root_terminal_fixture.h"

#include <algorithm>
#include <cstring>
#include <thread>

namespace arp::test {
// Shared original Graphics composition, cold pass and optional real fence path.
class VulkanOriginalBindlessGraphicsContentFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Root Graphics Content9: %s\n", id);
        assert(!id);
    }

public:
    bool execute = false;
    bool terminalCases = false;
    VulkanOriginalRootTerminalScenario terminal;
    bool bufferSideEffects = false;
    std::uint32_t expectedFaultBits = 0u;
    bool arraySampling = false;
    bool recordSideEffects = false;
    bool recordRanges = false;
    bool repeatCompletedAttachment = false;
    bool attachmentClearOptIn = false;
    std::optional<rhi::ResourceBarrierDesc> completedAttachmentBarrier;
    std::uint32_t completedReadContractVersion = 0u;
    rhi::PushConstantsDesc push;
    void (*verifyOrdinaryBuffers)(const rhi::OriginalCommandBindlessRootSource&) = nullptr;
    std::shared_ptr<VulkanOriginalGeometryDataFixture::Initialized> geometry;

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using K = CommandUsageKind;
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        // Usage must consume the caller-authored graph ceiling from this same
        // canonical inventory; the upload workload may retain larger CPU inputs.
        const auto bytes = inventory->originalBudget().maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)];
        assert(bytes);
        assert(std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            root->originalPrepared()));
        std::shared_ptr<const OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source)) {
                if ((*view)->originalObject()->desc().usage != TextureUsage::RenderAttachment)
                    continue;
                assert(!target);
                target = *view;
            }
        assert(
            target && target->originalParentLease()->originalObject()->originalContentExclusive());
        const auto& object = *target->originalParentLease()->originalObject();
        const auto range = defaultTextureSubresourceRange(object.desc());
        assert(object.observeOriginalContent(range)->controlVersion() == 4u);
        const auto initialGeneration = object.observeOriginalContent(range)->contentGeneration();
        const auto initialContentState = object.observeOriginalContent(range)->state();
        const bool indexed = bool(geometry);
        const bool depth = object.desc().format == TextureFormat::D32Float;
        const auto attachmentState =
            depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        const auto attachmentStages =
            depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
        const auto attachmentAccess =
            depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
        assert(!indexed || execute);
        const auto rootIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(root));
        const auto parentIndex = static_cast<std::uint32_t>(
            inventory->originalLeaseIndex(target->originalParentLease()));
        const auto viewIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(target));
        const auto& snapshot = root->originalSnapshot();
        const auto& latch =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
        const auto& readback =
            snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> q;
        for (auto& owner : q)
            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            const char* id = nullptr;
            RenderPassDesc passDesc;
            RenderPassColorAttachmentDesc color;
            color.target = target->resource();
            color.format = object.desc().format;
            color.loadOp = LoadOp::Clear;
            color.storeOp = StoreOp::Store;
            if (arraySampling)
                color.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            else
                color.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
            if (depth) {
                passDesc.hasDepthStencilAttachment = true;
                auto& d = passDesc.depthStencilAttachment;
                d.target = target->resource();
                d.format = object.desc().format;
                d.depthLoadOp = LoadOp::Clear;
                d.depthStoreOp = StoreOp::Store;
                d.stencilLoadOp = LoadOp::DontCare;
                d.stencilStoreOp = StoreOp::DontCare;
                d.depthClearValue = 0.125f;
            } else
                passDesc.colorAttachments.push_back(color);
            auto pass = RenderPassArguments::prepareOriginal(
                passDesc, {1u, true, depth ? 0u : 1u, 0u, 0u, 0u, 0u, bytes}, q[0], id);
            require(id);
            assert(pass);
            ResourceBarrierDesc barrier{
                object.id(), range, ResourceState::Undefined, attachmentState};
            barrier.sourceStages = PipelineStage::TopOfPipe;
            barrier.sourceAccess = ResourceAccess::None;
            if ((indexed || depth) && initialGeneration) {
                assert(attachmentClearOptIn);
                if (completedAttachmentBarrier) {
                    barrier = *completedAttachmentBarrier;
                    assert(barrier.resource == object.id() && barrier.textureSubresources == range
                        && barrier.after == attachmentState);
                } else {
                    barrier.before = attachmentState;
                    barrier.sourceStages = attachmentStages;
                    barrier.sourceAccess = attachmentAccess;
                }
                const auto committed = object.observeOriginalCommittedState(range);
                assert(committed && committed->state() == barrier.before);
            }
            barrier.destinationStages = attachmentStages;
            barrier.destinationAccess = attachmentAccess;
            core::pmr::Vector<CommandUsage> commands;
            core::pmr::Vector<OriginalCommandUsage::Reference> refs;
            core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
            core::pmr::Vector<std::pair<const GpuTexture*, GpuTexture::ContentExpectation>> sampled;
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
            const auto shaderStages = bindlessPipelineStagesForShader(
                std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                    root->originalPrepared())
                    ->owners()
                    ->stages());
            if (execute) {
                bufferBarrier(ResourceState::ShaderWrite,
                    ResourceState::CopyDst,
                    shaderStages,
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
                    PipelineStage::Transfer,
                    ResourceAccess::TransferWrite);
                append(K::ClearSnapshotFault, {rootIndex});
                bufferBarrier(ResourceState::CopyDst,
                    ResourceState::ShaderWrite,
                    PipelineStage::Transfer,
                    ResourceAccess::TransferWrite,
                    shaderStages,
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
            }
            for (const auto& source : inventory->sources()) {
                const auto* texture =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(&source);
                if (!texture || (*texture)->originalObject() == &object)
                    continue;
                const auto* input = (*texture)->originalObject();
                const auto content = input->observeOriginalContent();
                assert(content);
                sampled.emplace_back(input, *content);
                const auto index =
                    static_cast<std::uint32_t>(inventory->originalLeaseIndex(*texture));
                ResourceBarrierDesc read{input->id(),
                    defaultTextureSubresourceRange(input->desc()),
                    ResourceState::CopyDst,
                    ResourceState::ShaderRead};
                read.sourceStages = PipelineStage::Transfer;
                read.sourceAccess = ResourceAccess::TransferWrite;
                read.destinationStages = PipelineStage::FragmentShader;
                read.destinationAccess = ResourceAccess::ShaderRead;
                if (!indexed
                    || input->observeOriginalCommittedState(
                                defaultTextureSubresourceRange(input->desc()))
                            ->state()
                        == ResourceState::CopyDst)
                    descriptors.push_back({append(K::TextureBarrier, {index}),
                        *ResourceDependencyArguments::capture(K::TextureBarrier, read)});
            }
            descriptors.push_back({append(K::TextureBarrier, {parentIndex}),
                *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)});
            append(K::RetainIndexedSnapshot, {rootIndex});
            const auto scope = append(K::BeginRenderPass, {viewIndex});
            descriptors.push_back({scope, pass});
            const auto epoch = append(K::BindPreparedGraphicsBinding, {rootIndex});
            append(K::SetViewport, {});
            append(K::SetScissor, {});
            if (indexed) {
                for (std::size_t i = 0u; i < geometry->actors.size(); ++i) {
                    const auto& lease = geometry->actors[i]->originalLease();
                    const auto source =
                        static_cast<std::uint32_t>(inventory->originalLeaseIndex(lease));
                    const auto kind = i ? K::BindIndexBuffer : K::BindVertexBuffer;
                    descriptors.push_back({append(kind, {source}),
                        OriginalCommandGeometryBinding{kind,
                            source,
                            rootIndex,
                            epoch,
                            scope,
                            lease->originalObject()->originalResourceRegistration().generation(),
                            0u,
                            snapshot->generation()}});
                }
            }
            if (push.data)
                append(K::PushConstants, {rootIndex});
            append(indexed ? K::DrawIndexed : K::Draw, {});
            append(K::EndRenderPass, {});
            std::size_t faultCopy = SIZE_MAX;
            if (execute) {
                bufferBarrier(ResourceState::ShaderWrite,
                    ResourceState::CopySrc,
                    shaderStages,
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
                    PipelineStage::Transfer,
                    ResourceAccess::TransferRead);
                append(K::BeginTransferPass, {});
                faultCopy = append(K::BufferCopy,
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
                    shaderStages,
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
            }
            append(K::Finish, {});
            core::pmr::Vector<CommandVariableArguments::Input> fields;
            fields.reserve(commands.size());
            for (std::size_t i = 0u; i < commands.size(); ++i) {
                const auto kind = commands[i].kind;
                const auto scalar = kind == K::PushConstants
                    ? CommandScalarArguments::capture(kind,
                          CommandScalarArguments::Push{
                              push.stages, push.offsetBytes, push.sizeBytes})
                    : kind == K::BindVertexBuffer
                    ? CommandScalarArguments::capture(
                          kind, CommandScalarArguments::VertexBinding{0u, 16u})
                    : kind == K::BindIndexBuffer
                    ? CommandScalarArguments::capture(
                          kind, CommandScalarArguments::IndexBinding{IndexFormat::Uint16, 2u})
                    : kind == K::DrawIndexed
                    ? CommandScalarArguments::capture(
                          kind, CommandScalarArguments::DrawIndexed{3u, 1u, 0u, 0, 1u})
                    : kind == K::Draw ? CommandScalarArguments::capture(
                                            kind, CommandScalarArguments::Draw{3u, 1u, 0u, 0u})
                    : kind == K::SetViewport
                    ? CommandScalarArguments::capture(
                          kind, ViewportState{0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f})
                    : kind == K::SetScissor
                    ? CommandScalarArguments::capture(kind, ScissorState{0, 0, 2u, 2u})
                    : CommandScalarArguments::capture(kind, CommandScalarArguments::None{});
                assert(scalar);
                fields.push_back({*scalar});
                if (kind == K::PushConstants) {
                    fields.back().bytes = push.data;
                    fields.back().byteCount = push.sizeBytes;
                }
            }
            OriginalCommandInputsBudget inputBudget{1u, false};
            if (indexed) {
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
            OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 64u, 16u, 256u, bytes};
            // State/content must retain the caller's complete declared graph
            // closure, including each sampled input's original content owner.
            state.maxRetainedOwners =
                std::max(state.maxRetainedOwners, inventory->originalBudget().maxGraphOwners);
            state.maxTextureRanges = inventory->originalBudget().maxTextureRanges;
            state.bindlessRoots = {indexed ? 5u : 4u, true, 3u, root->size() + 1u, 8192u, 1u, 1u};
            if (push.data)
                state.bindlessRoots.pushConsumption = {1u, true, 1u, push.sizeBytes, 1u, true};
            std::uint64_t geometryAccesses = 0u;
            OriginalIndexedDrawFetchContract::Budget fetch;
            if (indexed) {
                state.geometryUsage = {1u, true, commands.size(), 2u, 6u, 1u, true, 1u, 2u, 64u};
                const auto projectedGeometry = inputs->visitGeometryAccesses(
                    state.geometryUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                require(projectedGeometry.diagnostic);
                state.geometryUsage.maxBindingScans = projectedGeometry.bindingScans;
                geometryAccesses = projectedGeometry.bindings + projectedGeometry.drawAccesses;
                fetch = {2u, true, commands.size(), 1u, 1u, 64u, 3u};
            }
            const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
                inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            require(projected.diagnostic);
            state.bindlessRoots.maxSourceVisits = projected.sourceVisits;
            for (unsigned attempt = 0u; attempt < 2u; ++attempt) {
                auto graph = OriginalCommandResourceStates::prepareOriginal(
                    inputs, *device, state, q[1], id);
                require(id);
                assert(graph);
                std::uint32_t transactionOwners = 0u, registrations = 0u;
                require(OriginalCommandResourceSources::registeredOwnerUnion(inventory,
                    inputs->retainedOwnerCount() + 2u,
                    state.maxRetainedOwners,
                    state.maxResourceRegistrations,
                    transactionOwners,
                    registrations));
                std::fprintf(stderr,
                    "Graphics content owners: transaction%u controls%llu stream1 state%u source%u\n",
                    transactionOwners,
                    static_cast<unsigned long long>(sampled.size() + 1u),
                    state.maxRetainedOwners,
                    inventory->originalBudget().maxGraphOwners);
                OriginalCommandStateSequenceBudget sequence{1u,
                    true,
                    static_cast<std::uint32_t>(commands.size()),
                    0u,
                    256u,
                    256u,
                    projected.accesses + geometryAccesses,
                    indexed ? 5u : 4u,
                    true};
                const auto accesses =
                    common::OriginalCommandAccessValidation::inspect(graph, sequence);
                require(accesses.diagnostic);
                sequence.maxStateCells = accesses.stateCells;
                sequence.maxSteps = accesses.steps;
                OriginalCommandStreamBudget sb;
                sb.contractVersion = 1u;
                sb.prototypeOptIn = true;
                sb.maxCommands = commands.size();
                sb.maxReferences = refs.size();
                sb.maxDescriptors = descriptors.size();
                sb.maxRenderPasses = sb.maxAttachmentReferences = 1u;
                sb.maxPipelineBinds = sb.maxConsumptions = 1u;
                sb.maxImplicitAccesses = projected.accesses + geometryAccesses;
                sb.maxCopyElements = execute ? 1u : 0u;
                sb.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
                sb.maxCpuMetadataBytes = bytes;
                sb.stateSequence = sequence;
                sb.bindlessRoots = state.bindlessRoots;
                sb.geometryUsage = state.geometryUsage;
                auto stream =
                    OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, q[2], id);
                require(id);
                assert(stream);
                OriginalFiniteGraphicsContentPlanBudget content{9u,
                    true,
                    sampled.size(),
                    1u,
                    1u,
                    sampled.size(),
                    2u,
                    512u,
                    8192u,
                    indexed ? 1u : 0u,
                    indexed};
                OriginalGraphicsContentVisitBudgetFixture::configure(content,
                    commands.size(),
                    graph->textureEntries().size(),
                    sampled.size(),
                    passDesc.colorAttachments.size());
                content.bindlessCompletedReadContractVersion = completedReadContractVersion;
                std::fprintf(stderr,
                    "Graphics content visits: commands%llu entries%llu bound%llu\n",
                    static_cast<unsigned long long>(commands.size()),
                    static_cast<unsigned long long>(graph->textureEntries().size()),
                    static_cast<unsigned long long>(content.maxProjectionVisits));
                if (attachmentClearOptIn)
                    content.attachmentClear = {1u, true};
                const auto prepare =
                    [&](const auto& planned, const auto& limits, const auto& owner) {
                        return OriginalCommandStateTransaction::
                            prepareOriginalBindlessGraphicsOwnership(
                                graph, stream, *device, state, planned, limits, owner, id);
                    };
                for (const auto version : {0u, 2u, 3u, 6u, 8u}) {
                    auto old = content;
                    old.contractVersion = version;
                    assert(!prepare(sequence, old, q[3]));
                }
                for (unsigned field = 0u; field < 2u; ++field) {
                    auto wrong = sequence;
                    if (field)
                        wrong.bindlessRootStateOptIn = false;
                    else
                        wrong.bindlessRootStateVersion = 3u;
                    assert(!prepare(wrong, content, q[3])
                        && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-CONTENT-STREAM-PROFILE");
                }
                assert(!prepare(sequence, content, q[2])
                    && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-CONTENT-STREAM-QUOTA-DOMAIN");
                if (!attempt) {
                    CpuQuotaFactoryFixture probe(q[3]);
                    std::fprintf(stderr, "Graphics content quota measure begin\n");
                    const auto peak = probe.measure([&] {
                        auto measured = prepare(sequence, content, q[3]);
                        require(id);
                        return measured;
                    });
                    std::fprintf(stderr,
                        "Graphics content quota measure peak%llu\n",
                        static_cast<unsigned long long>(peak));
                    probe.verify(peak, [&] {
                        return prepare(sequence, content, q[3]);
                    });
                    std::fprintf(stderr, "Graphics content quota verify done\n");
                }
                if ((indexed || depth) && initialGeneration) {
                    auto old = content;
                    old.attachmentClear = {};
                    assert(!prepare(sequence, old, q[3])
                        && core::StringView(id)
                            == "RHI-FINITE-GRAPHICS-CONTENT-ATTACHMENT-INITIAL");
                }
                auto tx = prepare(sequence, content, q[3]);
                require(id);
                assert(tx);
                const auto counts = *tx->originalMixedContentCounts();
                assert(tx->originalContentPlanContractVersion() == 9u
                    && !tx->nativeExecutionProven() && counts.attachmentControls == 1u
                    && counts.attachmentCells == 1u && counts.attachmentSteps == 2u
                    && counts.completedControls == sampled.size()
                    && counts.drawReads == sampled.size() && !counts.renderBindingReads);
                assert(core::StringView(tx->executionDiagnostic(device->backend()))
                    == (indexed ? "RHI-BINDLESS-GRAPHICS-GEOMETRY-VULKAN-EXECUTION-UNAVAILABLE"
                                : "RHI-BINDLESS-GRAPHICS-CONTENT-VULKAN-EXECUTION-UNAVAILABLE"));
                tx.reset();
                content.maxCommandVisits = counts.commandVisits;
                content.maxProjectionVisits = counts.projectionVisits;
                for (const auto member :
                    {&OriginalFiniteGraphicsContentPlanBudget::maxAttachmentControls,
                        &OriginalFiniteGraphicsContentPlanBudget::maxAttachmentCells,
                        &OriginalFiniteGraphicsContentPlanBudget::maxAttachmentSteps,
                        &OriginalFiniteGraphicsContentPlanBudget::maxCommandVisits,
                        &OriginalFiniteGraphicsContentPlanBudget::maxProjectionVisits}) {
                    auto less = content;
                    --(less.*member);
                    assert(!prepare(sequence, less, q[3]));
                    assert(!q[3]->liveBytes() && !graph->validate() && !stream->validate());
                }
                if (!sampled.empty()) {
                    for (const auto member :
                        {&OriginalFiniteGraphicsContentPlanBudget::maxCompletedControls,
                            &OriginalFiniteGraphicsContentPlanBudget::maxSampledSteps}) {
                        auto less = content;
                        --(less.*member);
                        assert(!prepare(sequence, less, q[3]));
                        assert(!q[3]->liveBytes());
                    }
                }
                // The public legacy cell-plan still cannot admit native /4 control.
                auto legacy = OriginalCommandStateTransaction::prepareOriginalOwnership(
                    graph, *device, state, q[3], id);
                require(id);
                assert(legacy);
                assert(legacy->preflightOriginalContentCells({2u, true, 1u, 0u, 1u, 2u, 32u, 4096u})
                        .diagnostic);
                legacy.reset();
                tx = prepare(sequence, content, q[3]);
                require(id);
                assert(tx);
                {
                    using NativePass = rhi_vulkan::VulkanOriginalBindlessGraphicsPass;
                    const auto closureQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
                    const auto passQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
                    const auto physical = core::makeShared<core::AtomicCapacityBudget>(3u);
                    auto entries = static_cast<std::uint32_t>(root->elements().size()
                        + (root->originalSnapshot()->originalBufferSource(
                               BindlessSnapshot::OriginalBufferRole::FaultReadback)
                                ? 1u
                                : 0u));
                    for (const auto& domain : root->ordinaryDomains())
                        entries += domain.source->frozenElements().size();
                    OriginalBindlessRootNativeClosureBudget closureBudget{indexed ? 3u : 1u,
                        true,
                        entries + (indexed ? 2u : 0u),
                        (entries + (indexed ? 2u : 0u)) * 2u + 2u,
                        512u,
                        bytes};
                    if (indexed) {
                        closureBudget.maxInitializedGeometryActors = 2u;
                        closureBudget.maxInitializedGeometryBytes = 72u;
                    }
                    using Closure = rhi_vulkan::VulkanBindlessRootNativeClosure;
                    std::array<std::optional<GpuBuffer::CommittedStateObservation>, 2u>
                        geometryCaptures;
                    if (indexed)
                        for (std::size_t i = 0u; i < geometryCaptures.size(); ++i)
                            geometryCaptures[i].emplace(*geometry->actors[i]
                                    ->originalLease()
                                    ->originalObject()
                                    ->observeOriginalCommittedState());
                    auto closure = Closure::prepareOriginal(root,
                        inventory,
                        *device,
                        closureBudget,
                        closureQuota,
                        id,
                        indexed ? Closure::GeometryContents{geometry->actors.data(),
                                      geometry->actors.size()}
                                : Closure::GeometryContents{});
                    require(id);
                    assert(closure);
                    if (!root->ordinaryDomains().empty()) {
                        const auto charged = closureQuota->liveBytes();
                        for (unsigned field = 0u; field < 3u; ++field) {
                            auto less = closureBudget;
                            if (!field)
                                --less.maxResourceEntries;
                            else if (field == 1u)
                                less.maxNativeOwners = closure->nativeOwnerCount() - 1u;
                            else
                                less.maxRetainedOwners = closure->retainedOwnerCount() - 1u;
                            assert(!Closure::prepareOriginal(
                                root, inventory, *device, less, closureQuota, id));
                            assert(closureQuota->liveBytes() == charged
                                && tx->status()
                                    == OriginalCommandStateTransaction::Status::Prepared);
                        }
                        {
                            core::CpuMemoryQuotaLease pressure(closureQuota, bytes - charged);
                            assert(pressure.charged());
                            assert(!Closure::prepareOriginal(
                                root, inventory, *device, closureBudget, closureQuota, id));
                        }
                        assert(closureQuota->liveBytes() == charged && !tx->validate());
                        auto wrong = closureBudget;
                        wrong.contractVersion = 2u;
                        assert(!Closure::prepareOriginal(
                                   root, inventory, *device, wrong, closureQuota, id)
                            && core::StringView(id)
                                == "RHI-VULKAN-BINDLESS-ROOT-ORDINARY-PROFILE-UNAVAILABLE");
                    }
                    OriginalBindlessGraphicsPassBudget passBudget{1u, true, 1u, 3u, 9u, bytes};
                    passBudget.attachmentClear = content.attachmentClear;
                    const auto cold = [&](const auto& b, const auto& quota) {
                        return NativePass::prepareOriginal(
                            closure, tx, stream, *device, b, physical, quota, id);
                    };
                    if (attachmentClearOptIn) {
                        auto old = passBudget;
                        old.attachmentClear = {};
                        assert(!cold(old, passQuota)
                            && core::StringView(id)
                                == "RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-INTENT");
                        assert(!passQuota->liveBytes() && !physical->used());
                    }
                    {
                        auto first = cold(passBudget, passQuota);
                        require(id);
                        assert(first);
                    }
                    assert(!cold(passBudget, q[3])
                        && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-PASS-QUOTA-DOMAIN");
                    assert(!cold(passBudget, closureQuota)
                        && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-PASS-QUOTA-DOMAIN");
                    for (const auto member :
                        {&OriginalBindlessGraphicsPassBudget::maxAttachmentViews,
                            &OriginalBindlessGraphicsPassBudget::maxRetainedOwners}) {
                        auto less = passBudget;
                        --(less.*member);
                        assert(!cold(less, passQuota));
                        assert(!passQuota->liveBytes() && !physical->used() && !tx->validate());
                    }
                    auto less = passBudget;
                    --less.maxNativeObjects;
                    const auto smallPhysical =
                        core::makeShared<core::AtomicCapacityBudget>(less.maxNativeObjects);
                    assert(!NativePass::prepareOriginal(
                        closure, tx, stream, *device, less, smallPhysical, passQuota, id));
                    assert(!passQuota->liveBytes() && !smallPhysical->used() && !physical->used());
                    // Same graph and values do not replace the exact retained stream.
                    auto other =
                        OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, q[2], id);
                    require(id);
                    assert(other);
                    assert(!NativePass::prepareOriginal(
                               closure, tx, other, *device, passBudget, physical, passQuota, id)
                        && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-PASS-STREAM-OWNER");
                    other.reset();
                    if (!attempt) {
                        CpuQuotaFactoryFixture probe(passQuota);
                        const auto peak = probe.measure([&] {
                            return cold(passBudget, passQuota);
                        });
                        probe.verify(peak, [&] {
                            return cold(passBudget, passQuota);
                        });
                        assert(!physical->used());
                    }
                    auto nativePass = cold(passBudget, passQuota);
                    require(id);
                    assert(nativePass);
                    assert(nativePass->nativeObjectCount() == 3u
                        && nativePass->retainedOwnerCount() == 9u
                        && !nativePass->nativeExecutionProven() && physical->used() == 3u
                        && nativePass->commandIndex() < commands.size());
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{passQuota, closureQuota, q[3]}, [&]() noexcept {
                            return nativePass->validate();
                        }));
                    assert(!cold(passBudget, passQuota)
                        && core::StringView(id) == "RHI-BINDLESS-GRAPHICS-PASS-PHYSICAL-CAPACITY");
                    assert(physical->used() == 3u && !nativePass->validate() && tx->journalEmpty());
                    // This preparation never grants content completion or a legacy seal.
                    assert(tx->status() == OriginalCommandStateTransaction::Status::Prepared
                        && object.observeOriginalContent(range)->state() == initialContentState);
                    if (execute) {
                        using namespace rhi_vulkan;
                        std::array<std::shared_ptr<core::CpuMemoryQuota>, 2u> nativeQuotas;
                        for (auto& owner : nativeQuotas)
                            owner = core::makeShared<core::CpuMemoryQuota>(bytes);
                        auto request = BufferReadbackRequestDeclaration::prepareOriginal(readback,
                            *device,
                            {1u, 1u, snapshot->generation()},
                            0u,
                            4u,
                            {1u, true, 4u, bytes},
                            q[0],
                            id);
                        require(id);
                        assert(request);
                        auto result =
                            OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                                request, nativeQuotas[0], id);
                        require(id);
                        assert(result);
                        if (indexed) {
                            assert(!OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                                       stream,
                                       request,
                                       result,
                                       faultCopy,
                                       rootIndex,
                                       *device,
                                       {2u,
                                           true,
                                           OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                                           commands.size(),
                                           bytes},
                                       q[0],
                                       id)
                                && core::StringView(id)
                                    == "RHI-ORIGINAL-BUFFER-READBACK-BINDING-PROFILE");
                        }
                        auto binding = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                            stream,
                            request,
                            result,
                            faultCopy,
                            rootIndex,
                            *device,
                            {indexed ? 3u : 2u,
                                true,
                                OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                                commands.size(),
                                bytes},
                            q[0],
                            id);
                        require(id);
                        assert(binding);
                        OriginalCommandNativeLifecycleBudget life{9u,
                            true,
                            1u,
                            0u,
                            0u,
                            tx->retainedOwnerCount() + 2u,
                            512u,
                            0u,
                            bytes,
                            bytes,
                            0u};
                        // The original fence command independently retains the
                        // closure, reservation (transaction/seal/slot), stream,
                        // readback binding, command/quota and graphics pass.
                        const auto submissionOwners = closure->retainedOwnerCount()
                            + tx->retainedOwnerCount()
                            + OriginalCommandRecordingSeal::additionalOwnerCountFor(life)
                            + OriginalCommandLifecycleReservation::additionalOwnerCount
                            + stream->retainedOwnerCount()
                            + OriginalCommandBufferReadbackBinding::retainedOwnerCount + 2u
                            + nativePass->retainedOwnerCount();
                        life.maxSubmissionOwners =
                            std::max(life.maxSubmissionOwners, submissionOwners);
                        // The terminal matrix directly observes the same original
                        // custody/retirement queue underneath the public view.
                        // Keep its negative driver hooks and owner-drop checks.
                        if ((indexed || depth || !root->ordinaryDomains().empty())
                            && !terminalCases) {
                            using Public = OriginalBindlessGraphicsExecution;
                            auto* extension = static_cast<BindlessCrossExtension*>(
                                device->queryInterface(kExtensionBindlessCross));
                            assert(extension
                                && extension->capabilities()
                                        .originalGraphicsExecutionContractVersion
                                    == 1u);
                            nativePass.reset();
                            assert(!physical->used());
                            const auto viewQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
                            std::array<std::shared_ptr<const OriginalInitializedGeometrySource>, 2u>
                                sources;
                            if (indexed)
                                for (std::size_t i = 0u; i < sources.size(); ++i)
                                    sources[i] = geometry->actors[i];
                            OriginalBindlessGraphicsExecutionInputs publicInputs{root,
                                inventory,
                                tx,
                                binding,
                                indexed ? sources.data() : nullptr,
                                indexed ? sources.size() : 0u,
                                closureQuota,
                                passQuota,
                                nativeQuotas[1],
                                viewQuota,
                                physical};
                            OriginalBindlessGraphicsExecutionBudget publicBudget;
                            publicBudget.requireWorkloadRetention = true;
                            publicBudget.intent = {1u, true};
                            publicBudget.closure = closureBudget;
                            publicBudget.pass = passBudget;
                            publicBudget.lifecycle = life;
                            publicBudget.capacity = {5u, true, 1u, 0u, 0u};
                            publicBudget.fetch = fetch;
                            publicBudget.maxCpuViewBytes = bytes;
                            publicBudget.maxDiagnosticBytes = 512u;
                            core::pmr::String diagnostic;
                            diagnostic.reserve(512u);
                            {
                                auto less = publicBudget;
                                less.lifecycle.maxSubmissionOwners = submissionOwners - 1u;
                                assert(!extension->prepareOriginalGraphicsExecution(
                                    *device, publicInputs, less, diagnostic));
                                assert(diagnostic == "RHI-VULKAN-ROOT-FENCE-OWNER-CAPACITY"
                                    && !physical->used() && !viewQuota->liveBytes()
                                    && !nativeQuotas[1]->liveBytes()
                                    && tx->status()
                                        == OriginalCommandStateTransaction::Status::Prepared);
                            }
                            {
                                auto less = publicBudget;
                                if (indexed)
                                    --less.fetch.maxAnalyzedIndices;
                                else
                                    --less.closure.maxResourceEntries;
                                assert(!extension->prepareOriginalGraphicsExecution(
                                    *device, publicInputs, less, diagnostic));
                                assert(tx->status()
                                        == OriginalCommandStateTransaction::Status::Prepared
                                    && !result->publishedBytes() && !physical->used()
                                    && !viewQuota->liveBytes());
                            }
                            {
                                auto wrong = publicBudget;
                                wrong.intent.prototypeOptIn = false;
                                assert(!extension->prepareOriginalGraphicsExecution(
                                    *device, publicInputs, wrong, diagnostic));
                            }
                            {
                                auto wrong = publicInputs;
                                wrong.viewQuota = closureQuota;
                                assert(!extension->prepareOriginalGraphicsExecution(
                                    *device, wrong, publicBudget, diagnostic));
                            }
                            auto issuer = extension->prepareOriginalGraphicsExecution(
                                *device, publicInputs, publicBudget, diagnostic);
                            if (!issuer)
                                std::fprintf(stderr,
                                    "Public Root Graphics prepare: %s\n",
                                    diagnostic.c_str());
                            assert(issuer);
                            assert(tx->status() == OriginalCommandStateTransaction::Status::Prepared
                                && !result->publishedBytes() && !issuer->recordingProven()
                                && !issuer->submissionCompleted() && !issuer->pollCompletion());
                            assert(Public::submit(issuer, {1u, true}));
                            auto forged =
                                std::shared_ptr<const Public>(issuer.get(), [](const Public*) {});
                            assert(Public::record(forged, {1u, true}));
                            forged.reset();
                            assert(Public::record(issuer, {2u, true}));
                            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                std::array{viewQuota, nativeQuotas[1], q[3]}, [&]() noexcept {
                                    return Public::record(issuer, {1u, true});
                                }));
                            assert(issuer->recordingProven() && !issuer->submissionCompleted()
                                && tx->status() == OriginalCommandStateTransaction::Status::Pending
                                && !result->publishedBytes());
                            assert(Public::record(issuer, {1u, true})
                                && !object.observeOriginalContent(range));
                            // Drop every caller geometry/closure owner while the public view
                            // retains the same original command, custody, pass and actors.
                            std::array<
                                std::weak_ptr<const VulkanOriginalGeometryDataFixture::Content>,
                                2u>
                                actors;
                            if (indexed)
                                for (std::size_t i = 0u; i < actors.size(); ++i)
                                    actors[i] = geometry->actors[i];
                            sources = {};
                            if (indexed)
                                geometry->actors = {};
                            closure.reset();
                            if (indexed)
                                for (const auto& actor : actors)
                                    assert(!actor.expired());
                            // Restore CPU references only after proving command-only
                            // retention; cancellation must keep actors for the retry.
                            if (indexed)
                                for (std::size_t i = 0u; i < actors.size(); ++i)
                                    geometry->actors[i] = actors[i].lock();
                            if (!attempt) {
                                require(issuer->cancel());
                                assert(tx->status()
                                        == OriginalCommandStateTransaction::Status::Abandoned
                                    && !result->publishedBytes());
                                assert(object.observeOriginalContent(range)->contentGeneration()
                                    == initialGeneration);
                            } else {
                                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                    std::array{viewQuota, nativeQuotas[1], q[3]}, [&]() noexcept {
                                        return Public::submit(issuer, {1u, true});
                                    }));
                                assert(issuer->cancel() && Public::submit(issuer, {1u, true}));
                                const auto deadline =
                                    std::chrono::steady_clock::now() + std::chrono::seconds(5);
                                while (!issuer->pollCompletion()
                                    && std::chrono::steady_clock::now() < deadline)
                                    std::this_thread::yield();
                                assert(issuer->submissionCompleted()
                                    && tx->status()
                                        == OriginalCommandStateTransaction::Status::Committed
                                    && result->publishedBytes());
                                std::uint32_t bits = ~0u;
                                std::memcpy(&bits, result->publishedBytes(), 4u);
                                assert(bits == expectedFaultBits);
                                if (verifyOrdinaryBuffers)
                                    verifyOrdinaryBuffers(*root);
                                const auto completed = object.observeOriginalContent(range);
                                assert(completed
                                    && completed->contentGeneration()
                                        == initialGeneration + attempt + 1u
                                    && completed->state()
                                        == common::OriginalTextureContentControl::State::Completed);
                            }
                            if (indexed)
                                for (std::size_t i = 0u; i < actors.size(); ++i)
                                    geometry->actors[i] = actors[i].lock();
                            issuer.reset();
                            assert(!viewQuota->liveBytes() && !physical->used()
                                && !passQuota->liveBytes());
                            publicInputs = {};
                            binding.reset();
                            result.reset();
                            request.reset();
                            std::puts(
                                "BINDLESS_VULKAN_PUBLIC_ROOT_GRAPHICS1 cold-prepared record submit poll same-original-issuer journal fault retirement");
                        } else {
                            auto seal = OriginalCommandRecordingSeal::prepareOriginal(
                                tx, *device, life, nativeQuotas[1], {}, id);
                            require(id);
                            assert(seal);
                            auto reservation = OriginalCommandLifecycleReservation::prepareOriginal(
                                seal, {5u, true, 1u, 0u, 0u}, id);
                            require(id);
                            assert(reservation);
                            core::pmr::String diagnostic;
                            diagnostic.reserve(192u);
                            if (indexed) {
                                auto less = fetch;
                                --less.maxAnalyzedIndices;
                                assert(!VulkanOriginalBindlessCommand::prepareOriginal(closure,
                                    reservation,
                                    binding,
                                    *device,
                                    diagnostic,
                                    nativePass,
                                    less));
                                assert(tx->status()
                                        == OriginalCommandStateTransaction::Status::Prepared
                                    && !result->publishedBytes());
                                auto old = fetch;
                                old.contractVersion = 1u;
                                assert(!VulkanOriginalBindlessCommand::prepareOriginal(closure,
                                           reservation,
                                           binding,
                                           *device,
                                           diagnostic,
                                           nativePass,
                                           old)
                                    && diagnostic == "RHI-VULKAN-GEOMETRY-FENCE-FETCH-VERSION");
                            }
                            auto command = VulkanOriginalBindlessCommand::prepareOriginal(closure,
                                reservation,
                                binding,
                                *device,
                                diagnostic,
                                nativePass,
                                fetch);
                            if (!command)
                                std::fprintf(
                                    stderr, "Root Graphics command: %s\n", diagnostic.c_str());
                            assert(command);
                            assert(tx->status() == OriginalCommandStateTransaction::Status::Pending
                                && !result->publishedBytes());
                            if (indexed)
                                for (const auto& actor : geometry->actors)
                                    assert(actor->validate());
                            assert(!object.observeOriginalContent(range));
                            std::weak_ptr<const NativePass> retained = nativePass;
                            nativePass.reset();
                            assert(!retained.expired() && physical->used() == 3u);
                            if (indexed) {
                                std::array<
                                    std::weak_ptr<const VulkanOriginalGeometryDataFixture::Content>,
                                    2u>
                                    actors;
                                for (std::size_t i = 0u; i < actors.size(); ++i)
                                    actors[i] = geometry->actors[i];
                                std::weak_ptr<const Closure> commandClosure = closure;
                                geometry->actors = {};
                                closure.reset();
                                assert(!commandClosure.expired());
                                for (const auto& actor : actors)
                                    assert(!actor.expired());
                                // Only the original command/pass custody retains all callers' dropped owners.
                                closure = commandClosure.lock();
                                for (std::size_t i = 0u; i < actors.size(); ++i)
                                    geometry->actors[i] = actors[i].lock();
                            }
                            if (!attempt) {
                                command.reset();
                                assert(tx->status()
                                    == OriginalCommandStateTransaction::Status::Abandoned);
                                assert(!result->publishedBytes());
                                const auto restored = object.observeOriginalContent(range);
                                assert(
                                    restored && restored->contentGeneration() == initialGeneration);
                            } else {
                                if (terminalCases) {
                                    VulkanOriginalRootTerminalFixture::verify(terminal,
                                        command,
                                        closure,
                                        seal,
                                        reservation,
                                        binding,
                                        tx,
                                        root,
                                        *device,
                                        nativeQuotas,
                                        diagnostic,
                                        [&] {
                                            for (const auto& [input, stamp] : sampled)
                                                require(input->validateOriginalContent(stamp));
                                            if (tx->status()
                                                != OriginalCommandStateTransaction::Status::
                                                    Committed)
                                                assert(!object.observeOriginalContent(range));
                                            if (tx->status()
                                                == OriginalCommandStateTransaction::Status::
                                                    Invalidated)
                                                assert(
                                                    !object.observeOriginalCommittedState(range));
                                            if (tx->status()
                                                == OriginalCommandStateTransaction::Status::Pending)
                                                assert(
                                                    !retained.expired() && physical->used() == 3u);
                                        });
                                } else {
                                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                        nativeQuotas, [&]() noexcept -> const char* {
                                            return command->submit(diagnostic) ? nullptr
                                                                               : diagnostic.c_str();
                                        }));
                                    assert(command->waitSubmittedFenceUntil(
                                        std::chrono::steady_clock::now() + std::chrono::seconds(5),
                                        diagnostic));
                                    retireCompletedVulkanRhiSubmittedCommands();
                                }
                                if (terminalCases
                                    && (terminal.uncertainSubmit || terminal.uncertainFence)) {
                                    assert(tx->status()
                                            == OriginalCommandStateTransaction::Status::Invalidated
                                        && !result->publishedBytes());
                                    assert(!object.observeOriginalContent(range)
                                        && !object.observeOriginalCommittedState(range));
                                } else {
                                    assert(tx->status()
                                            == OriginalCommandStateTransaction::Status::Committed
                                        && result->publishedBytes());
                                    std::uint32_t bits = ~0u;
                                    std::memcpy(&bits, result->publishedBytes(), 4u);
                                    if (bits != expectedFaultBits)
                                        std::fprintf(stderr,
                                            "Graphics fault bits actual%u expected%u\n",
                                            bits,
                                            expectedFaultBits);
                                    assert(bits == expectedFaultBits);
                                    if (bufferSideEffects)
                                        VulkanOriginalGraphicsBufferDataFixture::verify(root);
                                    if (recordSideEffects)
                                        VulkanOriginalGraphicsRecordDataFixture::verify(root);
                                    if (verifyOrdinaryBuffers)
                                        verifyOrdinaryBuffers(*root);
                                    // Cancellation consumes its generation; rollback never
                                    // reissues that identity to the successful draw.
                                    const auto completed = object.observeOriginalContent(range);
                                    assert(completed
                                        && completed->contentGeneration()
                                            == initialGeneration + attempt + 1u
                                        && completed->state()
                                            == common::OriginalTextureContentControl::State::
                                                Completed);
                                    assert(object.observeOriginalCommittedState(range)->state()
                                        == attachmentState);
                                }
                                for (const auto& [input, stamp] : sampled)
                                    require(input->validateOriginalContent(stamp));
                                command.reset();
                            }
                            assert(retained.expired() && !physical->used());
                            retained.reset();
                            assert(!passQuota->liveBytes());
                            reservation.reset();
                            seal.reset();
                            binding.reset();
                            result.reset();
                            request.reset();
                        }
                        for (const auto& owner : nativeQuotas)
                            assert(!owner->liveBytes());
                    }
                    nativePass.reset();
                    assert(!physical->used() && !passQuota->liveBytes());
                    if (indexed) {
                        for (const auto& actor : geometry->actors)
                            require(actor->validate());
                        const char* expectedCapture = nullptr;
                        for (const auto& captured : geometryCaptures)
                            if (!expectedCapture)
                                expectedCapture = captured->diagnostic();
                        const auto actualCapture = closure ? closure->validate() : expectedCapture;
                        assert((!expectedCapture && !actualCapture)
                            || (expectedCapture && actualCapture
                                && core::StringView(expectedCapture) == actualCapture));
                        if (attempt) {
                            auto recaptured = Closure::prepareOriginal(root,
                                inventory,
                                *device,
                                closureBudget,
                                closureQuota,
                                id,
                                {geometry->actors.data(), geometry->actors.size()});
                            require(id);
                            assert(recaptured);
                            require(recaptured->validate());
                            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                                std::array{closureQuota}, [&]() noexcept {
                                    return recaptured->validate();
                                }));
                            std::puts(
                                "BINDLESS_VULKAN_ROOT_GEOMETRY_COMPLETED_RECAPTURE1 current-readonly-actors fresh-closure exact-original-capture-validity no-second-submit");
                        } // Rollback is checked against the original controls' actual capture validity.
                    }
                    closure.reset();
                    assert(!closureQuota->liveBytes());
                }
                if (execute)
                    continue;
                for (const auto version : {6u, 8u, 10u}) {
                    OriginalCommandNativeLifecycleBudget life{version,
                        true,
                        1u,
                        0u,
                        0u,
                        tx->retainedOwnerCount() + 2u,
                        512u,
                        0u,
                        bytes,
                        bytes,
                        0u};
                    const auto submissionQuota = core::makeShared<core::CpuMemoryQuota>(bytes);
                    assert(!OriginalCommandRecordingSeal::prepareOriginal(
                        tx, *device, life, submissionQuota, {}, id));
                    assert(!submissionQuota->liveBytes()
                        && tx->status() == OriginalCommandStateTransaction::Status::Prepared);
                }
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                    return tx->validate();
                }));
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                    return tx->applyOriginalPrototypePlan();
                }));
                assert(tx->status() == OriginalCommandStateTransaction::Status::Pending
                    && !tx->journalEmpty());
                assert(!object.observeOriginalContent(range)
                    && !object.observeOriginalCommittedState(range));
                for (const auto& [input, completed] : sampled)
                    assert(!input->validateOriginalContent(completed));
                assert(core::StringView(tx->commitOriginalPendingOwnership())
                    == "RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED");
                assert(!tx->validate()
                    && tx->status() == OriginalCommandStateTransaction::Status::Pending);
                if (attempt)
                    tx.reset();
                else {
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept {
                        return tx->abandonOriginalPendingOwnership();
                    }));
                    assert(tx->journalEmpty());
                    tx.reset();
                }
                const auto restored = object.observeOriginalContent(range);
                assert(restored);
                assert(
                    restored->state() == common::OriginalTextureContentControl::State::Uninitialized
                    && !restored->contentGeneration());
                assert(
                    object.observeOriginalCommittedState(range)->state() == ResourceState::Undefined
                    && !q[3]->liveBytes());
                for (const auto& [input, completed] : sampled) {
                    assert(!input->validateOriginalContent(completed));
                    assert(input
                               ->observeOriginalCommittedState(
                                   defaultTextureSubresourceRange(input->desc()))
                               ->state()
                        == ResourceState::CopyDst);
                }
            }
            inputs.reset();
            pass.reset();
        }
        for (const auto& owner : q)
            assert(!owner->liveBytes());
        if (indexed && repeatCompletedAttachment) {
            auto next = *this;
            next.repeatCompletedAttachment = false;
            next(root, inventory, device);
            std::printf(
                "BINDLESS_VULKAN_ROOT_ATTACHMENT_CLEAR1 completed-generation=%llu restored-after-cancel same-resource new-graph original-plan pass-intent-exact actual-fence second-submit\n",
                static_cast<unsigned long long>(
                    object.observeOriginalContent(range)->contentGeneration()));
        }
        if (indexed)
            geometry->actors =
                {}; // Caller lease charges retire before the enclosing Root fixture's baseline check.
        if (execute) {
            if (indexed)
                std::printf(
                    "BINDLESS_VULKAN_ROOT_GEOMETRY_EXECUTION1 fault=%u fetch2 root5 closure3 content9 lifecycle9 cancel-reuse native-indexed firstInstance1 same-journal real-fence retirement\n",
                    expectedFaultBits);
            else if (recordSideEffects)
                std::printf(
                    "%s fault=%u material32 instance64 output48 nonuniform load-query-store native-fence original-journal\n",
                    recordRanges ? "BINDLESS_VULKAN_ROOT_GRAPHICS_RECORD_RANGES9"
                                 : "BINDLESS_VULKAN_ROOT_GRAPHICS_RECORDS9",
                    expectedFaultBits);
            else if (arraySampling)
                std::printf(
                    "BINDLESS_VULKAN_ROOT_GRAPHICS_ARRAY9 fault=%u layers2 mips2 independent-samplers coordinate-grad-before-discard native-fence original-journal\n",
                    expectedFaultBits);
            else if (bufferSideEffects)
                std::printf(
                    "BINDLESS_VULKAN_ROOT_GRAPHICS_BUFFERS9 fault=%u uniform16 readonly4 writable12 nonuniform load-query-store-atomic native-fence original-journal\n",
                    expectedFaultBits);
            else
                std::puts(
                    "BINDLESS_VULKAN_ROOT_GRAPHICS_EXECUTION9 genuine-draw same-journal real-fence fault0 content2 abandon retain retire");
        } else {
            std::puts(
                "BINDLESS_VULKAN_ROOT_GRAPHICS_CONTENT9 composition same-journal Pending rollback recapture capacity hot0 CPU-only");
            std::puts(
                "BINDLESS_VULKAN_ROOT_GRAPHICS_COLD_PASS1 native-create exact-stream capacity quota retire no-execution");
        }
    }
};
} // namespace arp::test
