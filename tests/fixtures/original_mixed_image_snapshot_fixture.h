#pragma once
#include "bindless_package_request_fixture.h"
#include "original_mixed_root_closure_fixture.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

namespace arp::test {
// Shared-image admission is cold. A package or a mixed snapshot never lends
// execution/completion authority from the existing independent depth/storage Roots.
class OriginalMixedImageSnapshotFixture final {
    // L2 failure injection only; actual allocation correspondence is checked
    // independently by verifyDevice on all three real backends.
    class AllocationProbe final : public rhi::BindlessCrossExtension {
    public:
        rhi::BindlessCapabilities caps;
        unsigned failure = 0u;
        mutable unsigned calls = 0u;

        const rhi::BindlessCapabilities& capabilities() const noexcept override { return caps; }

    protected:
        std::uint64_t queryOriginalSnapshotBufferAllocation(
            const rhi::BufferDesc& desc, const char*& id) const override
        {
            ++calls;
            id = nullptr;
            if (failure == 1u) {
                id = "TEST-ALLOCATION-QUERY-FAILURE";
                return 0u;
            }
            if (failure == 2u)
                return desc.sizeBytes - 1u;
            if (failure == 3u && calls == 1u)
                return UINT64_MAX;
            return desc.sizeBytes;
        }
    };

public:
    using EntryCheck = void (*)(rhi::GpuDevice&,
        const std::shared_ptr<rhi::BindlessSnapshot>&,
        const std::array<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>, 3u>&,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>&,
        const std::shared_ptr<core::CpuMemoryQuota>&);

