#pragma once
#include "original_bindless_root_source_fixture.h"
#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_bindless_preparation.h"
#include "rhi/backends/webgpu/webgpu_bindless_root.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_cold_command.h"
#include "rhi/backends/webgpu/webgpu_bindless_root_issuer_preparation.h"
#include "rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h"
#include "rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"

namespace arp::test {
template <bool Graphics = false> struct WebGpuOriginalBindlessPreparationRequestImpl {
    static constexpr bool enabled = false;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalPreparation = {Graphics ? 2u : 1u, true};
    }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        using Cold = rhi_webgpu::WebGpuBindlessRootColdAdmission;
        budget.maxCallbackTickets =
            Cold::baseCallbackTickets + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
        budget.maxCallbackRegistrations = Cold::baseCallbackRegistrations
            + rhi::OriginalCommandReadbackMapContract::participantsPerMap;
        budget.maxNativePayloadObjects = 1u;
    }
};

class WebGpuOriginalBindlessPreparationFixture final {
public:
    explicit WebGpuOriginalBindlessPreparationFixture(bool freezeProgram = false)
        : freezeProgram_(freezeProgram)
    {}

    void observeOriginalReadback(
        const std::shared_ptr<const rhi::OriginalCommandBufferReadbackBinding>& binding,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& transaction,
        rhi::GpuDevice& device) const
    {
        using Cold = rhi_webgpu::WebGpuBindlessRootColdAdmission;
        using Seal = rhi::OriginalCommandRecordingSeal;
        using Reservation = rhi::OriginalCommandLifecycleReservation;
        const auto cpu = transaction->originalMetadataQuota()->limitBytes();
        Cold::Budget budget{1u,
            true,
            binding->originalProgram()->operations().size(),
            binding->originalProgram()->operations().size() + 3u,
            512u,
            cpu,
            {1u, true, 1u, 1u, 1u, 4u, 4u, cpu},
            {1u,
                true,
                1u,
                1u,
                rhi::OriginalCommandReadbackMapContract::scopeCallbacksPerMap,
                Cold::baseCallbackTickets
                    + rhi::OriginalCommandReadbackMapContract::participantsPerMap,
                Cold::baseCallbackRegistrations
                    + rhi::OriginalCommandReadbackMapContract::participantsPerMap}};
        Cold::Requirements required;
        assert(!Cold::deriveRequirements(closure_, transaction, binding, budget, required));
        assert(required.capacity.contractVersion == 6u && required.capacity.callbackTickets == 12u
            && required.capacity.callbackRegistrations == 12u && required.readbackMap.maps == 1u);
        const auto refused = [&](const auto& invalid) {
            Cold::Requirements result;
            assert(Cold::deriveRequirements(closure_, transaction, binding, invalid, result));
            assert(result.originalCommands == 0u && result.capacity.pendingSubmissions == 0u
                && !rhi::OriginalCommandReadbackMapContract::hasRequirements(result.readbackMap));
        };
        auto invalid = budget;
        --invalid.maxCommands;
        refused(invalid);
        invalid = budget;
        --invalid.maxOperationVisits;
        refused(invalid);
        invalid = budget;
        --invalid.readbackMap.maxTotalCallbackRegistrations;
        refused(invalid);
        invalid = budget;
        --invalid.readback.maxUniqueResultBytes;
        refused(invalid);
        invalid = budget;
        invalid.prototypeOptIn = false;
        refused(invalid);
        auto submission = core::makeShared<core::CpuMemoryQuota>(cpu);
        auto callback = core::makeShared<core::CpuMemoryQuota>(cpu);
        rhi::OriginalCommandNativeLifecycleBudget life{10u,
            true,
            1u,
            required.capacity.callbackTickets,
            required.capacity.callbackRegistrations,
            512u,
            512u,
            512u,
            cpu,
            cpu,
            cpu};
        const char* diagnostic = nullptr;
        assert(rhi::OriginalCommandNativeLifecycleContract::inspectOriginalPreparation(
            transaction, life, submission, submission));
        auto seal =
            Seal::prepareOriginal(transaction, device, life, submission, callback, diagnostic);
        if (diagnostic)
            std::printf("RootCallback cold seal: %s\n", diagnostic);
        assert(seal && !diagnostic && !seal->nativeExecutionProven());
        auto wrongRequest = required.capacity;
        --wrongRequest.callbackTickets;
        auto reservation = Reservation::prepareOriginal(seal, wrongRequest, diagnostic);
        assert(reservation && !diagnostic);
        assert(!Cold::prepareOriginal(closure_, reservation, binding, device, budget, diagnostic)
            && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-CALLBACK-COLD-CAPACITY-MISMATCH");
        reservation.reset();
        reservation = Reservation::prepareOriginal(seal, required.capacity, diagnostic);
        assert(reservation && !diagnostic);
        const auto& inventory = closure_->originalInventory();
        assert(inventory->originalReservedSubmissionCount() == 1u
            && inventory->originalReservedCallbackTicketCount() == required.capacity.callbackTickets
            && inventory->originalReservedCallbackRegistrationCount()
                == required.capacity.callbackRegistrations);
        const auto factory = [&] {
            return Cold::prepareOriginal(
                closure_, reservation, binding, device, budget, diagnostic);
        };
        CpuQuotaFactoryFixture allocation(transaction->originalMetadataQuota());
        const auto peak = allocation.measure(factory);
        allocation.verify(peak, factory);
        auto admitted = factory();
        assert(admitted && !diagnostic && !admitted->nativeExecutionProven());
        budget.maxRetainedOwners = admitted->retainedOwnerCount();
        auto exact = factory();
        assert(exact && !diagnostic);
        exact.reset();
        --budget.maxRetainedOwners;
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-CALLBACK-COLD-OWNER-CAPACITY");
        ++budget.maxRetainedOwners;
        auto forged = std::shared_ptr<const Cold>(callback, admitted.get());
        assert(core::StringView(Cold::validateOriginal(forged))
            == "RHI-WEBGPU-ROOT-CALLBACK-COLD-OWNER");
        forged.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{transaction->originalMetadataQuota(), submission, callback, quota_},
            [&]() noexcept {
                return Cold::validateOriginal(admitted);
            }));
        assert(!transaction->nativeExecutionProven() && transaction->journalEmpty()
            && !binding->originalResult()->publishedBytes());
        verifyOriginalBufferStorage(admitted, device, freezeProgram_);
        const auto owners = admitted->retainedOwnerCount();
        admitted.reset();
        reservation.reset();
        seal.reset();
        assert(!inventory->originalReservedSubmissionCount()
            && !inventory->originalReservedCallbackTicketCount()
            && !inventory->originalReservedCallbackRegistrationCount() && !submission->liveBytes()
            && !callback->liveBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_CALLBACK_COLD_OK callbacks12 maps1 owners%u bytes%llu same-graph-stream-seal result-original exact-less-quota hot0 no-execution no-publication capacity-returned\n",
            owners,
            static_cast<unsigned long long>(peak));
    }

    template <class Root, class Sources>
    void operator()(const Root& root, const Sources& inventory, rhi::GpuDevice* device) const
    {
        if (!device) {
            assert(closure_ && closure_->validate());
            closure_.reset();
            assert(quota_->liveBytes() == 0u);
            return;
        }
        std::visit(
            [&](const auto& prepared) {
                constexpr bool graphics =
                    std::is_same_v<typename std::decay_t<decltype(prepared)>::element_type,
                        const rhi::BindlessPreparedGraphicsBinding>;
                observeRoot<graphics>(root, inventory, device);
            },
            root->originalPrepared());
    }

    template <bool Graphics, class Root, class Sources>
    void observeRoot(const Root& root, const Sources& inventory, rhi::GpuDevice* device) const
    {
        if (!device) {
            assert(closure_ && closure_->validate());
            closure_.reset();
            assert(quota_->liveBytes() == 0u);
            return;
        }
        using namespace rhi_webgpu;
        assert(root && inventory && root->matchesCanonicalSources(*inventory));
        const auto& snapshot = root->originalSnapshot();
        const auto& plan = snapshot->plan();
        assert(plan.success && !plan.executable
            && plan.originalPreparationContractVersion == (Graphics ? 2u : 1u));
        assert(!root->nativeExecutionProven()
            && root->authorization()->current(root->authorization()));
        using Prepared = std::conditional_t<Graphics,
            rhi::BindlessPreparedGraphicsBinding,
            rhi::BindlessPreparedComputeBinding>;
        const auto& binding = std::get<std::shared_ptr<const Prepared>>(root->originalPrepared());
        const auto pipeline = webGpuNativePayloadFor(*binding->owners()->desc().pipeline);
        const auto descriptors = webGpuNativePayloadFor(*root->originalObject());
        const auto physical = webGpuOriginalBindlessPhysicalGroup(binding);
        assert(binding->validatePayload().success && pipeline && pipeline->pipeline
            && pipeline->issuedContract() && descriptors && physical && physical->group
            && descriptors->bindings.size() == root->elements().size());
        auto* extension = static_cast<rhi::BindlessCrossExtension*>(
            device->queryInterface(rhi::kExtensionBindlessCross));
        assert(extension);
        // A tampered finite native write must be rejected before a new group
        // is emitted; the existing binding remains valid after restoration.
        auto& nativeWrite = descriptors->bindings.front().write;
        const auto wholeRange = nativeWrite.bufferBindingRange;
        nativeWrite.bufferBindingRange = {2u, 0u, descriptors->bindings.front().buffer->sizeBytes};
        core::pmr::String rangeDiagnostic;
        const auto rejectedRange = [&] {
            if constexpr (Graphics)
                return prepareWebGpuOriginalBindlessGraphics(
                    *extension, binding->owners(), rangeDiagnostic);
            else
                return prepareWebGpuOriginalBindlessCompute(
                    *extension, binding->owners(), rangeDiagnostic);
        }();
        assert(!rejectedRange && !rangeDiagnostic.empty());
        nativeWrite.bufferBindingRange = wholeRange;
        assert(binding->validatePayload().success);
        for (const auto& element : root->elements()) {
            const auto native = std::find_if(
                descriptors->bindings.begin(), descriptors->bindings.end(), [&](const auto& entry) {
                    return rhi::descriptorBindingWriteMatches(
                        entry.write, element.declaration.write);
                });
            assert(native != descriptors->bindings.end() && native->buffer && native->buffer->buffer
                && native->buffer->ownerDevice == pipeline->issuedContract()->nativeDevice());
        }
        assert(extension
            && extension->capabilities().execution == rhi::BindlessExecution::DiagnosticFailure
            && !extension->capabilities().executablePayload
            && extension->capabilities().originalPreparationContractVersion == 1u);
        auto selectionRequest = BindlessPackageRequestFixture::make(
            root->authorization()->package().artifact(), plan.deviceIdentity);
        selectionRequest.originalPreparation = {Graphics ? 2u : 1u, true};
        selectionRequest.shaderPackageIdentity = 0u;
        auto ordinaryRequest = selectionRequest;
        ordinaryRequest.shaderPackageIdentity = plan.shaderPackageIdentity;
        rhi::BindlessSnapshotPool ordinarySnapshots(*device, ordinaryRequest.budget);
        core::pmr::String ordinaryDiagnostic;
        assert(!ordinarySnapshots.prepare(
                   ordinaryRequest, root->authorization()->package().artifact(), ordinaryDiagnostic)
            && ordinaryDiagnostic == "RHI-BINDLESS-ORIGINAL-PREPARATION-DESCRIPTORS-REQUIRED");
        const auto selected = rhi::semantics::selectBindlessVariant(rhi::BindlessPolicy::Required,
            extension->capabilities(),
            selectionRequest,
            &root->authorization()->package().artifact(),
            1u);
        assert(selected.path == rhi::BindlessSelectionPath::Rejected
            && core::StringView(selected.diagnosticId)
                == "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE");
        assert(extension->capabilities().originalExecutionContractVersion == 1u);
        auto unavailableCaps = extension->capabilities();
        unavailableCaps.originalExecutionContractVersion = 0u;
        auto executionRequest = selectionRequest;
        executionRequest.originalPreparation = {};
        executionRequest.originalExecution = {1u, true};
        const auto& artifact = root->authorization()->package().artifact();
        for (const auto policy : {rhi::BindlessPolicy::Required, rhi::BindlessPolicy::Auto}) {
            const auto unavailable = rhi::semantics::selectBindlessVariant(
                policy, unavailableCaps, executionRequest, &artifact, 1u);
            assert(unavailable.path
                    == (policy == rhi::BindlessPolicy::Required
                            ? rhi::BindlessSelectionPath::Rejected
                            : rhi::BindlessSelectionPath::Traditional)
                && core::StringView(unavailable.diagnosticId)
                    == "RHI-BINDLESS-ORIGINAL-EXECUTION-UNAVAILABLE");
        }
        executionRequest.shaderPackageIdentity = artifact.shaderPackageIdentity;
        assert(!ordinarySnapshots.prepare(executionRequest, artifact, ordinaryDiagnostic)
            && ordinaryDiagnostic == "RHI-BINDLESS-ORIGINAL-EXECUTION-DESCRIPTORS-REQUIRED");
        if constexpr (!Graphics) {
            const auto independent = extension->plan(executionRequest, artifact);
            assert(independent.success && independent.executable
                && independent.originalExecutionContractVersion == 1u);
        } else {
            const auto refused = extension->plan(executionRequest, artifact);
            assert(!refused.success
                && core::StringView(refused.diagnosticId)
                    == "RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE");
            assert(!extension->capabilities().originalGraphicsExecutionContractVersion);
        }
        executionRequest.originalPreparation = {Graphics ? 2u : 1u, true};
        assert(!ordinarySnapshots.prepareOriginalDescriptors(
                   executionRequest, artifact, {2u, true, true}, ordinaryDiagnostic)
            && ordinaryDiagnostic == "RHI-BINDLESS-ORIGINAL-PROFILE-CONFLICT");
        // Keep the digest unchanged deliberately: profile equality must still
        // stop a forged promotion at authorization and prepared owners.
        auto& mutablePlan = const_cast<rhi::BindlessLoweringPlan&>(snapshot->plan());
        mutablePlan.originalExecutionContractVersion = 1u;
        assert(!extension->authorize(
                   root->authorization()->originalPackageOwner(), *snapshot, ordinaryDiagnostic)
            && ordinaryDiagnostic == "RHI-BINDLESS-AUTHORIZATION-PLAN-MISMATCH");
        assert(!binding->validateOwners().success && !binding->validatePayload().success);
        mutablePlan.originalExecutionContractVersion = 0u;
        assert(!plan.executable && !plan.originalExecutionContractVersion
            && root->authorization()->current(root->authorization())
            && binding->validatePayload().success);
        if constexpr (!Graphics)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_EXECUTION_PROFILE_REFUSED_OK independent-intent cap0 required-rejected auto-traditional original-descriptors conflict-refused preparation-unchanged");
        if constexpr (!Graphics) {
            auto ordinary = device->createCommandEncoder({"original Root execution refusal"});
            if (ordinary) {
                auto pass = ordinary->beginComputePass({"original Root execution refusal"});
                if (pass)
                    assert(!pass->setPipeline(*binding->owners()->desc().pipeline).success);
            }
        }
        using Closure = WebGpuBindlessRootNativeClosure;
        const char* diagnostic = nullptr;
        const auto count = static_cast<std::uint32_t>(root->elements().size()) + 1u;
        rhi::OriginalBindlessRootNativeClosureBudget budget{
            1u, true, count, count + 3u, 512u, quota_->limitBytes()};
        const auto factory = [&] {
            return Closure::prepareOriginal(root, inventory, *device, budget, quota_, diagnostic);
        };
        CpuQuotaFactoryFixture admission(quota_);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        assert(core::StringView(diagnostic) == "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ALLOCATION");
        closure_ = factory();
        assert(closure_ && !diagnostic && !closure_->nativeExecutionProven());
        budget.maxNativeOwners = closure_->nativeOwnerCount();
        budget.maxRetainedOwners = closure_->retainedOwnerCount();
        auto exact = factory();
        assert(exact && !diagnostic);
        exact.reset();
        --budget.maxResourceEntries;
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY");
        ++budget.maxResourceEntries;
        --budget.maxNativeOwners;
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY");
        ++budget.maxNativeOwners;
        --budget.maxRetainedOwners;
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-WEBGPU-BINDLESS-ROOT-OWNER-CAPACITY");
        ++budget.maxRetainedOwners;
        auto forged = std::shared_ptr<const Closure>(quota_, closure_.get());
        assert(core::StringView(Closure::validateOriginal(forged))
            == "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-OWNER");
        forged.reset();
        const auto& readback = root->originalSnapshot()->originalBufferSource(
            rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback);
        auto readbackNative = webGpuNativePayloadFor(*readback->originalObject());
        const auto originalBytes = readbackNative->sizeBytes;
        ++readbackNative->sizeBytes;
        assert(closure_->validate());
        readbackNative->sizeBytes = originalBytes;
        const auto originalGroup = physical->group;
        physical->group = nullptr;
        assert(closure_->validate());
        physical->group = originalGroup;
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 1u>{quota_}, [&]() noexcept {
                return Closure::validateOriginal(closure_);
            }));
        if constexpr (!Graphics)
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_CLOSURE_OK entries%u native%u retained%u bytes%llu exact-less-pressure forged-readback-mutated-refused hot0 original-inventory\n",
                count,
                closure_->nativeOwnerCount(),
                closure_->retainedOwnerCount(),
                static_cast<unsigned long long>(peak));
        if constexpr (Graphics)
            std::puts(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_GRAPHICS_PREPARATION_OK profile2 genuine-pipeline-group-buffers same-inventory exact-less-pressure forged-mutated-refused hot0 no-execution");
        else
            std::puts(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PREPARATION_OK genuine-pipeline-group-buffers same-canonical-inventory nonexecuting selection-record-refused");
    }

private:
    static void verifyOriginalBufferStorage(
        const std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootColdAdmission>& admission,
        rhi::GpuDevice& device,
        bool freezeProgram)
    {
        using Storage = rhi_webgpu::WebGpuCanonicalReadbackResultStorage;
        using Receipt = rhi_webgpu::WebGpuNativeBufferMapReceipt;
        const auto& result = admission->originalReadbackBinding()->originalResult();
        const auto& quota = result->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        Storage::Budget budget{3u, true, 1u, result->byteCapacity(), quota->limitBytes(), 512u};
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Storage::prepareOriginalBuffer(admission, device, budget, diagnostic);
        };
        CpuQuotaFactoryFixture metadata(admission->originalRoot()->originalMetadataQuota());
        const auto peak = metadata.measureRetained(factory);
        metadata.verify(peak, factory);
        auto storage = factory();
        assert(storage && !diagnostic && storage->snapshotCount() == 1u
            && storage->snapshots().empty());
        assert(core::SharedOwnerProjection::sameOwner(storage->originalBufferSnapshot(), result)
            && core::SharedOwnerProjection::sameOwner(
                storage->originalRoot(), admission->originalRoot())
            && core::SharedOwnerProjection::sameOwner(storage->originalBufferAdmission(), admission)
            && quota->liveBytes() == baseline && !storage->nativeExecutionProven());
        budget.maxRetainedOwners = storage->retainedOwnerCount();
        auto exact = factory();
        assert(exact && !diagnostic);
        exact.reset();
        --budget.maxRetainedOwners;
        assert(!factory());
        ++budget.maxRetainedOwners;
        --budget.maxResultBytes;
        assert(!factory());
        ++budget.maxResultBytes;
        --budget.maxSnapshots;
        assert(!factory());
        ++budget.maxSnapshots;
        for (const auto version : {1u, 2u, 4u}) {
            budget.contractVersion = version;
            assert(!factory());
        }
        budget.contractVersion = 3u;
        auto forged = std::shared_ptr<const Storage>(quota, storage.get());
        assert(core::StringView(Storage::validateOriginal(forged))
            == "RHI-WEBGPU-READBACK-STORAGE-OWNER");
        forged.reset();
        const auto& source =
            admission->originalClosure()->originalRoot()->originalSnapshot()->originalBufferSource(
                rhi::BindlessSnapshot::OriginalBufferRole::FaultReadback);
        const auto payload = rhi_webgpu::webGpuNativePayloadFor(*source->originalObject());
        assert(payload && payload->buffer);
        bool delivered = false;
        const auto callback = [](void* userdata, Receipt::Outcome) noexcept {
            *static_cast<bool*>(userdata) = true;
        };
        Receipt receipt;
        assert(storage->validateBufferMapReceipt(receipt));
        assert(!receipt.prepareCold(payload->buffer, result->byteCapacity(), &delivered, callback)
            && !storage->validateBufferMapReceipt(receipt) && !delivered
            && receipt.phase() == Receipt::Phase::Prepared && !receipt.nativeMapSucceeded());
        const auto bytes = payload->sizeBytes;
        ++payload->sizeBytes;
        assert(storage->validate());
        payload->sizeBytes = bytes;
        const auto& seal = admission->originalReservation()->originalSeal();
        assert(core::StringView(Storage::readOriginalUnpublished(storage, seal, 0u, receipt))
            == "RHI-WEBGPU-READBACK-STORAGE-RETAINED-PENDING");
        assert(core::StringView(Storage::publishOriginalCommitted(storage, seal, {}, {1u, true}))
            == "RHI-WEBGPU-READBACK-STORAGE-PUBLICATION-PROFILE");
        assert(!result->publishedBytes() && !delivered && quota->liveBytes() == baseline);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{admission->originalRoot()->originalMetadataQuota(), quota}, [&]() noexcept {
                return Storage::validateOriginal(storage);
            }));
        const auto owners = storage->retainedOwnerCount();
        verifyOriginalRootColdCommand(admission, storage, freezeProgram);
        storage.reset();
        assert(quota->liveBytes() == baseline);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_BUFFER_STORAGE_OK snapshots1 bytes4 owners%u metadata%llu same-result same-native receipt-prepared exact-less-quota forged-mutated-refused hot0 no-map no-publication\n",
            owners,
            static_cast<unsigned long long>(peak));
    }

    static void verifyOriginalRootColdCommand(
        const std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootColdAdmission>& admission,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalReadbackResultStorage>& storage,
        bool freezeProgram)
    {
        using Command = rhi_webgpu::WebGpuBindlessRootColdCommand;
        const auto& quota = admission->originalRoot()->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        Command::Request request{1u,
            true,
            storage->retainedOwnerCount()
                + rhi::OriginalBindlessRootColdCommandContract::additionalOwners,
            quota->limitBytes()};
        const auto& root = admission->originalClosure()->originalRoot();
        const auto& prepared = std::get<std::shared_ptr<const rhi::BindlessPreparedComputeBinding>>(
            root->originalPrepared());
        if (freezeProgram) {
            const auto& operations =
                admission->originalReadbackBinding()->originalProgram()->operations();
            const auto entries = admission->originalClosure()->resourceEntryCount();
            std::uint64_t visits = entries + operations.size();
            for (const auto& op : operations)
                visits += op.referenceCount + 2u * entries;
            request.contractVersion = 2u;
            request.program = {1u, true, operations.size(), visits};
        }
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Command::prepareOriginal(admission, storage, request, diagnostic);
        };
        --request.maxRetainedOwners;
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-ORIGINAL-ROOT-COLD-COMMAND-OWNER-CAPACITY");
        ++request.maxRetainedOwners;
        for (const auto version : {0u, 3u}) {
            request.contractVersion = version;
            assert(!factory());
        }
        request.contractVersion = freezeProgram ? 2u : 1u;
        if (freezeProgram) {
            --request.program.maxCommands;
            assert(!factory()
                && core::StringView(diagnostic) == "RHI-ORIGINAL-ROOT-PROGRAM-COMMAND-CAPACITY");
            ++request.program.maxCommands;
            const auto visits = request.program.maxOperationVisits;
            request.program.maxOperationVisits = 1u;
            assert(!factory()
                && core::StringView(diagnostic) == "RHI-ORIGINAL-ROOT-PROGRAM-VISIT-CAPACITY");
            request.program.maxOperationVisits = visits;
            assert(quota->liveBytes() == baseline);
            const auto& inputs =
                admission->originalReadbackBinding()->originalGraph()->originalInputs();
            for (const auto& op :
                admission->originalReadbackBinding()->originalProgram()->operations()) {
                if (op.kind != rhi::CommandUsageKind::Dispatch)
                    continue;
                auto& dispatch =
                    const_cast<rhi::CommandScalarArguments::Dispatch&>(*inputs->originalScalars()
                            ->originalArguments()
                            ->records()[op.commandIndex]
                            .scalar.get<rhi::CommandScalarArguments::Dispatch>());
                const auto previous = dispatch;
                auto limits = WGPU_LIMITS_INIT;
                assert(
                    wgpuDeviceGetLimits(static_cast<WGPUDevice>(rhi_webgpu::webGpuNativePayloadFor(
                                            *prepared->owners()->desc().pipeline)
                                                ->issuedContract()
                                                ->nativeDevice()),
                        &limits)
                        == WGPUStatus_Success
                    && limits.maxComputeWorkgroupsPerDimension < UINT32_MAX);
                dispatch.x = limits.maxComputeWorkgroupsPerDimension + 1u;
                assert(!factory()
                    && core::StringView(diagnostic) == "RHI-ORIGINAL-ROOT-PROGRAM-DISPATCH-LIMIT");
                dispatch = previous;
            }
            assert(quota->liveBytes() == baseline);
        }
        request.prototypeOptIn = false;
        assert(!factory());
        request.prototypeOptIn = true;
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - baseline);
            assert(pressure.charged() && !factory()
                && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-COLD-COMMAND-ALLOCATION");
        }
        assert(quota->liveBytes() == baseline);
        auto command = factory();
        if (diagnostic)
            std::printf("Root command preparation refused: %s\n", diagnostic);
        assert(command && !diagnostic && !command->nativeExecutionProven()
            && core::SharedOwnerProjection::sameOwner(command->originalAdmission(), admission)
            && core::SharedOwnerProjection::sameOwner(command->originalResultStorage(), storage));
        const auto peak = quota->liveBytes() - baseline;
        const auto* program = command->originalNativeProgram();
        assert(bool(program) == freezeProgram);
        if (program) {
            const auto& operations =
                admission->originalReadbackBinding()->originalProgram()->operations();
            assert(program->records().size() == operations.size() && program->queriedDispatchLimit()
                && program->operationVisits() <= request.program.maxOperationVisits);
            bool clear = false, dispatch = false, copy = false;
            for (std::size_t i = 0u; i < program->records().size(); ++i) {
                const auto& record = program->records()[i];
                assert(record.original.matches(operations[i]));
                if (record.original.kind == rhi::CommandUsageKind::ClearSnapshotFault) {
                    clear = true;
                    assert(record.destination);
                }
                if (record.original.kind == rhi::CommandUsageKind::Dispatch) {
                    dispatch = true;
                    assert(record.dispatch.x == 1u && record.dispatch.y == 1u
                        && record.dispatch.z == 1u);
                }
                if (record.original.kind == rhi::CommandUsageKind::BufferCopy) {
                    copy = true;
                    assert(record.source && record.destination
                        && record.copy.sizeBytes == sizeof(std::uint32_t));
                }
            }
            assert(clear && dispatch && copy);
            auto limits = WGPU_LIMITS_INIT;
            assert(wgpuDeviceGetLimits(static_cast<WGPUDevice>(rhi_webgpu::webGpuNativePayloadFor(
                                           *prepared->owners()->desc().pipeline)
                                               ->issuedContract()
                                               ->nativeDevice()),
                       &limits)
                    == WGPUStatus_Success
                && limits.maxComputeWorkgroupsPerDimension == program->queriedDispatchLimit());
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PROGRAM_COLD_OK commands%zu visits%llu dispatch-limit%u original-stream frozen-clear-bind-dispatch-copy budget-limit-refused quota-retry hot0 no-encoder no-execution\n",
                program->records().size(),
                static_cast<unsigned long long>(program->operationVisits()),
                program->queriedDispatchLimit());
        }
        assert(
            !factory() && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-COLD-COMMAND-DUPLICATE");
        auto forged = std::shared_ptr<const Command>(quota, command.get());
        assert(core::StringView(Command::validateOriginal(forged))
            == "RHI-WEBGPU-ROOT-COLD-COMMAND-OWNER");
        forged.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            return Command::validateOriginal(command);
        }));
        assert(admission->originalRoot()->journalEmpty()
            && !admission->originalRoot()->nativeExecutionProven()
            && !storage->originalBufferSnapshot()->publishedBytes());
        const auto owners = command->retainedOwnerCount();
        verifyOriginalRootCallbackCustody(command);
        command.reset();
        assert(quota->liveBytes() == baseline);
        assert(
            !factory() && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-COLD-COMMAND-DUPLICATE");
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMMAND_COLD_OK owners%u metadata%llu original-admission-result native-device-queue exact-less-quota forged-refused hot0 claim-once no-encoder no-execution\n",
            owners,
            static_cast<unsigned long long>(peak));
    }

    static void verifyOriginalRootCallbackCustody(
        std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootColdCommand>& command)
    {
        using Custody = rhi_webgpu::WebGpuBindlessRootCallbackCustody;
        using Contract = rhi::OriginalBindlessRootCallbackCustodyContract;
        const auto& admission = command->originalAdmission();
        const auto& seal = admission->originalReservation()->originalSeal();
        const auto& submission = seal->originalSubmissionQuota();
        const auto& callback = seal->originalCallbackQuota();
        const auto submissionBaseline = submission->liveBytes(),
                   callbackBaseline = callback->liveBytes();
        Custody::Request request{1u,
            true,
            1u,
            admission->requirements().capacity.callbackTickets,
            512u,
            submission->limitBytes(),
            callback->limitBytes()};
        request.maxRetainedOwners =
            command->retainedOwnerCount() + Contract::additionalOwners(request);
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Custody::prepareOriginal(command, request, diagnostic);
        };
        --request.maxRetainedOwners;
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-ORIGINAL-ROOT-CALLBACK-CUSTODY-OWNER-CAPACITY");
        ++request.maxRetainedOwners;
        for (const auto version : {0u, 2u}) {
            request.contractVersion = version;
            assert(!factory());
        }
        request.contractVersion = 1u;
        request.prototypeOptIn = false;
        assert(!factory());
        request.prototypeOptIn = true;
        --request.maxCallbackSlots;
        assert(!factory());
        ++request.maxCallbackSlots;
        for (const auto& quota : {submission, callback}) {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            assert(!factory()
                && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-ALLOCATION");
        }
        assert(submission->liveBytes() == submissionBaseline
            && callback->liveBytes() == callbackBaseline);
        auto custody = factory();
        assert(custody && !diagnostic && custody->originalNativeLossArmed()
            && custody->pendingOriginalRecords() == 1u && !custody->nativeExecutionProven());
        const auto submissionBytes = submission->liveBytes() - submissionBaseline;
        const auto callbackBytes = callback->liveBytes() - callbackBaseline;
        assert(submissionBytes && callbackBytes);
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-DUPLICATE");
        auto forged = std::shared_ptr<const Custody>(callback, custody.get());
        assert(core::StringView(Custody::validateOriginal(forged))
            == "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-OWNER");
        forged.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{submission, callback}, [&]() noexcept {
                return Custody::validateOriginal(custody);
            }));
        const auto owners = custody->retainedOwnerCount();
        verifyOriginalRootIssuers(custody, command);
        std::weak_ptr<const rhi_webgpu::WebGpuBindlessRootColdCommand> observed = command;
        command.reset();
        assert(!observed.expired() && !custody->validate());
        command = observed.lock();
        assert(command);
        assert(!custody->cancelOriginal() && !custody->originalNativeLossArmed()
            && !custody->pendingOriginalRecords());
        assert(custody->validate());
        custody.reset();
        assert(submission->liveBytes() == submissionBaseline
            && callback->liveBytes() == callbackBaseline);
        assert(!factory()
            && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-CALLBACK-CUSTODY-DUPLICATE");
        assert(admission->originalRoot()->journalEmpty()
            && !admission->originalRoot()->nativeExecutionProven()
            && !command->originalResultStorage()->originalBufferSnapshot()->publishedBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_CALLBACK_CUSTODY_OK callbacks12 maps1 owners%u submission%llu callback%llu canonical-pool original-result native-loss-armed-suppressed quota-retry claim-once forged-refused hot0 no-map no-execution\n",
            owners,
            static_cast<unsigned long long>(submissionBytes),
            static_cast<unsigned long long>(callbackBytes));
    }

    static void verifyOriginalRootIssuers(
        const std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootCallbackCustody>& custody,
        const std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootColdCommand>& command)
    {
        using Issuer = rhi_webgpu::WebGpuBindlessRootIssuerPreparation;
        using Contract = rhi::OriginalBindlessRootIssuerContract;
        const auto& admission = command->originalAdmission();
        const auto& seal = admission->originalReservation()->originalSeal();
        const auto& recording = admission->originalRoot()->originalMetadataQuota();
        const auto& submission = seal->originalSubmissionQuota();
        const auto& callback = seal->originalCallbackQuota();
        const auto recordingBaseline = recording->liveBytes(),
                   submissionBaseline = submission->liveBytes(),
                   callbackBaseline = callback->liveBytes();
        Issuer::Request request{1u,
            true,
            1u,
            1u,
            custody->retainedOwnerCount() + Contract::additionalOwners,
            recording->limitBytes(),
            submission->limitBytes(),
            callback->limitBytes()};
        const char* diagnostic = nullptr;
        const auto factory = [&] {
            return Issuer::prepareOriginal(custody, request, diagnostic);
        };
        --request.maxRetainedOwners;
        assert(!factory());
        ++request.maxRetainedOwners;
        for (const auto version : {0u, 2u}) {
            request.contractVersion = version;
            assert(!factory());
        }
        request.contractVersion = 1u;
        request.prototypeOptIn = false;
        assert(!factory());
        request.prototypeOptIn = true;
        for (const auto member : {&Issuer::Request::maxRecordingCpuBytes,
                 &Issuer::Request::maxSubmissionCpuBytes,
                 &Issuer::Request::maxCallbackCpuBytes}) {
            --(request.*member);
            assert(!factory());
            ++(request.*member);
        }
        for (const auto& quota : {recording, submission}) {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged());
            assert(
                !factory() && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-ISSUER-ALLOCATION");
        }
        assert(recording->liveBytes() == recordingBaseline
            && submission->liveBytes() == submissionBaseline
            && callback->liveBytes() == callbackBaseline && !custody->validate());
        auto issuer = factory();
        assert(issuer && !diagnostic && !issuer->validate() && issuer->issuersClosedForInspection()
            && !issuer->nativeExecutionProven());
        const auto recordingBytes = recording->liveBytes() - recordingBaseline,
                   submissionBytes = submission->liveBytes() - submissionBaseline;
        assert(recordingBytes && submissionBytes && callback->liveBytes() == callbackBaseline);
        assert(!factory() && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-ISSUER-DUPLICATE");
        auto forged = std::shared_ptr<const Issuer>(callback, issuer.get());
        assert(
            core::StringView(Issuer::validateOriginal(forged)) == "RHI-WEBGPU-ROOT-ISSUER-OWNER");
        forged.reset();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{recording, submission, callback}, [&]() noexcept {
                if (const auto* id = Issuer::validateOriginal(issuer))
                    return id;
                return issuer->issuersClosedForInspection() ? nullptr : "ROOT-ISSUERS-OPEN";
            }));
        const auto owners = issuer->retainedOwnerCount();
        issuer.reset();
        assert(!factory() && core::StringView(diagnostic) == "RHI-WEBGPU-ROOT-ISSUER-DUPLICATE");
        assert(recording->liveBytes() > recordingBaseline
            && submission->liveBytes() > submissionBaseline
            && callback->liveBytes() == callbackBaseline && !custody->validate()
            && admission->originalRoot()->journalEmpty()
            && !command->originalResultStorage()->originalBufferSnapshot()->publishedBytes());
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ISSUERS_COLD_OK owners%u recording%llu submission%llu original-issuers seven-native-listeners quota-retry claim-once forged-refused hot0 caller-drop-retained ordinary-closed no-execution\n",
            owners,
            static_cast<unsigned long long>(recordingBytes),
            static_cast<unsigned long long>(submissionBytes));
    }

    std::shared_ptr<core::CpuMemoryQuota> quota_ = core::makeShared<core::CpuMemoryQuota>(65536u);
    const bool freezeProgram_;
    mutable std::shared_ptr<const rhi_webgpu::WebGpuBindlessRootNativeClosure> closure_;
};

using WebGpuOriginalBindlessPreparationRequest = WebGpuOriginalBindlessPreparationRequestImpl<>;
using WebGpuOriginalBindlessGraphicsPreparationRequest =
    WebGpuOriginalBindlessPreparationRequestImpl<true>;
using WebGpuOriginalBindlessGraphicsPreparationFixture = WebGpuOriginalBindlessPreparationFixture;
} // namespace arp::test
