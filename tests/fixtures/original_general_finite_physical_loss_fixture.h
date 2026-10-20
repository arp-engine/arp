#pragma once
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
// Reuse the original native recording/submission probes and captured device
// loss receiver. Logical device retirement never supplies physical-loss proof.
class OriginalGeneralFinitePhysicalLossFixture final {
    using Issuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Root = rhi::OriginalCommandStateTransaction;
    using Execution = rhi::OriginalTypedNativeExecutionContract;

    static void require(const char* diagnostic)
    {
        if (diagnostic)
            std::fprintf(stderr, "General finite loss4: %s\n", diagnostic);
        assert(!diagnostic);
    }

public:
    enum class Terminal { QueueUncertain, RecordingShutdown, PartialPush, RecordingException };

    static const char* terminalName(Terminal terminal) noexcept
    {
        static constexpr std::array names{
            "queue-uncertain", "recording-shutdown", "partial-push", "recording-exception"};
        return names[static_cast<std::size_t>(terminal)];
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static std::optional<Terminal> selectedTerminal()
    {
        const auto choice = EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                const graphics = p.has("general-graphics-physical-loss");
                if (!graphics && !p.has("general-finite-lifecycle"))
                    return -1;
                const value = p.get(graphics ? "general-graphics-physical-loss" : "general-physical-loss");
                if (value === null)
                    return -1;
                const names =
                    ([ "queue-uncertain", "recording-shutdown", "partial-push", "recording-exception" ]);
                const index = names.indexOf(value);
                return index < 0 ? names.length : index;
            // clang-format on
        });
        assert(choice >= -1 && choice < 4);
        return choice < 0 ? std::nullopt : std::optional<Terminal>(static_cast<Terminal>(choice));
    }

    static void verifyActual(Terminal terminal,
        core::UniquePtr<rhi::GpuDevice>& device,
        WGPUDevice nativeDevice,
        const std::shared_ptr<const Issuer>& issuer,
        const std::shared_ptr<const Custody>& custody,
        const std::shared_ptr<Root>& root,
        const Execution::Request& execution,
        const std::shared_ptr<core::CpuMemoryQuota>& recording,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback)
    {
        const bool graphics =
            execution.contractVersion == 8u && execution.generalSubmission.contractVersion == 3u;
        assert(device && nativeDevice && issuer && custody && root
            && (graphics
                || (execution.contractVersion == 7u
                    && execution.generalSubmission.contractVersion == 2u))
            && root->status() == Root::Status::Prepared && root->journalEmpty()
            && !issuer->hasOriginalRecordedCommand() && !issuer->hasOriginalCompletedSubmission());
        using Held = WebGpuErrorScopeHandoffFixture<1026u>;
        using Probe = WebGpuNativeSubmissionProbeFixture;
        const auto record = [&](const char* expectedDiagnostic = nullptr) {
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                        const auto* diagnostic = Issuer::recordOriginal(issuer, execution);
                        if (expectedDiagnostic) {
                            assert(
                                diagnostic && core::StringView(diagnostic) == expectedDiagnostic);
                            return nullptr;
                        }
                        return diagnostic;
                    }));
            });
        };
        if (terminal == Terminal::QueueUncertain) {
            record();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuer->hasOriginalRecordedCommand();
            }));
            auto queue = wgpuDeviceGetQueue(nativeDevice);
            assert(queue);
            Probe::instrumentLoss(nativeDevice, queue, true);
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                        const auto* diagnostic = Issuer::submitOriginal(issuer, execution);
                        assert(diagnostic
                            && core::StringView(diagnostic)
                                == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                        return nullptr;
                    }));
            });
            Probe::restoreLoss();
            wgpuQueueRelease(queue);
        } else if (terminal == Terminal::RecordingShutdown) {
            Held::holdScopes(true);
            record();
            Held::restoreScopes();
            Held::waitHeldScopes();
            assert(root->status() == Root::Status::Pending && !root->journalEmpty());
            device.reset();
        } else if (terminal == Terminal::PartialPush) {
            // Actual partial push poisons the native scope stack. Nothing else
            // may be recorded on this device before its physical destruction.
            Probe::instrumentScopePush(nativeDevice);
            record("RHI-WEBGPU-TYPED-RECORDING-SCOPE-PUSH-UNCERTAIN");
            Probe::restoreScopePush();
        } else {
            Probe::instrumentRecordingException(nativeDevice);
            record("RHI-WEBGPU-TYPED-RECORDING-INVOCATION-UNCERTAIN");
            Probe::restoreRecordingException();
        }
        assert(custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
            && !custody->physicalLossDelivered() && !issuer->hasOriginalCompletedSubmission());
        std::printf(
            "%s %s pending-record1 original-loss-armed physical-loss-undelivered owners-retained no-completion no-output\n",
            graphics ? "BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_HELD_OK"
                     : "BINDLESS_RHI_GENERAL_FINITE_LOSS4_HELD_OK",
            terminalName(terminal));
        assert(EM_ASM_INT(
            {
                // clang-format off
                    const d = WebGPU.getJsObject($0);
                    if (d !== Module.preinitializedWebGPUDevice)
                        return 0;
                    d.destroy();
                    return 1;
                // clang-format on
            },
            nativeDevice));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return custody->physicalLossDelivered();
        }));
        if (terminal == Terminal::RecordingShutdown) {
            assert(custody->pendingOriginalRecords() == 1u && !issuer->hasOriginalRecordedCommand()
                && root->status() == Root::Status::Invalidated && root->journalEmpty());
            // Genuine loss invalidates the journal, but cannot release the
            // callback state while the original scopes still borrow userdata.
            Held::releaseScopes();
        }
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !custody->pendingOriginalRecords();
        }));
        assert(root->status() == Root::Status::Invalidated && root->journalEmpty()
            && custody->physicalLossDelivered() && !custody->physicalLossArmed()
            && !issuer->hasOriginalCompletedSubmission());
        assert(
            Issuer::recordOriginal(issuer, execution) && Issuer::submitOriginal(issuer, execution));
        std::printf(
            "%s %s actual-device-destroy original-loss-delivered genuine-local-drain root-Invalidated journal-empty no-completion unpublished no-retry public-closed\n",
            graphics ? "BINDLESS_RHI_GENERAL_GRAPHICS_LOSS4_TERMINAL_OK"
                     : "BINDLESS_RHI_GENERAL_FINITE_LOSS4_TERMINAL_OK",
            terminalName(terminal));
    }
#endif
};
} // namespace arp::test
