#pragma once

#include "original_finite_target_memory_write_fixture.h"
#include "original_target_memory_interval_fixture.h"
#include "original_typed_attachment_dependency_probe_fixture.h"
#include "original_typed_callback_custody_fixture.h"
#include "rhi/semantics/command_lowering.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "webgpu_error_scope_handoff_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"
#endif

namespace arp::test {
class OriginalTypedNativeExecutionFixture final {
    using C = rhi::OriginalTypedNativeExecutionContract;
    using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Root = rhi::OriginalCommandStateTransaction;
    static constexpr C::Request intent{
        1u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "typed execution expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        OriginalTargetMemoryIntervalFixture::verifyContract();
        assert(!C::validateIntent(intent));
        auto wrong = intent;
        wrong.contractVersion = 0u;
        assert(C::validateIntent(wrong));
        auto intent2 = intent;
        intent2.contractVersion = 2u;
        assert(!C::validateProfile(intent, 1u) && !C::validateProfile(intent2, 2u));
        is(C::validateProfile(intent, 2u), "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        is(C::validateProfile(intent2, 1u), "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        wrong = intent;
        wrong.prototypeOptIn = false;
        assert(C::validateIntent(wrong));
        wrong = intent;
        wrong.synchronizationPolicy = rhi::OriginalTypedSynchronizationPolicy::Unspecified;
        assert(C::validateIntent(wrong));
        assert(!C::validateDispatch(65535u, 1u, 0u));
        is(C::validateDispatch(65536u, 1u, 1u), "RHI-TYPED-NATIVE-EXECUTION-DISPATCH-LIMIT");
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend);
            assert(d.validatorOnly == (backend == rhi::Backend::Null));
            assert(d.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && d.submissionAvailable == d.recordingAvailable);
            assert(
                d.prototype == d.recordingAvailable && bool(d.diagnostic) != d.recordingAvailable);
            const auto v2 =
                arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 2u);
            assert(v2.validatorOnly == d.validatorOnly
                && v2.recordingAvailable == d.recordingAvailable
                && v2.submissionAvailable == d.submissionAvailable);
            const auto v3 =
                arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 3u);
            assert(v3.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && !v3.submissionAvailable);
            assert(v3.validatorOnly == (backend == rhi::Backend::Null) && v3.diagnostic);
            const auto v4 =
                arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 4u);
            assert(v4.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && v4.submissionAvailable == v4.recordingAvailable
                && v4.validatorOnly == (backend == rhi::Backend::Null));
            assert(!arp::rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 5u)
                    .recordingAvailable);
        }
        auto intent4 = intent;
        intent4.contractVersion = 4u;
        auto intent6 = intent;
        intent6.contractVersion = 6u;
        assert(!C::validateProfile(intent6, 6u));
        for (const auto version : {1u, 2u, 3u, 4u, 5u})
            assert(C::validateProfile(intent6, version));
        auto invalid6 = intent6;
        invalid6.contractVersion = 9u;
        assert(C::validateIntent(invalid6));
        invalid6 = intent6;
        invalid6.prototypeOptIn = false;
        assert(C::validateIntent(invalid6));
        invalid6 = intent6;
        invalid6.readbackCompletion = {1u, true};
        assert(C::validateIntent(invalid6));
        auto submit6 = intent6;
        submit6.generalSubmission = {1u, true};
        {
            using Snapshot = rhi::OriginalGeneralOutputSnapshotContract;
            rhi::TextureDesc output;
            output.extent = {1u, 1u, 1u};
            output.format = rhi::TextureFormat::Rgba16Float;
            output.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::CopySrc;
            Snapshot::Budget b{1u,
                true,
                Snapshot::Provenance::QueueOrderedSnapshot,
                rhi::defaultTextureSubresourceRange(output),
                241u,
                14u,
                8u,
                65536u};
            assert(!Snapshot::inspect(output, b, 240u, 14u, 8u));
            auto finite = b;
            finite.contractVersion = 2u;
            assert(!Snapshot::inspect(output, finite, 240u, 14u, 8u));
            assert(!Snapshot::validateUpstreamProfile(1u, 6u, true, false, 4u));
            assert(!Snapshot::validateUpstreamProfile(2u, 7u, false, true, 5u));
            for (const auto version : {0u, 1u, 2u, 3u})
                for (const auto issuer : {6u, 7u})
                    for (const bool whole : {false, true})
                        for (const bool finiteProfile : {false, true})
                            for (const auto content : {0u, 4u, 5u})
                                assert(!Snapshot::validateUpstreamProfile(
                                           version, issuer, whole, finiteProfile, content)
                                    == ((version == 1u && issuer == 6u && whole && !finiteProfile
                                            && content == 4u)
                                        || (version == 2u && issuer == 7u && !whole && finiteProfile
                                            && content == 5u)));
            for (const auto version : {0u, 4u}) {
                auto invalid = b;
                invalid.contractVersion = version;
                assert(Snapshot::validateIntent(invalid));
            }
            auto graphicsOutput = b;
            graphicsOutput.contractVersion = 3u;
            assert(!Snapshot::validateIntent(graphicsOutput));
            assert(Snapshot::inspect(output, graphicsOutput, 240u, 14u, 8u));
            auto invalid = b;
            invalid.prototypeOptIn = false;
            assert(Snapshot::validateIntent(invalid));
            invalid = b;
            invalid.provenance = Snapshot::Provenance::Unspecified;
            assert(Snapshot::validateIntent(invalid));
            invalid = b;
            --invalid.maxRetainedOwners;
            assert(Snapshot::inspect(output, invalid, 240u, 14u, 8u));
            invalid = b;
            --invalid.maxJoinVisits;
            assert(Snapshot::inspect(output, invalid, 240u, 14u, 8u));
            invalid = b;
            --invalid.maxSourceBytes;
            assert(Snapshot::inspect(output, invalid, 240u, 14u, 8u));
            invalid = b;
            invalid.maxCpuMetadataBytes = 0u;
            assert(Snapshot::validateIntent(invalid));
            invalid = b;
            invalid.range.baseMipLevel = 1u;
            assert(Snapshot::inspect(output, invalid, 240u, 14u, 8u));
            assert(Snapshot::inspect(output, b, UINT32_MAX, 14u, 8u));
            auto wrong = output;
            wrong.usage = rhi::TextureUsage::Storage;
            assert(Snapshot::inspect(wrong, b, 240u, 14u, 8u));
            wrong = output;
            wrong.format = rhi::TextureFormat::Rgba8Unorm;
            assert(Snapshot::inspect(wrong, b, 240u, 14u, 8u));
            wrong = output;
            wrong.sampleCount = 4u;
            assert(Snapshot::inspect(wrong, b, 240u, 14u, 8u));
            wrong = output;
            wrong.mipLevelCount = 2u;
            assert(Snapshot::inspect(wrong, b, 240u, 14u, 8u));
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU}) {
                const auto d =
                    rhi::semantics::OriginalGeneralOutputSnapshotLowering::lowering(backend);
                assert(d.privateAssociationAvailable == (backend == rhi::Backend::WebGPU)
                    && !d.copyAvailable && !d.mapAvailable && !d.publicExecutionAvailable
                    && d.diagnostic);
                assert(rhi::semantics::OriginalGeneralOutputSnapshotLowering::lowering(backend, 2u)
                           .privateAssociationAvailable
                    == (backend == rhi::Backend::WebGPU));
                assert(rhi::semantics::OriginalGeneralOutputSnapshotLowering::lowering(backend, 3u)
                           .privateAssociationAvailable
                    == (backend == rhi::Backend::WebGPU));
                const auto graphicsOutput =
                    rhi::semantics::OriginalGeneralOutputExecutionLowering::lowering(backend, 3u);
                assert(graphicsOutput.recordingAvailable == (backend == rhi::Backend::WebGPU));
                assert(graphicsOutput.submissionAvailable == (backend == rhi::Backend::WebGPU));
                assert(!graphicsOutput.nativeSyncAvailable);
            }
            const char* id = nullptr;
            using NativeSnapshot = rhi_webgpu::WebGpuOriginalGeneralOutputSnapshot;
            assert(!NativeSnapshot::prepareOriginal({}, {}, {}, b, {}, id));
            is(id, "RHI-GENERAL-OUTPUT-ISSUER-OWNER");
            is(NativeSnapshot::validateCompletedOriginal({}), "RHI-GENERAL-OUTPUT-SNAPSHOT-OWNER");
            std::puts(
                "BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT_CONTRACT_OK explicit-queue-snapshot1 exact-range owner-bytes-visits bounded four-lowerings copy-map-public-closed");
        }
        assert(!C::validateIntent(submit6) && !C::validateProfile(submit6, 6u));
        for (const auto version : {1u, 2u, 3u, 4u, 5u}) {
            auto legacy = submit6;
            legacy.contractVersion = version;
            is(C::validateIntent(legacy), "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-PROFILE");
        }
        invalid6 = submit6;
        invalid6.generalSubmission = {0u, true};
        is(C::validateIntent(invalid6), "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-VERSION");
        invalid6 = submit6;
        invalid6.generalSubmission = {2u, true};
        is(C::validateIntent(invalid6), "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-VERSION");
        invalid6 = submit6;
        invalid6.generalSubmission.prototypeOptIn = false;
        is(C::validateIntent(invalid6), "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-OPT-IN");
        invalid6 = submit6;
        invalid6.readbackCompletion = {1u, true};
        assert(C::validateIntent(invalid6));
        is(C::validateRecordedGeneralSubmission(submit6, UINT32_MAX),
            "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-UNRECORDED");
        is(C::validateRecordedGeneralSubmission(submit6, 0u),
            "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-RECORDED-PROFILE");
        is(C::validateRecordedGeneralSubmission(intent6, 1u),
            "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-RECORDED-PROFILE");
        assert(!C::validateRecordedGeneralSubmission(submit6, 1u)
            && !C::validateRecordedGeneralSubmission(intent6, 0u));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto pending =
                rhi::semantics::OriginalCommandNativeClosureLowering::generalPendingReadLowering(
                    backend);
            assert(pending.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !pending.recordingIssuerAvailable && !pending.executionAvailable
                && pending.diagnostic);
            const auto graphicsPending =
                rhi::semantics::OriginalCommandNativeClosureLowering::generalPendingReadLowering(
                    backend, 3u);
            assert(graphicsPending.resourceClosureAvailable == (backend == rhi::Backend::WebGPU)
                && !graphicsPending.recordingIssuerAvailable
                && !graphicsPending.executionAvailable);
            const auto execution =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 6u);
            assert(execution.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && !execution.submissionAvailable && execution.diagnostic);
            assert(!rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 6u, 1u)
                    .recordingAvailable);
            assert(!rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 9u)
                    .recordingAvailable);
            const auto graphics =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 8u);
            assert(graphics.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && graphics.prototype == (backend == rhi::Backend::WebGPU)
                && !graphics.submissionAvailable);
            const auto submission =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 6u, 0u, 1u);
            assert(submission.recordingAvailable == (backend == rhi::Backend::WebGPU)
                && submission.submissionAvailable == submission.recordingAvailable
                && submission.validatorOnly == (backend == rhi::Backend::Null));
            assert(
                !rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 6u, 0u, 2u)
                    .submissionAvailable);
            assert(
                !rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 5u, 1u, 1u)
                    .recordingAvailable);
        }
        is(Issuers::recordOriginal({}, intent6), "RHI-WEBGPU-TYPED-ISSUER-OWNER");
        is(Issuers::submitOriginal({}, intent6), "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-UNPROVEN");
        assert(!C::validateProfile(intent4, 4u));
        for (const auto version : {1u, 2u, 3u}) {
            is(C::validateProfile(intent4, version), "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
            auto legacy = intent;
            legacy.contractVersion = version;
            is(C::validateProfile(legacy, 4u), "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        }
        auto mixed4 = intent4;
        mixed4.readbackCompletion = {1u, true};
        is(C::validateIntent(mixed4), "RHI-TYPED-NATIVE-READBACK-COMPLETION-PROFILE");
        auto intent3 = intent;
        intent3.contractVersion = 3u;
        assert(!C::validateProfile(intent3, 3u));
        auto completion = intent3;
        completion.readbackCompletion = {1u, true};
        assert(!C::validateIntent(completion));
        auto badCompletion = completion;
        badCompletion.readbackCompletion.prototypeOptIn = false;
        is(C::validateIntent(badCompletion), "RHI-TYPED-NATIVE-READBACK-COMPLETION-OPT-IN");
        badCompletion = completion;
        badCompletion.readbackCompletion.contractVersion = 2u;
        is(C::validateIntent(badCompletion), "RHI-TYPED-NATIVE-READBACK-COMPLETION-VERSION");
        badCompletion = completion;
        badCompletion.contractVersion = 2u;
        is(C::validateIntent(badCompletion), "RHI-TYPED-NATIVE-READBACK-COMPLETION-PROFILE");
        auto readback = rhi_webgpu::WebGpuOriginalTypedColdAdmission::lifecycleRequirements(3u);
        readback.transfers = {1u, 1u, 1u};
        readback.readback.resultDeclarations = 1u;
        readback.readbackMap = {1u, 1u, 3u, 12u, 12u};
        readback.capacity.callbackTickets = readback.capacity.callbackRegistrations = 12u;
        rhi::OriginalTypedCallbackCustodyRequest custody{3u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            12u,
            1u,
            128u,
            65536u,
            65536u,
            {1u, true}};
        assert(!C::validateReadbackProfile(completion, readback, custody));
        is(C::validateReadbackProfile(intent3, readback, custody),
            "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED");
        auto missingMap = custody;
        missingMap.readbackMap = {};
        is(C::validateReadbackProfile(completion, readback, missingMap),
            "RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-VERSION");
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto proof =
                rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 3u, 1u);
            assert(proof.submissionAvailable == (backend == rhi::Backend::WebGPU)
                && proof.recordingAvailable == proof.submissionAvailable
                && proof.validatorOnly == (backend == rhi::Backend::Null));
            assert(!rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 3u, 2u)
                    .submissionAvailable);
            assert(!rhi::semantics::OriginalTypedNativeExecutionLowering::lowering(backend, 2u, 1u)
                    .submissionAvailable);
        }
        for (const auto version : {1u, 2u}) {
            is(C::validateProfile(intent3, version), "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
            auto legacy = intent;
            legacy.contractVersion = version;
            is(C::validateProfile(legacy, 3u), "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        }
        is(Issuers::recordOriginal({}, intent3), "RHI-WEBGPU-TYPED-ISSUER-OWNER");
        is(Issuers::submitOriginal({}, intent3),
            "RHI-TYPED-NATIVE-EXECUTION-READBACK-COMPLETION-MISSING");
        is(Issuers::recordOriginal({}, intent), "RHI-WEBGPU-TYPED-ISSUER-OWNER");
        is(Issuers::submitOriginal({}, intent), "RHI-WEBGPU-TYPED-ISSUER-OWNER");
        assert(Issuers::recordOriginal({}, wrong) && Issuers::submitOriginal({}, wrong));
        std::puts(
            "BINDLESS_RHI_TYPED_NATIVE_EXECUTION_CONTRACT_OK explicit-Prototype1 scopes-lowering1 dispatch-guarantee65535 four-backends fail-closed no-general-promotion");
        OriginalTypedNativeGraphicsExecutionFixture::verifyContract();
        OriginalFiniteTargetMemoryWriteFixture::verifyContract();
        rhi::OriginalCommandDescriptorSourceBudget finite{3u,
            true,
            1u,
            1u,
            rhi::OriginalCommandDescriptorSetSource::finiteExclusiveRetainedOwnerCount,
            65536u};
        finite.finitePipelineSourceIndex = 2u;
        finite.finiteLogicalSet = 1u;
        finite.finiteFootprintContractVersion = 1u;
        finite.finiteFullBoundViewOptIn = true;
        assert(!rhi::OriginalCommandDescriptorSetSource::validateBudget(finite));
        finite.finiteFullBoundViewOptIn = false;
        assert(rhi::OriginalCommandDescriptorSetSource::validateBudget(finite));
        std::puts(
            "BINDLESS_RHI_TYPED_NATIVE_FINITE_CONTRACT_OK Source3 full-bound-view-explicit-opt-in original-pipeline-slot no-general-promotion");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
private:
    using Builder = OriginalTypedCallbackCustodyFixture;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using Scopes = WebGpuErrorScopeHandoffFixture<24u>;

    static void prepareIssuers(Builder::Live& live)
    {
        const char* id = nullptr;
        live.custody = Custody::prepareOriginal(live.command, live.request, id);
        assert(live.custody && !id);
        live.coldIssuers = Issuers::prepareOriginal(live.custody,
            {1u,
                true,
                1u,
                1u,
                7u,
                live.request.maxRetainedOwners,
                live.retained.recordingQuota->limitBytes(),
                live.submission->limitBytes(),
                live.callback->limitBytes()},
            id);
        assert(live.coldIssuers && !id);
    }

    template <std::uint32_t Domain = 24u> static void recordPending(Builder::Live& live)
    {
        using Held = WebGpuErrorScopeHandoffFixture<Domain>;
        Held::holdScopes();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{live.retained.recordingQuota, live.submission, live.callback},
                [&]() noexcept -> const char* {
                    return Issuers::recordOriginal(live.coldIssuers, intent);
                }));
        });
        Held::restoreScopes();
        Held::waitHeldScopes();
        assert(live.retained.transaction->status() == Root::Status::Pending
            && !live.retained.transaction->journalEmpty());
        assert(!live.coldIssuers->hasOriginalRecordedCommand()
            && !live.coldIssuers->hasOriginalCompletedSubmission());
        is(live.coldIssuers->recordingDiagnostic(), "RHI-WEBGPU-TYPED-RECORDING-PENDING");
        assert(live.retained.nativeClosure->validate()
            && live.retained.dependencyDecision->validate()
            && live.retained.typedNativePayload->validate() && live.admission->validate()
            && live.command->validate() && live.custody->validate()
            && live.coldIssuers->validate());
        is(Issuers::submitOriginal(live.coldIssuers, intent),
            "RHI-WEBGPU-CANONICAL-SUBMISSION-UNRECORDED");
    }

    static void observeInvocation(void* borrowed) noexcept
    {
        const auto* live = static_cast<const Builder::Live*>(borrowed);
        assert(live->retained.transaction->status() == Root::Status::Pending
            && !live->retained.transaction->journalEmpty());
        is(live->coldIssuers->submissionDiagnostic(), "RHI-WEBGPU-TYPED-SUBMISSION-PENDING");
        assert(!live->coldIssuers->hasOriginalCompletedSubmission());
    }

    template <bool targetMemory = false, class Factory, class Input, class Layout>
    static void verifyFailures(Factory&& create, const Input& input, const Layout& layout)
    {
        using Probe = WebGpuNativeSubmissionProbeFixture;
        const char* id = nullptr;
        auto limited =
            Builder::prepare<targetMemory, targetMemory>(create, input, layout, true, 65536u);
        prepareIssuers(*limited);
        is(Issuers::recordOriginal(limited->coldIssuers, intent),
            "RHI-TYPED-NATIVE-EXECUTION-DISPATCH-LIMIT");
        assert(limited->retained.transaction->status() == Root::Status::Prepared
            && limited->retained.transaction->journalEmpty() && !limited->coldIssuers->validate()
            && !limited->custody->cancelOriginal());
        Builder::release(limited);
        auto dropped = Builder::prepare<targetMemory, targetMemory>(create, input, layout);
        prepareIssuers(*dropped);
        recordPending(*dropped);
        auto retainedRoot = dropped->retained.transaction;
        dropped->coldIssuers.reset();
        dropped->custody.reset();
        assert(retainedRoot->status() == Root::Status::Pending);
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return retainedRoot->status() == Root::Status::Abandoned;
        }));
        assert(retainedRoot->journalEmpty());
        retainedRoot.reset();
        Builder::release(dropped);
        std::puts(targetMemory
                ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_PREFLIGHT_DROP_OK original-dispatch65536 rejected-before-Pending-and-encoder drop-real-scopes-retained genuine-drain quotas-zero"
                : "BINDLESS_RHI_TYPED_NATIVE_PREFLIGHT_DROP_OK original-dispatch65536 rejected-before-Pending-and-encoder drop-real-scopes-retained genuine-drain quotas-zero");
        for (const bool reverse : std::array{false, true}) {
            auto live = Builder::prepare<targetMemory, targetMemory>(create, input, layout);
            prepareIssuers(*live);
            const auto device =
                static_cast<WGPUDevice>(live->retained.nativeClosure->nativeDevice());
            Probe::instrumentRecording(device, reverse);
            assert(!Issuers::recordOriginal(live->coldIssuers, intent));
            Probe::restoreRecording();
            if (reverse) {
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return live->coldIssuers->hasOriginalRecordedCommand();
                }));
                assert(!live->custody->cancelOriginal());
            } else
                assert(WebGpuAsyncWaitFixture::until([&] {
                    return !live->custody->pendingOriginalRecords();
                }));
            assert(live->retained.transaction->status() == Root::Status::Abandoned
                && live->retained.transaction->journalEmpty());
            Builder::release(live);
        }
        std::puts(targetMemory
                ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_RECORDING_FAILURE_OK genuine-validation-error real-reverse-scope-delivery rollback-before-submit quotas-zero"
                : "BINDLESS_RHI_TYPED_NATIVE_RECORDING_FAILURE_OK genuine-validation-error real-reverse-scope-delivery rollback-before-submit quotas-zero");
        for (const auto probe : std::array{Probe::Probe::ThrowBefore,
                 Probe::Probe::ThrowAfter,
                 Probe::Probe::NativeError,
                 Probe::Probe::Reverse,
                 Probe::Probe::PartialScope}) {
            auto live = Builder::prepare<targetMemory, targetMemory>(create, input, layout);
            prepareIssuers(*live);
            assert(!Issuers::recordOriginal(live->coldIssuers, intent));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return live->coldIssuers->hasOriginalRecordedCommand();
            }));
            const auto device =
                static_cast<WGPUDevice>(live->retained.nativeClosure->nativeDevice());
            auto queue = wgpuDeviceGetQueue(device);
            Probe::instrument(device, queue, probe, live.get(), &observeInvocation);
            id = Issuers::submitOriginal(live->coldIssuers, intent);
            Probe::restore();
            if (probe == Probe::Probe::ThrowBefore || probe == Probe::Probe::ThrowAfter)
                is(id, "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN");
            if (probe == Probe::Probe::PartialScope)
                is(id, "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !live->custody->pendingOriginalRecords();
            }));
            assert(live->retained.transaction->journalEmpty());
            if (probe == Probe::Probe::Reverse)
                assert(live->coldIssuers->hasOriginalCompletedSubmission()
                    && live->retained.transaction->status() == Root::Status::Committed);
            else
                assert(!live->coldIssuers->hasOriginalCompletedSubmission()
                    && live->retained.transaction->status() == Root::Status::Invalidated);
            wgpuQueueRelease(queue);
            Builder::release(live);
        }
        std::puts(targetMemory
                ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_SUBMISSION_FAILURE_OK throw-before-after genuine-double-submit-error reverse4 partial-listener-suppression actual-queue-drain no-retry quotas-zero"
                : "BINDLESS_RHI_TYPED_NATIVE_SUBMISSION_FAILURE_OK throw-before-after genuine-double-submit-error reverse4 partial-listener-suppression actual-queue-drain no-retry quotas-zero");
        for (const unsigned terminal : std::array<unsigned, 3u>{0u, 1u, 2u}) {
            auto live = Builder::prepare<targetMemory, targetMemory>(create, input, layout);
            prepareIssuers(*live);
            assert(!Issuers::recordOriginal(live->coldIssuers, intent));
            assert(WebGpuAsyncWaitFixture::until([&] {
                return live->coldIssuers->hasOriginalRecordedCommand();
            }));
            Scopes::holdScopes();
            assert(!Issuers::submitOriginal(live->coldIssuers, intent));
            Scopes::restoreScopes();
            if (terminal == 0u)
                is(live->custody->cancelOriginal(), "RHI-WEBGPU-CANONICAL-CALLBACK-QUARANTINED");
            else if (terminal == 1u)
                live->context->device->retireOriginalDeviceLifetime();
            else
                live->context->device.reset();
            Scopes::waitHeldScopes();
            assert(!live->coldIssuers->hasOriginalCompletedSubmission()
                && live->custody->pendingOriginalRecords() == 1u
                && live->custody->physicalLossArmed() && !live->custody->physicalLossDelivered());
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !live->custody->pendingOriginalRecords();
            }));
            assert(live->retained.transaction->status() == Root::Status::Invalidated
                && live->retained.transaction->journalEmpty()
                && !live->coldIssuers->hasOriginalCompletedSubmission()
                && !live->custody->physicalLossDelivered());
            Builder::release(live);
        }
        std::puts(targetMemory
                ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_TERMINAL_OK possible-submit-cancel Retired-Shutdown original-observer-retained scope-queue-drain no-logical-loss quotas-zero"
                : "BINDLESS_RHI_TYPED_NATIVE_TERMINAL_OK possible-submit-cancel Retired-Shutdown original-observer-retained scope-queue-drain no-logical-loss quotas-zero");
        if constexpr (!targetMemory)
            verifyPhysicalLosses(create, input, layout);
    }

    template <bool targetMemory, class Factory, class Input, class Layout>
    static auto preparePhysicalLosses(Factory&& create, const Input& input, const Layout& layout)
    {
        std::array<core::UniquePtr<Builder::Live>, 4u> losses;
        for (auto& live : losses) {
            live = Builder::prepare<targetMemory, targetMemory>(create, input, layout);
            prepareIssuers(*live);
        }
        return losses;
    }

    template <std::uint32_t Domain>
    static auto armPhysicalLosses(std::array<core::UniquePtr<Builder::Live>, 4u>& losses)
    {
        using Probe = WebGpuNativeSubmissionProbeFixture;
        // A genuine queue then partially attached listener cannot prove GPU
        // lifetime; the shared original receiver retains it until physical loss.
        auto& queued = *losses[0];
        assert(!Issuers::recordOriginal(queued.coldIssuers, intent));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return queued.coldIssuers->hasOriginalRecordedCommand();
        }));
        const auto device = static_cast<WGPUDevice>(queued.retained.nativeClosure->nativeDevice());
        auto queue = wgpuDeviceGetQueue(device);
        Probe::instrumentLoss(device, queue, true);
        is(Issuers::submitOriginal(queued.coldIssuers, intent),
            "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED");
        Probe::restoreLoss();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return queued.submission->liveBytes() && queued.custody->pendingOriginalRecords() == 1u;
        }));
        assert(queued.custody->physicalLossArmed()
            && !queued.coldIssuers->hasOriginalCompletedSubmission());
        // Genuine recording scopes remain held across logical Shutdown and
        // physical loss. Native loss alone must not suppress those deliveries.
        auto& recording = *losses[1];
        recordPending<Domain>(recording);
        recording.context->device.reset();
        auto& thrown = *losses[3];
        Probe::instrumentRecordingException(thrown.retained.nativeClosure->nativeDevice());
        is(Issuers::recordOriginal(thrown.coldIssuers, intent),
            "RHI-WEBGPU-TYPED-RECORDING-INVOCATION-UNCERTAIN");
        Probe::restoreRecordingException();
        assert(thrown.custody->pendingOriginalRecords() == 1u
            && !thrown.coldIssuers->hasOriginalRecordedCommand());
        auto& uncertain = *losses[2];
        Probe::instrumentScopePush(uncertain.retained.nativeClosure->nativeDevice());
        is(Issuers::recordOriginal(uncertain.coldIssuers, intent),
            "RHI-WEBGPU-TYPED-RECORDING-SCOPE-PUSH-UNCERTAIN");
        Probe::restoreScopePush();
        assert(uncertain.custody->pendingOriginalRecords() == 1u
            && uncertain.custody->physicalLossArmed()
            && !uncertain.coldIssuers->hasOriginalRecordedCommand());
        return queue;
    }

    template <bool targetMemory, class Factory, class Input, class Layout>
    static void executePhysicalLosses(Factory&& create, const Input& input, const Layout& layout)
    {
        auto losses = preparePhysicalLosses<targetMemory>(create, input, layout);
        const auto queue = armPhysicalLosses<24u>(losses);
        const auto device =
            static_cast<WGPUDevice>(losses[0]->retained.nativeClosure->nativeDevice());
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
            device));
        assert(WebGpuAsyncWaitFixture::until([&] {
            return losses[1]->custody->physicalLossDelivered()
                && losses[2]->custody->physicalLossDelivered()
                && losses[0]->custody->physicalLossDelivered();
        }));
        assert(losses[1]->custody->pendingOriginalRecords() == 1u
            && !losses[1]->coldIssuers->hasOriginalRecordedCommand());
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            for (const auto& live : losses)
                if (live->custody->pendingOriginalRecords())
                    return false;
            return true;
        }));
        for (auto& live : losses) {
            assert(!live->coldIssuers->hasOriginalCompletedSubmission()
                && live->retained.transaction->status() == Root::Status::Invalidated
                && live->retained.transaction->journalEmpty());
            Builder::release(live);
        }
        wgpuQueueRelease(queue);
        std::puts(targetMemory
                ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_PHYSICAL_LOSS_OK Source4 actual-loss4 queue-registration-uncertain Shutdown-recording partial-push foreign-exception genuine-drain quotas-zero"
                : "BINDLESS_RHI_TYPED_NATIVE_PHYSICAL_LOSS_OK possible-submit-queue-registration-failure recording-scopes-held-after-Shutdown partial-native-push recording-foreign-exception actual-loss4 genuine-drain no-fake-maySubmit quotas-zero");
    }

    template <class Factory, class Input, class Layout>
    static void verifyPhysicalLosses(Factory&& create, const Input& input, const Layout& layout)
    {
        // Scope-stack uncertainty poisons one actual native device permanently.
        // Independent browser sessions select each regression's physical matrix;
        // no flag reset or new work after an unknown native push is permitted.
        if (EM_ASM_INT({
                // clang-format off
                return new URLSearchParams(location.search).has("target-finite-loss");
                // clang-format on
            }))
            executePhysicalLosses<true>(create, input, layout);
        else
            executePhysicalLosses<false>(create, input, layout);
    }

