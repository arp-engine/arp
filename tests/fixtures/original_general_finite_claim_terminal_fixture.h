#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
// Pre-record terminal probes use the already claimed original Issuer7 graph.
// No callback receipt, encoder, queue completion or replacement root is authored.
class OriginalGeneralFiniteClaimTerminalFixture final {
    using Issuer = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Root = rhi::OriginalCommandStateTransaction;
    using Execution = rhi::OriginalTypedNativeExecutionContract;

    static void require(const char* diagnostic)
    {
        if (diagnostic)
            std::fprintf(stderr, "General finite claim4: %s\n", diagnostic);
        assert(!diagnostic);
    }

public:
    enum class Terminal { None, Cancel, Drop, Retired, Shutdown, PhysicalLoss };

    static const char* terminalName(Terminal terminal) noexcept
    {
        static constexpr std::array names{
            "none", "cancel", "drop", "retired", "shutdown", "physical-loss"};
        return names[static_cast<std::size_t>(terminal)];
    }

    static bool physicalLoss(Terminal terminal) noexcept
    {
        return terminal == Terminal::PhysicalLoss;
    }

    static bool logicalTerminal(Terminal terminal) noexcept
    {
        return terminal == Terminal::Retired || terminal == Terminal::Shutdown;
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    static Terminal selectedTerminal()
    {
        const auto choice = EM_ASM_INT({
            // clang-format off
                const p = new URLSearchParams(location.search);
                if (!p.has("general-finite-lifecycle"))
                    return 0;
                const value = p.get("general-claim-terminal");
                if (value === null)
                    return 0;
                const names = ([ "cancel", "drop", "retired", "shutdown", "physical-loss" ]);
                const index = names.indexOf(value);
                return index < 0 ? names.length + 1 : index + 1;
            // clang-format on
        });
        assert(choice >= 0 && choice <= 5);
        return static_cast<Terminal>(choice);
    }

    static Root::Status verifyActual(Terminal terminal,
        core::UniquePtr<rhi::GpuDevice>& device,
        WGPUDevice nativeDevice,
        std::shared_ptr<const Issuer>& issuer,
        std::shared_ptr<const Custody>& custody,
        const std::shared_ptr<Root>& root,
        const Execution::Request& execution,
        const rhi::OriginalCommandNativeLifecycleBudget& lifecycle,
        const std::shared_ptr<core::CpuMemoryQuota>& recording,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback)
    {
        assert(terminal != Terminal::None && device && nativeDevice && issuer && custody && root
            && root->status() == Root::Status::Prepared && root->journalEmpty()
            && execution.contractVersion == 7u && execution.generalSubmission.contractVersion == 2u
            && custody->pendingOriginalRecords() == 1u && custody->physicalLossArmed()
            && !custody->physicalLossDelivered() && !issuer->hasOriginalRecordedCommand()
            && !issuer->hasOriginalCompletedSubmission());
        const auto cpuClosed = [&] {
            for (const auto* diagnostic : {root->applyOriginalPrototypePlan(),
                     root->commitOriginalPendingOwnership(),
                     root->abandonOriginalPendingOwnership()}) {
                assert(diagnostic
                    && core::StringView(diagnostic) == "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
            }
            assert(root->journalEmpty());
        };
        const char* diagnostic = nullptr;
        assert(!rhi::OriginalCommandRecordingSeal::prepareOriginal(
                   root, *device, lifecycle, submission, callback, diagnostic)
            && diagnostic); // The same original root cannot be claimed again.
        cpuClosed();
        using Probe = WebGpuNativeSubmissionProbeFixture;

        struct Observation {
            std::uint32_t submits = 0u;

            static void observe(void* value) noexcept
            {
                ++static_cast<Observation*>(value)->submits;
            }
        } observation;

        auto queue = wgpuDeviceGetQueue(nativeDevice);
        assert(queue);
        Probe::instrument(
            nativeDevice, queue, Probe::Probe::Success, &observation, &Observation::observe);
        Probe::beginColdCallObservation();
        std::puts(
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_HELD_OK original-root-Prepared journal-empty sticky-claim original-loss-armed pending-record1 issuers-live no-recording no-submission no-output public-closed");
        std::weak_ptr<const Issuer> issuerObserver = issuer;
        std::weak_ptr<const Custody> custodyObserver = custody;
        if (terminal == Terminal::Drop) {
            issuer.reset();
            custody.reset();
            assert(issuerObserver.expired() && custodyObserver.expired());
        } else {
            if (terminal == Terminal::Retired)
                device->retireOriginalDeviceLifetime();
            else if (terminal == Terminal::Shutdown)
                device.reset();
            if (physicalLoss(terminal)) {
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
                assert(!custody->pendingOriginalRecords() && !custody->physicalLossArmed()
                    && !issuer->hasOriginalRecordedCommand()
                    && !issuer->hasOriginalCompletedSubmission());
            } else {
                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                    require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{recording, submission, callback}, [&]() noexcept {
                            return custody->cancelOriginal();
                        }));
                });
                assert(!custody->pendingOriginalRecords() && !custody->physicalLossArmed()
                    && !custody->physicalLossDelivered() && !issuer->hasOriginalRecordedCommand()
                    && !issuer->hasOriginalCompletedSubmission());
            }
            assert(Issuer::recordOriginal(issuer, execution)
                && Issuer::submitOriginal(issuer, execution));
        }
        const auto expected =
            physicalLoss(terminal) ? Root::Status::Invalidated : Root::Status::Abandoned;
        assert(root->status() == expected && root->journalEmpty());
        cpuClosed();
        assert(!observation.submits);
        Probe::finishColdCallObservation();
        Probe::restore();
        wgpuQueueRelease(queue);
        // Quota-owned weak control blocks must not survive the final zero check.
        issuerObserver.reset();
        custodyObserver.reset();
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_TERMINAL_OK %s %s journal-empty sticky-CPU-closed encoder0 groups0 submit0 original-local-loss-drained unpublished no-retry %s %s public-closed\n",
            terminalName(terminal),
            physicalLoss(terminal) ? "root-Invalidated" : "root-Abandoned",
            physicalLoss(terminal) ? "actual-device-loss" : "physical-loss-undelivered",
            terminal == Terminal::Drop ? "issuer-callback-facades-expired"
                                       : "issuer-callback-facades-retained");
        return expected;
    }
#endif
};
} // namespace arp::test
