#pragma once
#include "original_typed_cold_admission_fixture.h"
#include "original_typed_issuer_preparation_fixture.h"
#include "original_typed_native_cold_contract_fixture.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteNativeRecordingFixture final {
    using ColdContract = rhi::OriginalTypedNativeColdContract;
    using CommandContract = rhi::OriginalTypedNativeColdCommandContract;
    using CallbackContract = rhi::OriginalTypedCallbackCustodyContract;
    using IssuerContract = rhi::OriginalTypedIssuerPreparationContract;
    using Execution = rhi::OriginalTypedNativeExecutionContract;
    using Cold = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
    using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
    using Callback = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Issuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Root = rhi::OriginalCommandStateTransaction;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "General native7: %s\n", id);
        assert(!id);
    }

public:
    enum class RecordingTerminal { None, Cancel, Drop, NativeError, ReverseScopes };
    enum class CompletionStage { Prepared, RecordingPending, SubmissionPending, Committed };

    static void verifyContract()
    {
        using Completion = rhi::OriginalGeneralSubmissionCompletionContract;
        for (const auto version : {0u, 1u, 2u, 3u, 4u, 5u, UINT32_MAX})
            for (const bool whole : {false, true})
                for (const bool finite : {false, true})
                    for (std::uint32_t content = 0u; content <= 8u; ++content) {
                        const bool accepted = (!version && !whole && !finite && content <= 3u)
                            || (version == 1u && whole && !finite && content == 4u)
                            || (version == 2u && finite && !whole && content == 5u)
                            || (version == 3u && finite && !whole && content == 6u)
                            || (version == 4u && !finite && !whole && content == 7u);
                        assert(!Completion::validateProfile(version, whole, finite, content)
                            == accepted);
                    }
        auto cold = OriginalTypedNativeColdContractFixture::request(
            OriginalTypedNativeColdContractFixture::required());
        cold.contractVersion = 7u;
        assert(!ColdContract::validateIntent(cold)
            && !ColdContract::validatePayloadProfile(cold, 7u)
            && !ColdContract::validateLifecycleProfile(cold, 4u));
        for (const auto old : {0u, 1u, 2u, 3u, 5u})
            assert(ColdContract::validateLifecycleProfile(cold, old));
        for (const auto old : {1u, 2u, 3u, 4u, 5u, 6u})
            assert(ColdContract::validatePayloadProfile(cold, old));
        CommandContract::Request command{7u, true, 1u, 256u, 65536u};
        IssuerContract::Request issuer{7u, true, 1u, 1u, 7u, 256u, 65536u, 65536u, 65536u};
        assert(!CommandContract::validateProfile(command, 7u)
            && !IssuerContract::validateProfile(issuer, 7u));
        for (const auto old : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u}) {
            assert(CommandContract::validateProfile(command, old)
                && IssuerContract::validateProfile(issuer, old));
        }
        const Execution::Request recording{
            7u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
        assert(!Execution::validateIntent(recording) && !Execution::validateProfile(recording, 7u));
        auto submission = recording;
        submission.generalSubmission = {2u, true};
        assert(!Execution::validateIntent(submission)
            && !Execution::validateRecordedGeneralSubmission(submission, 2u));
        assert(Execution::validateRecordedGeneralSubmission(submission, 1u));
        submission.generalSubmission = {1u, true};
        assert(Execution::validateIntent(submission));
        submission = recording;
        submission.readbackCompletion = {1u, true};
        assert(Execution::validateIntent(submission));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            assert(rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(backend, 7u)
                       .admissionAvailable
                == (backend == rhi::Backend::WebGPU));
            assert(rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(backend, 7u)
                       .connectionAvailable
                == (backend == rhi::Backend::WebGPU));
            assert(rhi::semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 7u)
                       .observerPreparationAvailable
                == (backend == rhi::Backend::WebGPU));
            assert(rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 7u)
                       .preparationAvailable
                == (backend == rhi::Backend::WebGPU));
            const auto decision =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 7u);
            assert(decision.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && !decision.submissionAvailable);
            assert(
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 7u, 0u, 2u)
                    .submissionAvailable
                == (backend == rhi::Backend::WebGPU));
            const auto graphics =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 8u);
            assert(graphics.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && !graphics.submissionAvailable);
            const auto graphicsSubmission =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 8u, 0u, 3u);
            assert(graphicsSubmission.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && graphicsSubmission.submissionAvailable == (backend == rhi::Backend::WebGPU));
            assert(
                !rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, UINT32_MAX)
                    .recordingAvailable);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_NATIVE7_CONTRACT_OK Cold7 Command7 Callback7 Issuer7 Recording7 Lifecycle4 ContentPlan5 Pending3 TexturePending2 exact-opt-in legacy-isolated four-backends GeneralSubmission2-explicit-private");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static RecordingTerminal selectedRecordingTerminal()
    {
        const auto choice = EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                if (!p.has("general-finite-lifecycle"))
                    return 0;
                const value = p.get("general-recording-terminal");
                if (value === null)
                    return 0;
                return value === "cancel"           ? 1
                       : value === "drop"           ? 2
                       : value === "native-error"   ? 3
                       : value === "reverse-scopes" ? 4
                                                    : 5;
            // clang-format on
        });
        assert(choice >= 0 && choice <= 4);
        return static_cast<RecordingTerminal>(choice);
    }

    template <class Rebuild,
        class Drop,
        class Completion = std::nullptr_t,
        class TerminalDriver = std::nullptr_t,
        class PreparedDriver = std::nullptr_t,
        class ClaimDriver = std::nullptr_t>
    static auto verifyActual(
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedNativePayload>& payload,
        std::shared_ptr<Root>& root,
        const rhi::GpuDevice& device,
        const std::shared_ptr<const rhi::OriginalCommandSamplerLease>& sampler,
        const std::weak_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>&
            retainedInitializer,
        const rhi::GpuTexture& sampled,
        const rhi::GpuTexture::ContentExpectation& completed,
        Rebuild&& rebuildPayload,
        Drop&& dropCaller,
        Completion completion = nullptr,
        TerminalDriver terminalDriver = nullptr,
        PreparedDriver preparedDriver = nullptr,
        ClaimDriver claimDriver = nullptr)
    {
        const auto recording = payload->originalMetadataQuota();
        const auto sources = root->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources();
        const auto& authored = sources->originalBudget();
        using Domain = rhi::OriginalCommandAdmissionBudget::CpuDomain;
        auto submission = core::makeShared<core::CpuMemoryQuota>(
            authored.maxCpuBytes[static_cast<std::size_t>(Domain::Submission)]);
        auto callback = core::makeShared<core::CpuMemoryQuota>(
            authored.maxCpuBytes[static_cast<std::size_t>(Domain::Callback)]);
        auto request = OriginalTypedColdAdmissionFixture::requestFor(payload, 7u);
        request.maxRetainedOwners =
            payload->originalRequest().maxRetainedOwners + Cold::additionalOwners;
        const auto inspected = Cold::inspectOriginalPayload(payload, request);
        require(inspected.diagnostic);
        const auto capacity = inspected.required.capacity;
        const auto ceiling = request.maxRetainedOwners;
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{4u,
            true,
            capacity.pendingSubmissions,
            capacity.callbackTickets,
            capacity.callbackRegistrations,
            ceiling,
            ceiling,
            ceiling,
            recording->limitBytes(),
            submission->limitBytes(),
            callback->limitBytes()};
        std::shared_ptr<const Seal> seal;
        std::shared_ptr<const Reservation> reservation;
        std::shared_ptr<const Cold> cold;
        std::shared_ptr<const Command> command;
        std::shared_ptr<const Callback> custody;
        std::shared_ptr<const Issuer> issuer;
        const char* id = nullptr;
        const auto prepareCold = [&] {
            seal = Seal::prepareOriginal(root, device, lifecycle, submission, callback, id);
            require(id);
            assert(seal);
            reservation = Reservation::prepareOriginal(seal, capacity, id);
            require(id);
            assert(reservation);
            cold = Cold::prepareOriginal(payload, reservation, request, id);
            require(id);
            assert(cold);
        };
        const auto rebuildCold = [&] {
            cold.reset();
            reservation.reset();
            seal.reset();
            rebuildPayload();
            assert(!submission->liveBytes() && !callback->liveBytes()
                && !sources->originalReservedSubmissionCount());
            prepareCold();
        };
        prepareCold();
        const Command::Request commandRequest{7u, true, 1u, ceiling, recording->limitBytes()};
        const Callback::Request callbackRequest{7u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            capacity.callbackTickets,
            1u,
            ceiling,
            submission->limitBytes(),
            callback->limitBytes()};
        for (const auto old : {1u, 2u, 3u, 4u, 5u, 6u, 8u}) {
            auto bad = commandRequest;
            bad.contractVersion = old;
            assert(!Command::prepareOriginal(cold, bad, id) && id);
        }
        const auto makeCommand = [&] {
            return Command::prepareOriginal(cold, commandRequest, id);
        };
        const auto rebindCommand = [&] {
            command.reset();
            rebuildCold();
        };
        CpuQuotaFactoryFixture recordingPressure(recording);
        const auto commandBytes = recordingPressure.measureSingleUse(rebindCommand, makeCommand);
        recordingPressure.verifySingleUse(commandBytes, rebindCommand, makeCommand);
        rebindCommand();
        command = makeCommand();
        require(id);
        assert(command);
        assert(!makeCommand() && id);
        const auto makeCallback = [&] {
            return Callback::prepareOriginal(command, callbackRequest, id);
        };
        const auto rebindCallback = [&] {
            custody.reset();
            rebindCommand();
            command = makeCommand();
            require(id);
            assert(command);
        };
        for (const auto old : {1u, 2u, 3u, 4u, 5u, 6u, 8u}) {
            auto bad = callbackRequest;
            bad.contractVersion = old;
            assert(!Callback::prepareOriginal(command, bad, id) && id);
        }
        CpuQuotaFactoryFixture submissionPressure(submission), callbackPressure(callback);
        const auto submissionBytes =
            submissionPressure.measureSingleUse(rebindCallback, makeCallback);
        submissionPressure.verifySingleUse(submissionBytes, rebindCallback, makeCallback);
        const auto callbackBytes = callbackPressure.measureSingleUse(rebindCallback, makeCallback);
        callbackPressure.verifySingleUse(callbackBytes, rebindCallback, makeCallback);
        rebindCallback();
        custody = makeCallback();
        require(id);
        assert(custody);
        const auto coldOwners = cold->retainedOwnerCount(),
                   commandOwners = command->retainedOwnerCount();
        const auto issuerOwners = commandOwners
            + CallbackContract::additionalOwners(callbackRequest)
            + IssuerContract::additionalOwners;
        // Existing helper covers native receipt partial failure, identity mutation,
        // exact/one-less/exhausted issuer quotas and one-shot claims.
        OriginalTypedIssuerPreparationFixture::verifyGeneral(command,
            custody,
            issuer,
            callbackRequest,
            recording,
            submission,
            callback,
            rebindCallback);
        require(issuer->validate());
        const auto liveRoot = root;
        auto admission = cold;
        auto retainedCustody = custody;
        const auto contentGeneration = completed.contentGeneration(),
                   contentRevision = completed.revision();
        const auto native = rhi_webgpu::webGpuNativePayloadFor(*sampler->originalObject());
        assert(native);
        const auto savedSampler = native->sampler;
        const auto savedDevice = native->ownerDevice;
        Execution::Request execution{
            7u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
        const auto terminal = selectedRecordingTerminal();
        const bool finiteMap = EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                return p.has("general-finite-lifecycle") &&
                       (p.has("general-map-terminal") ||
                        p.get("general-post-suppression-failure") === "publication");
            // clang-format on
        });
        assert(terminal == RecordingTerminal::None || (std::is_same_v<Completion, std::nullptr_t>));
        if (terminal != RecordingTerminal::None)
            execution.generalSubmission = {2u, true};
        if constexpr (!std::is_same_v<Completion, std::nullptr_t>) {
            execution.generalSubmission = {2u, true};
            completion(issuer, liveRoot, CompletionStage::Prepared);
        }
        auto badExecution = execution;
        badExecution.contractVersion = 6u;
        assert(Issuer::recordOriginal(issuer, badExecution));
        badExecution = execution;
        badExecution.prototypeOptIn = false;
        assert(Issuer::recordOriginal(issuer, badExecution));
        auto forged = std::shared_ptr<const Issuer>(callback, issuer.get());
        assert(Issuer::recordOriginal(forged, execution));
        forged.reset();
        if constexpr (!std::is_same_v<ClaimDriver, std::nullptr_t>) {
            static_assert(!std::is_same_v<Completion, std::nullptr_t>
                && std::is_same_v<TerminalDriver, std::nullptr_t>
                && std::is_same_v<PreparedDriver, std::nullptr_t>);
            assert(terminal == RecordingTerminal::None
                && execution.generalSubmission.contractVersion == 2u);
            dropCaller();
            command.reset();
            cold.reset();
            reservation.reset();
            seal.reset();
            custody.reset();
            admission.reset();
            assert(!retainedInitializer.expired());
            const auto expected = claimDriver(issuer,
                retainedCustody,
                liveRoot,
                execution,
                recording,
                submission,
                callback,
                savedDevice,
                lifecycle);
            assert((expected == Root::Status::Abandoned || expected == Root::Status::Invalidated)
                && liveRoot->status() == expected && liveRoot->journalEmpty());
            issuer.reset();
            retainedCustody.reset();
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            assert(retainedInitializer.expired() && !submission->liveBytes()
                && !callback->liveBytes() && !sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_RETIRE_OK initializer-expired original-prepared-native-receipts7-suppressed reservations-quotas-zero no-recording-no-completion public-closed");
            return liveRoot;
        }
        if constexpr (!std::is_same_v<PreparedDriver, std::nullptr_t>) {
            static_assert(!std::is_same_v<Completion, std::nullptr_t>
                && std::is_same_v<TerminalDriver, std::nullptr_t>);
            assert(terminal == RecordingTerminal::None
                && execution.generalSubmission.contractVersion == 2u);
            dropCaller();
            command.reset();
            cold.reset();
            reservation.reset();
            seal.reset();
            custody.reset();
            admission.reset();
            assert(!retainedInitializer.expired());
            preparedDriver(issuer,
                retainedCustody,
                liveRoot,
                execution,
                recording,
                submission,
                callback,
                savedDevice);
            assert(liveRoot->status() == Root::Status::Invalidated && liveRoot->journalEmpty()
                && !retainedCustody->pendingOriginalRecords()
                && retainedCustody->physicalLossDelivered() && !retainedCustody->physicalLossArmed()
                && !issuer->hasOriginalCompletedSubmission());
            issuer.reset();
            retainedCustody.reset();
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            assert(retainedInitializer.expired() && !submission->liveBytes()
                && !callback->liveBytes() && !sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_LOSS4_RETIRE_OK initializer-expired original-receipts-reservations-quotas-zero actual-loss-drained public-closed");
            return liveRoot;
        }
        using Scopes = WebGpuErrorScopeHandoffFixture<1024u>;
        using Probe = WebGpuNativeSubmissionProbeFixture;

        struct QueueObservation {
            std::uint32_t invocations = 0u;

            static void observe(void* context) noexcept
            {
                ++static_cast<QueueObservation*>(context)->invocations;
            }
        } queueObservation;

        WGPUQueue observedQueue = nullptr;
        if (terminal != RecordingTerminal::None) {
            observedQueue = wgpuDeviceGetQueue(savedDevice);
            assert(observedQueue);
            Probe::instrument(savedDevice,
                observedQueue,
                Probe::Probe::Success,
                &queueObservation,
                &QueueObservation::observe);
        }
        const bool recordingProbe = terminal == RecordingTerminal::NativeError
            || terminal == RecordingTerminal::ReverseScopes;
        if (recordingProbe)
            Probe::instrumentRecording(savedDevice, terminal == RecordingTerminal::ReverseScopes);
        Scopes::holdScopes(terminal != RecordingTerminal::NativeError);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept {
                    return Issuer::recordOriginal(issuer, execution);
                }));
        });
        Scopes::restoreScopes();
        if (recordingProbe)
            Probe::restoreRecording();
        Scopes::waitHeldScopes();
        assert(liveRoot->status() == Root::Status::Pending && !liveRoot->journalEmpty()
            && !issuer->hasOriginalRecordedCommand() && !issuer->hasOriginalCompletedSubmission());
        if constexpr (!std::is_same_v<Completion, std::nullptr_t>)
            completion(issuer, liveRoot, CompletionStage::RecordingPending);
        assert(!sampled.observeOriginalCommittedState(
            rhi::defaultTextureSubresourceRange(sampled.desc())));
        dropCaller();
        command.reset();
        cold.reset();
        reservation.reset();
        seal.reset();
        custody.reset();
        auto initialized = retainedInitializer.lock();
        assert(initialized && initialized->readyDiagnostic());
        assert(issuer->validate()); // public Prepared validator must remain strict
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    using Preparation = rhi_webgpu::WebGpuNativeCommandPreparation;
                    require(Preparation::validateTypedAdmissionRetained(admission));
                    auto alias = std::shared_ptr<const Cold>(callback, admission.get());
                    assert(Preparation::validateTypedAdmissionRetained(alias));
                    native->sampler = nullptr;
                    assert(Preparation::validateTypedAdmissionRetained(admission));
                    native->sampler = savedSampler;
                    native->ownerDevice = nullptr;
                    assert(Preparation::validateTypedAdmissionRetained(admission));
                    native->ownerDevice = savedDevice;
                    assert(!completed.diagnostic()
                        && completed.contentGeneration() == contentGeneration
                        && completed.revision() == contentRevision);
                    assert(Issuer::recordOriginal(issuer, execution)
                        && Issuer::submitOriginal(issuer, execution));
                    return Preparation::validateTypedAdmissionRetained(admission);
                }));
        });
        initialized.reset();
        if (terminal != RecordingTerminal::None) {
            const auto* kind = terminal == RecordingTerminal::Cancel ? "cancel"
                : terminal == RecordingTerminal::Drop                ? "drop"
                : terminal == RecordingTerminal::NativeError         ? "native-error"
                                                                     : "reverse-scopes";
            std::weak_ptr<const Issuer> issuerObserver = issuer;
            std::weak_ptr<const Callback> custodyObserver = retainedCustody;
            assert(retainedCustody->pendingOriginalRecords() == 1u
                && retainedCustody->physicalLossArmed()
                && !retainedCustody->physicalLossDelivered());
            if (terminal == RecordingTerminal::Cancel) {
                const auto* failure = retainedCustody->cancelOriginal();
                assert(failure
                    && core::StringView(failure)
                        == "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING");
            } else if (terminal == RecordingTerminal::Drop) {
                issuer.reset();
                retainedCustody.reset();
                admission.reset();
                assert(issuerObserver.expired() && custodyObserver.expired());
            }
            assert(liveRoot->status() == Root::Status::Pending && !liveRoot->journalEmpty()
                && !retainedInitializer.expired());
            std::printf(
                "BINDLESS_RHI_GENERAL_FINITE_RECORDING4_HELD_OK %s root-Pending journal-retained initializer-retained native-scopes3 no-submit no-output %s\n",
                kind,
                terminal == RecordingTerminal::Drop ? "issuer-custody-expired" : "custody-live");
            if (terminal == RecordingTerminal::ReverseScopes) {
                for (std::uint32_t i = 0u; i < 2u; ++i) {
                    Scopes::releaseScope(i);
                    emscripten_sleep(0);
                    assert(liveRoot->status() == Root::Status::Pending && !liveRoot->journalEmpty()
                        && !issuer->hasOriginalRecordedCommand()
                        && retainedCustody->pendingOriginalRecords() == 1u);
                }
                Scopes::releaseScope(2u);
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return issuer->hasOriginalRecordedCommand();
                }));
                require(issuer->recordingDiagnostic());
                assert(!issuer->hasOriginalCompletedSubmission());
                require(retainedCustody->cancelOriginal());
            } else
                Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return liveRoot->status() != Root::Status::Pending;
            }));
            assert(liveRoot->status() == Root::Status::Abandoned && liveRoot->journalEmpty()
                && !completed.diagnostic() && completed.contentGeneration() == contentGeneration
                && completed.revision() == contentRevision);
            if (issuer) {
                assert(!issuer->hasOriginalCompletedSubmission());
                if (terminal == RecordingTerminal::NativeError) {
                    const auto* failure = issuer->recordingDiagnostic();
                    assert(failure
                        && core::StringView(failure) == "RHI-WEBGPU-TYPED-RECORDING-NATIVE-ERROR"
                        && !issuer->hasOriginalRecordedCommand());
                }
                assert(Issuer::recordOriginal(issuer, execution)
                    && Issuer::submitOriginal(issuer, execution));
                assert(!retainedCustody->pendingOriginalRecords()
                    && !retainedCustody->physicalLossArmed()
                    && !retainedCustody->physicalLossDelivered());
            }
            issuer.reset();
            retainedCustody.reset();
            admission.reset();
            assert(issuerObserver.expired() && custodyObserver.expired());
            issuerObserver.reset();
            custodyObserver.reset();
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            assert(retainedInitializer.expired() && !submission->liveBytes()
                && !callback->liveBytes() && !sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            assert(!queueObservation.invocations);
            Probe::restore();
            wgpuQueueRelease(observedQueue);
            std::printf(
                "BINDLESS_RHI_GENERAL_FINITE_RECORDING4_TERMINAL_OK %s root-Abandoned journal-empty scopes-drained loss-suppressed initializer-expired original-reservations-quotas-zero unpublished no-retry public-closed\n",
                kind);
            return liveRoot;
        }
        // Genuine native scope promises arrive independently. No record proof
        // may appear before all three receipts have drained.
        for (std::uint32_t i = 0u; i < 2u; ++i) {
            Scopes::releaseScope(i);
            emscripten_sleep(0);
            assert(!issuer->hasOriginalRecordedCommand()
                && liveRoot->status() == Root::Status::Pending);
        }
        Scopes::releaseScope(2u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return issuer->hasOriginalRecordedCommand();
        }));
        require(issuer->recordingDiagnostic());
        if constexpr (!std::is_same_v<TerminalDriver, std::nullptr_t>) {
            static_assert(!std::is_same_v<Completion, std::nullptr_t>);
            assert(terminal == RecordingTerminal::None
                && execution.generalSubmission.contractVersion == 2u);
            terminalDriver(issuer,
                retainedCustody,
                liveRoot,
                execution,
                recording,
                submission,
                callback,
                savedDevice);
            assert(liveRoot->journalEmpty() && !retainedCustody->pendingOriginalRecords()
                && !retainedCustody->physicalLossArmed()
                && !retainedCustody->physicalLossDelivered());
            issuer.reset();
            retainedCustody.reset();
            admission.reset();
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            assert(retainedInitializer.expired() && !submission->liveBytes()
                && !callback->liveBytes() && !sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            std::puts(
                "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETIRE_OK initializer-expired original-recording-submission-callback-receipts-reservations-quotas-zero public-closed");
            return liveRoot;
        }
        if constexpr (!std::is_same_v<Completion, std::nullptr_t>) {
            auto missing = execution;
            missing.generalSubmission = {};
            assert(Issuer::submitOriginal(issuer, missing));
            auto mixed = execution;
            mixed.generalSubmission = {1u, true};
            assert(Issuer::submitOriginal(issuer, mixed));
            Scopes::holdScopes(true);
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{recording, submission, callback}, [&]() noexcept {
                        return Issuer::submitOriginal(issuer, execution);
                    }));
            });
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            assert(liveRoot->status() == Root::Status::Pending && !liveRoot->journalEmpty()
                && !issuer->hasOriginalCompletedSubmission()
                && retainedCustody->pendingOriginalRecords());
            completion(issuer, liveRoot, CompletionStage::SubmissionPending);
            assert(Issuer::submitOriginal(issuer, execution));
            for (std::uint32_t i = 0u; i < 2u; ++i) {
                Scopes::releaseScope(i);
                emscripten_sleep(0);
                assert(liveRoot->status() == Root::Status::Pending
                    && !issuer->hasOriginalCompletedSubmission());
                completion(issuer, liveRoot, CompletionStage::SubmissionPending);
            }
            Scopes::releaseScope(2u);
            const bool drained = WebGpuAsyncWaitFixture::until([&] {
                return !retainedCustody->pendingOriginalRecords();
            });
            if (!drained)
                std::fprintf(stderr,
                    "General finite submission drain: status%u pending%u recorded%d completed%d lossArmed%d diagnostic=%s\n",
                    static_cast<unsigned>(liveRoot->status()),
                    retainedCustody->pendingOriginalRecords(),
                    issuer->hasOriginalRecordedCommand(),
                    issuer->hasOriginalCompletedSubmission(),
                    retainedCustody->physicalLossArmed(),
                    issuer->submissionDiagnostic() ? issuer->submissionDiagnostic() : "none");
            assert(drained);
            require(issuer->submissionDiagnostic());
            assert(issuer->hasOriginalCompletedSubmission()
                && liveRoot->status() == Root::Status::Committed && liveRoot->journalEmpty()
                && !retainedCustody->physicalLossArmed() && !completed.diagnostic());
            completion(issuer, liveRoot, CompletionStage::Committed);
            issuer.reset();
            retainedCustody.reset();
            admission.reset();
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            assert(!submission->liveBytes() && !callback->liveBytes()
                && !sources->originalReservedSubmissionCount()
                && !sources->originalReservedCallbackTicketCount()
                && !sources->originalReservedCallbackRegistrationCount());
            if (finiteMap)
                std::puts(
                    "BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_RETIRE_OK original-Submission2 completed-committed recording-scopes3 submission-scopes3 queue1 original-receipts-reservations-quotas-zero public-closed");
            else
                std::printf(
                    "BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK Cold7 Command7 Callback7 Issuer7 Lifecycle4 ContentPlan5 Submission2 Output2 commands10 recordingScopes3 submissionScopes3 queue1 coldOwners%u commandOwners%u issuerOwners%u ceiling%u commandBytes%llu submissionBytes%llu callbackBytes%llu exact-less-exhausted original-seal-journal full-backing caller-drop forged-native-mutation strict-public hot0-query0 real-completed committed numerical-readback quotas-zero public-closed\n",
                    coldOwners,
                    commandOwners,
                    issuerOwners,
                    ceiling,
                    static_cast<unsigned long long>(commandBytes),
                    static_cast<unsigned long long>(submissionBytes),
                    static_cast<unsigned long long>(callbackBytes));
            return liveRoot;
        }
        auto submit = execution;
        submit.generalSubmission = {2u, true};
        assert(Issuer::submitOriginal(issuer, submit));
        require(retainedCustody->cancelOriginal());
        assert(liveRoot->status() == Root::Status::Abandoned && liveRoot->journalEmpty()
            && !retainedCustody->pendingOriginalRecords() && !retainedCustody->physicalLossArmed()
            && !issuer->hasOriginalCompletedSubmission() && !completed.diagnostic());
        issuer.reset();
        retainedCustody.reset();
        admission.reset();
        OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
        std::printf(
            "BINDLESS_RHI_GENERAL_NATIVE_RECORDING7_OK Cold7 Command7 Callback7 Issuer7 Lifecycle4 ContentPlan5 Pending3 TexturePending2 commands10 scopes3 coldOwners%u commandOwners%u issuerOwners%u ceiling%u commandBytes%llu submissionBytes%llu callbackBytes%llu exact-less-exhausted original-seal-journal full-backing caller-drop forged-native-mutation strict-public hot0-query0 real-recorded cancel-rollback submission-closed\n",
            coldOwners,
            commandOwners,
            issuerOwners,
            ceiling,
            static_cast<unsigned long long>(commandBytes),
            static_cast<unsigned long long>(submissionBytes),
            static_cast<unsigned long long>(callbackBytes));
        return liveRoot;
    }
#endif
};
} // namespace arp::test