    static std::uint64_t additionalGpuBytes()
    {
#if defined(ARP_BINDLESS_DEPTH_PYRAMID_PACKAGE)
        core::pmr::String diagnostic;
        const auto package = rhi::BindlessShaderPackage::load(ARP_BINDLESS_DEPTH_PYRAMID_PACKAGE,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(package);
        return package->artifact().snapshotBytes + 8u * sizeof(std::uint32_t);
#else
        assert(false && "Mixed snapshot requires its authenticated depth-pyramid package");
        return 0u;
#endif
    }

    template <class Closure>
    static void verifyBindings(rhi::GpuDevice& device,
        const rhi::BindlessCrossExtension& extension,
        const std::shared_ptr<rhi::BindlessSnapshot>& snapshot,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        core::pmr::String& diagnostic,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& inputs,
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& depth,
        const std::shared_ptr<const rhi::OriginalCommandBufferLease>& results,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        EntryCheck entryCheck)
    {
        using namespace rhi;
        auto authorization = extension.authorize(package, *snapshot, diagnostic);
        if (!authorization)
            std::fprintf(stderr, "mixed preparation10 authorization: %s\n", diagnostic.c_str());
        const bool executable = snapshot->plan().originalExecutionContractVersion != 0u;
        assert(authorization && authorization->plan().executable == executable);
        assert(
            !semantics::OriginalBindlessPreparationLowering::hasStoragePlan(authorization->plan()));
        // All resources are scalar in this package; no native argument-buffer
        // array is needed. The finite test envelope is the snapshot footprint.
        const auto argumentBytes = snapshot->plan().snapshotBytes;
        const auto retained = argumentBytes + package->sourceBytes();
        BindlessComputeBindingPool pool(extension,
            {3u,
                3u * (retained + argumentBytes),
                retained + argumentBytes,
                static_cast<std::uint32_t>(package->artifact().physicalLayout.bindings.size()),
                argumentBytes});
        std::array<std::shared_ptr<const BindlessPreparedComputeBinding>, 3u> prepared;
        constexpr const char* entries[] = {
            "depthPyramidCopyMain", "depthPyramidReduceMain", "depthPyramidReadMain"};
        constexpr std::uint32_t masks[] = {0xc3u, 0xccu, 0xf8u};
        for (unsigned i = 0u; i < prepared.size(); ++i) {
            auto pipeline = core::shareUnique(
                extension.createComputePipeline(authorization, entries[i], diagnostic));
            if (!pipeline)
                std::fprintf(stderr,
                    "mixed preparation10 pipeline %s: %s\n",
                    entries[i],
                    diagnostic.c_str());
            assert(pipeline && pipeline->bindlessAuthorization() == authorization);
            const char* id = nullptr;
            const auto* module =
                selectBindlessComputeEntryAccess(*package, pipeline->shaderReflection(), id);
            assert(module && !id);
            for (const auto& shape : package->artifact().physicalLayout.bindings) {
                bool active = false;
                id = semantics::OriginalBindlessEntryAccessContract::binding(
                    module->reflection.reflection,
                    snapshot->plan().execution,
                    package->artifact().set,
                    shape.binding,
                    ShaderStage::Compute,
                    active);
                if (id)
                    std::fprintf(stderr, "mixed preparation10 entry reflection: %s\n", id);
                assert(!id && active == bool(masks[i] & (1u << shape.binding)));
            }
            prepared[i] = pool.prepare(
                {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()}, diagnostic);
            if (!prepared[i])
                std::fprintf(
                    stderr, "mixed preparation10 binding %s: %s\n", entries[i], diagnostic.c_str());
            assert(prepared[i] && prepared[i]->validatePayload().success);
            if (i + 1u == prepared.size()) {
                assert(!pool.prepare(
                           {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()},
                           diagnostic)
                    && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-POOL-FULL");
            }
            // The cold profile must refuse ordinary recording. Executable
            // profiles are exercised only by their sealed public stream below.
            if (executable)
                continue;
            // Ordinary command metadata has its own finite fixture budget;
            // it must not consume the snapshot's resource-allocation quota.
            auto commandQuota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
            core::CpuMemoryQuotaScope commandScope(commandQuota);
            auto encoder =
                device.createCommandEncoder({"mixed preparation is not execution", 1u, 1u});
            assert(encoder);
            auto pass = encoder->beginComputePass({"mixed preparation is not execution"});
            if (device.backend() == Backend::WebGPU) {
                assert(!pass && !encoder->lastValidation().success);
            } else {
                assert(pass && !pass->setPipeline(*pipeline).success);
                assert(pass->end().success);
            }
        }
        assert(pool.liveBindings() == prepared.size());
        if (!executable)
            OriginalMixedRootClosureFixture::verify<Closure>(
                device, snapshot, prepared, inputs, depth, results);
        if (entryCheck) {
            // CPU command construction is independent of the exact snapshot
            // resource quota, just like the ordinary record-refusal probes.
            auto commandQuota = core::makeShared<core::CpuMemoryQuota>(4u * 1024u * 1024u);
            core::CpuMemoryQuotaScope commandScope(commandQuota);
            entryCheck(device,
                snapshot,
                prepared,
                OriginalMixedRootClosureFixture::completeSources(inputs, snapshot, depth, results),
                resources);
        }
        for (auto& binding : prepared)
            binding.reset();
        assert(!pool.liveBindings() && !pool.chargedBytes());
        if (executable)
            std::printf(
                "BINDLESS_MIXED_IMAGE_EXECUTION_BINDINGS_OK backend=%u execution=%u shared-authorization entries3 authentic-masks pool0\n",
                static_cast<unsigned>(device.backend()),
                snapshot->plan().originalExecutionContractVersion);
        else
            std::printf(
                "BINDLESS_MIXED_IMAGE_PREPARATION10_OK backend=%u shared-authorization entries3 authentic-masks physical-bindings pool0 record-refused\n",
                static_cast<unsigned>(device.backend()));
    }

    template <class Closure>
    static void verifyDevice(rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& depthRoot,
        const core::pmr::Vector<rhi::OriginalCommandResourceSources::Source>& inputs,
        const std::shared_ptr<core::CpuMemoryQuota>& resources,
        EntryCheck entryCheck = nullptr,
        EntryCheck executionCheck = nullptr)
    {
#if defined(ARP_BINDLESS_DEPTH_PYRAMID_PACKAGE)
        using namespace rhi;
        core::pmr::String diagnostic;
        auto package = BindlessShaderPackage::load(ARP_BINDLESS_DEPTH_PYRAMID_PACKAGE,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(package);
        const auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension
            && semantics::OriginalBindlessPreparationLowering::capabilityAvailable(
                extension->capabilities(), 9u));
        std::shared_ptr<const OriginalCompletedDepthTextureSource> proof;
        for (std::size_t i = 0u; i < depthRoot->elements().size() && !proof; ++i)
            proof = depthRoot->originalCompletedDepthAt(i);
        assert(proof && !OriginalCompletedDepthTextureSource::validateOriginal(proof));
        diagnostic.reserve(512u);
        const auto gpuBaseline = device.memoryStats().allocatedBytes;
        const auto resourceBaseline = resources->liveBytes();
        const auto depthOwners = proof.use_count();
        const auto deviceLifetime = device.originalDeviceLifetimeTicket();
        const auto executionVersion = device.backend() == Backend::WebGPU ? 12u : 11u;
        for (const auto version : {9u, 10u, executionVersion}) {
            if (version == executionVersion && !executionCheck)
                continue;
            {
                core::CpuMemoryQuotaScope allocation(resources);
                auto artifact = package->artifact();
                const char* allocationDiagnostic = nullptr;
                const auto resident = extension->queryOriginalMixedSnapshotAllocation(
                    artifact, 1u, allocationDiagnostic);
                if (!resident)
                    std::fprintf(stderr, "mixed snapshot9 allocation: %s\n", allocationDiagnostic);
                assert(resident && !allocationDiagnostic && resident >= artifact.snapshotBytes);
                assert(!extension->queryOriginalMixedSnapshotAllocation(
                           artifact, 2u, allocationDiagnostic)
                    && allocationDiagnostic);
                // The retained package remains immutable; only the cold allocation
                // envelope is projected from actual backend allocation requirements.
                artifact.snapshotBytes = resident;
                auto request = BindlessPackageRequestFixture::make(
                    artifact, extension->capabilities().deviceIdentity);
                if (version == executionVersion)
                    request.originalExecution = {version, true};
                else
                    request.originalPreparation = {version, true};
                auto insufficient = request;
                --insufficient.budget.maxResidentBytes;
                assert(!extension->plan(insufficient, artifact).success);
                BindlessSnapshotPool pool(device, request.budget);
                assert(!pool.prepare(request, artifact, diagnostic));
                auto snapshot = core::shareUnique(pool.prepareOriginalDescriptors(
                    request, artifact, {2u, true, true}, diagnostic));
                if (!snapshot)
                    std::fprintf(stderr, "mixed snapshot9 create: %s\n", diagnostic.c_str());
                assert(snapshot && !snapshot->sealed());
                assert(!pool.prepareOriginalDescriptors(
                           request, artifact, {2u, true, true}, diagnostic)
                    && diagnostic == "RHI-BINDLESS-SNAPSHOT-POOL-FULL");
                const bool native = device.backend() != Backend::WebGPU;
                assert(!snapshot
                        ->writeOriginalCompletedTexture(snapshot->handle(2u, 0u),
                            device,
                            proof,
                            {native ? 4u : 3u,
                                true,
                                1u,
                                1u,
                                OriginalCompletedDepthTextureSource::completeRetainedOwnerCount})
                        .success);
                const auto written =
                    snapshot->writeOriginalCompletedTexture(snapshot->handle(1u, 0u),
                        device,
                        proof,
                        {native ? 4u : 3u,
                            true,
                            1u,
                            1u,
                            OriginalCompletedDepthTextureSource::completeRetainedOwnerCount});
                if (!written.success)
                    std::fprintf(
                        stderr, "mixed snapshot9 depth: %s\n", written.diagnostics.c_str());
                assert(written.success && proof.use_count() > depthOwners);
                // Select the original exact views, without inventing new content
                // identity or treating an uninitialized storage cell as completed.
                for (unsigned role = 0u; role < 4u; ++role) {
                    const auto mip = role == 0u || role == 2u ? 1u : 2u;
                    const auto usage = role < 2u ? TextureUsage::Storage : TextureUsage::Sampled;
                    std::shared_ptr<const OriginalCommandTextureViewSource> view;
                    for (const auto& input : inputs)
                        if (const auto* source = std::get_if<
                                std::shared_ptr<const OriginalCommandTextureViewSource>>(&input))
                            if ((*source)->originalObject()->desc().baseMipLevel == mip
                                && (*source)->originalObject()->desc().usage == usage)
                                view = *source;
                    assert(view);
                    const auto& texture = *view->originalParentLease()->originalObject();
                    const auto before =
                        texture.observeOriginalContent({mip, 1u, 0u, 1u, TextureAspect::Color});
                    assert(before);
                    assert(!snapshot
                            ->writeOriginalTextureCell(snapshot->handle(1u, 0u),
                                device,
                                view,
                                *before,
                                {native ? 1u : 2u,
                                    true,
                                    1u,
                                    1u,
                                    OriginalCommandTextureViewSource::completeRetainedOwnerCount})
                            .success);
                    const auto write =
                        snapshot->writeOriginalTextureCell(snapshot->handle(role + 2u, 0u),
                            device,
                            view,
                            *before,
                            {native ? 1u : 2u,
                                true,
                                1u,
                                1u,
                                OriginalCommandTextureViewSource::completeRetainedOwnerCount});
                    if (!write.success)
                        std::fprintf(
                            stderr, "mixed snapshot9 cell: %s\n", write.diagnostics.c_str());
                    assert(write.success && !texture.validateOriginalContentIdentity(*before));
                }
                auto results = device.createBuffer({"mixed pyramid eight-lane receipt",
                    8u * sizeof(std::uint32_t),
                    ResourceUsage::StorageBuffer | ResourceUsage::CopySrc,
                    MemoryUsage::CpuToGpu,
                    ResourceState::ShaderWrite});
                assert(
                    results && snapshot->writeBuffer(snapshot->handle(6u, 0u), *results).success);
                std::shared_ptr<const OriginalCommandBufferLease> resultSource;
                const auto sealed = snapshot->seal(device);
                if (!sealed.success)
                    std::fprintf(stderr, "mixed snapshot9 seal: %s\n", sealed.diagnostics.c_str());
                assert(sealed.success && snapshot->validate().success && snapshot->sealed());
                assert(snapshot->reportedInternalBufferBytes() == resident);
                if (version == 9u) {
                    assert(!extension->authorize(package, *snapshot, diagnostic)
                        && diagnostic == "RHI-BINDLESS-ORIGINAL-MIXED-IMAGE-SNAPSHOT-ONLY");
                } else {
                    const auto metadata =
                        std::get<std::shared_ptr<const OriginalCommandTextureLease>>(inputs.front())
                            ->originalMetadataQuota();
                    const char* id = BindlessSnapshot::prepareOriginalBufferSources(
                        snapshot, device, {1u, true, 2u, metadata->limitBytes()}, metadata);
                    assert(!id);
                    resultSource = OriginalCommandBufferLease::prepareOriginal(
                        results, {1u, true, 2u, metadata->limitBytes()}, metadata, id);
                    assert(resultSource && !id);
                    verifyBindings<Closure>(device,
                        *extension,
                        snapshot,
                        package,
                        diagnostic,
                        inputs,
                        proof,
                        resultSource,
                        resources,
                        version == executionVersion ? executionCheck : entryCheck);
                }
                assert(snapshot->plan().executable == (version == executionVersion));
                assert(snapshot->plan().originalExecutionContractVersion
                    == (version == executionVersion ? executionVersion : 0u));
                assert(package->artifact().snapshotBytes == 144u);
                std::printf(
                    "BINDLESS_MIXED_SNAPSHOT_ALLOCATION1_OK logical=%llu resident=%llu exact-less\n",
                    static_cast<unsigned long long>(package->artifact().snapshotBytes),
                    static_cast<unsigned long long>(resident));
                snapshot.reset();
            }
            const auto* depthDiagnostic =
                OriginalCompletedDepthTextureSource::validateOriginal(proof);
            const auto* lifetimeDiagnostic = device.validateOriginalDeviceLifetime(deviceLifetime);
            if (proof.use_count() != depthOwners || (!lifetimeDiagnostic && depthDiagnostic))
                std::fprintf(stderr,
                    "Mixed depth retirement: owners=%ld/%ld depth=%s lifetime=%s\n",
                    static_cast<long>(proof.use_count()),
                    static_cast<long>(depthOwners),
                    depthDiagnostic ? depthDiagnostic : "none",
                    lifetimeDiagnostic ? lifetimeDiagnostic : "none");
            assert(proof.use_count() == depthOwners);
            // Device retirement revokes registered-resource authority, including
            // the retained depth prerequisite. It must not revive that proof.
            if (lifetimeDiagnostic) {
                assert(
                    std::string_view(lifetimeDiagnostic) == "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
                assert(depthDiagnostic
                    && std::string_view(depthDiagnostic) == "RHI-COMPLETED-DEPTH-SOURCE-PARENT");
            } else
                assert(!depthDiagnostic);
            if (device.memoryStats().allocatedBytes != gpuBaseline
                || resources->liveBytes() != resourceBaseline)
                std::fprintf(stderr,
                    "mixed snapshot9 retirement: gpu %llu/%llu cpu %llu/%llu\n",
                    static_cast<unsigned long long>(device.memoryStats().allocatedBytes),
                    static_cast<unsigned long long>(gpuBaseline),
                    static_cast<unsigned long long>(resources->liveBytes()),
                    static_cast<unsigned long long>(resourceBaseline));
            assert(device.memoryStats().allocatedBytes == gpuBaseline
                && resources->liveBytes() == resourceBaseline);
        }
        std::puts(
            "BINDLESS_MIXED_IMAGE_SNAPSHOT9_OK shared-depth-storage exact-views no-authorization retirement0");
#else
        (void)device;
        (void)depthRoot;
        (void)inputs;
        (void)resources;
        assert(false && "Mixed snapshot requires its authenticated depth-pyramid package");
#endif
    }

    static void verifyPlan(const rhi::BindlessShaderArtifact& artifact)
    {
        using namespace rhi;
        using Lowering = semantics::OriginalBindlessPreparationLowering;
        for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
            for (const auto version : {9u, 10u}) {
                const auto lowering = Lowering::lowering(backend, version);
                assert(lowering.preparationAvailable == (backend != Backend::Null));
                assert(lowering.validatorOnly == (backend == Backend::Null)
                    && !lowering.executionAvailable);
            }
        BindlessCapabilities caps;
        caps.deviceIdentity = 1u;
        caps.limitsQueried = true;
        const bool native = artifact.execution == BindlessExecution::Native;
        caps.execution = native ? BindlessExecution::Native : BindlessExecution::DiagnosticFailure;
        caps.originalMixedImageSnapshotPreparationContractVersion = 9u;
        caps.originalStoragePreparationContractVersion = 7u;
        caps.originalNativeDepthRootPreparationContractVersion = native ? 6u : 0u;
        caps.originalDepthSnapshotPreparationContractVersion = native ? 0u : 4u;
        caps.maxBindGroups = 1u;
        caps.maxBindingsPerGroup = 8u;
        caps.maxStorageBufferBindingSize = artifact.snapshotBytes;
        caps.stageLimits[2u] = {3u, 0u, 2u, 0u, 3u};
        caps.support[2u][0u] = {BindlessOperation::Load | BindlessOperation::Query, true};
        caps.support[2u][2u] = {BindlessOperation::Store | BindlessOperation::Query, true};
        caps.support[2u][5u] = {BindlessOperation::Store, true};
        AllocationProbe probe;
        probe.caps = caps;
        const char* queryDiagnostic = nullptr;
        assert(probe.queryOriginalMixedSnapshotAllocation(artifact, 1u, queryDiagnostic)
                == artifact.snapshotBytes
            && !queryDiagnostic && probe.calls == 3u);
        for (unsigned failure = 1u; failure <= 3u; ++failure) {
            probe.calls = 0u;
            probe.failure = failure;
            assert(!probe.queryOriginalMixedSnapshotAllocation(artifact, 1u, queryDiagnostic)
                && queryDiagnostic);
        }
        probe.calls = 0u;
        probe.failure = 0u;
        assert(!probe.queryOriginalMixedSnapshotAllocation(artifact, 2u, queryDiagnostic)
            && queryDiagnostic && !probe.calls);
        probe.caps.originalMixedImageSnapshotPreparationContractVersion = 0u;
        assert(!probe.queryOriginalMixedSnapshotAllocation(artifact, 1u, queryDiagnostic)
            && queryDiagnostic && !probe.calls);
        auto request = BindlessPackageRequestFixture::make(artifact, caps.deviceIdentity);
        request.originalPreparation = {9u, true};
        const auto plan = planBindless(caps, request, artifact);
        if (!plan.success)
            std::fprintf(stderr, "mixed snapshot9 plan: %s\n", plan.diagnosticId);
        assert(Lowering::hasMixedImageSnapshotPlan(plan) && !plan.executable
            && plan.descriptorCount == 8u);
        assert(!Lowering::hasStoragePlan(plan));
        for (unsigned closure = 1u; closure <= 12u; ++closure)
            assert(
                OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(plan, closure));
        for (unsigned field = 0u; field < 15u; ++field) {
            auto c = caps;
            auto r = request;
            auto a = artifact;
            if (field == 0u)
                c.originalMixedImageSnapshotPreparationContractVersion = 0u;
            if (field == 1u)
                c.originalStoragePreparationContractVersion = 0u;
            if (field == 2u) {
                c.originalNativeDepthRootPreparationContractVersion = 0u;
                c.originalDepthSnapshotPreparationContractVersion = 0u;
            }
            if (field == 3u)
                r.originalPreparation.prototypeOptIn = false;
            if (field == 4u)
                r.originalExecution = {native ? 9u : 10u, true};
            if (field == 5u)
                --c.stageLimits[2u].sampledTextures;
            if (field == 6u)
                --c.stageLimits[2u].storageTextures;
            if (field == 7u)
                --r.budget.maxUpdatesPerSnapshot;
            if (field == 8u)
                --r.budget.maxResidentBytes;
            if (field == 9u)
                c.execution = BindlessExecution::Validator;
            if (field == 10u)
                for (auto& b : a.physicalLayout.bindings)
                    if (b.type == DescriptorType::SampledTexture)
                        b.depthTexture = false;
            if (field == 11u)
                for (auto& b : a.physicalLayout.bindings)
                    if (b.type == DescriptorType::SampledTexture)
                        b.depthTexture = true;
            if (field == 12u)
                a.physicalLayout.bindings[1u].storageTextureFormat = TextureFormat::R32Float;
            if (field == 13u)
                a.physicalLayout.bindings[0u].stages = ShaderStage::Fragment;
            if (field == 14u) {
                r.tables[0u].operations = BindlessOperation::SampleLevel;
                a.tables[0u].operations = BindlessOperation::SampleLevel;
            }
            const auto rejected = planBindless(c, r, a);
            assert(!rejected.success && !rejected.executable);
        }
        for (unsigned legacy = 1u; legacy <= 8u; ++legacy) {
            auto old = request;
            old.originalPreparation = {legacy, true};
            assert(!planBindless(caps, old, artifact).success);
        }
        auto bindingCaps = caps;
        bindingCaps.originalMixedImageRootPreparationContractVersion = 10u;
        bindingCaps.originalStorageRootPreparationContractVersion = 8u;
        auto bindingRequest = request;
        bindingRequest.originalPreparation = {10u, true};
        assert(!planBindless(caps, bindingRequest, artifact).success);
        const auto bindingPlan = planBindless(bindingCaps, bindingRequest, artifact);
        assert(Lowering::hasMixedImageBindingPlan(bindingPlan)
            && Lowering::hasImageCellBindingPlan(bindingPlan)
            && !Lowering::hasStoragePlan(bindingPlan) && !bindingPlan.executable);
        assert(bindingPlan.identity != plan.identity);
        // Synthetic capabilities test planning only; actual backends keep the
        // independent execution fields zero until their payload is implemented.
        auto executionRequest = request;
        executionRequest.originalPreparation = {};
        executionRequest.originalExecution = {native ? 11u : 12u, true};
        assert(!planBindless(bindingCaps, executionRequest, artifact).success);
        auto executionCaps = bindingCaps;
        if (native)
            executionCaps.originalNativeMixedComputeExecutionContractVersion = 11u;
        else
            executionCaps.originalEmulatedMixedComputeExecutionContractVersion = 12u;
        const auto noFaultReadback = planBindless(executionCaps, executionRequest, artifact);
        assert(!noFaultReadback.success && !noFaultReadback.executable
            && std::strcmp(noFaultReadback.diagnosticId, "RHI-BINDLESS-FAULT-READBACK-UNAVAILABLE")
                == 0);
        // Planning fixture only: execution must explicitly declare the real
        // fault-readback contract in addition to the mixed stream contract.
        executionCaps.faultReadbackContractVersion = 1u;
        const auto executionPlan = planBindless(executionCaps, executionRequest, artifact);
        if (!executionPlan.success)
            std::fprintf(stderr, "mixed execution planning: %s\n", executionPlan.diagnosticId);
        assert(Lowering::hasMixedImageExecutionPlan(executionPlan)
            && Lowering::hasImageCellBindingPlan(executionPlan)
            && !Lowering::hasMixedImageBindingPlan(executionPlan)
            && !Lowering::hasStoragePlan(executionPlan));
        assert(!OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
            executionPlan, native ? 14u : 15u));
        for (unsigned legacy = 1u; legacy <= 13u; ++legacy)
            assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
                executionPlan, legacy));
        assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
            bindingPlan, native ? 14u : 15u));
        assert(executionPlan.identity != bindingPlan.identity
            && executionPlan.identity != plan.identity);
        for (unsigned field = 0u; field < 4u; ++field) {
            auto missing = bindingCaps;
            if (field == 0u)
                missing.originalMixedImageRootPreparationContractVersion = 0u;
            if (field == 1u)
                missing.originalMixedImageSnapshotPreparationContractVersion = 0u;
            if (field == 2u)
                missing.originalStorageRootPreparationContractVersion = 0u;
            if (field == 3u) {
                missing.originalNativeDepthRootPreparationContractVersion = 0u;
                missing.originalDepthSnapshotPreparationContractVersion = 0u;
            }
            assert(!planBindless(missing, bindingRequest, artifact).success);
        }
        for (unsigned closure = 1u; closure <= 12u; ++closure)
            assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(
                bindingPlan, closure));
        assert(
            !OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(bindingPlan, 13u));
        assert(OriginalBindlessRootNativeClosureContract::storageProfileDiagnostic(plan, 13u));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto closure =
                semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 13u);
            assert(closure.nativeCorrespondenceAvailable == (backend != Backend::Null));
            assert(
                closure.validatorOnly == (backend == Backend::Null) && !closure.executionAvailable);
        }
        OriginalBindlessRootNativeClosureBudget mixedClosure{13u, true, 9u, 256u, 256u, 65536u};
        mixedClosure.completedDepthInputs = {native ? 2u : 1u,
            true,
            1u,
            4u,
            OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        assert(!OriginalBindlessRootNativeClosureContract::validateBudget(mixedClosure));
        auto badClosure = mixedClosure;
        badClosure.completedDepthInputs = {};
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        badClosure = mixedClosure;
        badClosure.maxAttachmentViews = 1u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        badClosure = mixedClosure;
        badClosure.prototypeOptIn = false;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        for (const auto& shape : artifact.physicalLayout.bindings) {
            if (shape.type != DescriptorType::SampledTexture
                && shape.type != DescriptorType::StorageTexture)
                continue;
            const bool depth = shape.type == DescriptorType::SampledTexture && shape.depthTexture;
            for (unsigned bits = 0u; bits < 4u; ++bits) {
                const bool hasDepth = bool(bits & 1u), hasCell = bool(bits & 2u);
                const auto* id =
                    Lowering::imageProofDiagnostic(bindingPlan, shape, hasDepth, hasCell);
                assert(bool(id) == !(hasDepth == depth && hasCell == !depth));
                assert(Lowering::imageProofDiagnostic(plan, shape, hasDepth, hasCell));
            }
        }
        auto selectedBinding = bindingRequest;
        selectedBinding.shaderPackageIdentity = 0u;
        assert(
            selectBindlessVariant(BindlessPolicy::Auto, bindingCaps, selectedBinding, &artifact, 1u)
                .path
            == BindlessSelectionPath::Traditional);
        assert(selectBindlessVariant(
                   BindlessPolicy::Required, bindingCaps, selectedBinding, &artifact, 1u)
                   .path
            == BindlessSelectionPath::Rejected);
        auto selected = request;
        selected.shaderPackageIdentity = 0u;
        assert(selectBindlessVariant(BindlessPolicy::Auto, caps, selected, &artifact, 1u).path
            == BindlessSelectionPath::Traditional);
        assert(selectBindlessVariant(BindlessPolicy::Required, caps, selected, &artifact, 1u).path
            == BindlessSelectionPath::Rejected);
    }
};
} // namespace arp::test