public:
    template <bool finite = false, bool targetMemory = false, class Factory, class Wait, class Map>
    static void verify(Factory&& create, Wait&& wait, Map&& map)
    {
        if constexpr (!finite) {
            verifyContract();
            verify<true>(create, wait, map);
            verify<true, true>(create, wait, map);
            OriginalTypedAttachmentDependencyProbeFixture::verify<true, true>(create, wait, map);
            OriginalTypedAttachmentDependencyProbeFixture::verify<true>(create, wait, map);
        }
        Builder::withOriginalInput([&](const auto& input, const auto& layout) {
            if constexpr (finite && !targetMemory)
                WebGpuNativeSubmissionProbeFixture::beginColdCallObservation();
            auto live = Builder::prepare<finite, targetMemory>(create, input, layout);
            if constexpr (finite && !targetMemory) {
                is(live->actor.diagnostic, "SHADER-ENTRY-RUNTIME-LENGTH-AVAILABLE-TARGET-PROFILE");
                assert(!live->actor.inputs && !live->retained.transaction && !live->custody
                    && !live->coldIssuers && !live->actor.root->originalReservedSubmissionCount()
                    && !live->actor.root->originalReservedCallbackTicketCount());
                const auto& sources = live->actor.root->sources();
                const auto& pipeline =
                    std::get<std::shared_ptr<const rhi::OriginalCommandComputePipelineSource>>(
                        sources[2]);
                OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                    for (std::uint32_t set = 0u; set < 2u; ++set) {
                        const auto& buffer =
                            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                                sources[set]);
                        OriginalTargetMemoryIntervalFixture::verifyActual(
                            *live->context->device, pipeline->originalPipeline(), buffer, set);
                    }
                });
                std::puts(
                    "BINDLESS_RHI_TYPED_NATIVE_TARGET_INTERVAL_DIAGNOSTIC_OK two-set-plus7 sealed-target-minima independent-Prototype1 forged-version-range-quota-reject hot0 query0 group-encoder-Pending0 no-length-write-proof");
                WebGpuNativeSubmissionProbeFixture::finishColdCallObservation();
                Builder::release(live);
                std::puts(
                    "BINDLESS_RHI_TYPED_NATIVE_FINITE_PROFILE_REJECT_OK two-set-plus7 selected-artifact-profile-missing deterministic-before-group-encoder-Pending quotas-zero no-fallback");
                auto candidate =
                    Builder::prepare<true, true>(create, input, layout, true, 1u, false);
                assert(candidate->actor.inputs && !candidate->actor.diagnostic
                    && !candidate->retained.transaction && !candidate->coldIssuers);
                for (const auto& source : candidate->actor.sets)
                    assert(source && source->sourceContractVersion() == 4u
                        && !rhi::OriginalCommandDescriptorSetSource::validateOriginal(source));
                Builder::release(candidate);
                std::puts(
                    "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_CANDIDATE_OK independent1 identity2 Source4 assembly4 scopes3 atomic-cancel-retry version-budget-profile-reject unique-adopt hot0 query0 quotas-zero execution-closed");
                return;
            }
            prepareIssuers(*live);
            auto forged = std::shared_ptr<const Issuers>(live->submission, live->coldIssuers.get());
            is(Issuers::recordOriginal(forged, intent), "RHI-WEBGPU-TYPED-ISSUER-OWNER");
            forged.reset();
            const auto& records = live->retained.typedNativePayload->records();
            const auto device =
                static_cast<WGPUDevice>(live->retained.nativeClosure->nativeDevice());
            auto queue = wgpuDeviceGetQueue(device);
            assert(queue);
            constexpr std::array<std::uint32_t, 4u> seed{29u, 0u, 0u, 0u}, zero{};
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            wgpuQueueWriteBuffer(queue, records[0].buffer, 0u, seed.data(), sizeof(seed));
            wgpuQueueWriteBuffer(queue, records[1].buffer, 0u, zero.data(), sizeof(zero));
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device), 3u, 0u));
            recordPending(*live);
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return live->coldIssuers->hasOriginalRecordedCommand();
            }));
            assert(!live->coldIssuers->recordingDiagnostic());
            if constexpr (targetMemory) {
                using Source = rhi::OriginalCommandDescriptorSetSource;
                const auto& source = live->actor.sets[0];
                assert(source->sourceContractVersion() == 4u);
                const auto payload = rhi_webgpu::webGpuNativePayloadFor(*source->originalObject());
                is(payload->diagnoseOriginalNativeIdentity({1u,
                       true,
                       rhi::OriginalDescriptorNativeIdentityRequest::Profile::FiniteWrite1}),
                    "RHI-ORIGINAL-SCOPED-WRITE-PROFILE");
                const auto& inputs = *live->retained.transaction->originalGraph()->originalInputs();
                const auto& canonical =
                    *inputs.originalScalars()->originalUsage()->originalSources();
                const auto found = std::find_if(inputs.descriptors().begin(),
                    inputs.descriptors().end(),
                    [&](const auto& record) {
                        const auto* binding =
                            std::get_if<rhi::OriginalCommandDescriptorBinding>(&record.arguments);
                        if (!binding || binding->sourceIndex >= canonical.sources().size())
                            return false;
                        const auto* candidate = std::get_if<std::shared_ptr<const Source>>(
                            &canonical.sources()[binding->sourceIndex]);
                        return candidate
                            && core::SharedOwnerProjection::sameOwner(*candidate, source);
                    });
                assert(found != inputs.descriptors().end());
                auto& positional =
                    const_cast<rhi::OriginalCommandInputs::OriginalDescriptor&>(*found);
                auto& descriptor =
                    std::get<rhi::OriginalCommandDescriptorBinding>(positional.arguments);
                const auto pipeline = descriptor.pipelineSourceIndex;
                descriptor.pipelineSourceIndex = 0u;
                assert(Issuers::submitOriginal(live->coldIssuers, intent));
                descriptor.pipelineSourceIndex = pipeline;
                auto& scalar = const_cast<rhi::CommandScalarArguments::DescriptorBinding&>(
                    *inputs.originalScalars()
                        ->originalArguments()
                        ->records()[positional.commandIndex]
                        .scalar.template get<rhi::CommandScalarArguments::DescriptorBinding>());
                ++scalar.set;
                assert(Issuers::submitOriginal(live->coldIssuers, intent));
                --scalar.set;
                auto& element = const_cast<Source::FrozenElement&>(source->frozenElements()[0]);
                ++element.declaration.bufferAccessBytes;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-COVERAGE");
                --element.declaration.bufferAccessBytes;
                ++element.declaration.bufferAccessOffset;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-ACCESS-COVERAGE");
                --element.declaration.bufferAccessOffset;
                const auto stages = element.declaration.stages;
                element.declaration.stages = rhi::PipelineStage::None;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-STAGES");
                element.declaration.stages = stages;
                const auto access = element.declaration.access;
                element.declaration.access = rhi::ResourceAccess::None;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-ACCESS");
                element.declaration.access = access;
                auto& declaration = const_cast<rhi::OriginalFiniteDescriptorBufferWrite&>(
                    source->frozenFiniteDeclarations()[0]);
                ++declaration.write.bufferBindingRange.sizeBytes;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-WRITE");
                --declaration.write.bufferBindingRange.sizeBytes;
                const auto owner = declaration.buffer;
                declaration.buffer = live->actor.sets[1]->frozenFiniteDeclarations()[0].buffer;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-BUFFER");
                declaration.buffer = owner;
                ++declaration.dynamicOffset;
                is(Issuers::submitOriginal(live->coldIssuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-DYNAMIC");
                --declaration.dynamicOffset;
                assert(live->retained.transaction->status() == Root::Status::Pending
                    && !live->coldIssuers->hasOriginalCompletedSubmission());
                std::puts(
                    "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_MUTATIONS_OK identity-profile positional-pipeline-set bytes-offset-stages-access frozen-range-owner-displacement Pending-reject9 before-queue");
            }
            // These are immutable-binding mutation negatives over an authentic
            // issuer-created Pending graph, never a public CPU plan application.
            auto& dependency =
                const_cast<rhi_webgpu::WebGpuOriginalTypedDependencyDecision::Record&>(
                    live->retained.dependencyDecision->records()[2]);
            const auto saved = dependency.candidate;
            dependency.candidate = rhi_webgpu::WebGpuOriginalTypedDependencyDecision::
                CandidateOperation::InitialStateLedger;
            is(Issuers::submitOriginal(live->coldIssuers, intent),
                "RHI-WEBGPU-TYPED-DEPENDENCY-BINDING");
            dependency.candidate = saved;
            const auto endpoint = *dependency.original.following;
            --dependency.original.following->contributions;
            is(Issuers::submitOriginal(live->coldIssuers, intent),
                "RHI-ORIGINAL-DEPENDENCY-PROJECTION-FROZEN-BINDING");
            dependency.original.following = endpoint;
            auto& binding =
                const_cast<rhi_webgpu::WebGpuOriginalTypedNativePayload::Record&>(records[4]);
            const auto originalGroup = binding.group;
            binding.group = records[5].group;
            is(Issuers::submitOriginal(live->coldIssuers, intent),
                "RHI-WEBGPU-TYPED-PAYLOAD-ORIGINAL-BINDING");
            binding.group = originalGroup;
            assert(!live->coldIssuers->hasOriginalCompletedSubmission()
                && live->retained.transaction->status() == Root::Status::Pending);
            Scopes::holdScopes();
            OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array{live->retained.recordingQuota, live->submission, live->callback},
                    [&]() noexcept -> const char* {
                        return Issuers::submitOriginal(live->coldIssuers, intent);
                    }));
            });
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            assert(!live->coldIssuers->hasOriginalCompletedSubmission()
                && live->retained.transaction->status() == Root::Status::Pending);
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return live->coldIssuers->hasOriginalCompletedSubmission();
            }));
            assert(live->retained.transaction->status() == Root::Status::Committed
                && live->retained.transaction->journalEmpty()
                && !live->coldIssuers->submissionDiagnostic()
                && !live->custody->pendingOriginalRecords() && !live->custody->physicalLossArmed());
            is(Issuers::submitOriginal(live->coldIssuers, intent),
                "RHI-WEBGPU-TYPED-SUBMISSION-ONE-SHOT");
            // Explicit native inspection fixture only. Neither seed writes nor
            // copies/map provide issuer/plan completion proof.
            auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = 2u * sizeof(std::uint32_t);
            desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            auto readback = wgpuDeviceCreateBuffer(device, &desc);
            assert(readback);
            rhi_webgpu::WebGpuNativeErrorScopeBatch::push(device);
            auto encoderDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
            auto encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);
            assert(encoder);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, records[0].buffer, 0u, readback, 0u, sizeof(std::uint32_t));
            wgpuCommandEncoderCopyBufferToBuffer(encoder,
                records[1].buffer,
                0u,
                readback,
                sizeof(std::uint32_t),
                sizeof(std::uint32_t));
            auto bufferDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            auto command = wgpuCommandEncoderFinish(encoder, &bufferDesc);
            assert(command);
            wgpuQueueSubmit(queue, 1u, &command);
            assert(wait(queue) && map(readback, desc.size));
            const auto* values = static_cast<const std::uint32_t*>(
                wgpuBufferGetConstMappedRange(readback, 0u, desc.size));
            assert(values && values[0] == 43u && values[1] == 36u);
            std::puts(targetMemory
                    ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_EXECUTION_VALUES_OK A43 B36 original-Recording-Submission real-scopes3-3 queue1 Pending-journal-commit full-quotas-hot0 cold-guards7 mutation3 forged-owner one-shot"
                    : "BINDLESS_RHI_TYPED_NATIVE_EXECUTION_VALUES_OK A43 B36 original-Recording-Submission real-scopes3-3 queue1 Pending-journal-commit full-quotas-hot0 cold-guards7 mutation3 forged-owner one-shot");
            wgpuBufferUnmap(readback);
            assert(arpWebGpuFixtureObserveScopes(reinterpret_cast<std::uintptr_t>(device), 3u, 0u));
            wgpuCommandBufferRelease(command);
            wgpuCommandEncoderRelease(encoder);
            wgpuBufferRelease(readback);
            wgpuQueueRelease(queue);
            Builder::release(live);
            auto cancelled = Builder::prepare<finite, targetMemory>(create, input, layout);
            prepareIssuers(*cancelled);
            recordPending(*cancelled);
            is(cancelled->custody->cancelOriginal(),
                "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING");
            assert(cancelled->retained.transaction->status() == Root::Status::Pending
                && cancelled->custody->pendingOriginalRecords() == 1u);
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return !cancelled->custody->pendingOriginalRecords();
            }));
            assert(cancelled->retained.transaction->status() == Root::Status::Abandoned
                && cancelled->retained.transaction->journalEmpty());
            Builder::release(cancelled);
            std::puts(targetMemory
                    ? "BINDLESS_RHI_TYPED_NATIVE_TARGET_FINITE_EXECUTION_CANCEL_OK authentic-Pending scopes-held retained-until-real-drain rollback-original-journal quotas-zero"
                    : "BINDLESS_RHI_TYPED_NATIVE_EXECUTION_CANCEL_OK authentic-Pending scopes-held retained-until-real-drain rollback-original-journal quotas-zero");
            if constexpr (!finite || targetMemory)
                verifyFailures<targetMemory>(create, input, layout);
        });
    }
#endif
};
} // namespace arp::test
