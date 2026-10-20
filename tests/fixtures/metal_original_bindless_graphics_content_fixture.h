#pragma once
#include "completed_texture_root_program_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "metal_completed_graphics_fixture.h"
#include "original_bindless_root_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "original_graphics_content_visit_budget_fixture.h"
#include "original_graphics_runtime_memory_fixture.h"
#include "original_initialized_geometry_data_fixture.h"
#include "rhi/backends/metal/metal_device.h"
#include "rhi/backends/metal/metal_native_sync.h"
#include "rhi/backends/metal/metal_original_bindless_graphics_pass.h"
#include "rhi/frontend/bindless_graphics_execution.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace arp::test {
using MetalOriginalGeometryDataFixture =
    OriginalInitializedGeometryDataFixture<rhi_metal::MetalOriginalGeometryContent,
        MetalCompletedSampledGraphicsFixture>;

class MetalOriginalGraphicsBufferDataFixture {
public:
    static constexpr bool enabled = false, tableBufferData = true;
    bool retireDevice = false;

    bool originalDeviceLifetimeRetired() const noexcept { return retireDevice; }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        budget.maxCallbackTickets = budget.maxCallbackRegistrations = 0u;
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table& table) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = table.bufferLayout.minBindingSizeBytes;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        core::pmr::String diagnostic;
        std::array<float, 4u> color = index ? std::array<float, 4u>{0.f, 1.f, 0.f, 1.f}
                                            : std::array<float, 4u>{1.f, 0.f, 0.f, 1.f};
        const auto value = (table.tableId == 2u ? 10u : 100u) * (index + 1u);
        assert(buffer.desc().sizeBytes == (table.tableId == 1u ? sizeof(color) : sizeof(value)));
        const void* data = table.tableId == 1u ? static_cast<const void*>(color.data()) : &value;
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            data,
            buffer.desc().sizeBytes,
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }

    static void verify(const rhi::OriginalCommandBindlessRootSource& root)
    {
        if (!root.ordinaryDomains().empty()) {
            std::uint32_t checked = 0u;
            for (const auto& domain : root.ordinaryDomains())
                for (const auto& element : domain.source->frozenElements()) {
                    if (domain.set != 2u || element.declaration.write.binding != 4u)
                        continue;
                    const auto& lease =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            element.resourceOwner);
                    std::array<std::uint32_t, 4u> words{};
                    core::pmr::String diagnostic;
                    assert(rhi_metal::readMetalRhiNativeBuffer(
                        rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
                        words.data(),
                        sizeof(words),
                        0u,
                        lease->originalObject()->desc().sizeBytes,
                        diagnostic));
                    assert((words == std::array<std::uint32_t, 4u>{875u, 876u, 875u, 876u}));
                    ++checked;
                }
            assert(checked == 1u);
            return;
        }
        std::uint32_t checked = 0u;
        for (const auto& element : root.elements()) {
            if (element.declaration.write.binding != 4u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(element.owner);
            std::uint32_t value = 0u;
            core::pmr::String diagnostic;
            assert(rhi_metal::readMetalRhiNativeBuffer(
                rhi_metal::metalRhiNativePayloadFor(*lease->originalObject()),
                &value,
                sizeof(value),
                0u,
                sizeof(value),
                diagnostic));
            assert(value == 100u * (element.declaration.write.arrayElement + 1u) + 2u);
            ++checked;
        }
        assert(checked == 2u);
    }
};

