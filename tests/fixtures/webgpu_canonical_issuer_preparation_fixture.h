#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_canonical_copy_pass_lowering_fixture.h"

namespace arp::test {
class WebGpuCanonicalIssuerPreparationFixture final {
    using Issuers = rhi_webgpu::WebGpuCanonicalIssuerPreparation;
    using Lowering = rhi_webgpu::WebGpuCanonicalCopyPassLowering;
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Contract = rhi::OriginalCommandIssuerPreparationContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Live final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        std::weak_ptr<rhi::OriginalCommandStateTransaction> root;
        std::shared_ptr<const Issuers> issuers;
    };

    static inline core::UniquePtr<Live> live_;
#endif
public:
    static Issuers::Budget budget() { return {1u, true, 1u, 1u, 67u, 65536u, 65536u}; }

    static void verifyContract()
    {
        auto b = budget();
        assert(!Contract::validateIntent(b) && Contract::additionalOwnerCount == 3u);
        b.contractVersion = 0u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-ISSUER-PREPARATION-VERSION");
        b = budget();
        b.prototypeOptIn = false;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-ISSUER-PREPARATION-OPT-IN");
        for (auto member :
            {&Issuers::Budget::maxRecordingIssuers, &Issuers::Budget::maxSubmissionIssuers})
            for (unsigned count : {0u, 2u}) {
                b = budget();
                b.*member = count;
                is(Contract::validateIntent(b), "RHI-ORIGINAL-ISSUER-PREPARATION-COUNT");
            }
        b = budget();
        b.maxRecordingCpuBytes = 0u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-ISSUER-PREPARATION-BUDGET");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandIssuerPreparationLowering::lowering(backend);
            assert(d.diagnostic && !d.recordingAvailable && !d.executionAvailable);
            assert(d.issuerStorageAvailable == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_CANONICAL_ISSUER_CONTRACT_OK actual-existing-issuer-types owner-add3 inline-evidence no-new-callbacks four-record-submit-gates-closed");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const char* missingDiagnostic = nullptr;
        assert(!Issuers::prepareOriginal({}, budget(), missingDiagnostic));
        is(missingDiagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-OWNER");
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& graph, const char* diagnostic) {
                assert(graph && !diagnostic);
                const auto source =
                    graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
                std::shared_ptr<rhi::OriginalCommandStateTransaction> root;
                std::shared_ptr<const Command> command;
                std::shared_ptr<const Custody> custody;
                std::shared_ptr<const Lowering> lowering;
                std::shared_ptr<core::CpuMemoryQuota> submission, callback;
                const auto prepare = [&] {
                    root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                        *context.device,
                        context.quotas[5],
                        diagnostic,
                        {1u, true, 10u, 0u, 16u, 32u});
                    assert(root);
                    auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                        context, root, 67u, diagnostic);
                    assert(admission);
                    Command::Budget wrongCommand{1u, true, 1u, 67u, 65536u};
                    wrongCommand.generalOutput = {1u, true};
                    assert(!Command::prepareOriginal(admission, wrongCommand, diagnostic));
                    is(diagnostic, "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
                    command = Command::prepareOriginal(
                        admission, {1u, true, 1u, 67u, 65536u}, diagnostic);
                    assert(command);
                    submission = admission->originalSubmissionQuota();
                    callback = admission->originalCallbackQuota();
                    Custody::Budget wrongCallback{2u, true, 1u, 12u, 67u, 65536u, 65536u};
                    wrongCallback.generalOutput = {1u, true};
                    assert(!Custody::prepareOriginal(command, wrongCallback, diagnostic));
                    is(diagnostic, "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
                    custody = Custody::prepareOriginal(
                        command, {1u, true, 1u, 8u, 67u, 65536u, 65536u}, diagnostic);
                    assert(custody);
                    Lowering::Budget wrongLowering;
                    wrongLowering.contractVersion = 5u;
                    wrongLowering.prototypeOptIn = true;
                    wrongLowering.maxRetainedOwners = 67u;
                    wrongLowering.maxCpuMetadataBytes = 65536u;
                    wrongLowering.readback = {1u, true, 1u, 1u, 1u, 256u, 256u, 65536u};
                    wrongLowering.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
                    wrongLowering.generalOutput = {1u, true};
                    assert(!Lowering::prepareOriginal(custody, wrongLowering, diagnostic));
                    is(diagnostic, "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
                    lowering = Lowering::prepareOriginal(
                        custody, {1u, true, 7u, 1u, 67u, 65536u}, diagnostic);
                    assert(lowering);
                    auto wrongIssuer = budget();
                    wrongIssuer.generalOutput = {1u, true};
                    assert(!Issuers::prepareOriginal(lowering, wrongIssuer, diagnostic));
                    is(diagnostic, "RHI-GENERAL-OUTPUT-PROFILE-MISMATCH");
                };
                const auto retire = [&] {
                    if (custody->pendingOriginalRecords())
                        assert(!custody->cancelOriginal());
                    lowering.reset();
                    custody.reset();
                    command.reset();
                    root.reset();
                    assert(!source->originalStateTransactionCount()
                        && !source->originalReservedSubmissionCount()
                        && !source->originalReservedCallbackTicketCount()
                        && !source->originalReservedCallbackRegistrationCount());
                    assert(!context.quotas[5]->liveBytes() && !submission->liveBytes()
                        && !callback->liveBytes());
                };
                const auto make = [&] {
                    return Issuers::prepareOriginal(lowering, budget(), diagnostic);
                };
                prepare();
                auto recording = context.quotas[5];
                std::uint64_t recordingPeak = 0u, submissionPeak = 0u;
                {
                    core::CpuMemoryQuotaLease rMarker(
                        recording, recording->peakBytes() - recording->liveBytes() + 1u);
                    core::CpuMemoryQuotaLease sMarker(
                        submission, submission->peakBytes() - submission->liveBytes() + 1u);
                    assert(rMarker.charged() && sMarker.charged());
                    const auto rBase = recording->liveBytes(), sBase = submission->liveBytes(),
                               cBase = callback->liveBytes();
                    auto issuers = make();
                    assert(
                        issuers && !issuers->validate() && issuers->issuersClosedForInspection());
                    recordingPeak = recording->peakBytes() - rBase;
                    submissionPeak = submission->peakBytes() - sBase;
                    assert(recordingPeak && submissionPeak && callback->liveBytes() == cBase);
                    assert(!custody->cancelOriginal());
                    issuers.reset();
                }
                lowering.reset();
                custody.reset();
                command.reset();
                root.reset();
                assert(
                    !recording->liveBytes() && !submission->liveBytes() && !callback->liveBytes());
                for (bool submissionDomain : {false, true})
                    for (int difference : {0, -1, -2}) {
                        prepare();
                        const auto quota = submissionDomain ? submission : recording;
                        const auto peak = submissionDomain ? submissionPeak : recordingPeak;
                        const auto available = difference == -2 ? 0u : peak + difference;
                        const auto rBase = recording->liveBytes(), sBase = submission->liveBytes(),
                                   cBase = callback->liveBytes();
                        {
                            core::CpuMemoryQuotaLease pressure(
                                quota, quota->limitBytes() - quota->liveBytes() - available);
                            assert(pressure.charged());
                            const auto pressuredRecording = recording->liveBytes(),
                                       pressuredSubmission = submission->liveBytes();
                            auto issuers = make();
                            assert(bool(issuers) == (difference == 0));
                            if (issuers) {
                                assert(
                                    !issuers->validate() && issuers->issuersClosedForInspection());
                                assert(!custody->cancelOriginal());
                            } else {
                                is(diagnostic, "RHI-WEBGPU-CANONICAL-ISSUER-ALLOCATION");
                                assert(recording->liveBytes() == pressuredRecording
                                    && submission->liveBytes() == pressuredSubmission
                                    && callback->liveBytes() == cBase);
                                assert(!lowering->validate() && !root->validate()
                                    && root->journalEmpty());
                            }
                        }
                        if (difference != 0) {
                            assert(recording->liveBytes() == rBase
                                && submission->liveBytes() == sBase
                                && callback->liveBytes() == cBase);
                            auto retry = make();
                            assert(retry && !retry->validate());
                        }
                        retire();
                    }
                prepare();
                auto invalid = budget();
                --invalid.maxRetainedOwners;
                assert(!Issuers::prepareOriginal(lowering, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-ISSUER-OWNER-CAPACITY");
                invalid = budget();
                --invalid.maxRecordingCpuBytes;
                assert(!Issuers::prepareOriginal(lowering, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA");
                invalid = budget();
                --invalid.maxSubmissionCpuBytes;
                assert(!Issuers::prepareOriginal(lowering, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-ISSUER-QUOTA");
                auto forgedLowering = std::shared_ptr<const Lowering>(submission, lowering.get());
                assert(!Issuers::prepareOriginal(forgedLowering, budget(), diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-LOWERING-OWNER");
                forgedLowering.reset();
                auto issuers = make();
                assert(issuers && issuers->retainedOwnerCount() == 67u);
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-ISSUER-DUPLICATE");
                auto forged = std::shared_ptr<const Issuers>(recording, issuers.get());
                is(Issuers::validateOriginal(forged), "RHI-WEBGPU-CANONICAL-ISSUER-OWNER");
                forged.reset();
                const std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    submission,
                    callback};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        assert(core::SharedOwnerProjection::sameOwner(
                            issuers->originalLowering(), lowering));
                        assert(issuers->issuersClosedForInspection()
                            && !issuers->nativeExecutionProven() && issuers->executionDiagnostic());
                        assert(root->journalEmpty()
                            && source->originalReservedSubmissionCount() == 1u
                            && source->originalReservedCallbackTicketCount() == 8u
                            && source->originalReservedCallbackRegistrationCount() == 8u);
                        return issuers->validate();
                    }));
                std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = root;
                root.reset();
                command.reset();
                custody.reset();
                lowering.reset();
                context.nativeLossObservation.reset();
                context.device.reset();
                assert(!retained.expired());
                is(issuers->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        issuers.reset();
                        assert(retained.expired());
                        retained.reset();
                        return nullptr;
                    }));
                assert(!recording->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
                    && !context.nativeLossQuota->liveBytes());
                std::printf(
                    "BINDLESS_RHI_CANONICAL_ISSUER_OK owner67 recording%llu submission%llu callbacks0 extra-source0 actual-existing-types exact-less-exhausted retry-duplicate-forged-refuse old-record-submit-closed caller-drop-shutdown hot-allocation0 no-encoder-no-proof\n",
                    static_cast<unsigned long long>(recordingPeak),
                    static_cast<unsigned long long>(submissionPeak));
            });
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& graph, const char* diagnostic) {
                assert(graph && !diagnostic);
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
                auto issuers = Issuers::prepareOriginal(lowering, budget(), diagnostic);
                assert(issuers && !issuers->validate());
                live_ = core::makeUnique<Live>();
                live_->root = root;
                live_->issuers = std::move(issuers);
                live_->quotas = {context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    admission->originalSubmissionQuota(),
                    admission->originalCallbackQuota()};
                context.device.reset();
            });
#else
        (void)create;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(live_ && WebGpuAsyncWaitFixture::until([] {
            const auto* id = live_->issuers->validate();
            return id && std::string_view(id) == "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST";
        }));
        assert(live_->root.expired());
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            live_->quotas, [&]() noexcept -> const char* {
                assert(!live_->issuers->nativeExecutionProven());
                live_->issuers.reset();
                live_->root.reset();
                return nullptr;
            }));
        for (const auto& quota : live_->quotas)
            assert(!quota->liveBytes());
        live_.reset();
        std::puts(
            "BINDLESS_RHI_CANONICAL_ISSUER_LOSS_OK actual-browser-loss same-root real-issuer-objects-released-before-lowering no-strong-runtime-cycle all-nine-quotas-zero hot-allocation0 no-completion-proof");
#endif
    }
};
} // namespace arp::test
