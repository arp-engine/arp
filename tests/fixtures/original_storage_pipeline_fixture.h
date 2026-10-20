#pragma once
#include "bindless_package_request_fixture.h"
#include "original_bindless_root_assembly_fixture.h"
#include "original_storage_entry_access_fixture.h"
#include "original_texture_cell_descriptor_fixture.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

namespace arp::test {
// Genuine package/pipeline/physical binding, deliberately without GPU work.
class OriginalStoragePipelineFixture final {
    static void require(const rhi::ValidationResult& r)
    {
        if (!r.success)
            std::fprintf(stderr, "Storage pipeline: %s\n", r.diagnostics.c_str());
        assert(r.success);
    }

public:
    using RootCheck = void (*)(rhi::GpuDevice&,
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&,
        const std::shared_ptr<rhi::OriginalCommandResourceSources>&);
    using ExecuteCheck = void (*)(rhi::GpuDevice&,
        const std::shared_ptr<rhi::BindlessSnapshot>&,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 2u>&,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>&,
        const std::shared_ptr<core::CpuMemoryQuota>&);

    static void verifyDevice(rhi::GpuDevice& device,
        const OriginalTextureCellDescriptorFixture::DevicePublication& publication,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        RootCheck rootCheck = nullptr,
        ExecuteCheck executionCheck = nullptr,
        bool executionOnly = false)
    {
#if !defined(ARP_BINDLESS_STORAGE_ROOT_PACKAGE)
        (void)device;
        (void)publication;
        (void)resources;
        (void)rootCheck;
        (void)executionCheck;
        (void)executionOnly;
        assert(false && "Storage pipeline device smoke requires an authenticated package");
#else
        using namespace rhi;
        const auto baseline = device.memoryStats().allocatedBytes;
        unsigned rejectedAliases = 0u;
        auto quota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::pmr::String diagnostic;
            diagnostic.reserve(512u);
            const char* id = nullptr;
            auto package = BindlessShaderPackage::load(ARP_BINDLESS_STORAGE_ROOT_PACKAGE,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
            if (!package)
                std::fprintf(stderr, "Storage package: %s\n", diagnostic.c_str());
            assert(package);
            const auto parent = publication.retained[0u].lock()->originalParentLease();
            auto secondPublication = OriginalTextureCellDescriptorFixture::prepareDevice(
                device, parent, resources, publication.exportCheck, 2u);
            const auto* ext = static_cast<BindlessCrossExtension*>(
                device.queryInterface(kExtensionBindlessCross));
            assert(ext && ext->capabilities().originalStorageRootPreparationContractVersion == 8u);
            const auto& artifact = package->artifact();
            const auto executionVersion = device.backend() == Backend::WebGPU ? 10u : 9u;
            for (auto version : {7u, 8u, executionVersion}) {
                if ((version == executionVersion && !executionCheck)
                    || (version != executionVersion && executionOnly))
                    continue;
                // Reject physically bound writable aliases before native emission,
                // including the old active-one/padded-one arrangement.
                const unsigned aliasCases =
                    version == 8u && device.backend() == Backend::WebGPU ? 3u : 1u;
                for (unsigned aliasCase = 0u; aliasCase != aliasCases; ++aliasCase) {
                    auto request = BindlessPackageRequestFixture::make(
                        artifact, ext->capabilities().deviceIdentity);
                    if (version == executionVersion)
                        request.originalExecution = {version, true};
                    else
                        request.originalPreparation = {version, true};
                    for (auto& table : request.tables)
                        table.resourceCount = aliasCase ? 1u : 2u;
                    const auto plan = ext->plan(request, artifact);
                    if (!plan.success)
                        std::fprintf(stderr, "Storage packaged plan: %s\n", plan.diagnosticId);
                    assert(version == executionVersion
                            ? OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                                  plan, version)
                            : semantics::OriginalBindlessPreparationLowering::hasPlan(
                                  plan, version));
                    auto selected = request;
                    selected.shaderPackageIdentity = 0u;
                    if (version != executionVersion)
                        assert(
                            selectBindlessVariant(
                                BindlessPolicy::Auto, ext->capabilities(), selected, &artifact, 1u)
                                .path
                            == BindlessSelectionPath::Traditional);
                    if (version != executionVersion)
                        assert(selectBindlessVariant(BindlessPolicy::Required,
                                   ext->capabilities(),
                                   selected,
                                   &artifact,
                                   1u)
                                   .path
                            == BindlessSelectionPath::Rejected);
                    BindlessSnapshotPool snapshots(device, request.budget);
                    if (version != executionVersion)
                        assert(!snapshots.prepare(request, artifact, diagnostic));
                    auto snapshot = core::shareUnique(snapshots.prepareOriginalDescriptors(
                        request, artifact, {2u, true, true}, diagnostic));
                    if (!snapshot)
                        std::fprintf(stderr, "Storage pipeline snapshot: %s\n", diagnostic.c_str());
                    assert(snapshot);
                    for (unsigned slot = 0u; slot != (aliasCase ? 1u : 2u); ++slot)
                        for (unsigned i = 0u; i != 2u; ++i) {
                            const auto source =
                                (slot ? secondPublication->retained[i] : publication.retained[i])
                                    .lock();
                            assert(source);
                            const auto identity =
                                source->originalParentLease()
                                    ->originalObject()
                                    ->observeOriginalContent(
                                        {slot + 1u, 1u, 0u, 1u, TextureAspect::Color});
                            assert(identity);
                            const auto handle = snapshot->handle(i + 1u, slot);
                            require(snapshot->writeOriginalTextureCell(handle,
                                device,
                                source,
                                *identity,
                                {device.backend() == Backend::WebGPU ? 2u : 1u,
                                    true,
                                    1u,
                                    1u,
                                    OriginalCommandTextureViewSource::completeRetainedOwnerCount}));
                            if (aliasCase)
                                require(snapshot->initializeInactiveSlots(handle, device));
                        }
                    // Four uint lanes are the shader's explicitly declared output footprint.
                    const auto resultCount = aliasCase ? 1u : 2u;
                    std::array<core::UniquePtr<GpuBuffer>, 2u> results;
                    for (unsigned i = 0u; i < resultCount; ++i) {
                        core::CpuMemoryQuotaScope resourceScope(resources);
                        results[i] = device.createBuffer({"storage pipeline result",
                            4u * sizeof(std::uint32_t),
                            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                            MemoryUsage::CpuToGpu,
                            ResourceState::ShaderWrite});
                        assert(results[i]);
                        require(snapshot->writeBuffer(snapshot->handle(3u, i), *results[i]));
                    }
                    if (aliasCase)
                        require(snapshot->initializeInactiveSlots(snapshot->handle(3u, 0u)));
                    require(snapshot->seal(device));
                    const auto metadata = publication.retained[0u].lock()->originalMetadataQuota();
                    id = BindlessSnapshot::prepareOriginalBufferSources(
                        snapshot, device, {1u, true, 2u, metadata->limitBytes()}, metadata);
                    if (id)
                        std::fprintf(stderr, "Storage pipeline buffers: %s\n", id);
                    assert(!id);
                    if (publication.exportCheck)
                        publication.exportCheck(*snapshot->descriptorSet());
                    auto authorization = ext->authorize(package, *snapshot, diagnostic);
                    if (version == 7u) {
                        assert(!authorization
                            && diagnostic
                                == "RHI-BINDLESS-ORIGINAL-STORAGE-PREPARATION-SNAPSHOT-ONLY");
                        continue;
                    }
                    if (!authorization)
                        std::fprintf(stderr, "Storage authorization: %s\n", diagnostic.c_str());
                    assert(authorization);
                    std::array<std::shared_ptr<const OriginalCommandBufferLease>, 2u> resultSources;
                    for (unsigned i = 0u; i < resultCount; ++i) {
                        resultSources[i] = OriginalCommandBufferLease::prepareOriginal(
                            results[i], {1u, true, 2u, metadata->limitBytes()}, metadata, id);
                        assert(resultSources[i] && !id);
                    }
                    const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
                    BindlessComputeBindingPool bindings(*ext,
                        {version == executionVersion ? 2u : 1u,
                            (retained + 4096u) * (version == executionVersion ? 2u : 1u),
                            retained + 4096u,
                            8u,
                            4096u});
                    if (version == executionVersion) {
                        std::array<std::shared_ptr<const BindlessPreparedComputeBinding>, 2u> pair;
                        unsigned i = 0u;
                        for (const auto* entry : {"storeMain", "readMain"}) {
                            auto pipeline = core::shareUnique(
                                ext->createComputePipeline(authorization, entry, diagnostic));
                            if (!pipeline)
                                std::fprintf(
                                    stderr, "Storage execution pipeline: %s\n", diagnostic.c_str());
                            assert(pipeline);
                            pair[i] = bindings.prepare({true,
                                                           snapshot,
                                                           pipeline,
                                                           snapshot->plan(),
                                                           snapshot->generation()},
                                diagnostic);
                            if (!pair[i])
                                std::fprintf(
                                    stderr, "Storage execution binding: %s\n", diagnostic.c_str());
                            assert(pair[i]);
                            ++i;
                        }
                        using Sources = OriginalCommandResourceSources;
                        core::pmr::Vector<Sources::Source> inputs{parent};
                        for (const auto* p : std::array<
                                 const OriginalTextureCellDescriptorFixture::DevicePublication*,
                                 2u>{&publication, secondPublication.get()})
                            for (const auto& source : p->retained)
                                inputs.push_back(source.lock());
                        for (const auto& result : resultSources)
                            inputs.push_back(result);
                        for (auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                                 BindlessSnapshot::OriginalBufferRole::FaultLatch,
                                 BindlessSnapshot::OriginalBufferRole::FaultReadback})
                            inputs.push_back(snapshot->originalBufferSource(role));
                        executionCheck(device, snapshot, pair, inputs, resources);
                        continue;
                    }
                    for (const auto* entry : {"storeMain", "readMain"}) {
                        if (aliasCase
                            && (core::StringView(entry) == "storeMain") != (aliasCase == 1u))
                            continue;
                        auto pipeline = core::shareUnique(
                            ext->createComputePipeline(authorization, entry, diagnostic));
                        if (!pipeline)
                            std::fprintf(
                                stderr, "Storage pipeline %s: %s\n", entry, diagnostic.c_str());
                        assert(pipeline && pipeline->validationReport().success);
                        auto binding = bindings.prepare(
                            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()},
                            diagnostic);
                        if (aliasCase) {
                            assert(!binding
                                && diagnostic
                                    == (aliasCase == 1u
                                            ? "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-TEXTURE-ALIAS"
                                            : "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-BUFFER-ALIAS"));
                            assert(!bindings.liveBindings() && !bindings.chargedBytes());
                            ++rejectedAliases;
                            continue;
                        }
                        if (!binding)
                            std::fprintf(
                                stderr, "Storage prepared %s: %s\n", entry, diagnostic.c_str());
                        assert(binding);
                        require(binding->validatePayload());
                        {
                            using Sources = OriginalCommandResourceSources;
                            const auto storage = publication.retained[0u].lock();
                            const auto sampled = publication.retained[1u].lock();
                            core::pmr::Vector<Sources::Source> inputs{
                                storage->originalParentLease(), storage, sampled};
                            for (const auto& source : secondPublication->retained)
                                inputs.push_back(source.lock());
                            for (unsigned i = 0u; i < resultCount; ++i)
                                inputs.push_back(resultSources[i]);
                            for (auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                                     BindlessSnapshot::OriginalBufferRole::FaultLatch,
                                     BindlessSnapshot::OriginalBufferRole::FaultReadback})
                                inputs.push_back(snapshot->originalBufferSource(role));
                            OriginalCommandAdmissionBudget admission;
                            admission.contractVersion = 1u;
                            admission.prototypeOptIn = true;
                            admission.maxCommands = 6u;
                            admission.maxReferences = 2u;
                            admission.maxGraphOwners = 256u;
                            admission.maxCpuBytes.fill(metadata->limitBytes());
                            for (const auto& input : inputs)
                                std::visit(
                                    [&](const auto& source) {
                                        using T = std::remove_const_t<
                                            typename std::decay_t<decltype(source)>::element_type>;
                                        ++admission
                                              .maxSourceOwners[static_cast<std::size_t>(T::domain)];
                                    },
                                    input);
                            admission.maxSourceOwners[static_cast<std::size_t>(
                                CommandReferenceDomain::BindlessRoot)] = 1u;
                            auto inventory = Sources::prepareColdOriginalTyped(inputs.data(),
                                inputs.size(),
                                admission,
                                {6u, true, 1u},
                                metadata,
                                id);
                            if (id)
                                std::fprintf(stderr, "Storage Root inventory: %s\n", id);
                            assert(inventory && !id);
                            auto root = OriginalBindlessRootAssemblyFixture::prepare(
                                snapshot, binding, inventory, device, metadata, id);
                            if (id)
                                std::fprintf(stderr, "Storage Root: %s\n", id);
                            assert(root && !id);
                            assert(
                                !OriginalCommandBindlessRootSource::validateOriginal(root, device));
                            assert(root->matchesCanonicalSources(*inventory)
                                && !root->nativeExecutionProven());
                            if (rootCheck)
                                rootCheck(device, root, inventory);
                            if (rootCheck)
                                OriginalStorageEntryAccessFixture::verify(
                                    root, inventory, core::StringView(entry) == "storeMain");
                            assert(root->elements().size() == snapshot->plan().descriptorCount);
                            for (std::size_t i = 0u; i != root->size(); ++i) {
                                BindlessResourceAccess access;
                                std::size_t declaration = SIZE_MAX;
                                assert(root->readOriginalDeclaration(i, access, declaration));
                                if (access.kind == BindlessAccessKind::Texture
                                    && access.state == ResourceState::ShaderWrite)
                                    assert(access.access == ResourceAccess::ShaderWrite);
                            }
                        }
                        assert(bindings.liveBindings() == 1u);
                        assert(!bindings.prepare(
                            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()},
                            diagnostic));
                        assert(diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-POOL-FULL");
                        auto encoder =
                            device.createCommandEncoder({"storage cold preparation gate", 1u, 1u});
                        assert(encoder);
                        auto pass = encoder->beginComputePass({"storage cold preparation gate"});
                        if (device.backend() == Backend::WebGPU) {
                            assert(!pass);
                            const auto denied = encoder->lastValidation();
                            if (denied.success)
                                std::fprintf(
                                    stderr, "Storage ordinary pass unexpectedly accepted\n");
                            assert(!denied.success
                                && denied.diagnostics
                                    == "RHI-WEBGPU-COMMAND-RECORDING-USAGE-UNPROVEN");
                        } else {
                            assert(pass);
                            const auto denied = pass->setPipeline(*pipeline);
                            const auto expected = device.backend() == Backend::Vulkan
                                ? "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE"
                                : device.backend() == Backend::Metal
                                ? "RHI-METAL-BINDLESS-PIPELINE-AUTHORIZATION-INVALID"
                                : "RHI-WEBGPU-BINDLESS-PIPELINE-AUTHORIZATION-INVALID";
                            if (denied.success || denied.diagnostics != expected)
                                std::fprintf(stderr,
                                    "Storage record gate: success=%u %s\n",
                                    denied.success,
                                    denied.diagnostics.c_str());
                            assert(!denied.success && denied.diagnostics == expected);
                            assert(pass->end().success);
                        }
                        binding.reset();
                        assert(!bindings.liveBindings() && !bindings.chargedBytes());
                    }
                    assert(snapshot->validate().success);
                }
            }
        }
        assert(!quota->liveBytes());
        assert(device.memoryStats().allocatedBytes == baseline);
        assert(
            rejectedAliases == (!executionOnly && device.backend() == Backend::WebGPU ? 2u : 0u));
        if (executionOnly)
            return;
        if (rejectedAliases)
            std::puts(
                "BINDLESS_RHI_STORAGE_PHYSICAL_ALIAS_DEVICE_OK backend=3 texture-padding buffer-padding rejected-before-native-group pool0 quota0 no-execution");
        std::printf(
            "BINDLESS_RHI_STORAGE_PIPELINE8_DEVICE_OK backend=%u authenticated-package store-read-pipelines physical-bindings disjoint-mips independent-results pool-retirement record-refused no-execution\n",
            static_cast<unsigned>(device.backend()));
        if (rootCheck)
            std::printf(
                "BINDLESS_RHI_STORAGE_CLOSURE10_DEVICE_OK backend=%u store-read same-inventory native-identity exact-less oom quota0 record-refused no-execution\n",
                static_cast<unsigned>(device.backend()));
        if (rootCheck)
            std::printf(
                "BINDLESS_RHI_STORAGE_ENTRY_ACCESS1_DEVICE_OK backend=%u authenticated-entry canonical-dispatch retained9 store4 read6 exact-less hot0 quota0 no-execution\n",
                static_cast<unsigned>(device.backend()));
#endif
    }
};
} // namespace arp::test