class MetalOriginalGraphicsDomainsBufferDataFixture final
    : public MetalOriginalGraphicsBufferDataFixture {
public:
    explicit MetalOriginalGraphicsDomainsBufferDataFixture(
        bool retire = false, bool inspectMembers = false, bool fault = false)
        : inspectMembers_(inspectMembers), fault_(fault)
    {
        retireDevice = retire;
    }

    template <class Root, class Sources>
    void inspectOriginalRoot(const Root& root, const Sources& sources, rhi::GpuDevice* device) const
    {
        if (inspectMembers_)
            OriginalGraphicsRuntimeMemoryFixture::inspect(root, sources, device, fault_);
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        const std::uint32_t value = 777u + index;
        core::pmr::String diagnostic;
        assert(buffer.desc().sizeBytes == sizeof(value));
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            &value,
            sizeof(value),
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }

    void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t, const rhi::DescriptorBindingDesc&) const
    {
        desc.memory = rhi::MemoryUsage::CpuToGpu;
    }

    void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        std::array<std::uint32_t, 4u> words{};
        if (set == 0u && binding.binding == 3u) {
            const float value = 17.f;
            std::memcpy(words.data(), &value, sizeof(value));
        } else if (set == 2u && binding.binding == 3u)
            words[0u] = 32u;
        else
            assert(set == 2u && binding.binding == 4u);
        core::pmr::String diagnostic;
        assert(buffer.desc().sizeBytes == sizeof(words));
        assert(rhi_metal::writeMetalRhiNativeBuffer(rhi_metal::metalRhiNativePayloadFor(buffer),
            words.data(),
            sizeof(words),
            0u,
            buffer.desc().sizeBytes,
            diagnostic));
    }

private:
    bool inspectMembers_ = false, fault_ = false;
};

class MetalOriginalBindlessGraphicsContentFixture final {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Metal Root Graphics Content9: %s\n", id);
        assert(!id);
    }

