#pragma once
#include "bindless_package_request_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_produced_program.h"
#include "rhi/frontend/bindless_root_readback_sequence.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/visibility/gpu_visibility_root_consumer.h"
#include "rhi/frontend/visibility/gpu_visibility_root_readback.h"
#include "rhi/gpu_extensions.h"

#include <cstdio>

namespace arp::test {
// Authentic CPU admission through the public snapshot/Root/Sources/Inputs8 and
// RootState6 APIs. Null is deliberately used only for graph correspondence;
// these resources cannot certify initialized native bytes or GPU execution.
class OriginalBindlessVisibilityRootsFixture final {
    static void require(const char* id, unsigned line = 0u)
    {
        if (id)
            std::fprintf(stderr, "Visibility Root composition at %u: %s\n", line, id);
        assert(!id);
    }

public:
    static void verify(const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        bool separateConsumerSnapshot = false)
    {
        using namespace rhi;
        using namespace rhi::visibility;
        using Sources = OriginalCommandResourceSources;
        using Root = OriginalCommandBindlessRootSource;
        using K = CommandUsageKind;
        using Scalar = CommandScalarArguments;
        constexpr std::uint64_t cpu = 4u * 1024u * 1024u;
        const auto quota = [=] {
            return core::makeShared<core::CpuMemoryQuota>(cpu);
        };
        auto sourceQuota = quota(), inputQuota = quota(), graphQuota = quota(),
             streamQuota = quota();
        auto device = createNullDevice();
        const char* id = nullptr;
        require(device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpu}, quota()));
        // Original workload buffers plus three independent snapshot buffers
        // for the cross-snapshot scope identity check below.
        OriginalResourceRegistrationBudget registry{3u, true, 19u, 2u, 2u, 256u, cpu};
        registry.maxDescriptorArenas = registry.maxDescriptorSets = 3u;
        require(device->prepareOriginalResourceRegistration(registry, quota()));
        auto* extension =
            static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
        assert(extension && package->gpuVisibilityConsumerContractVersion() == 2u);
        const auto& artifact = package->artifact();
        auto request =
            BindlessPackageRequestFixture::make(artifact, extension->capabilities().deviceIdentity);
        BindlessSnapshotPool pool(*device, request.budget);
        core::pmr::String diagnostic;
        auto snapshot = core::shareUnique(
            pool.prepareOriginalDescriptors(request, artifact, {2u, true, true}, diagnostic));
        assert(snapshot && diagnostic.empty());
        std::shared_ptr<BindlessSnapshot> secondarySnapshot;

