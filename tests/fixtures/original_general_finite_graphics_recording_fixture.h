#pragma once
#include "original_general_finite_physical_loss_fixture.h"
#include "original_general_finite_submission_terminal_fixture.h"
#include "original_typed_issuer_preparation_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
class OriginalGeneralFiniteGraphicsRecordingFixture final {
    using Issuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Root = rhi::OriginalCommandStateTransaction;
    using Execution = rhi::OriginalTypedNativeExecutionContract;

    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Graphics recording8: %s\n", id);
        assert(!id);
    }

public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <class Drop, class Recover, class Pending, class Complete, class Failed>
    static void verifyActual(std::shared_ptr<const Issuer>& issuer,
        std::shared_ptr<const Custody>& custody,
        std::shared_ptr<Root>& root,
        WGPUDevice device,
        core::UniquePtr<rhi::GpuDevice>& ownerDevice,
        const rhi::GpuTexture::ContentExpectation& completedInput,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 3u>& quotas,
        Drop&& dropCaller,
        Recover&& recoverCaller,
        Pending&& inspectPending,
        Complete&& inspectComplete,
        Failed&& inspectFailed)
    {
        const auto terminal = EM_ASM_INT({
            // clang-format off
                const value = new URLSearchParams(location.search).get("general-graphics-recording-terminal");
                return value === null || value === "cancel" ? 1
                       : value === "drop"                   ? 2
                       : value === "native-error"           ? 3
                       : value === "reverse-scopes"         ? 4
                       : value === "retired"                ? 5
                       : value === "shutdown"               ? 6
                                                            : 0;
            // clang-format on
        });
        assert(terminal >= 1 && terminal <= 6 && issuer && custody && root && device);
        const bool logicalLifetime = terminal == 5 || terminal == 6;
        using Scopes = WebGpuErrorScopeHandoffFixture<1024u>;
        using Probe = WebGpuNativeSubmissionProbeFixture;
        const bool submission = EM_ASM_INT({
            // clang-format off
                return new URLSearchParams(location.search).has("general-graphics-native-submission");
            // clang-format on
        });
        const Execution::Request intent{8u,
            true,
            rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1,
            {},
            {submission ? 3u : 0u, submission}};
        auto wrong = intent;
        wrong.prototypeOptIn = false;
        assert(Issuer::recordOriginal(issuer, wrong));
        wrong = intent;
        wrong.contractVersion = 7u;
        assert(Issuer::recordOriginal(issuer, wrong));
        for (const auto version : {1u, 2u}) {
            wrong = intent;
            wrong.generalSubmission = {version, true};
            assert(Issuer::recordOriginal(issuer, wrong));
        }
        wrong = intent;
        wrong.readbackCompletion = {1u, true};
        assert(Issuer::recordOriginal(issuer, wrong));
        const auto* unavailable = Issuer::submitOriginal(issuer, intent);
        assert(unavailable);
        if (!submission)
            assert(core::StringView(unavailable)
                == "RHI-ORIGINAL-GRAPHICS-EXECUTION-WEBGPU-SUBMISSION-MISSING");
        assert(root->status() == Root::Status::Prepared && root->journalEmpty());
        using Loss = OriginalGeneralFinitePhysicalLossFixture;
        if (const auto physical = Loss::selectedTerminal()) {
            assert(submission && terminal == 1);
            std::weak_ptr<const Issuer> issuerObserver = issuer;
            std::weak_ptr<const Custody> custodyObserver = custody;
            // Same original issuer/custody/queue; the shared driver receives no
            // alternate resource inventory or completion authority.
            dropCaller();
            recoverCaller();
            Loss::verifyActual(*physical,
                ownerDevice,
                device,
                issuer,
                custody,
                root,
                intent,
                quotas[0],
                quotas[1],
                quotas[2]);
            assert(completedInput.diagnostic());
            inspectFailed();
            issuer.reset();
            custody.reset();
            assert(issuerObserver.expired() && custodyObserver.expired());
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_RECEIPTS_OK original-native-receipts7-retired no-output no-retry public-closed");
            return;
        }

        struct QueueObservation {
            std::uint32_t invocations = 0u;

            static void observe(void* context) noexcept
            {
                ++static_cast<QueueObservation*>(context)->invocations;
            }
        } observation;

        const auto queue = wgpuDeviceGetQueue(device);
        assert(queue);
        Probe::instrument(
            device, queue, Probe::Probe::Success, &observation, &QueueObservation::observe);
        if (!submission && (terminal == 3 || terminal == 4))
            Probe::instrumentRecording(device, terminal == 4);
        Scopes::holdScopes(submission || terminal != 3);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                return Issuer::recordOriginal(issuer, intent);
            }));
        });
        Scopes::restoreScopes();
        if (!submission && (terminal == 3 || terminal == 4))
            Probe::restoreRecording();
        Scopes::waitHeldScopes();
        assert(root->status() == Root::Status::Pending && !root->journalEmpty()
            && !issuer->hasOriginalRecordedCommand() && !issuer->hasOriginalCompletedSubmission()
            && custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed());
        inspectPending();
        const auto inspectedRoot = root;
        dropCaller();
        // The public Prepared validator must not authorize a Pending issuer.
        const auto* preparedFailure = issuer->validate();
        assert(preparedFailure
            && core::StringView(preparedFailure) == "RHI-WEBGPU-TYPED-ISSUER-ORIGINAL-BINDING");
        recoverCaller();
        assert(root == inspectedRoot && Issuer::recordOriginal(issuer, intent)
            && Issuer::submitOriginal(issuer, intent));
        std::weak_ptr<const Issuer> issuerObserver = issuer;
        std::weak_ptr<const Custody> custodyObserver = custody;
        if (submission) {
            assert(terminal == 1); // First genuine submission pilot is success-only.
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->hasOriginalRecordedCommand();
            }));
            require(issuer->recordingDiagnostic());
            auto changed = intent;
            changed.generalSubmission = {2u, true};
            assert(Issuer::submitOriginal(issuer, changed));
            using Terminals = OriginalGeneralFiniteSubmissionTerminalFixture;
            const auto submissionTerminal = Terminals::selectedTerminal();
            if (submissionTerminal != Terminals::Terminal::None) {
                Probe::restore();
                wgpuQueueRelease(queue);
                Terminals::verifyActual(submissionTerminal,
                    ownerDevice,
                    device,
                    issuer,
                    custody,
                    root,
                    intent,
                    quotas[0],
                    quotas[1],
                    quotas[2],
                    completedInput,
                    [](bool) {});
                if (Terminals::invalidates(submissionTerminal))
                    inspectFailed();
                else
                    inspectComplete();
                issuer.reset();
                custody.reset();
                assert(issuerObserver.expired() && custodyObserver.expired());
                OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
                std::puts(
                    "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_RECEIPTS_OK original-native-receipts7-retired no-readback public-closed");
                return;
            }
            Scopes::holdScopes(true);
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    return Issuer::submitOriginal(issuer, intent);
                }));
            });
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            assert(observation.invocations == 1u && inspectedRoot->status() == Root::Status::Pending
                && !inspectedRoot->journalEmpty() && !issuer->hasOriginalCompletedSubmission()
                && custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed());
            std::puts(
                "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_HELD_OK original-recorded-version3 queue-invocation1 scopes3 Pending caller-drop-retained unpublished");
            for (std::uint32_t index = 0u; index < 2u; ++index) {
                Scopes::releaseScope(index);
                emscripten_sleep(0);
                assert(inspectedRoot->status() == Root::Status::Pending
                    && !issuer->hasOriginalCompletedSubmission()
                    && custody->pendingOriginalRecords() == 1u);
            }
            Scopes::releaseScope(2u);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->hasOriginalCompletedSubmission();
            }));
            require(issuer->submissionDiagnostic());
            assert(inspectedRoot->status() == Root::Status::Committed
                && inspectedRoot->journalEmpty() && !custody->pendingOriginalRecords()
                && !custody->physicalLossArmed() && !custody->physicalLossDelivered());
            assert(
                Issuer::recordOriginal(issuer, intent) && Issuer::submitOriginal(issuer, intent));
            // Downstream copy/map owns its own native observation. The upstream
            // submission has already completed and its exact call was checked.
            Probe::restore();
            wgpuQueueRelease(queue);
            inspectComplete();
            issuer.reset();
            custody.reset();
            assert(issuerObserver.expired() && custodyObserver.expired());
            OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
            std::puts(EM_ASM_INT({
                // clang-format off
                    return new URLSearchParams(location.search).has("general-graphics-output-readback");
                // clang-format on
            })
                    ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_COMPLETE_OK genuine-queue-scopes-drained loss-detached Committed journal-empty receipts-retired readback3 public-closed"
                    : "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_COMPLETE_OK genuine-queue-scopes-drained loss-detached Committed journal-empty receipts-retired no-output-readback public-closed");
            return;
        }
        if (terminal == 1) {
            const auto* pending = custody->cancelOriginal();
            assert(pending
                && core::StringView(pending)
                    == "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING");
        } else if (terminal == 2) {
            // The caller's failed output candidate is a real issuer owner.
            // Release that alias too before testing wrapper-drop retirement;
            // the already-issued scopes retain native command/storage custody.
            inspectFailed();
            issuer.reset();
            custody.reset();
            assert(issuerObserver.expired() && custodyObserver.expired());
        } else if (terminal == 5)
            ownerDevice->retireOriginalDeviceLifetime();
        else if (terminal == 6)
            ownerDevice.reset();
        if (logicalLifetime) {
            assert(completedInput.diagnostic() && !issuer->hasOriginalRecordedCommand()
                && custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
                && !custody->physicalLossDelivered());
            assert(
                Issuer::recordOriginal(issuer, intent) && Issuer::submitOriginal(issuer, intent));
        }
        assert(inspectedRoot->status() == Root::Status::Pending && !inspectedRoot->journalEmpty());
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_HELD_OK terminal%u actual-scopes3 Pending caller-drop-retained no-submit no-output\n",
            terminal);
        if (terminal == 4) {
            for (std::uint32_t index = 0u; index < 2u; ++index) {
                Scopes::releaseScope(index);
                emscripten_sleep(0);
                assert(inspectedRoot->status() == Root::Status::Pending
                    && !issuer->hasOriginalRecordedCommand()
                    && custody->pendingOriginalRecords() == 1u);
            }
            Scopes::releaseScope(2u);
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->hasOriginalRecordedCommand();
            }));
            require(issuer->recordingDiagnostic());
            require(custody->cancelOriginal());
        } else if (logicalLifetime) {
            for (std::uint32_t index = 0u; index < 2u; ++index) {
                Scopes::releaseScope(index);
                emscripten_sleep(0);
                assert(inspectedRoot->status() == Root::Status::Pending
                    && !inspectedRoot->journalEmpty() && !issuer->hasOriginalRecordedCommand()
                    && custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
                    && !custody->physicalLossDelivered());
            }
            Scopes::releaseScope(2u);
        } else
            Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return inspectedRoot->status() != Root::Status::Pending;
        }));
        assert(inspectedRoot->status() == Root::Status::Abandoned && inspectedRoot->journalEmpty());
        if (issuer) {
            assert(!issuer->hasOriginalCompletedSubmission());
            if (terminal == 3) {
                const auto* error = issuer->recordingDiagnostic();
                assert(error && core::StringView(error) == "RHI-WEBGPU-TYPED-RECORDING-NATIVE-ERROR"
                    && !issuer->hasOriginalRecordedCommand());
            }
            assert(
                Issuer::recordOriginal(issuer, intent) && Issuer::submitOriginal(issuer, intent));
            assert(!custody->pendingOriginalRecords() && !custody->physicalLossArmed()
                && !custody->physicalLossDelivered());
            if (logicalLifetime) {
                const auto* diagnostic = issuer->recordingDiagnostic();
                assert(diagnostic && !issuer->hasOriginalRecordedCommand()
                    && core::StringView(diagnostic)
                        == (terminal == 5 ? "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED"
                                          : "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN"));
                inspectFailed();
            }
        }
        if (!logicalLifetime && terminal != 2)
            inspectFailed();
        issuer.reset();
        custody.reset();
        assert(issuerObserver.expired() && custodyObserver.expired());
        OriginalTypedIssuerPreparationFixture::verifyGeneralReceiptsRetired();
        assert(!observation.invocations);
        Probe::restore();
        wgpuQueueRelease(queue);
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_RECORDING8_TERMINAL_OK terminal%u Abandoned journal-empty native-scopes-drained receipts-retired no-submit no-output public-closed\n",
            terminal);
    }
#endif
};
} // namespace arp::test
