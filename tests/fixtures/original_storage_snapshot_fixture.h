#pragma once
#include "original_texture_cell_descriptor_fixture.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"
#include "shader/shader_compiler.h"

namespace arp::test {
// Synthetic shader interface for real-device snapshot publication only. No
// compiled package, authorization, dispatch or content completion is inferred.
class OriginalStorageSnapshotFixture final {
    static constexpr std::uint64_t bytes = 65536u;

    static void require(const rhi::ValidationResult& result)
    {
        if (!result.success)
            std::fprintf(stderr, "Storage snapshot: %s\n", result.diagnostics.c_str());
        assert(result.success);
    }

public:
    struct Interface {
        rhi::BindlessRequirements request;
        rhi::BindlessShaderArtifact artifact;

        Interface(const rhi::BindlessCapabilities& caps, bool native)
        {
            using namespace rhi;
            request.optIn = true;
            request.allowEmulated = true;
            request.deviceIdentity = caps.deviceIdentity;
            request.shaderPackageIdentity = 123u;
            request.originalPreparation = {7u, true};
            request.budget = {1u, 6u, bytes, 4u};
            artifact.execution = native ? BindlessExecution::Native : BindlessExecution::Emulated;
            artifact.shaderPackageIdentity = request.shaderPackageIdentity;
            artifact.physicalLayout.name = "original RGBA16F cell snapshot";
            artifact.snapshotBytes = bytes;
            artifact.switchCases = native ? 0u : 4u;
            artifact.activePrefixContractVersion = 2u;
            artifact.faultReportingContractVersion = 1u;
            artifact.snapshotMetadata = {1u, 6u, 16u, ShaderStage::Compute};
            artifact.faultLatch = {1u, 7u, ShaderStage::Compute};
            for (unsigned i = 0u; i != 2u; ++i) {
                BindlessTableRequirement table;
                table.tableId = i + 1u;
                table.resourceCount = 1u;
                table.binding.binding = i * 2u;
                table.binding.type =
                    i ? DescriptorType::SampledTexture : DescriptorType::StorageTexture;
                table.binding.stages = ShaderStage::Compute;
                table.binding.arrayCount = native ? 2u : 1u;
                table.binding.storageTextureFormat = TextureFormat::Rgba16Float;
                table.operations = i ? BindlessOperation::Load
                                     : BindlessOperation::Store | BindlessOperation::Query;
                table.nonUniform = true;
                request.tables.push_back(table);
                BindlessTableMapping mapping{
                    table.tableId, table.binding.binding, 2u, table.operations, true};
                mapping.metadataWordOffset = 4u + i * 6u;
                artifact.tables.push_back(mapping);
                artifact.physicalLayout.bindings.push_back(table.binding);
                if (!native) {
                    auto second = table.binding;
                    ++second.binding;
                    artifact.physicalLayout.bindings.push_back(second);
                }
            }
            artifact.physicalLayout.bindings.push_back(
                {6u, DescriptorType::ReadOnlyStorageBuffer, ShaderStage::Compute, 1u});
            artifact.physicalLayout.bindings.push_back(
                {7u, DescriptorType::StorageBuffer, ShaderStage::Compute, 1u});
        }
    };