        GpuVisibilityDrawGroup group{};
        group.indexCount = 3u;
        group.globalInstanceCount = 2u;
        group.localBoundsCenterRadius[3u] = 1.f;
        const std::array<std::uint32_t, 2u> owners{};
        const std::uint32_t indexCount = 3u;
        GpuVisibilityLayoutContract::Input layout{
            &group, owners.data(), &indexCount, 1u, 2u, 1u, 1u, 3u, 2u, 1u};
        const GpuVisibilityLayoutContract::Budget layoutBudget{1u, true, 16u, 16u, 1u};
        GpuVisibilityLayoutContract::Report sizes;
        require(GpuVisibilityLayoutContract::inspect(layout, layoutBudget, sizes));
        const std::array<std::uint64_t, 9u> bufferBytes{sizes.drawGroupBytes,
            sizes.inputInstanceBytes,
            sizes.ownerMapBytes,
            sizes.countBytes,
            sizes.visibleInstanceBytes,
            sizes.indirectBytes,
            sizes.zeroBaseMappingBytes,
            sizes.controlBytes,
            sizes.parameterBytes};
        core::pmr::Vector<Sources::Source> sources;
        std::array<std::uint32_t, 10u> roles{};
        const OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 2u, cpu};
        const auto buffer = [&](BufferDesc desc) {
            auto object = device->createBuffer(desc);
            assert(object);
            auto lease =
                OriginalCommandBufferLease::prepareOriginal(object, leaseBudget, sourceQuota, id);
            require(id, __LINE__);
            assert(lease);
            const auto result = static_cast<std::uint32_t>(sources.size());
            sources.push_back(lease);
            return result;
        };
        const auto texture = [&](bool attachment) {
            TextureDesc desc;
            desc.extent = {2u, 2u, 1u};
            desc.format = TextureFormat::Rgba8Unorm;
            desc.usage = attachment ? TextureUsage::RenderAttachment : TextureUsage::Sampled;
            desc.initialState =
                attachment ? ResourceState::ColorAttachment : ResourceState::ShaderRead;
            auto object = device->createTexture(desc);
            assert(object);
            TextureViewDesc vd;
            vd.format = desc.format;
            vd.usage = desc.usage;
            auto view = device->createTextureView(*object, vd);
            assert(view);
            auto parent =
                OriginalCommandTextureLease::prepareOriginal(object, leaseBudget, sourceQuota, id);
            require(id, __LINE__);
            auto child = OriginalCommandTextureViewLease::prepareOriginal(
                view, leaseBudget, sourceQuota, id);
            require(id, __LINE__);
            auto joined = OriginalCommandTextureViewSource::prepareOriginal(
                child, parent, {1u, true, 5u, cpu}, sourceQuota, id);
            require(id, __LINE__);
            assert(joined);
            sources.push_back(parent);
            const auto result = static_cast<std::uint32_t>(sources.size());
            sources.push_back(joined);
            return result;
        };
        for (const auto& table : artifact.tables) {
            assert(table.capacity == 1u && table.tableId && table.tableId <= roles.size());
            const auto role = table.tableId - 1u;
            if (role == 9u) {
                roles[role] = texture(false);
                const auto& view =
                    std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        sources.back());
                assert(snapshot
                        ->writeTexture(snapshot->handle(table.tableId, 0u),
                            *view->originalObject(),
                            ResourceState::ShaderRead)
                        .success);
                continue;
            }
            const auto* binding =
                findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
            assert(binding);
            const bool writable = binding->type == DescriptorType::StorageBuffer;
            const auto usage = binding->type == DescriptorType::UniformBuffer
                ? ResourceUsage::UniformBuffer
                : ResourceUsage::StorageBuffer;
            roles[role] = buffer({"visibility-role",
                bufferBytes[role],
                role == 5u       ? usage | ResourceUsage::IndirectBuffer
                    : role == 7u ? usage | ResourceUsage::CopySrc
                                 : usage,
                MemoryUsage::GpuOnly,
                writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead});
            const auto& lease =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(sources.back());
            assert(
                snapshot->writeBuffer(snapshot->handle(table.tableId, 0u), *lease->originalObject())
                    .success);
        }
        assert(snapshot->seal(*device).success);
        {
            // Actual independently issued snapshots can belong to one command
            // scope without sharing a generation or their internal buffers.
            // These projections test identity only, never native custody.
            BindlessSnapshotPool secondPool(*device, request.budget);
            auto second = core::shareUnique(secondPool.prepareOriginalDescriptors(
                request, artifact, {2u, true, true}, diagnostic));
            assert(second && diagnostic.empty());
            for (const auto& table : artifact.tables) {
                const auto& source = sources[roles[table.tableId - 1u]];
                const auto handle = second->handle(table.tableId, 0u);
                if (const auto* view =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                            &source))
                    assert(second
                            ->writeTexture(
                                handle, *(*view)->originalObject(), ResourceState::ShaderRead)
                            .success);
                else {
                    const auto& buffer =
                        std::get<std::shared_ptr<const OriginalCommandBufferLease>>(source);
                    assert(second->writeBuffer(handle, *buffer->originalObject()).success);
                }
            }
            assert(second->seal(*device).success && second->generation() != snapshot->generation());

            struct Projection {
                std::shared_ptr<const BindlessSnapshot> snapshot;

                const auto& originalSnapshot() const noexcept { return snapshot; }
            };

            std::array<Projection, 3u> projections{{{snapshot}, {second}, {snapshot}}};
            using Scope = OriginalBindlessSnapshotScopeContract;
            const auto inspect = [&](const Scope::Intent& intent) {
                return Scope::inspect(
                    projections.size(),
                    [&](std::size_t i) noexcept {
                        return &projections[i];
                    },
                    intent);
            };
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const Scope::Intent intent{1u, true, 2u, 6u};
            const auto joined = inspect(intent);
            require(joined.diagnostic);
            assert(joined.snapshots == 2u && joined.roots == 3u && joined.visits == 6u);
            assert(inspect({1u, true, 1u, 6u}).diagnostic);
            assert(inspect({1u, true, 2u, 5u}).diagnostic);
            assert(inspect({1u, false, 2u, 6u}).diagnostic);
            projections[1u].snapshot =
                std::shared_ptr<const BindlessSnapshot>(sourceQuota, snapshot.get());
            assert(inspect(intent).diagnostic);
            projections[1u].snapshot = second;
            assert(!inspect(intent).diagnostic);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            if (separateConsumerSnapshot)
                secondarySnapshot = second;
        }
        require(BindlessSnapshot::prepareOriginalBufferSources(
            snapshot, *device, leaseBudget, sourceQuota));
        for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                 BindlessSnapshot::OriginalBufferRole::FaultLatch,
                 BindlessSnapshot::OriginalBufferRole::FaultReadback}) {
            assert(snapshot->originalBufferSource(role));
            sources.push_back(snapshot->originalBufferSource(role));
        }
        const auto faultIndex = static_cast<std::uint32_t>(sources.size() - 2u);
        const auto readbackIndex = static_cast<std::uint32_t>(sources.size() - 1u);
        std::uint32_t secondaryFault = UINT32_MAX, secondaryReadback = UINT32_MAX;
        if (secondarySnapshot) {
            require(BindlessSnapshot::prepareOriginalBufferSources(
                secondarySnapshot, *device, leaseBudget, sourceQuota));
            for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                     BindlessSnapshot::OriginalBufferRole::FaultLatch,
                     BindlessSnapshot::OriginalBufferRole::FaultReadback})
                sources.push_back(secondarySnapshot->originalBufferSource(role));
            secondaryFault = static_cast<std::uint32_t>(sources.size() - 2u);
            secondaryReadback = static_cast<std::uint32_t>(sources.size() - 1u);
        }
        const auto target = texture(true);
        const auto vertices = buffer({"visibility-vertices",
            36u,
            ResourceUsage::VertexBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        const auto indices = buffer({"visibility-indices",
            6u,
            ResourceUsage::IndexBuffer,
            MemoryUsage::GpuOnly,
            ResourceState::ShaderRead});
        const auto controlReadback = buffer({"visibility-control-readback",
            sizes.controlBytes,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu,
            ResourceState::CopyDst});

        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = admission.maxReferences = admission.maxInputDescriptors = 256u;
        admission.maxPushBytes = sizeof(GpuVisibilityDrawSelection);
        admission.maxGraphOwners = 8192u;
        admission.maxStateObservations = 512u;
        admission.maxTextureRanges = 16u;
        admission.maxStateTransactions = 1u;
        admission.maxPassColorAttachments = admission.maxPassAttachmentReferences = 1u;
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Source)] = cpu;
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Graph)] = cpu;
        admission.maxCpuBytes[static_cast<std::size_t>(
            OriginalCommandAdmissionBudget::CpuDomain::Recording)] = cpu;
        for (const auto& source : sources)
            ++admission.maxSourceOwners[static_cast<std::size_t>(Sources::domain(source))];
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] =
            1u;
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] =
            5u;
        auto inventory = Sources::prepareColdOriginalTyped(
            sources.data(), sources.size(), admission, {6u, true, 6u}, sourceQuota, id);
        require(id, __LINE__);
        assert(inventory);
        const auto declarations = [&](const auto& selected) {
            core::pmr::Vector<OriginalCommandDescriptorSetElement> elements;
            for (const auto& binding : artifact.physicalLayout.bindings) {
                const auto observed = selected->descriptorSet()->observeSuccessfulBindingWrite(
                    binding.binding, 0u, id);
                require(id, __LINE__);
                assert(observed);
                OriginalCommandDescriptorSetElement e;
                e.write = observed->write();
                for (std::size_t i = 0u; i < sources.size(); ++i) {
                    if (Sources::resource(sources[i]) != e.write.resource)
                        continue;
                    e.sourceIndex = i;
                    std::visit(
                        [&](const auto& value) {
                            using T = std::remove_const_t<
                                typename std::decay_t<decltype(value)>::element_type>;
                            if constexpr (T::domain == CommandReferenceDomain::Buffer
                                || T::domain == CommandReferenceDomain::TextureView)
                                e.registrationGeneration = value->originalObject()
                                                               ->originalResourceRegistration()
                                                               .generation();
                            if constexpr (T::domain == CommandReferenceDomain::Buffer)
                                e.bufferAccessBytes = value->layout().sizeBytes;
                        },
                        sources[i]);
                    break;
                }
                assert(e.registrationGeneration);
                e.stages = bindlessPipelineStagesForShader(binding.stages);
                e.access = binding.type == DescriptorType::StorageBuffer
                    ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                    : ResourceAccess::ShaderRead;
                elements.push_back(e);
            }
            return elements;
        };
        const auto& aliasDefinition = artifact.additionalDomains.front();
        assert(artifact.additionalDomains.size() == 1u && aliasDefinition.set == 2u);
        std::shared_ptr<DescriptorArena> arena;
        core::UniquePtr<DescriptorSet> set;
        {
            core::CpuMemoryQuotaScope scope(sourceQuota);
            arena = core::shareUnique(
                device->createOriginalDescriptorArena({"visibility-aliases", 1u, 4u}));
            assert(arena);
            set = device->allocateOriginalDescriptorSet(
                arena, {"visibility-aliases", aliasDefinition.layout});
            assert(set);
        }
        std::array<OriginalCommandDescriptorSetElement, 4u> aliases;
        for (std::size_t i = 0u; i < aliases.size(); ++i) {
            const auto source = roles[GpuVisibilityConsumerAliasContract::roles[i]];
            const auto& lease =
                std::get<std::shared_ptr<const OriginalCommandBufferLease>>(sources[source]);
            auto& e = aliases[i];
            e.sourceIndex = source;
            e.write.binding = static_cast<std::uint32_t>(i);
            e.write.type =
                i == 3u ? DescriptorType::UniformBuffer : DescriptorType::ReadOnlyStorageBuffer;
            e.write.resourceKind = DescriptorResourceKind::Buffer;
            e.write.resource = lease->resource();
            e.write.resourceState = ResourceState::ShaderRead;
            e.registrationGeneration =
                lease->originalObject()->originalResourceRegistration().generation();
            e.stages = PipelineStage::VertexShader;
            e.access = ResourceAccess::ShaderRead;
            e.bufferAccessBytes = lease->layout().sizeBytes;
            assert(set->write(e.write).success);
        }
        auto ordinary = OriginalCommandDescriptorSetSource::prepareOriginal(set,
            arena,
            inventory,
            aliases.data(),
            aliases.size(),
            {2u,
                true,
                4u,
                4u,
                OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount,
                cpu},
            sourceQuota,
            *device,
            id);
        require(id, __LINE__);
        assert(ordinary);
        const Root::OrdinaryDomain domain{2u, inventory->sources().size(), ordinary};
        require(Sources::appendOriginalTyped(inventory, ordinary, *device));
        auto authorization = extension->authorize(package, *snapshot, diagnostic);
        assert(authorization && diagnostic.empty());
        const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
        BindlessComputeBindingPool computes(
            *extension, {4u, 4u * (retained + cpu), retained + cpu, 8u, cpu});
        BindlessGraphicsBindingPool graphics(
            *extension, {1u, retained + cpu, retained + cpu, 8u, cpu});
        std::array<std::shared_ptr<const Root>, 5u> roots;
        constexpr std::array<core::StringView, 4u> entries{"visibilityResetComputeMain",
            "visibilityCullComputeMain",
            "visibilityEmitComputeMain",
            "visibilityFinalizeComputeMain"};
        for (std::size_t i = 0u; i < roots.size(); ++i) {
            const auto& selected = i == 4u && secondarySnapshot ? secondarySnapshot : snapshot;
            const auto elements = declarations(selected);
            auto selectedAuthorization = i == 4u && secondarySnapshot
                ? extension->authorize(package, *selected, diagnostic)
                : authorization;
            assert(selectedAuthorization && diagnostic.empty());
            Root::Prepared prepared;
            if (i < 4u) {
                auto pipeline = core::shareUnique(
                    extension->createComputePipeline(authorization, entries[i], diagnostic));
                assert(pipeline && diagnostic.empty());
                prepared = computes.prepare(
                    {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()},
                    diagnostic);
            } else {
                RenderPipelineDesc state;
                state.colorFormat = TextureFormat::Rgba8Unorm;
                state.dynamicViewportScissor = true;
                VertexBufferLayoutDesc vb;
                vb.strideBytes = 12u;
                vb.attributes.push_back({0u, 0u, VertexFormat::Float32x3});
                state.vertexBuffers.push_back(vb);
                auto pipeline =
                    core::shareUnique(extension->createRenderPipeline(selectedAuthorization,
                        state,
                        "visibilityVertexMain",
                        "visibilityFragmentMain",
                        {1u, true, 16u},
                        diagnostic));
                assert(pipeline && diagnostic.empty());
                prepared = graphics.prepare(
                    {true, selected, pipeline, selected->plan(), selected->generation()},
                    diagnostic);
            }
            if (!diagnostic.empty())
                std::fprintf(stderr, "Visibility binding %zu: %s\n", i, diagnostic.c_str());
            assert(diagnostic.empty());
            roots[i] = Root::prepareOriginal(selected,
                prepared,
                inventory,
                elements.data(),
                elements.size(),
                &domain,
                1u,
                {2u,
                    true,
                    static_cast<std::uint32_t>(elements.size()),
                    Root::retainedOwnerCount + Root::cpuOwnerSlots,
                    cpu,
                    1u,
                    4u},
                sourceQuota,
                *device,
                id);
            require(id, __LINE__);
            assert(roots[i]);
            require(Sources::appendOriginalTyped(inventory, roots[i], *device));
            const auto stages =
                i < 4u ? ShaderStage::Compute : ShaderStage::Vertex | ShaderStage::Fragment;
            const auto accessVersion = i < 4u ? 3u : 2u;
            assert(roots[i]->originalShaderEntryAccessVersion() == accessVersion);
            for (std::size_t index = 0u; index < roots[i]->size(); ++index) {
                BindlessResourceAccess access;
                std::size_t source = SIZE_MAX;
                bool active = false;
                require(roots[i]->readOriginalShaderDeclaration(
                            index, stages, access, source, active, accessVersion),
                    __LINE__);
                assert(roots[i]->readOriginalShaderDeclaration(
                    index, stages, access, source, active, 4u));
            }
        }
        verifyStream(*device,
            roots,
            inventory,
            layout,
            layoutBudget,
            roles,
            faultIndex,
            readbackIndex,
            controlReadback,
            target,
            vertices,
            indices,
            inputQuota,
            graphQuota,
            streamQuota,
            secondaryFault,
            secondaryReadback);
    }

