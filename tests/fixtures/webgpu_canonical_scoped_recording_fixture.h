#pragma once

#include "core/runtime/completion_evidence_batch.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_canonical_issuer_preparation_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"

namespace arp::test {
class WebGpuCanonicalScopedRecordingFixture final {
    using Issuers = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
    using Lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering;
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Root = rhi::OriginalCommandStateTransaction;
    using Contract = rhi::OriginalCommandScopedRecordingContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Live final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        std::shared_ptr<Root> root;
        std::shared_ptr<const Issuers> issuers;
        core::UniquePtr<rhi::GpuDevice> device;
    };

    static inline core::UniquePtr<Live> live_;
#endif
public:
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static bool originalRecordingSlotsDrained(const Issuers& issuers) noexcept
    {
        return issuers.recordingSlotsDrainedForInspection();
    }

    static std::uint32_t originalRecordingScopesAtLoss(const Issuers& issuers) noexcept
    {
        return issuers.recordingScopesAtLossForInspection();
    }
#endif
    static void verifyContract()
    {
        assert(!Contract::validateIntent({1u, true}));
        is(Contract::validateIntent({}), "RHI-ORIGINAL-SCOPED-RECORDING-VERSION");
        is(Contract::validateIntent({2u, true}), "RHI-ORIGINAL-SCOPED-RECORDING-VERSION");
        is(Contract::validateIntent({1u, false}), "RHI-ORIGINAL-SCOPED-RECORDING-OPT-IN");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandScopedRecordingLowering::lowering(backend);
            assert(d.diagnostic && !d.executionAvailable);
            assert(d.recordingAvailable == (backend == rhi::Backend::WebGPU));
        }
        assert(!arp::rhi::semantics::OriginalCommandScopedRecordingLowering::lowering(
            static_cast<rhi::Backend>(255))
                .recordingAvailable);
        core::runtime::CompletionEvidenceBatch evidence(3u);
        assert(evidence.observe(2u, true) && !evidence.proven());
        assert(evidence.observe(0u, false) && !evidence.proven());
        assert(evidence.observe(1u, true) && evidence.allReceived() && !evidence.proven());
        assert(!evidence.observe(1u, true) && !evidence.proven());
        std::puts(
            "BINDLESS_RHI_CANONICAL_SCOPED_RECORDING_CONTRACT_OK explicit-opt-in four-lowering-outcomes scopes3 drain-before-proof no-submit-no-commit");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        enum class Probe {
            Success,
            NativeError,
            Cancel,
            Drop,
            Shutdown,
            ChangedNative,
            ReverseScopes
        };
        for (const auto probe : {Probe::Success,
                 Probe::NativeError,
                 Probe::Cancel,
                 Probe::Drop,
                 Probe::Shutdown,
                 Probe::ChangedNative,
                 Probe::ReverseScopes}) {
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> finalQuotas;
            OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
                WebGpuCanonicalColdAdmissionFixture::configure,
                [&](auto& context, auto& graph, const char* diagnostic) {
                    assert(graph && !diagnostic);
                    const auto* buffer =
                        std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                            context.leases[0])
                            ->originalObject();
                    auto native = rhi_webgpu::webGpuNativePayloadFor(*buffer);
                    assert(native && native->buffer);
                    auto root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                        *context.device,
                        context.quotas[5],
                        diagnostic,
                        {1u, true, 10u, 0u, 16u, 32u});
                    assert(root);
                    auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                        context, root, 67u, diagnostic);
                    assert(admission);
                    auto command = Command::prepareOriginal(
                        admission, {1u, true, 1u, 67u, 65536u}, diagnostic);
                    assert(command);
                    auto custody = Custody::prepareOriginal(
                        command, {1u, true, 1u, 8u, 67u, 65536u, 65536u}, diagnostic);
                    assert(custody);
                    auto lowering = Lowering::prepareOriginal(
                        custody, {1u, true, 7u, 1u, 67u, 65536u}, diagnostic);
                    assert(lowering);
                    auto issuers = Issuers::prepareOriginal(
                        lowering, WebGpuCanonicalIssuerPreparationFixture::budget(), diagnostic);
                    assert(issuers);
                    const auto source = graph->originalInputs()
                                            ->originalScalars()
                                            ->originalUsage()
                                            ->originalSources();
                    finalQuotas = {context.quotas[0],
                        context.quotas[1],
                        context.quotas[2],
                        context.quotas[3],
                        context.quotas[4],
                        context.quotas[5],
                        context.nativeLossQuota,
                        admission->originalSubmissionQuota(),
                        admission->originalCallbackQuota()};
                    is(Issuers::recordOriginal(issuers, {}),
                        "RHI-ORIGINAL-SCOPED-RECORDING-VERSION");
                    is(Issuers::recordOriginal(issuers, {1u, false}),
                        "RHI-ORIGINAL-SCOPED-RECORDING-OPT-IN");
                    auto forged = std::shared_ptr<const Issuers>(context.quotas[5], issuers.get());
                    is(Issuers::recordOriginal(forged, {1u, true}),
                        "RHI-WEBGPU-CANONICAL-ISSUER-OWNER");
                    forged.reset();
                    assert(!issuers->validate() && root->status() == Root::Status::Prepared
                        && root->journalEmpty());
                    const auto size = native->sizeBytes;
                    if (probe == Probe::NativeError || probe == Probe::ReverseScopes)
                        WebGpuNativeSubmissionProbeFixture::instrumentRecording(
                            native->ownerDevice, probe == Probe::ReverseScopes);
                    std::weak_ptr<Root> weak = root;
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        finalQuotas, [&]() noexcept -> const char* {
                            assert(!Issuers::recordOriginal(issuers, {1u, true}));
                            if (probe == Probe::NativeError || probe == Probe::ReverseScopes)
                                WebGpuNativeSubmissionProbeFixture::restoreRecording();
                            assert(!issuers->hasOriginalRecordedCommand()
                                && !issuers->recordingSlotsDrainedForInspection());
                            assert(
                                root->status() == Root::Status::Pending && !root->journalEmpty());
                            is(root->commitOriginalPendingOwnership(),
                                "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                            is(root->abandonOriginalPendingOwnership(),
                                "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                            is(Issuers::recordOriginal(issuers, {1u, true}),
                                "RHI-WEBGPU-CANONICAL-RECORDING-ONE-SHOT");
                            if (probe == Probe::Cancel)
                                is(custody->cancelOriginal(),
                                    "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING");
                            if (probe == Probe::ChangedNative)
                                --native->sizeBytes;
                            if (probe == Probe::Shutdown)
                                context.device.reset();
                            if (probe == Probe::Drop) {
                                issuers.reset();
                                lowering.reset();
                                custody.reset();
                                command.reset();
                                admission.reset();
                                root.reset();
                                assert(!weak.expired());
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return weak.expired();
                                }));
                            } else {
                                assert(WebGpuAsyncWaitFixture::until([&] {
                                    return issuers->recordingSlotsDrainedForInspection();
                                }));
                                if (probe == Probe::Success || probe == Probe::ReverseScopes) {
                                    assert(issuers->hasOriginalRecordedCommand()
                                        && !issuers->recordingDiagnostic());
                                    assert(root->status() == Root::Status::Pending
                                        && !root->validate() && !root->journalEmpty());
                                    assert(issuers->validate() && !issuers->nativeExecutionProven()
                                        && issuers->executionDiagnostic());
                                    assert(!custody->cancelOriginal());
                                } else {
                                    assert(!issuers->hasOriginalRecordedCommand()
                                        && issuers->recordingDiagnostic());
                                    if (probe == Probe::NativeError)
                                        is(issuers->recordingDiagnostic(),
                                            "RHI-WEBGPU-CANONICAL-RECORDING-NATIVE-ERROR");
                                    if (probe == Probe::Cancel)
                                        is(issuers->recordingDiagnostic(),
                                            "RHI-WEBGPU-CANONICAL-RECORDING-CANCELLED");
                                }
                                assert(root->status() == Root::Status::Abandoned
                                    && root->journalEmpty());
                                native->sizeBytes = size;
                                issuers.reset();
                                lowering.reset();
                                custody.reset();
                                command.reset();
                                admission.reset();
                                root.reset();
                                assert(weak.expired());
                            }
                            weak.reset();
                            assert(!source->originalReservedSubmissionCount()
                                && !source->originalReservedCallbackTicketCount()
                                && !source->originalReservedCallbackRegistrationCount());
                            return nullptr;
                        }));
                    assert(!finalQuotas[5]->liveBytes() && !finalQuotas[7]->liveBytes()
                        && !finalQuotas[8]->liveBytes());
                    context.nativeLossObservation.reset();
                    context.device.reset();
                    graph.reset();
                    std::printf(
                        "BINDLESS_RHI_CANONICAL_SCOPED_RECORDING_OK probe%u actual-encoder-finish copies7 scopes3 same-root-pending drain-before-proof rollback-once hot-allocation0 no-submit-no-commit\n",
                        static_cast<unsigned>(probe));
                });
            for (const auto& quota : finalQuotas)
                assert(!quota->liveBytes());
        }
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& graph, const char* diagnostic) {
                auto root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                    *context.device,
                    context.quotas[5],
                    diagnostic,
                    {1u, true, 10u, 0u, 16u, 32u});
                assert(root);
                auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                    context, root, 67u, diagnostic);
                assert(admission);
                auto command =
                    Command::prepareOriginal(admission, {1u, true, 1u, 67u, 65536u}, diagnostic);
                assert(command);
                auto custody = Custody::prepareOriginal(
                    command, {1u, true, 1u, 8u, 67u, 65536u, 65536u}, diagnostic);
                assert(custody);
                auto lowering =
                    Lowering::prepareOriginal(custody, {1u, true, 7u, 1u, 67u, 65536u}, diagnostic);
                assert(lowering);
                auto issuers = Issuers::prepareOriginal(
                    lowering, WebGpuCanonicalIssuerPreparationFixture::budget(), diagnostic);
                assert(issuers);
                live_ = core::makeUnique<Live>();
                live_->root = root;
                live_->issuers = std::move(issuers);
                live_->device = std::move(context.device);
                live_->quotas = {context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    admission->originalSubmissionQuota(),
                    admission->originalCallbackQuota()};
            });