    static void verifyContract()
    {
        using namespace rhi;
        using Lowering = semantics::OriginalBindlessPreparationLowering;
        {
            using C = semantics::OriginalBindlessEntryAccessContract;
            for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
                const auto decision = C::lowering(backend);
                assert(decision.projectionAvailable && !decision.executionAvailable
                    && !decision.diagnostic
                    && decision.validatorOnly == (backend == Backend::Null));
                const auto graphics = C::lowering(backend, 2u);
                assert(graphics.projectionAvailable && !graphics.executionAvailable
                    && !graphics.diagnostic
                    && graphics.validatorOnly == (backend == Backend::Null));
                const auto computeAliases = C::lowering(backend, 3u);
                assert(computeAliases.projectionAvailable && !computeAliases.executionAvailable
                    && !computeAliases.diagnostic
                    && computeAliases.validatorOnly == (backend == Backend::Null));
                const auto scene = C::lowering(backend, 4u);
                assert(scene.projectionAvailable && !scene.executionAvailable && !scene.diagnostic
                    && scene.validatorOnly == (backend == Backend::Null));
                assert(!C::lowering(backend, 5u).projectionAvailable
                    && C::lowering(backend, 5u).diagnostic);
            }
            assert(!C::lowering(static_cast<Backend>(99u)).projectionAvailable);
            shader::ShaderReflection reflection;
            bool active = true;
            assert(C::binding(
                       reflection, BindlessExecution::Native, 0u, 2u, ShaderStage::Compute, active)
                && !active);
            shader::ShaderBinding binding;
            binding.set = 0u;
            binding.binding = 2u;
            binding.stageMask = 4u;
            reflection.bindings.push_back(binding);
            assert(!C::binding(
                       reflection, BindlessExecution::Native, 0u, 2u, ShaderStage::Compute, active)
                && active);
            assert(C::binding(
                       reflection, BindlessExecution::Native, 0u, 2u, ShaderStage::Fragment, active)
                && !active);
            reflection.bindings.push_back(binding);
            assert(C::binding(
                       reflection, BindlessExecution::Native, 0u, 2u, ShaderStage::Compute, active)
                && !active);
        }
        {
            shader::ShaderReflection reflection;
            reflection.webGpuBindingSpaceContractVersion = "WebGpuBindingSpace/5";
            reflection.webGpuBindingSpaceStatus = "Available";
            shader::ShaderWebGpuBindingSpaceBinding map;
            map.set = 0u;
            map.binding = 2u;
            map.active = false;
            reflection.webGpuBindingSpace.push_back(map);
            {
                using Access = semantics::OriginalBindlessEntryAccessContract;

                struct Stage {
                    ShaderStage stage;
                    const shader::ShaderReflection* reflection;
                };

                auto vertex = reflection;
                shader::ShaderBinding read;
                read.set = 0u;
                read.binding = 2u;
                read.stageMask = static_cast<std::uint32_t>(ShaderStage::Vertex);
                vertex.bindings.push_back(read);
                vertex.webGpuBindingSpace.front().active = true;
                std::array<Stage, 2u> stages{
                    {{ShaderStage::Vertex, &vertex}, {ShaderStage::Fragment, &reflection}}};
                const auto select = [](const Stage& stage) noexcept {
                    return stage.reflection;
                };
                bool selected = false;
                assert(!Access::selectedBindings(
                           stages, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && selected);
                stages[0].reflection = &reflection;
                assert(!Access::selectedBindings(
                           stages, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && !selected);
                stages[0].reflection = &vertex;
                stages[1].reflection = nullptr;
                assert(Access::selectedBindings(
                           stages, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && !selected);
                auto absent = reflection;
                absent.webGpuBindingSpace.clear();
                stages[1].reflection = &absent;
                assert(Access::selectedBindings(
                           stages, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && !selected);
                stages[1] = stages[0];
                assert(Access::selectedBindings(
                           stages, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && !selected);
                const std::array<Stage, 1u> compute{{{ShaderStage::Compute, &reflection}}};
                assert(!Access::selectedBindings(
                           compute, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && !selected);
                const std::array<Stage, 1u> incomplete{{{ShaderStage::Vertex, &vertex}}};
                assert(Access::selectedBindings(
                           incomplete, BindlessExecution::Emulated, 0u, 2u, select, selected)
                    && !selected);
            }
            using C = semantics::BindlessPipelineInterfaceContract;
            assert(C::explicitWebGpuInactiveBinding(reflection, 0u, 2u));
            bool active = true;
            assert(
                !semantics::OriginalBindlessEntryAccessContract::binding(
                    reflection, BindlessExecution::Emulated, 0u, 2u, ShaderStage::Compute, active)
                && !active);
            for (unsigned field = 0u; field != 8u; ++field) {
                auto bad = reflection;
                if (field == 0u)
                    bad.webGpuBindingSpace.clear();
                if (field == 1u)
                    bad.webGpuBindingSpace.front().active = true;
                if (field == 2u)
                    bad.webGpuBindingSpace.push_back(map);
                if (field == 3u) {
                    shader::ShaderBinding active;
                    active.set = 0u;
                    active.binding = 2u;
                    bad.bindings.push_back(active);
                }
                if (field == 4u)
                    bad.webGpuBindingSpaceStatus = "Unavailable";
                if (field == 5u)
                    bad.webGpuBindingSpaceContractVersion = "WebGpuBindingSpace/4";
                if (field == 6u)
                    bad.webGpuBindingSpace.front().set = 1u;
                if (field == 7u)
                    bad.webGpuBindingSpace.front().binding = 3u;
                assert(!C::explicitWebGpuInactiveBinding(bad, 0u, 2u));
                assert(semantics::OriginalBindlessEntryAccessContract::binding(
                    bad, BindlessExecution::Emulated, 0u, 2u, ShaderStage::Compute, active));
            }
        }
        {
            shader::ShaderReflection reflection;
            reflection.metalBindingSpaceContractVersion = "MetalBindingSpace/1";
            reflection.metalBindingSpaceStatus = "Available";
            reflection.metalArgumentBufferContractVersion = "MetalArgumentBuffer/1";
            reflection.metalArgumentBufferStatus = "Available";
            shader::ShaderMetalBindingSpaceBinding direct;
            direct.set = 0u;
            direct.binding = 2u;
            direct.active = false;
            shader::ShaderMetalArgumentBufferBinding array;
            array.set = 0u;
            array.binding = 2u;
            array.active = false;
            reflection.metalBindingSpace.push_back(direct);
            reflection.metalArgumentBuffer.push_back(array);
            using C = semantics::BindlessPipelineInterfaceContract;
            assert(C::explicitMetalInactiveBinding(reflection, 0u, 2u));
            bool active = true;
            assert(!semantics::OriginalBindlessEntryAccessContract::binding(
                       reflection, BindlessExecution::Native, 0u, 2u, ShaderStage::Compute, active)
                && !active);
            for (unsigned field = 0u; field != 9u; ++field) {
                auto bad = reflection;
                if (field == 0u)
                    bad.metalBindingSpace.clear();
                if (field == 1u)
                    bad.metalArgumentBuffer.clear();
                if (field == 2u)
                    bad.metalBindingSpace.front().active = true;
                if (field == 3u)
                    bad.metalArgumentBuffer.front().active = true;
                if (field == 4u)
                    bad.metalBindingSpace.push_back(direct);
                if (field == 5u)
                    bad.metalArgumentBuffer.push_back(array);
                if (field == 6u) {
                    shader::ShaderBinding active;
                    active.set = 0u;
                    active.binding = 2u;
                    bad.bindings.push_back(active);
                }
                if (field == 7u)
                    bad.metalArgumentBufferStatus = "Unavailable";
                if (field == 8u)
                    bad.metalBindingSpaceContractVersion = "MetalBindingSpace/2";
                assert(!C::explicitMetalInactiveBinding(bad, 0u, 2u));
                assert(semantics::OriginalBindlessEntryAccessContract::binding(
                    bad, BindlessExecution::Native, 0u, 2u, ShaderStage::Compute, active));
            }
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto lower = Lowering::lowering(backend, 7u);
            const auto root = Lowering::lowering(backend, 8u);
            assert(root.preparationAvailable == (backend != Backend::Null)
                && !root.executionAvailable);
            assert(lower.preparationAvailable == (backend != Backend::Null)
                && lower.validatorOnly == (backend == Backend::Null) && !lower.executionAvailable);
        }
        for (auto version : {7u, 8u})
            for (const bool native : {false, true}) {
                BindlessCapabilities caps;
                caps.deviceIdentity = 7u;
                caps.limitsQueried = true;
                caps.execution =
                    native ? BindlessExecution::Native : BindlessExecution::DiagnosticFailure;
                caps.originalStoragePreparationContractVersion = 7u;
                caps.originalStorageRootPreparationContractVersion = 8u;
                caps.maxBindGroups = 1u;
                caps.maxBindingsPerGroup = 8u;
                caps.maxStorageBufferBindingSize = bytes;
                caps.stageLimits[2u] = {2u, 0u, 2u, 0u, 2u};
                caps.support[2u][0u] = {BindlessOperation::Load, true};
                caps.support[2u][2u] = {BindlessOperation::Store | BindlessOperation::Query, true};
                Interface fixture(caps, native);
                fixture.request.originalPreparation.contractVersion = version;
                const auto plan = planBindless(caps, fixture.request, fixture.artifact);
                if (!plan.success)
                    std::fprintf(stderr, "Storage plan: %s\n", plan.diagnosticId);
                assert(Lowering::hasPlan(plan, version) && !plan.executable
                    && plan.descriptorCount == 6u);
                assert(!OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
                    plan, version == 8u ? 10u : 1u));
                assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
                    plan, version == 8u ? 1u : 10u));
                for (unsigned field = 0u; field != 13u; ++field) {
                    auto c = caps;
                    auto f = fixture;
                    if (field == 0u) {
                        if (version == 7u)
                            c.originalStoragePreparationContractVersion = 6u;
                        else
                            c.originalStorageRootPreparationContractVersion = 0u;
                    }
                    if (field == 1u)
                        f.request.originalPreparation.prototypeOptIn = false;
                    if (field == 2u)
                        f.request.originalPreparation.contractVersion = 3u;
                    if (field == 3u)
                        f.request.originalExecution = {1u, true};
                    if (field == 4u)
                        f.artifact.physicalLayout.bindings.front().storageTextureFormat =
                            TextureFormat::R32Float;
                    if (field == 5u)
                        f.artifact.physicalLayout.bindings.front().storageTextureAccess =
                            StorageTextureAccess::ReadWrite;
                    if (field == 6u)
                        f.artifact.physicalLayout.bindings.front().stages = ShaderStage::Fragment;
                    if (field == 7u)
                        f.request.tables.front().operations = BindlessOperation::Load;
                    if (field == 8u)
                        f.request.tables.front().operations = BindlessOperation::Query;
                    if (field == 9u)
                        --c.stageLimits[2u].storageTextures;
                    if (field == 10u)
                        --f.request.budget.maxUpdatesPerSnapshot;
                    if (field == 11u)
                        f.artifact.physicalLayout.bindings.front().textureViewDimension =
                            TextureViewDimension::Texture2DArray;
                    if (field == 12u)
                        c.execution = BindlessExecution::Validator;
                    const auto bad = planBindless(c, f.request, f.artifact);
                    assert(!bad.success && !bad.executable);
                }
                auto selectionRequest = fixture.request;
                selectionRequest.shaderPackageIdentity = 0u;
                for (auto policy : {BindlessPolicy::Auto, BindlessPolicy::Required}) {
                    const auto choice = selectBindlessVariant(
                        policy, caps, selectionRequest, &fixture.artifact, 1u);
                    assert(choice.path
                        == (policy == BindlessPolicy::Auto ? BindlessSelectionPath::Traditional
                                                           : BindlessSelectionPath::Rejected));
                }
            }
        std::puts(
            "BINDLESS_RHI_STORAGE_SNAPSHOT7_CONTRACT_OK four-backend bounded compute-only no-mode-selection");
    }

    // Returns the sealed snapshot so the existing original-journal fixture can
    // revalidate the exact same descriptor proofs through Pending and rollback.
    static std::shared_ptr<rhi::BindlessSnapshot> prepareDevice(rhi::GpuDevice& device,
        const OriginalTextureCellDescriptorFixture::DevicePublication& publication,
        const std::shared_ptr<core::CpuMemoryQuota>& resourceQuota)
    {
        using namespace rhi;
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(
            extension && extension->capabilities().originalStoragePreparationContractVersion == 7u);
        Interface fixture(extension->capabilities(), device.backend() != Backend::WebGPU);
        BindlessSnapshotPool pool(device, fixture.request.budget);
        core::pmr::String diagnostic;
        diagnostic.reserve(512u);
        const auto make = [&] {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            try {
                return pool.prepareOriginalDescriptors(
                    fixture.request, fixture.artifact, {2u, true, true}, diagnostic);
            } catch (const std::bad_alloc&) {
                return core::UniquePtr<BindlessSnapshot>{};
            }
        };
        const auto baseline = device.memoryStats().allocatedBytes;
        CpuQuotaFactoryFixture pressure(resourceQuota);
        const auto peak = pressure.measureSingleUse([] {}, make);
        pressure.verifySingleUse(peak, [] {}, make);
        assert(device.memoryStats().allocatedBytes == baseline);
        auto snapshot = core::shareUnique(make());
        if (!snapshot)
            std::fprintf(stderr, "Storage snapshot creation: %s\n", diagnostic.c_str());
        assert(snapshot && !snapshot->sealed() && !snapshot->descriptorSet());
        assert(
            !snapshot->seal(device).success && !snapshot->sealed() && !snapshot->descriptorSet());
        assert(!make() && diagnostic == "RHI-BINDLESS-SNAPSHOT-POOL-FULL");
        std::array<std::weak_ptr<const OriginalCommandBufferLease>, 3u> internal;
        for (unsigned i = 0u; i != 2u; ++i) {
            const auto source = publication.retained[i].lock();
            assert(source);
            const auto identity =
                source->originalParentLease()->originalObject()->observeOriginalContent(
                    {1u, 1u, 0u, 1u, TextureAspect::Color});
            assert(identity);
            const auto handle = snapshot->handle(i + 1u, 0u);
            const OriginalTextureCellDescriptorRequest request{
                device.backend() == Backend::WebGPU ? 2u : 1u,
                true,
                1u,
                1u,
                OriginalCommandTextureViewSource::completeRetainedOwnerCount};
            assert(!snapshot->handle(i + 1u, 1u).generation);
            assert(!snapshot
                    ->writeOriginalTextureCell(
                        snapshot->handle(3u, 0u), device, source, *identity, request)
                    .success);
            {
                core::CpuMemoryQuotaScope allocation(resourceQuota);
                require(
                    snapshot->writeOriginalTextureCell(handle, device, source, *identity, request));
            }
            assert(!snapshot->initializeInactiveSlots(handle).success);
            {
                core::CpuMemoryQuotaScope allocation(resourceQuota);
                require(snapshot->initializeInactiveSlots(handle, device));
            }
        }
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            require(snapshot->seal(device));
        }
        require(snapshot->validate());
        assert(snapshot->descriptorSet());
        assert(!snapshot->plan().executable
            && snapshot->plan().originalPreparationContractVersion == 7u);
        if (publication.exportCheck)
            publication.exportCheck(*snapshot->descriptorSet());
        std::array<BindlessResourceAccess, 5u> accesses;
        for (unsigned i = 0u; i != 2u; ++i) {
            const auto source = publication.retained[i].lock();
            auto& access = accesses[i];
            access.resource = source->originalParentLease()->resource();
            access.kind = BindlessAccessKind::Texture;
            access.stages = ShaderStage::Compute;
            access.pipelineStages = PipelineStage::ComputeShader;
            access.state = i ? ResourceState::ShaderRead : ResourceState::ShaderWrite;
            access.access = i ? ResourceAccess::ShaderRead : ResourceAccess::ShaderWrite;
            access.textureRange = {1u, 1u, 0u, 1u, TextureAspect::Color};
        }
        accesses[2u] = snapshot->metadataAccess();
        accesses[3u] = snapshot->faultLatchAccess();
        accesses[4u] = snapshot->faultReadbackAccess();
        // Store|Query is a write and metadata query, never a content read.
        require(snapshot->validateForRecord(
            snapshot->plan(), snapshot->generation(), accesses.data(), accesses.size()));
        auto bad = accesses;
        bad[0u].access = ResourceAccess::ShaderRead;
        assert(!snapshot
                ->validateForRecord(
                    snapshot->plan(), snapshot->generation(), bad.data(), bad.size())
                .success);
        const char* id = BindlessSnapshot::prepareOriginalBufferSources(
            snapshot, device, {1u, true, 2u, resourceQuota->limitBytes()}, resourceQuota);
        if (id)
            std::fprintf(stderr, "Storage snapshot internals: %s\n", id);
        assert(!id);
        for (unsigned i = 0u; i != 3u; ++i) {
            const auto& source = snapshot->originalBufferSource(
                static_cast<BindlessSnapshot::OriginalBufferRole>(i));
            assert(source && !OriginalCommandBufferLease::validateOriginal(source));
            internal[i] = source;
        }
        const auto generation = snapshot->generation();
        assert(!snapshot
                ->writeOriginalTextureCell(snapshot->handle(1u, 0u),
                    device,
                    publication.retained[0u].lock(),
                    *publication.retained[0u]
                        .lock()
                        ->originalParentLease()
                        ->originalObject()
                        ->observeOriginalContent({1u, 1u, 0u, 1u, TextureAspect::Color}),
                    {device.backend() == Backend::WebGPU ? 2u : 1u,
                        true,
                        1u,
                        1u,
                        OriginalCommandTextureViewSource::completeRetainedOwnerCount})
                .success);
        snapshot.reset();
        for (const auto& weak : internal)
            assert(weak.expired());
        auto replacement = core::shareUnique(make());
        assert(replacement && replacement->generation() > generation);
        replacement.reset();
        assert(device.memoryStats().allocatedBytes == baseline);
        // Recreate a sealed candidate for the original journal observer below.
        snapshot = core::shareUnique(make());
        assert(snapshot);
        for (unsigned i = 0u; i != 2u; ++i) {
            const auto source = publication.retained[i].lock();
            const auto identity =
                source->originalParentLease()->originalObject()->observeOriginalContent(
                    {1u, 1u, 0u, 1u, TextureAspect::Color});
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            require(snapshot->writeOriginalTextureCell(snapshot->handle(i + 1u, 0u),
                device,
                source,
                *identity,
                {device.backend() == Backend::WebGPU ? 2u : 1u,
                    true,
                    1u,
                    1u,
                    OriginalCommandTextureViewSource::completeRetainedOwnerCount}));
            require(snapshot->initializeInactiveSlots(snapshot->handle(i + 1u, 0u), device));
        }
        {
            core::CpuMemoryQuotaScope allocation(resourceQuota);
            require(snapshot->seal(device));
        }
        return snapshot;
    }
};
} // namespace arp::test