private:
    static void verifyStream(rhi::GpuDevice& device,
        const std::array<std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>, 5u>& roots,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        const rhi::visibility::GpuVisibilityLayoutContract::Input& layout,
        const rhi::visibility::GpuVisibilityLayoutContract::Budget& layoutBudget,
        const std::array<std::uint32_t, 10u>& roles,
        std::uint32_t fault,
        std::uint32_t readback,
        std::uint32_t controlReadback,
        std::uint32_t target,
        std::uint32_t vertices,
        std::uint32_t indices,
        const std::shared_ptr<core::CpuMemoryQuota>& inputQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& graphQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& streamQuota,
        std::uint32_t secondaryFault,
        std::uint32_t secondaryReadback)
    {
        using namespace rhi;
        using namespace rhi::visibility;
        using K = CommandUsageKind;
        using Scalar = CommandScalarArguments;
        using Root = OriginalCommandBindlessRootSource;
        const char* id = nullptr;
        const auto cpu = inputQuota->limitBytes();
        const bool multipleSnapshots = secondaryFault != UINT32_MAX;
        const auto snapshotCount = multipleSnapshots ? 2u : 1u;
        core::pmr::Vector<CommandUsage> commands;
        core::pmr::Vector<OriginalCommandUsage::Reference> refs;
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        core::pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
        const auto append = [&](K kind,
                                std::initializer_list<std::uint32_t> operands,
                                std::optional<Scalar> value = {}) {
            const auto ordinal = commands.size();
            commands.push_back({kind,
                static_cast<std::uint32_t>(refs.size()),
                static_cast<std::uint32_t>(operands.size())});
            for (const auto source : operands)
                refs.push_back({source});
            if (!value)
                value = Scalar::capture(kind, Scalar::None{});
            assert(value && value->kind() == kind);
            fields.push_back({*value});
            return ordinal;
        };
        const auto lease = [&](std::uint32_t source) -> const auto& {
            return std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                inventory->sources()[source]);
        };
        const auto barrier = [&](std::uint32_t source,
                                 ResourceState before,
                                 ResourceState after,
                                 PipelineStage from,
                                 ResourceAccess fromAccess,
                                 PipelineStage to,
                                 ResourceAccess toAccess) {
            ResourceBarrierDesc b;
            b.resource = lease(source)->resource();
            b.before = before;
            b.after = after;
            b.sourceStages = from;
            b.sourceAccess = fromAccess;
            b.destinationStages = to;
            b.destinationAccess = toAccess;
            descriptors.push_back({append(K::BufferBarrier, {source}),
                *ResourceDependencyArguments::capture(K::BufferBarrier, b)});
        };
        const auto rw = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
        const auto cs = PipelineStage::ComputeShader;
        barrier(fault,
            ResourceState::ShaderWrite,
            ResourceState::CopyDst,
            cs,
            rw,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite);
        const auto root0 = static_cast<std::uint32_t>(inventory->originalLeaseIndex(roots[0u]));
        append(K::ClearSnapshotFault, {root0});
        barrier(fault,
            ResourceState::CopyDst,
            ResourceState::ShaderWrite,
            PipelineStage::Transfer,
            ResourceAccess::TransferWrite,
            cs,
            rw);
        if (multipleSnapshots) {
            barrier(secondaryFault,
                ResourceState::ShaderWrite,
                ResourceState::CopyDst,
                cs,
                rw,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite);
            append(K::ClearSnapshotFault,
                {static_cast<std::uint32_t>(inventory->originalLeaseIndex(roots[4u]))});
            barrier(secondaryFault,
                ResourceState::CopyDst,
                ResourceState::ShaderWrite,
                PipelineStage::Transfer,
                ResourceAccess::TransferWrite,
                cs,
                rw);
        }
        for (std::size_t phase = 0u; phase < 4u; ++phase) {
            append(K::BeginComputePass, {});
            append(K::BindPreparedComputeBinding,
                {static_cast<std::uint32_t>(inventory->originalLeaseIndex(roots[phase]))});
            GpuVisibilityDispatchContract::Dispatch dispatch;
            require(GpuVisibilityDispatchContract::derive(
                layout, static_cast<GpuVisibilityDispatchContract::Phase>(phase), 64u, dispatch));
            append(K::Dispatch,
                {},
                Scalar::capture(K::Dispatch, Scalar::Dispatch{dispatch.x, dispatch.y, dispatch.z}));
            append(K::EndComputePass, {});
            // Explicit dependencies also cover resources whose active role
            // changes between entries; RootState6 proves their exact scopes.
            for (const auto role : {3u, 4u, 5u, 6u, 7u})
                barrier(roles[role],
                    ResourceState::ShaderWrite,
                    ResourceState::ShaderWrite,
                    cs,
                    rw,
                    cs,
                    rw);
            barrier(fault, ResourceState::ShaderWrite, ResourceState::ShaderWrite, cs, rw, cs, rw);
        }
        for (const auto role : {4u, 6u})
            barrier(roles[role],
                ResourceState::ShaderWrite,
                ResourceState::ShaderRead,
                cs,
                rw,
                PipelineStage::VertexShader,
                ResourceAccess::ShaderRead);
        barrier(roles[5u],
            ResourceState::ShaderWrite,
            ResourceState::ShaderRead,
            cs,
            rw,
            PipelineStage::DrawIndirect,
            ResourceAccess::IndirectCommandRead);
        RenderPassDesc passDesc;
        RenderPassColorAttachmentDesc color;
        color.target = std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
            inventory->sources()[target])
                           ->resource();
        color.format = TextureFormat::Rgba8Unorm;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        passDesc.colorAttachments.push_back(color);
        auto pass = RenderPassArguments::prepareOriginal(
            passDesc, {1u, true, 1u, 0u, 0u, 0u, 0u, cpu}, inputQuota, id);
        require(id, __LINE__);
        assert(pass);
        const auto scope = append(K::BeginRenderPass, {target});
        descriptors.push_back({scope, pass});
        const auto graphics = static_cast<std::uint32_t>(inventory->originalLeaseIndex(roots[4u]));
        const auto epoch = append(K::BindPreparedGraphicsBinding, {graphics});
        append(K::SetViewport,
            {},
            Scalar::capture(K::SetViewport, ViewportState{0.f, 0.f, 2.f, 2.f, 0.f, 1.f}));
        append(K::SetScissor, {}, Scalar::capture(K::SetScissor, ScissorState{0, 0, 2u, 2u}));
        const auto bind = [&](K kind, std::uint32_t source) {
            const auto ordinal = append(kind,
                {source},
                kind == K::BindVertexBuffer
                    ? Scalar::capture(kind, Scalar::VertexBinding{0u, 0u})
                    : Scalar::capture(kind, Scalar::IndexBinding{IndexFormat::Uint16, 0u}));
            descriptors.push_back({ordinal,
                OriginalCommandGeometryBinding{kind,
                    source,
                    graphics,
                    epoch,
                    scope,
                    lease(source)->originalObject()->originalResourceRegistration().generation(),
                    0u,
                    roots[4u]->originalSnapshot()->generation()}});
        };
        bind(K::BindVertexBuffer, vertices);
        bind(K::BindIndexBuffer, indices);
        const GpuVisibilityDrawSelection selection{};
        append(K::PushConstants,
            {graphics},
            Scalar::capture(K::PushConstants, Scalar::Push{ShaderStage::Vertex, 0u, 16u}));
        fields.back().bytes = &selection;
        fields.back().byteCount = sizeof(selection);
        append(K::DrawIndexedIndirect,
            {roles[5u]},
            Scalar::capture(K::DrawIndexedIndirect, Scalar::DrawIndirect{0u, 1u, 20u}));
        append(K::EndRenderPass, {});
        barrier(fault,
            ResourceState::ShaderWrite,
            ResourceState::CopySrc,
            cs,
            rw,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead);
        barrier(roles[7u],
            ResourceState::ShaderWrite,
            ResourceState::CopySrc,
            cs,
            rw,
            PipelineStage::Transfer,
            ResourceAccess::TransferRead);
        if (multipleSnapshots)
            barrier(secondaryFault,
                ResourceState::ShaderWrite,
                ResourceState::CopySrc,
                cs,
                rw,
                PipelineStage::Transfer,
                ResourceAccess::TransferRead);
        append(K::BeginTransferPass, {});
        const auto controlCopy = append(K::BufferCopy, {roles[7u], controlReadback});
        descriptors.push_back({controlCopy,
            *TransferCopyArguments::capture(BufferCopyDesc{lease(roles[7u])->resource(),
                lease(controlReadback)->resource(),
                0u,
                0u,
                lease(controlReadback)->layout().sizeBytes})});
        const auto faultCopy = append(K::BufferCopy, {fault, readback});
        descriptors.push_back({faultCopy,
            *TransferCopyArguments::capture(BufferCopyDesc{
                lease(fault)->resource(), lease(readback)->resource(), 0u, 0u, 4u})});
        std::size_t secondaryCopy = SIZE_MAX;
        if (multipleSnapshots) {
            secondaryCopy = append(K::BufferCopy, {secondaryFault, secondaryReadback});
            descriptors.push_back({secondaryCopy,
                *TransferCopyArguments::capture(BufferCopyDesc{lease(secondaryFault)->resource(),
                    lease(secondaryReadback)->resource(),
                    0u,
                    0u,
                    4u})});
        }
        append(K::EndTransferPass, {});
        append(K::Finish, {});
        OriginalCommandInputsBudget ib{8u, true, 5u};
        ib.maxVertexBindings = ib.maxIndexBindings = 1u;
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(inventory,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            descriptors.data(),
            descriptors.size(),
            ib,
            inputQuota,
            id);
        require(id, __LINE__);
        assert(inputs);
        OriginalCommandStateAdmissionBudget state{1u, true, 1u, 512u, 512u, 16u, 8192u, cpu};
        state.bindlessRoots = {6u, true, 16u, 256u, 65536u, snapshotCount, snapshotCount};
        // Null proves consumption/correspondence only. Native recording has
        // its own capability and opt-in, exercised by the platform fixture.
        state.bindlessRoots.pushConsumption = {1u, true, 1u, sizeof(GpuVisibilityDrawSelection)};
        state.bindlessRoots.maxIndirectCalls = state.bindlessRoots.maxIndirectRecords = 1u;
        state.bindlessRoots.maxDependencyVisits = 1048576u;
        state.geometryUsage = {1u, true, commands.size(), 2u, 6u, 1u, true, 1u, 2u, 65536u};
        const auto rootAccess = common::OriginalCommandBindlessRootAccesses::visit(
            inputs, state.bindlessRoots, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        require(rootAccess.diagnostic);
        const auto geometry = inputs->visitGeometryAccesses(
            state.geometryUsage, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        require(geometry.diagnostic);
        auto graph =
            OriginalCommandResourceStates::prepareOriginal(inputs, device, state, graphQuota, id);
        require(id, __LINE__);
        assert(graph);
        const auto implicit = rootAccess.accesses + geometry.bindings + geometry.drawAccesses;
        OriginalCommandStateSequenceBudget sequence{1u,
            true,
            static_cast<std::uint32_t>(commands.size()),
            0u,
            512u,
            2048u,
            implicit,
            6u,
            true};
        const auto accesses = common::OriginalCommandAccessValidation::inspect(graph, sequence);
        require(accesses.diagnostic);
        OriginalCommandStreamBudget sb;
        sb.contractVersion = 1u;
        sb.prototypeOptIn = true;
        sb.maxCommands = commands.size();
        sb.maxReferences = refs.size();
        sb.maxDescriptors = descriptors.size();
        sb.maxRenderPasses = sb.maxAttachmentReferences = 1u;
        sb.maxCopyElements = snapshotCount + 1u;
        sb.maxPipelineBinds = sb.maxConsumptions = 5u;
        sb.maxImplicitAccesses = implicit;
        sb.maxRetainedOwners = 8192u;
        sb.maxCpuMetadataBytes = cpu;
        sb.bindlessRoots = state.bindlessRoots;
        sb.geometryUsage = state.geometryUsage;
        sb.stateSequence = sequence;
        auto stream =
            OriginalCommandStreamProgram::prepareOriginalTyped(graph, sb, streamQuota, id);
        require(id, __LINE__);
        assert(stream);
        const auto rootsReport = OriginalBindlessRootSequenceContract::inspect(roots.data(),
            roots.size(),
            inventory,
            stream,
            {1u, true, roots[0u]->originalSnapshot()->generation(), 5u, 65536u});
        assert(bool(rootsReport.diagnostic) == multipleSnapshots);
        if (!multipleSnapshots)
            assert(rootsReport.computeBindings == 4u && rootsReport.graphicsBindings == 1u);
        OriginalBindlessRootSequenceContract::Budget scopedBudget{2u,
            true,
            roots[0u]->originalSnapshot()->generation(),
            5u,
            65536u,
            {1u, true, snapshotCount, 15u}};
        const auto scopedRoots = OriginalBindlessRootSequenceContract::inspect(
            roots.data(), roots.size(), inventory, stream, scopedBudget);
        require(scopedRoots.diagnostic);
        assert(scopedRoots.bindings == roots.size());
        --scopedBudget.snapshotScope.maxVisits;
        assert(OriginalBindlessRootSequenceContract::inspect(
            roots.data(), roots.size(), inventory, stream, scopedBudget)
                .diagnostic);
        auto reordered = roots;
        std::swap(reordered[1u], reordered[2u]);
        const auto wrongOrder = OriginalBindlessRootSequenceContract::inspect(reordered.data(),
            reordered.size(),
            inventory,
            stream,
            {1u, true, roots[0u]->originalSnapshot()->generation(), 5u, 65536u});
        assert(wrongOrder.diagnostic && !wrongOrder.bindings && !wrongOrder.resourceVisits);
        auto forged = roots;
        forged[2u] =
            std::shared_ptr<const OriginalCommandBindlessRootSource>(graphQuota, roots[2u].get());
        const auto wrongOwner = OriginalBindlessRootSequenceContract::inspect(forged.data(),
            forged.size(),
            inventory,
            stream,
            {1u, true, roots[0u]->originalSnapshot()->generation(), 5u, 65536u});
        assert(wrongOwner.diagnostic && !wrongOwner.bindings && !wrongOwner.resourceVisits);
        const GpuVisibilityRootProducerContract::Roots producers{
            roots[0u], roots[1u], roots[2u], roots[3u]};
        const GpuVisibilityRootProducerContract::Budget producerBudget{
            1u, true, 256u, commands.size()};
        const auto produced = GpuVisibilityRootProducerContract::inspect(
            producers, inventory, graph, stream, layout, layoutBudget, 64u, producerBudget);
        require(produced.diagnostic);
        auto tooFewCommands = producerBudget;
        --tooFewCommands.maxCommandVisits;
        const auto refused = GpuVisibilityRootProducerContract::inspect(
            producers, inventory, graph, stream, layout, layoutBudget, 64u, tooFewCommands);
        assert(refused.diagnostic && !refused.indirectRecords && !refused.commandVisits
            && refused.sources[0u] == SIZE_MAX);
        auto wrongProducerOrder = producers;
        std::swap(wrongProducerOrder[0u], wrongProducerOrder[1u]);
        assert(GpuVisibilityRootProducerContract::inspect(
            wrongProducerOrder, inventory, graph, stream, layout, layoutBudget, 64u, producerBudget)
                .diagnostic);
        const auto consumer = GpuVisibilityRootConsumerContract::inspect(producers,
            inventory,
            graph,
            stream,
            layout,
            layoutBudget,
            64u,
            producerBudget,
            {1u, true, commands.size(), 256u});
        assert(bool(consumer.diagnostic) == multipleSnapshots);
        if (!multipleSnapshots)
            assert(
                consumer.draws == 1u && consumer.bindings == 1u && produced.indirectRecords == 1u);
        const auto resultQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
        const std::array destinations{controlReadback, readback, secondaryReadback};
        const std::array copyCommands{controlCopy, faultCopy, secondaryCopy};
        core::pmr::Vector<OriginalBindlessRootReadbackSequence::Owner> readbacks(
            snapshotCount + 1u);
        for (std::size_t i = 0u; i < readbacks.size(); ++i) {
            const auto& resultRoot = i == 2u ? roots[4u] : i == 0u ? roots[3u] : roots[0u];
            const auto bytes = lease(destinations[i])->layout().sizeBytes;
            auto request = BufferReadbackRequestDeclaration::prepareOriginal(lease(destinations[i]),
                device,
                {i + 1u, 1u, resultRoot->originalSnapshot()->generation()},
                0u,
                bytes,
                {1u, true, bytes, cpu},
                inputQuota,
                id);
            require(id, __LINE__);
            assert(request);
            auto result = OriginalCommandBufferReadbackResultSnapshot::prepareOriginalCold(
                request, resultQuota, id);
            require(id, __LINE__);
            assert(result);
            const auto bindingVisits =
                stream->operations().size() + (i == 0u ? roots[3u]->elements().size() : 0u);
            readbacks[i] = OriginalCommandBufferReadbackBinding::prepareOriginal(graph,
                stream,
                request,
                result,
                copyCommands[i],
                inventory->originalLeaseIndex(resultRoot),
                device,
                {5u,
                    true,
                    OriginalCommandBufferReadbackBinding::retainedOwnerCount,
                    bindingVisits,
                    cpu},
                inputQuota,
                id);
            require(id, __LINE__);
            assert(readbacks[i] && !result->publishedBytes());
        }
        const auto resultBytes = lease(controlReadback)->layout().sizeBytes + snapshotCount * 4u;
        const OriginalCommandReadbackResultContract::Budget resultBudget{1u,
            true,
            snapshotCount + 1u,
            snapshotCount + 1u,
            snapshotCount + 1u,
            resultBytes,
            resultBytes,
            cpu};
        const OriginalBindlessRootReadbackSequence::Input readbackInput{readbacks.data(),
            readbacks.size(),
            65536u,
            resultBudget,
            multipleSnapshots ? OriginalBindlessSnapshotScopeContract::Intent{1u, true, 2u, 65536u}
                              : OriginalBindlessSnapshotScopeContract::Intent{}};
        require(OriginalBindlessRootReadbackSequence::inspectInput(readbacks[1u], readbackInput),
            __LINE__);
        require(OriginalBindlessRootReadbackSequence::inspectRootCoverage(
                    readbacks[1u],
                    readbackInput,
                    roots.size(),
                    [&](std::size_t i) noexcept -> const auto& {
                        return roots[i];
                    },
                    readbackInput.snapshotScope),
            __LINE__);
        {
            OriginalBindlessRootReadbackSequence retained(readbacks[1u], readbackInput);
            require(retained.diagnostic(), __LINE__);
            assert(retained.faultBinding().get() == readbacks[1u].get());
            OriginalBindlessRootFaultCustody custody;
            const auto custodyQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
            {
                core::CpuMemoryQuotaScope scope(custodyQuota);
                require(custody.prepare(retained), __LINE__);
            }
            core::pmr::String faultDiagnostic;
            auto primary = OriginalCommandBufferReadbackBinding::acquireOriginalFaultCustody(
                readbacks[1u], faultDiagnostic);
            assert(primary.valid());
            if (multipleSnapshots) {
                auto occupied = OriginalCommandBufferReadbackBinding::acquireOriginalFaultCustody(
                    readbacks[2u], faultDiagnostic);
                assert(occupied.valid());
                assert(custody.acquire(retained, primary, faultDiagnostic));
                occupied.release();
            }
            faultDiagnostic.clear();
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            require(custody.acquire(retained, primary, faultDiagnostic), __LINE__);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            assert(custody.acquire(retained, primary, faultDiagnostic));
            assert(custody.validatePending(retained, {}, primary));
            custody.release();
            primary.release();
            if (multipleSnapshots) {
                auto returned = OriginalCommandBufferReadbackBinding::acquireOriginalFaultCustody(
                    readbacks[2u], faultDiagnostic);
                assert(returned.valid());
            }
        }
        require(OriginalBindlessRootReadbackSequence::inspectMapDestinations(
                    readbacks[1u], readbackInput),
            __LINE__);
        const GpuVisibilityRootReadbackContract::Budget readbackBudget{
            1u, true, commands.size(), 65536u, resultBudget};
        const auto joinedReadback = GpuVisibilityRootReadbackContract::inspect(
            produced, stream, layout, readbacks.data(), readbacks.size(), readbackBudget);
        assert(bool(joinedReadback.diagnostic) == multipleSnapshots);
        if (!multipleSnapshots)
            assert(joinedReadback.controlBinding == 0u && joinedReadback.faultBinding == 1u
                && joinedReadback.declaredBytes == resultBytes);
        auto duplicateResults = readbacks;
        duplicateResults[0u] = duplicateResults[1u];
        assert(GpuVisibilityRootReadbackContract::inspect(produced,
            stream,
            layout,
            duplicateResults.data(),
            duplicateResults.size(),
            readbackBudget)
                .diagnostic);
        auto shortResults = readbackBudget;
        --shortResults.results.maxDeclaredReadbackBytes;
        assert(GpuVisibilityRootReadbackContract::inspect(
            produced, stream, layout, readbacks.data(), readbacks.size(), shortResults)
                .diagnostic);
        for (const auto& value : readbacks)
            assert(!value->originalResult()->publishedBytes());
        // The five workloads share one transaction slot and one state journal.
        // Cancellation must return that slot without publishing either result
        // or changing the original observations. This is CPU custody evidence,
        // not native submission or callback-drain evidence.
        const auto transactionQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
        const auto competingQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
        auto transaction = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, transactionQuota, id);
        require(id, __LINE__);
        assert(transaction && transaction->hasOriginalBindlessProducedGeometryStateProfile()
            && !transaction->hasOriginalBindlessProducedGeometryProfile()
            && transaction->journalEmpty());
        auto competing = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, competingQuota, id);
        assert(!competing && id && !competingQuota->liveBytes());
        require(transaction->abandonOriginalPendingOwnership(), __LINE__);
        assert(transaction->status() == OriginalCommandStateTransaction::Status::Abandoned
            && transaction->journalEmpty());
        transaction.reset();
        assert(!transactionQuota->liveBytes() && !graph->validate() && !stream->validate());
        transaction = OriginalCommandStateTransaction::prepareOriginalOwnership(
            graph, device, state, transactionQuota, id);
        require(id, __LINE__);
        assert(transaction && transaction->journalEmpty());
        device.retireOriginalDeviceLifetime();
        assert(graph->validate() && stream->validate() && transaction->validate());
        for (const auto& root : roots)
            assert(Root::validateOriginal(root));
        for (const auto& value : readbacks)
            assert(!value->originalResult()->publishedBytes());
        transaction.reset();
        assert(!transactionQuota->liveBytes());
        std::puts(
            "BINDLESS_VISIBILITY_PUBLIC_ROOT_COMPOSITION_OK Inputs8 RootState6 four-producers alias-consumer finite-indirect same-inventory no-native-grant");
    }
};
} // namespace arp::test