#else
        (void)create;
#endif
    }

    static void beginActualLossProbe()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(live_);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            live_->quotas, []() noexcept -> const char* {
                const auto* id = Issuers::recordOriginal(live_->issuers, {1u, true});
                assert(!id);
                assert(!live_->issuers->recordingSlotsDrainedForInspection()
                    && live_->root->status() == Root::Status::Pending);
                return id;
            }));
        std::puts(
            "BINDLESS_RHI_CANONICAL_SCOPED_RECORDING_LOSS_ARMED scopes3 genuinely-outstanding-at-physical-destroy-request");
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(live_ && WebGpuAsyncWaitFixture::until([] {
            const auto* id = live_->issuers->recordingDiagnostic();
            return id && std::string_view(id) == "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST"
                && live_->issuers->recordingSlotsDrainedForInspection();
        }));
        assert(live_->root->status() == Root::Status::Invalidated && live_->root->journalEmpty());
        const auto atLoss = live_->issuers->recordingScopesAtLossForInspection();
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            live_->quotas, []() noexcept -> const char* {
                assert(!live_->issuers->hasOriginalRecordedCommand());
                live_->issuers.reset();
                live_->root.reset();
                live_->device.reset();
                return nullptr;
            }));
        for (const auto& quota : live_->quotas)
            assert(!quota->liveBytes());
        live_.reset();
        std::printf(
            "BINDLESS_RHI_CANONICAL_SCOPED_RECORDING_LOSS_OK actual-browser-loss outstanding-at-observer%u invalidated-root scopes-drained all-nine-quotas-zero hot-allocation0 no-completion-proof\n",
            atLoss);
#endif
    }
};
} // namespace arp::test
