#pragma once
#include "bindless_package_request_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "original_bindless_depth_stream_fixture.h"
#include "original_bindless_root_assembly_fixture.h"
#include "rhi/frontend/bindless_root_execution.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

namespace arp::test {
template <class BackendFixture> class OriginalNativeDepthRootPreparationFixture final {
public:
    static auto prepare(
        const std::shared_ptr<const rhi::OriginalCompletedDepthTextureSource>& proof,
        const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        bool execute = false,
        bool fault = false,
        unsigned terminal = 0u,
        void (*mixedPlanCheck)(rhi::GpuDevice&,
            const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&,
            const std::shared_ptr<const rhi::OriginalCommandResourceSources>&) = nullptr)
    {
        using namespace rhi;
        using Sources = OriginalCommandResourceSources;
        using Closure = typename BackendFixture::Closure;
        const auto check = [](const char* id) {
            if (id)
                std::fprintf(stderr, "native depth Root6: %s\n", id);
            assert(!id);
        };
        const char* id = nullptr;
        core::pmr::String diagnostic;
        auto* extension =
            static_cast<BindlessCrossExtension*>(device.queryInterface(kExtensionBindlessCross));
        assert(extension
            && extension->capabilities().originalNativeDepthRootPreparationContractVersion == 6u);
        assert(!extension->capabilities().originalDepthComputeExecutionContractVersion);
        const auto& artifact = package->artifact();
        auto request =
            BindlessPackageRequestFixture::make(artifact, extension->capabilities().deviceIdentity);
        request.allowEmulated = false;
        request.originalPreparation = {6u, true};
        if (execute) {
            request.originalPreparation = {};
            request.originalExecution = {6u, true};
        }
        const auto plan = extension->plan(request, artifact);
        assert(execute ? OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 6u)
                       : semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 6u)
                    && !plan.executable);
        auto unscoped = request;
        unscoped.originalPreparation = {};
        unscoped.originalExecution = {};
        assert(!extension->plan(unscoped, artifact).success);
        auto legacy = request;
        legacy.originalPreparation = {5u, true};
        assert(!extension->plan(legacy, artifact).success);
        BindlessSnapshotPool snapshots(device, request.budget);
        if (!execute)
            assert(!snapshots.prepare(request, artifact, diagnostic));
        auto snapshot = core::shareUnique(
            snapshots.prepareOriginalDescriptors(request, artifact, {2u, true, true}, diagnostic));
        if (!snapshot)
            std::fprintf(stderr, "native depth Snapshot6: %s\n", diagnostic.c_str());
        assert(snapshot);
        const auto& view = proof->originalViewSource();
        core::pmr::Vector<Sources::Source> inputs{view->originalParentLease(), view};
        const OriginalCommandResourceLeaseBudget leases{1u, true, 2u, quota->limitBytes()};
        for (const auto& table : artifact.tables) {
            const auto* shape =
                findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
            assert(shape);
            for (std::uint32_t i = 0u; i < table.capacity; ++i) {
                const auto handle = snapshot->handle(table.tableId, i);
                if (shape->type == DescriptorType::SampledTexture) {
                    assert(snapshot
                            ->writeOriginalCompletedTexture(handle,
                                device,
                                proof,
                                {4u,
                                    true,
                                    1u,
                                    1u,
                                    OriginalCompletedDepthTextureSource::
                                        completeRetainedOwnerCount})
                            .success);
                } else if (shape->type == DescriptorType::Sampler) {
                    SamplerDesc desc;
                    desc.minFilter = desc.magFilter = SamplerFilter::Nearest;
                    desc.mipFilter = SamplerMipFilter::Nearest;
                    desc.compareOp = CompareOp::Never;
                    auto object = device.createSampler(desc);
                    assert(object);
                    assert(snapshot->writeSampler(handle, *object).success);
                    auto lease =
                        OriginalCommandSamplerLease::prepareOriginal(object, leases, quota, id);
                    check(id);
                    assert(lease);
                    inputs.push_back(std::move(lease));
                } else {
                    assert(shape->type == DescriptorType::StorageBuffer);
                    auto object = device.createBuffer({"native depth Results",
                        16u,
                        ResourceUsage::StorageBuffer,
                        MemoryUsage::CpuToGpu,
                        ResourceState::ShaderWrite});
                    if (!object) {
                        const auto failure = device.lastOriginalResourceRegistrationDiagnostic();
                        std::fprintf(stderr,
                            "native depth Results[%u]: %.*s\n",
                            i,
                            static_cast<int>(failure.size()),
                            failure.data());
                    }
                    assert(object);
                    assert(snapshot->writeBuffer(handle, *object).success);
                    auto lease =
                        OriginalCommandBufferLease::prepareOriginal(object, leases, quota, id);
                    check(id);
                    assert(lease);
                    inputs.push_back(std::move(lease));
                }
            }
        }
        const auto sealed = snapshot->seal(device);
        if (!sealed.success)
            std::fprintf(stderr, "native depth Seal6: %s\n", sealed.diagnostics.c_str());
        assert(sealed.success);
        check(BindlessSnapshot::prepareOriginalBufferSources(snapshot, device, leases, quota));
        for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
                 BindlessSnapshot::OriginalBufferRole::FaultLatch,
                 BindlessSnapshot::OriginalBufferRole::FaultReadback})
            inputs.push_back(snapshot->originalBufferSource(role));
        auto authorization = extension->authorize(package, *snapshot, diagnostic);
        if (!authorization)
            std::fprintf(stderr, "native depth Authorization6: %s\n", diagnostic.c_str());
        assert(authorization);
        auto pipeline = core::shareUnique(extension->createComputePipeline(authorization,
            execute ? (fault ? "faultMain" : "nativeSingleCellMain") : "cleanMain",
            diagnostic));
        if (!pipeline)
            std::fprintf(stderr, "native depth Pipeline6: %s\n", diagnostic.c_str());
        assert(pipeline);
        const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
        BindlessComputeBindingPool bindings(
            *extension, {1u, retained + 4096u, retained + 4096u, 8u, 4096u});
        auto binding = bindings.prepare(
            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()}, diagnostic);
        if (!binding)
            std::fprintf(stderr, "native depth Binding6: %s\n", diagnostic.c_str());
        assert(binding);
        BackendFixture::verifyStandalone(device, pipeline, binding, execute);
        OriginalCommandAdmissionBudget admission;
        admission.contractVersion = 1u;
        admission.prototypeOptIn = true;
        admission.maxCommands = OriginalBindlessDepthStreamFixture::maxCommands;
        admission.maxReferences = OriginalBindlessDepthStreamFixture::maxReferences;
        admission.maxInputDescriptors = OriginalBindlessDepthStreamFixture::maxDescriptors;
        admission.maxGraphOwners = 256u;
        admission.maxStateTransactions = 1u;
        admission.maxStateObservations = 16u;
        admission.maxTextureRanges = 16u;
        admission.maxCpuBytes.fill(quota->limitBytes());
        if (execute) {
            admission.maxPendingSubmissions = admission.maxNativePayloadObjects = 1u;
            admission.maxCpuBytes[static_cast<std::size_t>(
                OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
        }
        for (const auto& input : inputs)
            std::visit(
                [&](const auto& source) {
                    using T =
                        std::remove_const_t<typename std::decay_t<decltype(source)>::element_type>;
                    ++admission.maxSourceOwners[static_cast<std::size_t>(T::domain)];
                },
                input);
        admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] =
            1u;
        auto inventory = Sources::prepareColdOriginalTyped(
            inputs.data(), inputs.size(), admission, {6u, true, 1u}, quota, id);
        check(id);
        assert(inventory);
        auto root = OriginalBindlessRootAssemblyFixture::prepare(
            snapshot, binding, inventory, device, quota, id);
        check(id);
        assert(root);
        BackendFixture::verifyNativeProofs(root, snapshot, proof);
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
        const auto count = static_cast<std::uint32_t>(root->elements().size() + 1u);
        OriginalBindlessRootNativeClosureBudget budget{
            9u, true, count, 256u, 512u, nativeQuota->limitBytes()};
        budget.completedDepthInputs = {
            2u, true, 1u, 16u, OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        const auto factory = [&] {
            return Closure::prepareOriginal(root, inventory, device, budget, nativeQuota, id);
        };
        CpuQuotaFactoryFixture pressure(nativeQuota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        auto closure = factory();
        check(id);
        assert(closure && !closure->nativeExecutionProven());
        budget.maxNativeOwners = closure->nativeOwnerCount();
        budget.maxRetainedOwners = closure->retainedOwnerCount();
        {
            auto exact = factory();
            check(id);
            assert(exact);
        }
        const auto refuse = [&] {
            const auto before = nativeQuota->liveBytes();
            assert(!factory() && id);
            assert(nativeQuota->liveBytes() == before);
        };
        --budget.maxResourceEntries;
        refuse();
        ++budget.maxResourceEntries;
        --budget.maxNativeOwners;
        refuse();
        ++budget.maxNativeOwners;
        --budget.maxRetainedOwners;
        refuse();
        ++budget.maxRetainedOwners;
        --budget.completedDepthInputs.maxCells;
        refuse();
        ++budget.completedDepthInputs.maxCells;
        --budget.completedDepthInputs.maxTextureTexelBytes;
        refuse();
        ++budget.completedDepthInputs.maxTextureTexelBytes;
        --budget.completedDepthInputs.maxRetainedOwners;
        refuse();
        ++budget.completedDepthInputs.maxRetainedOwners;
        budget.completedDepthInputs.contractVersion = 1u;
        refuse();
        budget.completedDepthInputs.contractVersion = 2u;
        auto old = budget;
        old.contractVersion = 1u;
        old.completedDepthInputs = {};
        assert(!Closure::prepareOriginal(root, inventory, device, old, nativeQuota, id));
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{nativeQuota, quota}, [&]() noexcept {
                return closure->validate();
            }));
        // No old command issuer can consume this cold Root, despite a genuine
        // shader pipeline and physical descriptor set having been prepared.
        assert(
            !OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(snapshot->plan(), 5u));
        assert(OriginalCompletedDepthReadInputContract::inspect({proof,
                                                                    ResourceState::ShaderRead,
                                                                    PipelineStage::FragmentShader,
                                                                    ResourceAccess::ShaderRead},
            view,
            budget.completedDepthInputs)
                .diagnostic);
        auto forged =
            std::shared_ptr<const OriginalCompletedDepthTextureSource>(nativeQuota, proof.get());
        assert(OriginalCompletedDepthReadInputContract::inspect({forged,
                                                                    ResourceState::ShaderRead,
                                                                    PipelineStage::ComputeShader,
                                                                    ResourceAccess::ShaderRead},
            view,
            budget.completedDepthInputs)
                .diagnostic);
        auto different =
            OriginalCommandTextureViewSource::prepareOriginal(view->originalViewLease(),
                view->originalParentLease(),
                {1u,
                    true,
                    OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                    quota->limitBytes()},
                quota,
                id);
        check(id);
        assert(different);
        const auto wrongView =
            OriginalCompletedDepthReadInputContract::inspect({proof,
                                                                 ResourceState::ShaderRead,
                                                                 PipelineStage::ComputeShader,
                                                                 ResourceAccess::ShaderRead},
                different,
                budget.completedDepthInputs);
        assert(wrongView.diagnostic && !wrongView.cells && !wrongView.textureTexelBytes);
        OriginalBindlessDepthStreamFixture::execute(
            root,
            inventory,
            device,
            [&](const auto&, const auto&, const auto&, const auto& transaction, GpuDevice&) {
                using Transaction = OriginalCommandStateTransaction;
                const auto& parent = view->originalParentLease();
                const auto range = defaultTextureSubresourceRange(parent->originalObject()->desc());
                const auto captured =
                    parent->originalObject()->observeOriginalCommittedState(range);
                assert(captured);
                assert(transaction->originalContentPlanContractVersion() == 12u
                    && transaction->plannedContentControlCount() == 1u
                    && transaction->plannedContentCellCount() == 1u);
                for (const auto backend :
                    {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU})
                    assert(transaction->executionDiagnostic(backend));
                check(transaction->applyOriginalPrototypePlan());
                assert(transaction->status() == Transaction::Status::Pending);
                assert(!parent->originalObject()->observeOriginalCommittedState(range));
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{quota, nativeQuota, transaction->originalMetadataQuota()},
                    [&]() noexcept -> const char* {
                        if (const auto* failure = transaction->validate())
                            return failure;
                        return closure->validate();
                    }));
                check(transaction->abandonOriginalPendingOwnership());
                assert(transaction->journalEmpty());
                const auto restored =
                    parent->originalObject()->observeOriginalCommittedState(range);
                // Original state rollback restores the value and advances revision
                // to invalidate stale state observations. Content was never enlisted.
                assert(restored && restored->state() == captured->state()
                    && restored->revision() > captured->revision());
                check(proof->validate());
                check(closure->validate());
            },
            12u,
            1u);
        if (mixedPlanCheck)
            mixedPlanCheck(device, root, inventory);
        else if (execute)
            BackendFixture::execute(root, inventory, budget, device, fault, terminal);
        else {
            BackendFixture::reportCold();
        }
        return closure;
    }
};
} // namespace arp::test
