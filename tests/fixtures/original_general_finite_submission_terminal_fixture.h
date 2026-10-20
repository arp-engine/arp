#pragma once
#include "original_descriptor_buffer_binding_fixture.h"
#include "original_general_finite_post_suppression_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
// Test driver for the same prepared finite issuer/root. All native submission,
// completion and lifetime transitions are consumed by the production issuer.
// This helper prepares no second source, root, command or output inventory.
class OriginalGeneralFiniteSubmissionTerminalFixture final {
    using Issuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Root = rhi::OriginalCommandStateTransaction;
    using Execution = rhi::OriginalTypedNativeExecutionContract;

    static void require(const char* diagnostic)
    {
        if (diagnostic)
            std::fprintf(stderr, "General finite submission4: %s\n", diagnostic);
        assert(!diagnostic);
    }

public:
    enum class Terminal {
        None,
        Cancel,
        Retired,
        Shutdown,
        ThrowBefore,
        ThrowAfter,
        NativeError,
        ReverseScopes,
        PartialScope,
        PostSuppressionCommit
    };

    static const char* terminalName(Terminal terminal) noexcept
    {
        static constexpr std::array names{"none",
            "cancel",
            "retired",
            "shutdown",
            "throw-before",
            "throw-after",
            "native-error",
            "reverse-scopes",
            "partial-scope",
            "post-suppression-commit"};
        return names[static_cast<std::size_t>(terminal)];
    }

    static bool invalidates(Terminal terminal) noexcept
    {
        return terminal != Terminal::None && terminal != Terminal::ReverseScopes;
    }

    static bool lifetimeTerminates(Terminal terminal) noexcept
    {
        return terminal == Terminal::Retired || terminal == Terminal::Shutdown;
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static Terminal selectedTerminal()
    {
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
        if (EM_ASM_INT({
                // clang-format off
                const p = new URLSearchParams(location.search);
                return p.has("general-finite-lifecycle") &&
                       p.get("general-post-suppression-failure") === "commit";
                // clang-format on
            }))
            return Terminal::PostSuppressionCommit;
#endif
        const auto choice = EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                const graphics = p.has("general-graphics-native-submission");
                if (!graphics && !p.has("general-finite-lifecycle"))
                    return 0;
                const value =
                    p.get(graphics ? "general-graphics-submission-terminal" : "general-submission-terminal");
                if (value === null)
                    return 0;
                const names = ([
                    "cancel", "retired", "shutdown", "throw-before", "throw-after", "native-error",
                    "reverse-scopes", "partial-scope"
                ]);
                const index = names.indexOf(value);
                return index < 0 ? names.length + 1 : index + 1;
            // clang-format on
        });
        assert(choice >= 0 && choice <= 8);
        return static_cast<Terminal>(choice);
    }

    template <class Checkpoint>
    static void verifyActual(Terminal terminal,
        core::UniquePtr<rhi::GpuDevice>& device,
        WGPUDevice savedDevice,
        const std::shared_ptr<const Issuer>& issuer,
        const std::shared_ptr<const Custody>& custody,
        const std::shared_ptr<Root>& root,
        const Execution::Request& execution,
        const std::shared_ptr<core::CpuMemoryQuota>& recording,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback,
        const rhi::GpuTexture::ContentExpectation& completed,
        Checkpoint&& checkpoint)
    {
        const bool graphics =
            execution.contractVersion == 8u && execution.generalSubmission.contractVersion == 3u;
        assert(terminal != Terminal::None && device && issuer && custody && root
            && (graphics
                || (execution.contractVersion == 7u
                    && execution.generalSubmission.contractVersion == 2u))
            && issuer->hasOriginalRecordedCommand() && !issuer->hasOriginalCompletedSubmission()
            && root->status() == Root::Status::Pending && !root->journalEmpty());
        using Scopes = WebGpuErrorScopeHandoffFixture<1025u>;
        using Probe = WebGpuNativeSubmissionProbeFixture;
        // Caller supplies the original sampler's authenticated owner device,
        // as in the existing finite recording fixture.
        assert(savedDevice);
        auto queue = wgpuDeviceGetQueue(savedDevice);
        assert(queue);

        struct Observation {
            const Issuer* issuer;
            const Root* root;
            std::uint32_t count = 0u;

            static void observe(void* userdata) noexcept
            {
                auto& self = *static_cast<Observation*>(userdata);
                assert(!self.count && self.root->status() == Root::Status::Pending
                    && !self.root->journalEmpty()
                    && !self.issuer->hasOriginalCompletedSubmission());
                const auto* diagnostic = self.issuer->submissionDiagnostic();
                assert(diagnostic
                    && core::StringView(diagnostic) == "RHI-WEBGPU-TYPED-SUBMISSION-PENDING");
                ++self.count;
            }
        } observation{issuer.get(), root.get()};

        const bool earlyQuarantine = terminal == Terminal::ThrowBefore
            || terminal == Terminal::ThrowAfter || terminal == Terminal::PartialScope;
        const bool partialRegistration = terminal == Terminal::PartialScope;
        if (partialRegistration)
            Scopes::holdScopes(true);
        const auto probe = terminal == Terminal::ThrowBefore ? Probe::Probe::ThrowBefore
            : terminal == Terminal::ThrowAfter               ? Probe::Probe::ThrowAfter
            : terminal == Terminal::NativeError              ? Probe::Probe::NativeError
            : terminal == Terminal::ReverseScopes            ? Probe::Probe::Reverse
            : partialRegistration                            ? Probe::Probe::PartialScope
                                                             : Probe::Probe::Success;
        Probe::instrument(savedDevice, queue, probe, &observation, &Observation::observe);
        if (!partialRegistration)
            Scopes::holdScopes(terminal != Terminal::NativeError);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    const auto* diagnostic = Issuer::submitOriginal(issuer, execution);
                    if (terminal == Terminal::ThrowBefore || terminal == Terminal::ThrowAfter) {
                        assert(diagnostic
                            && core::StringView(diagnostic)
                                == "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
                        return nullptr;
                    }
                    if (partialRegistration) {
                        assert(diagnostic
                            && core::StringView(diagnostic)
                                == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
                        return nullptr;
                    }
                    return diagnostic;
                }));
        });
        // Preserve the authentic throwing then() boundary rather than hiding
        // it behind an outer holding promise.
        if (partialRegistration)
            Probe::restore();
        Scopes::restoreScopes();
        if (!partialRegistration)
            Probe::restore();
        wgpuQueueRelease(queue);
        assert(observation.count == 1u);
        Scopes::waitHeldScopes();
        assert(
            root->status() == (earlyQuarantine ? Root::Status::Invalidated : Root::Status::Pending)
            && root->journalEmpty() == earlyQuarantine && !issuer->hasOriginalCompletedSubmission()
            && custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
            && !custody->physicalLossDelivered());
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
        if (terminal == Terminal::PostSuppressionCommit)
            OriginalGeneralFinitePostSuppressionFixture::arm(
                custody, WebGpuPostSuppressionFailureProbeFixture::Failure::Commit, root);