public:
    bool execute = false, cancel = false;
    std::uint32_t expectedFaultBits = 0u;
    rhi::PushConstantsDesc push;
    bool dropView = false, retireDevice = false;
    bool arraySampling = false;
    std::shared_ptr<MetalOriginalGeometryDataFixture::Initialized> geometry;
    bool ordinaryColorSampling = false;
    std::uint32_t completedReadContractVersion = 0u;
    rhi::PipelineStage sampledReadStages = rhi::PipelineStage::FragmentShader;
    void (*verifyAdditionalBuffers)(const rhi::OriginalCommandBindlessRootSource&) = nullptr;
    void (*verifyOutputs)(
        const rhi::OriginalCommandBindlessRootSource&, const rhi::GpuTexture&, bool) = nullptr;

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        using namespace rhi;
        using K = CommandUsageKind;
        using NativePass = rhi_metal::MetalOriginalBindlessGraphicsPass;
        using Closure = rhi_metal::MetalRhiBindlessRootNativeClosure;
        const bool indexed = bool(geometry);
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        // Retained usage consumes the same caller-authored graph ceiling as
        // the canonical inventory, including larger completed-upload inputs.
        const auto bytes = inventory->originalBudget().maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)];
        assert(bytes);
        if (verifyOutputs && !ordinaryColorSampling) {
            assert(!rhi_metal::metalRhiNativePayloadFor(*root->originalObject()));
            const auto prepared = std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                root->originalPrepared());
            auto encoder =
                device->createCommandEncoder({"Completed Graphics standalone refusal", 1u, 1u});
            assert(encoder);
            const auto native = rhi_metal::metalRhiNativePayloadFor(*encoder);
            assert(native);
            core::pmr::String diagnostic;
            assert(!native->prepareGraphicsPassScope(
                rhi_metal::metalRhiPreparedGraphicsPayloadFor(*prepared),
                root->originalSnapshot(),
                BindlessAccessArray(nullptr, 0u),
                diagnostic));
            assert(diagnostic == "RHI-METAL-COMPLETED-TEXTURE-ORIGINAL-ROOT-REQUIRED");
        }
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
        const bool depth = object.desc().format == TextureFormat::D32Float;
        const auto state = depth ? ResourceState::DepthAttachment : ResourceState::ColorAttachment;
        const auto rootIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(root));
        const auto parentIndex = static_cast<std::uint32_t>(
            inventory->originalLeaseIndex(target->originalParentLease()));
        const auto viewIndex = static_cast<std::uint32_t>(inventory->originalLeaseIndex(target));
        const auto quota = [bytes] {
            return core::makeShared<core::CpuMemoryQuota>(bytes);
        };
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 6u> q;
        for (auto& owner : q)
            owner = quota();
        {
            const char* id = nullptr;
            RenderPassDesc passDesc;
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
            } else {
                RenderPassColorAttachmentDesc color;
                color.target = target->resource();
                color.format = object.desc().format;
                color.loadOp = LoadOp::Clear;
                color.storeOp = StoreOp::Store;
                if (arraySampling)
                    color.clearColor = {0.f, 0.f, 0.f, 0.f};
                else
                    color.clearColor = {0.f, 0.f, 0.f, 1.f};
                passDesc.colorAttachments.push_back(color);
            }
            auto pass = RenderPassArguments::prepareOriginal(
                passDesc, {1u, true, depth ? 0u : 1u, 0u, 0u, 0u, 0u, bytes}, q[0], id);
            require(id);
            assert(pass);
            ResourceBarrierDesc barrier{object.id(), range, ResourceState::Undefined, state};
            barrier.sourceStages = PipelineStage::TopOfPipe;
            barrier.sourceAccess = ResourceAccess::None;
            barrier.destinationStages =
                depth ? PipelineStage::DepthStencil : PipelineStage::ColorAttachmentOutput;
            barrier.destinationAccess =
                depth ? ResourceAccess::DepthStencilWrite : ResourceAccess::ColorAttachmentWrite;
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
            const auto& snapshot = root->originalSnapshot();
            const auto& latch =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultLatch);
            const auto& readback =
                snapshot->originalBufferSource(BindlessSnapshot::OriginalBufferRole::FaultReadback);
            const auto stages = PipelineStage::VertexShader | PipelineStage::FragmentShader;
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
            if (execute) {
                bufferBarrier(ResourceState::ShaderWrite,
                    ResourceState::CopyDst,
                    stages,
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite,
                    PipelineStage::Transfer,
                    ResourceAccess::TransferWrite);
                append(K::ClearSnapshotFault, {rootIndex});
                bufferBarrier(ResourceState::CopyDst,
                    ResourceState::ShaderWrite,
                    PipelineStage::Transfer,
                    ResourceAccess::TransferWrite,
                    stages,
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite);
            }
            const auto sampled = CompletedTextureRootProgramFixture::appendCompletedReads(
                *inventory, &object, sampledReadStages, [&](const auto& parent, const auto& read) {
                    descriptors.push_back(
                        {append(K::TextureBarrier,
                             {static_cast<std::uint32_t>(inventory->originalLeaseIndex(parent))}),
                            *ResourceDependencyArguments::capture(K::TextureBarrier, read)});
                });
            descriptors.push_back({append(K::TextureBarrier, {parentIndex}),
                *ResourceDependencyArguments::capture(K::TextureBarrier, barrier)});
            append(K::RetainIndexedSnapshot, {rootIndex});
            const auto scope = append(K::BeginRenderPass, {viewIndex});
            descriptors.push_back({scope, pass});
            const auto epoch = append(K::BindPreparedGraphicsBinding, {rootIndex});
            if (push.data)
                append(K::PushConstants, {rootIndex});
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
            append(indexed ? K::DrawIndexed : K::Draw, {});
            append(K::EndRenderPass, {});
            std::size_t faultCopy = SIZE_MAX;
            if (execute) {
                bufferBarrier(ResourceState::ShaderWrite,
                    ResourceState::CopySrc,
                    stages,
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
                    stages,
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
            OriginalCommandStateAdmissionBudget admission{
                1u, true, 1u, 512u, 64u, 16u, 256u, bytes};
            admission.maxTextureRanges = inventory->originalBudget().maxTextureRanges;
            admission.maxRetainedOwners =
                std::max(admission.maxRetainedOwners, inventory->originalBudget().maxGraphOwners);
            admission.bindlessRoots = {
                indexed ? 5u : 4u, true, 3u, root->size() + 1u, 8192u, 1u, 1u};
            std::uint64_t geometryAccesses = 0u;
            OriginalIndexedDrawFetchContract::Budget fetch;
            if (indexed) {
                admission.geometryUsage = {
                    1u, true, commands.size(), 2u, 6u, 1u, true, 1u, 2u, 64u};
                const auto projectedGeometry = inputs->visitGeometryAccesses(
                    admission.geometryUsage, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                require(projectedGeometry.diagnostic);
                admission.geometryUsage.maxBindingScans = projectedGeometry.bindingScans;
                geometryAccesses = projectedGeometry.bindings + projectedGeometry.drawAccesses;
                fetch = {2u, true, commands.size(), 1u, 1u, 64u, 3u};
            }
            if (push.data)
                admission.bindlessRoots.pushConsumption = {1u, true, 1u, push.sizeBytes, 1u, true};
            const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
                inputs, admission.bindlessRoots, [](const auto&) noexcept -> const char* {
                    return nullptr;
                });
            require(projected.diagnostic);
            admission.bindlessRoots.maxSourceVisits = projected.sourceVisits;
            auto graph = OriginalCommandResourceStates::prepareOriginal(
                inputs, *device, admission, q[1], id);
            require(id);
            assert(graph);
            OriginalCommandStateSequenceBudget sequence{1u,
                true,
                static_cast<std::uint32_t>(commands.size()),
                0u,
                256u,
                256u,
                projected.accesses + geometryAccesses,
                indexed ? 5u : 4u,
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
            sb.maxRenderPasses = sb.maxAttachmentReferences = 1u;
            sb.maxPipelineBinds = sb.maxConsumptions = 1u;
            sb.maxImplicitAccesses = projected.accesses + geometryAccesses;
            sb.maxCopyElements = execute ? 1u : 0u;
            sb.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
            sb.maxCpuMetadataBytes = bytes;
            sb.stateSequence = sequence;
            sb.bindlessRoots = admission.bindlessRoots;
            sb.geometryUsage = admission.geometryUsage;
            auto stream = OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, q[2], id);
            require(id);
            assert(stream);
            OriginalFiniteGraphicsContentPlanBudget content{
                9u, true, sampled.size(), 1u, 1u, sampled.size() * 2u, 2u, 512u, 8192u};
            content.bindlessCompletedReadContractVersion = completedReadContractVersion;
            content.bindlessGeometryContractVersion = indexed ? 1u : 0u;
            content.bindlessGeometryOptIn = indexed;
            OriginalGraphicsContentVisitBudgetFixture::configure(content,
                commands.size(),
                graph->textureEntries().size(),
                sampled.size(),
                passDesc.colorAttachments.size());
            if (completedReadContractVersion == 2u) {
                const auto baseline = q[3]->liveBytes();
                for (const auto oldVersion : {0u, 1u}) {
                    auto old = content;
                    old.bindlessCompletedReadContractVersion = oldVersion;
                    assert(
                        !OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                            graph, stream, *device, admission, sequence, old, q[3], id));
                    assert(id && q[3]->liveBytes() == baseline && !graph->validate());
                }
            }
            auto tx = OriginalCommandStateTransaction::prepareOriginalBindlessGraphicsOwnership(
                graph, stream, *device, admission, sequence, content, q[3], id);
            require(id);
            assert(tx && tx->originalContentPlanContractVersion() == 9u);
            auto count = static_cast<std::uint32_t>(root->elements().size() + 1u);
            for (const auto& domain : root->ordinaryDomains())
                count += domain.source->frozenElements().size();
            OriginalBindlessRootNativeClosureBudget closureBudget{
                indexed ? 3u : 1u, true, count + (indexed ? 2u : 0u), 512u, 1024u, bytes};
            if (indexed) {
                closureBudget.maxInitializedGeometryActors = 2u;
                closureBudget.maxInitializedGeometryBytes = 72u;
            }
            auto closure = Closure::prepareOriginal(root,
                inventory,
                *device,
                closureBudget,
                q[4],
                id,
                indexed
                    ? Closure::GeometryContents{geometry->actors.data(), geometry->actors.size()}
                    : Closure::GeometryContents{});
            require(id);
            assert(closure);
            OriginalBindlessGraphicsPassBudget budget{1u, true, 1u, 1u, 128u, bytes};
            auto physical = core::makeShared<core::AtomicCapacityBudget>(1u);
            const auto prepare = [&](const auto& b, const auto& owner, const auto& cap) {
                return NativePass::prepareOriginal(closure, tx, stream, *device, b, cap, owner, id);
            };
            for (const auto& owner : {q[0], q[1], q[2], q[3], q[4]}) {
                assert(!prepare(budget, owner, physical));
                assert(!physical->used());
            }
            {
                core::AtomicCapacityLease pressure(physical, 1u);
                assert(pressure.charged());
                assert(!prepare(budget, q[5], physical) && !q[5]->liveBytes());
            }
            {
                core::CpuMemoryQuotaLease pressure(q[5], bytes);
                assert(pressure.charged());
                assert(!prepare(budget, q[5], physical) && !physical->used());
            }
            auto native = prepare(budget, q[5], physical);
            require(id);
            assert(native);
            assert(!native->nativeExecutionProven() && native->nativeObjectCount() == 1u
                && physical->used() == 1u);
            const auto owners = native->retainedOwnerCount();
            assert(owners > 1u);
            budget.maxRetainedOwners = owners;
            native.reset();
            assert(!physical->used() && !q[5]->liveBytes());
            auto less = budget;
            --less.maxRetainedOwners;
            assert(!prepare(less, q[5], physical) && !q[5]->liveBytes() && !physical->used());
            {
                CpuQuotaFactoryFixture probe(q[5]);
                const auto peak = probe.measureSingleUse([] {},
                    [&] {
                        return prepare(budget, q[5], physical);
                    });
                probe.verifySingleUse(
                    peak,
                    [] {},
                    [&] {
                        return prepare(budget, q[5], physical);
                    });
            }
            native = prepare(budget, q[5], physical);
            require(id);
            assert(native);
            const auto live = q[5]->liveBytes();
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(q, [&]() noexcept -> const char* {
                for (unsigned i = 0u; i < 16u; ++i)
                    if (const auto* failure = NativePass::validateOriginal(native))
                        return failure;
                return nullptr;
            }));
            assert(q[5]->liveBytes() == live);
            auto payload = rhi_metal::metalRhiNativePayloadFor(*target->originalObject());
            const auto saved = payload->resource.nativeObject;
            payload->resource.nativeObject.reset();
            assert(NativePass::validateOriginal(native));
            payload->resource.nativeObject = saved;
            require(NativePass::validateOriginal(native));
            assert(rhi_metal::metalRhiRenderPassWriteDiagnostic(
                rhi_metal::metalRhiIssuingResourceRegistry(*device), passDesc));
            native.reset();
            assert(!physical->used() && !q[5]->liveBytes());
            if (execute) {
                using Public = OriginalBindlessGraphicsExecution;
                const auto closureOwners = closure->retainedOwnerCount();
                closure.reset();
                const auto resultQuota = quota(), submissionQuota = quota(), viewQuota = quota();
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
                auto result = OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                    request, resultQuota, id);
                require(id);
                assert(result);
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
                // Same complete custody formula as the native command factory:
                // closure, reservation, stream, readback, command and pass.
                const auto submissionOwners = closureOwners + tx->retainedOwnerCount()
                    + OriginalCommandRecordingSeal::additionalOwnerCountFor(life)
                    + OriginalCommandLifecycleReservation::additionalOwnerCount
                    + stream->retainedOwnerCount()
                    + OriginalCommandBufferReadbackBinding::retainedOwnerCount + 3u + owners + 3u;
                life.maxSubmissionOwners = std::max(life.maxSubmissionOwners, submissionOwners);
                OriginalBindlessGraphicsExecutionInputs in{root,
                    inventory,
                    tx,
                    binding,
                    nullptr,
                    0u,
                    q[4],
                    q[5],
                    submissionQuota,
                    viewQuota,
                    physical};
                OriginalBindlessGraphicsExecutionBudget limits;
                limits.requireWorkloadRetention = true;
                limits.intent = {1u, true};
                std::array<std::shared_ptr<const OriginalInitializedGeometrySource>, 2u> actors;
                if (indexed) {
                    for (std::size_t i = 0u; i < actors.size(); ++i)
                        actors[i] = geometry->actors[i];
                    in.geometry = actors.data();
                    in.geometryCount = actors.size();
                }
                limits.closure = closureBudget;
                limits.fetch = fetch;
                limits.pass = budget;
                limits.lifecycle = life;
                limits.capacity = {5u, true, 1u, 0u, 0u};
                limits.maxCpuViewBytes = bytes;
                limits.maxDiagnosticBytes = 512u;
                auto* extension = static_cast<BindlessCrossExtension*>(
                    device->queryInterface(kExtensionBindlessCross));
                assert(extension
                    && extension->capabilities().originalGraphicsExecutionContractVersion == 1u);
                core::pmr::String diagnostic;
                diagnostic.reserve(512u);
                for (unsigned field = 0u; field < 4u; ++field) {
                    auto less = limits;
                    if (!field)
                        --less.closure.maxResourceEntries;
                    else if (field == 1u)
                        --less.pass.maxRetainedOwners;
                    else if (field == 2u)
                        less.intent.prototypeOptIn = false;
                    else
                        less.lifecycle.maxSubmissionOwners = submissionOwners - 1u;
                    assert(!extension->prepareOriginalGraphicsExecution(
                        *device, in, less, diagnostic));
                    if (field == 3u)
                        assert(diagnostic == "RHI-METAL-ROOT-COMMAND-OWNER-CAPACITY");
                    assert(tx->status() == OriginalCommandStateTransaction::Status::Prepared
                        && !result->publishedBytes() && !physical->used() && !q[5]->liveBytes()
                        && !viewQuota->liveBytes());
                }
                {
                    auto wrong = in;
                    wrong.viewQuota = q[4];
                    assert(!extension->prepareOriginalGraphicsExecution(
                        *device, wrong, limits, diagnostic));
                }
                auto issuer =
                    extension->prepareOriginalGraphicsExecution(*device, in, limits, diagnostic);
                if (!issuer)
                    std::fprintf(stderr, "Metal public Root Graphics: %s\n", diagnostic.c_str());
                assert(issuer);
                assert(!issuer->recordingProven() && !issuer->pollCompletion()
                    && !result->publishedBytes());
                assert(Public::submit(issuer, {1u, true}));
                auto forged = std::shared_ptr<const Public>(issuer.get(), [](const Public*) {});
                assert(Public::record(forged, {1u, true}));
                forged.reset();
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{viewQuota, submissionQuota, q[3]}, [&]() noexcept {
                        return Public::record(issuer, {1u, true});
                    }));
                assert(issuer->recordingProven()
                    && tx->status() == OriginalCommandStateTransaction::Status::Pending
                    && !result->publishedBytes() && !object.observeOriginalContent(range));
                assert(Public::record(issuer, {1u, true}));
                if (cancel) {
                    require(issuer->cancel());
                    assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned
                        && !result->publishedBytes());
                } else {
                    auto gate =
                        rhi_metal::createMetalRhiNativeSharedEventPayload("original-graphics-gate");
                    auto blocker = rhi_metal::createMetalRhiNativeCommandPayload(
                        "original-graphics-blocker", {});
                    assert(gate && blocker);
                    auto blocked = blocker->preparedCompletionState();
                    rhi_metal::MetalRhiNativeQueueStats queue;
                    assert(blocker->finish(diagnostic)
                        && blocker->submit(queue, blocked, gate, 1u, {}, 0u, diagnostic));
                    const std::array quotas{
                        viewQuota, submissionQuota, q[3], q[4], q[5], resultQuota, q[0]};
                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                        return Public::submit(issuer, {1u, true});
                    }));
                    assert(
                        issuer->cancel() && !issuer->pollCompletion() && !result->publishedBytes());
                    if (dropView)
                        issuer.reset();
                    assert(q[4]->liveBytes() && q[5]->liveBytes() && physical->used());
                    if (retireDevice)
                        device->retireOriginalDeviceLifetime();
                    const auto closurePressure = q[4]->limitBytes() - q[4]->liveBytes();
                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        quotas, [&]() noexcept -> const char* {
                            rhi_metal::signalMetalRhiNativeSharedEvent(gate, 1u);
                            const auto deadline =
                                std::chrono::steady_clock::now() + std::chrono::seconds(10);
                            while (q[4]->liveBytes() != closurePressure) {
                                if (std::chrono::steady_clock::now() > deadline)
                                    return "Metal original Graphics completion timeout";
                                std::this_thread::yield();
                            }
                            return nullptr;
                        }));
                    assert(tx->status()
                        == (retireDevice ? OriginalCommandStateTransaction::Status::Invalidated
                                         : OriginalCommandStateTransaction::Status::Committed));
                    assert(bool(result->publishedBytes()) == !retireDevice);
                    if (issuer) {
                        const auto deadline =
                            std::chrono::steady_clock::now() + std::chrono::seconds(10);
                        while (!issuer->pollCompletion()) {
                            assert(std::chrono::steady_clock::now() < deadline);
                            std::this_thread::yield();
                        }
                        assert(issuer->submissionCompleted() == !retireDevice
                            && bool(issuer->completionDiagnostic()) == retireDevice);
                    }
                    if (!retireDevice) {
                        std::uint32_t bits = ~0u;
                        std::memcpy(&bits, result->publishedBytes(), sizeof(bits));
                        assert(bits == expectedFaultBits);
                    }
                    if (verifyOutputs)
                        verifyOutputs(*root, object, !retireDevice);
                    else if (verifyAdditionalBuffers && !retireDevice)
                        verifyAdditionalBuffers(*root);
                    else if (!depth)
                        MetalOriginalGraphicsBufferDataFixture::verify(*root);
                    blocked->wait();
                    blocker.reset();
                    std::printf(
                        "BINDLESS_METAL_PUBLIC_ROOT_GRAPHICS1 root=%u fault=%u drop=%d retire-device=%d %s completion=real hot-completion0\n",
                        root->sourceContractVersion(),
                        expectedFaultBits,
                        dropView,
                        retireDevice,
                        verifyAdditionalBuffers               ? "additional-buffers=verified"
                            : verifyOutputs                   ? "sampled-ordinary=verified"
                            : depth                           ? "depth=consumer-readback-next"
                            : root->ordinaryDomains().empty() ? "counters=102,202"
                                                              : "ordinary=875,876,875,876");
                }
                issuer.reset();
                in = {};
                assert(!physical->used() && !q[5]->liveBytes() && !submissionQuota->liveBytes()
                    && !viewQuota->liveBytes());
            }
            if (!retireDevice)
                for (const auto& input : sampled)
                    require(input.first->validateOriginalContent(input.second));
            const auto observation = object.observeOriginalContent(range);
            if (retireDevice) {
                assert(!observation);
            } else {
                const bool completed = execute && !cancel;
                assert(observation && observation->contentGeneration() == (completed ? 1u : 0u)
                    && observation->state()
                        == (completed
                                ? common::OriginalTextureContentControl::State::Completed
                                : common::OriginalTextureContentControl::State::Uninitialized));
            }
        }
        for (const auto& owner : q)
            assert(!owner->liveBytes());
    }
};
} // namespace arp::test