#endif
        checkpoint(false);
        if (terminal == Terminal::Cancel) {
            const auto* diagnostic = custody->cancelOriginal();
            assert(diagnostic
                && core::StringView(diagnostic) == "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
        } else if (terminal == Terminal::Retired)
            device->retireOriginalDeviceLifetime();
        else if (terminal == Terminal::Shutdown)
            device.reset();
        assert(custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
            && !custody->physicalLossDelivered() && !issuer->hasOriginalCompletedSubmission());
        assert(
            Issuer::recordOriginal(issuer, execution) && Issuer::submitOriginal(issuer, execution));
        assert(
            root->status() == Root::Status::Pending || root->status() == Root::Status::Invalidated);
        assert(root->journalEmpty() == (root->status() == Root::Status::Invalidated));
        std::printf(
            "%s %s submit-invocation1 pending-record1 loss-armed physical-loss-undelivered scopes-retained output-unpublished %s\n",
            graphics ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_HELD_OK"
                     : "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_HELD_OK",
            terminalName(terminal),
            root->status() == Root::Status::Invalidated ? "root-Invalidated journal-empty"
                                                        : "root-Pending journal-retained");
        // Completion authority requires every genuine receipt, even when native
        // promises are delivered in reverse order.
        for (std::uint32_t index = 0u; index < 2u; ++index) {
            Scopes::releaseScope(index);
            emscripten_sleep(0u);
            assert(custody->pendingOriginalRecords() == 1u
                && !issuer->hasOriginalCompletedSubmission());
        }
        Scopes::releaseScope(2u);
        assert(WebGpuAsyncWaitFixture::until([&] {
            return !custody->pendingOriginalRecords();
        }));
        assert(!custody->physicalLossArmed() && !custody->physicalLossDelivered()
            && root->journalEmpty());
        if (invalidates(terminal)) {
            assert(root->status() == Root::Status::Invalidated
                && !issuer->hasOriginalCompletedSubmission() && issuer->submissionDiagnostic());
            const auto* diagnostic = issuer->submissionDiagnostic();
            if (terminal == Terminal::NativeError)
                assert(core::StringView(diagnostic) == "RHI-WEBGPU-TYPED-SUBMISSION-NATIVE-ERROR");
            if (terminal == Terminal::ThrowBefore || terminal == Terminal::ThrowAfter)
                assert(core::StringView(diagnostic)
                    == "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
            if (partialRegistration)
                assert(core::StringView(diagnostic)
                    == "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
            // Graphics' completed texture is a sampled input, not its writable
            // attachment. Submission failure cannot invalidate read-only content.
            if (graphics && !lifetimeTerminates(terminal))
                require(completed.diagnostic());
            else
                assert(completed.diagnostic());
        } else {
            require(issuer->submissionDiagnostic());
            assert(issuer->hasOriginalCompletedSubmission()
                && root->status() == Root::Status::Committed && !completed.diagnostic());
        }
        checkpoint(true);
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
        if (terminal == Terminal::PostSuppressionCommit)
            OriginalGeneralFinitePostSuppressionFixture::verifyTerminal(custody,
                WebGpuPostSuppressionFailureProbeFixture::Failure::Commit,
                root,
                issuer->submissionDiagnostic());
#endif
        assert(
            Issuer::recordOriginal(issuer, execution) && Issuer::submitOriginal(issuer, execution));
        std::printf(
            "%s %s scopes-queue-drained loss-suppressed physical-loss-undelivered no-retry %s public-closed\n",
            graphics ? "BINDLESS_RHI_GENERAL_GRAPHICS_SUBMISSION3_FAULT_TERMINAL_OK"
                     : "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_TERMINAL_OK",
            terminalName(terminal),
            invalidates(terminal) ? "root-Invalidated output-unpublished"
                : graphics        ? "root-Committed attachment-completed no-readback"
                                  : "root-Committed numerical-output");
    }
#endif
};
} // namespace arp::test
