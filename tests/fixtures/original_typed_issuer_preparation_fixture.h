#pragma once

#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "rhi/semantics/command_lowering.h"

#include <cstdio>

namespace arp::test {
class OriginalTypedIssuerPreparationFixture final {
    using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
    using C = rhi::OriginalTypedIssuerPreparationContract;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "typed issuers expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        C::Request request{1u, true, 1u, 1u, 7u, 128u, 65536u, 65536u, 65536u};
        assert(!C::validateIntent(request));
        assert(C::additionalOwners == 3u);
        auto request2 = request;
        request2.contractVersion = 2u;
        assert(!C::validateProfile(request, 1u) && !C::validateProfile(request2, 2u));
        is(C::validateProfile(request, 2u), "RHI-TYPED-ISSUER-PREPARATION-CALLBACK-PROFILE");
        is(C::validateProfile(request2, 1u), "RHI-TYPED-ISSUER-PREPARATION-CALLBACK-PROFILE");
        for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
            auto candidate = request;
            candidate.contractVersion = version;
            for (const auto other : {1u, 2u, 3u, 4u, 5u, 6u}) {
                if (version == other)
                    assert(!C::validateProfile(candidate, other));
                else
                    is(C::validateProfile(candidate, other),
                        "RHI-TYPED-ISSUER-PREPARATION-CALLBACK-PROFILE");
            }
        }
        const auto negative = [&](auto edit, const char* id) {
            auto wrong = request;
            edit(wrong);
            is(C::validateIntent(wrong), id);
        };
        negative(
            [](auto& r) {
                r.contractVersion = 0u;
            },
            "RHI-TYPED-ISSUER-PREPARATION-VERSION");
        negative(
            [](auto& r) {
                r.contractVersion = 9u;
            },
            "RHI-TYPED-ISSUER-PREPARATION-VERSION");
        negative(
            [](auto& r) {
                r.prototypeOptIn = false;
            },
            "RHI-TYPED-ISSUER-PREPARATION-OPT-IN");
        for (const auto member : std::array{&C::Request::maxRecordingIssuers,
                 &C::Request::maxSubmissionIssuers,
                 &C::Request::maxNativeReceiptSlots})
            negative(
                [&](auto& r) {
                    --(r.*member);
                },
                "RHI-TYPED-ISSUER-PREPARATION-PARTICIPANTS");
        for (const auto member : std::array{&C::Request::maxRecordingCpuBytes,
                 &C::Request::maxSubmissionCpuBytes,
                 &C::Request::maxCallbackCpuBytes})
            negative(
                [&](auto& r) {
                    r.*member = 0u;
                },
                "RHI-TYPED-ISSUER-PREPARATION-BUDGET");
        auto general = request;
        general.contractVersion = 6u;
        general.maxRetainedOwners = 242u;
        rhi::OriginalCommandAdmissionBudget source;
        source.maxGraphOwners = 48u;
        rhi::OriginalCommandNativeLifecycleBudget lifecycle{
            3u, true, 1u, 8u, 8u, 242u, 242u, 242u, 65536u, 65536u, 65536u};
        assert(!C::validateOwners(237u, general, 242u, 242u, 242u, source, lifecycle, 46u));
        auto exact = general;
        exact.maxRetainedOwners = 240u;
        assert(!C::validateOwners(237u, exact, 242u, 242u, 242u, source, lifecycle, 46u));
        auto less = general;
        less.maxRetainedOwners = 239u;
        assert(C::validateOwners(237u, less, 242u, 242u, 242u, source, lifecycle, 46u));
        less = general;
        ++less.maxRetainedOwners;
        assert(C::validateOwners(237u, less, 242u, 242u, 242u, source, lifecycle, 46u));
        assert(C::validateOwners(237u, general, 239u, 242u, 242u, source, lifecycle, 46u));
        assert(C::validateOwners(237u, general, 242u, 239u, 242u, source, lifecycle, 46u));
        assert(C::validateOwners(237u, general, 242u, 242u, 239u, source, lifecycle, 46u));
        assert(C::validateOwners(237u, general, 242u, 242u, 242u, source, lifecycle, 49u));
        assert(C::validateOwners(237u, general, 242u, 242u, 242u, source, lifecycle, 0u));
        is(C::validateOwners(UINT32_MAX, general, 242u, 242u, 242u, source, lifecycle, 46u),
            "RHI-WEBGPU-TYPED-ISSUER-OWNER-OVERFLOW");
        for (const auto member : {&rhi::OriginalCommandNativeLifecycleBudget::maxRecordingOwners,
                 &rhi::OriginalCommandNativeLifecycleBudget::maxSubmissionOwners,
                 &rhi::OriginalCommandNativeLifecycleBudget::maxCallbackOwners}) {
            auto shortLifecycle = lifecycle;
            shortLifecycle.*member = 239u;
            assert(C::validateOwners(237u, general, 242u, 242u, 242u, source, shortLifecycle, 46u));
        }
        for (const auto old : {1u, 2u}) {
            auto legacy = lifecycle;
            legacy.contractVersion = old;
            assert(C::validateOwners(237u, general, 242u, 242u, 242u, source, legacy, 46u));
        }
        auto legacy = general;
        legacy.contractVersion = 5u;
        assert(C::validateOwners(237u, legacy, 242u, 242u, 242u, source, lifecycle, 46u));
        for (const auto backend : std::array{rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend);
            assert(decision.preparationAvailable == (backend == rhi::Backend::WebGPU));
            assert(!decision.recordingAvailable && !decision.submissionAvailable
                && decision.diagnostic);
            const auto v2 =
                arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 2u);
            assert(v2.preparationAvailable == decision.preparationAvailable
                && !v2.recordingAvailable && !v2.submissionAvailable);
            const auto v3 =
                arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 3u);
            assert(v3.preparationAvailable == decision.preparationAvailable
                && !v3.recordingAvailable && !v3.submissionAvailable);
            const auto v4 =
                arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 4u);
            assert(v4.preparationAvailable == decision.preparationAvailable
                && !v4.recordingAvailable && !v4.submissionAvailable);
            const auto v6 =
                arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 6u);
            assert(v6.preparationAvailable == decision.preparationAvailable
                && v6.validatorOnly == decision.validatorOnly && !v6.recordingAvailable
                && !v6.submissionAvailable && v6.diagnostic);
            assert(
                !arp::rhi::semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 9u)
                    .preparationAvailable);
        }
        const char* id = nullptr;
        assert(!Issuers::prepareOriginal({}, request, id));
        is(id, "RHI-WEBGPU-TYPED-CALLBACK-OWNER");
        is(Issuers::validateOriginal({}), "RHI-WEBGPU-TYPED-ISSUER-OWNER");
        std::puts(
            "BINDLESS_RHI_TYPED_ISSUER_CONTRACT_OK explicit-preparation1 existing-Recording1-Submission1 native-receipts7 owner-add3 four-execution-gates-closed no-proof");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
private:
    template <class Live> static C::Request requestFor(const Live& live)
    {
        return {live.request.contractVersion,
            true,
            1u,
            1u,
            7u,
            live.request.maxRetainedOwners,
            live.retained.recordingQuota->limitBytes(),
            live.submission->limitBytes(),
            live.callback->limitBytes()};
    }

    static void captureStart(int fail = -1)
    {
        // clang-format off
        EM_ASM({
            // clang-format off
                if (Module.arpTypedIssuerToHandle)
                    throw new Error('Nested native receipt capture');
                Module.arpTypedIssuerToHandle = Emval.toHandle;
                Module.arpTypedIssuerCaptured = [];
                Module.arpTypedIssuerFail = $0;
                Emval.toHandle = function(value) {
                    if (value && value.complete && value.userdata &&
                        Object.prototype.hasOwnProperty.call(value, 'scope') &&
                        value.phase === value.prepared && value.prepared === 0) {
                        if (Module.arpTypedIssuerCaptured.length === Module.arpTypedIssuerFail)
                            throw new Error('Authored native receipt preparation failure');
                        Module.arpTypedIssuerCaptured.push(value);
                    }
                    return Module.arpTypedIssuerToHandle.call(this, value);
                };
                // clang-format on
            },
            fail);
        // clang-format on
    }

    static void captureEnd(unsigned count, bool suppressed, bool retained = false)
    {
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTypedIssuerCaptured.length;
            // clang-format on
        })
            // clang-format on
            == static_cast<int>(count));
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTypedIssuerCaptured.every(
                    s => $0 ? s.phase === s.suppressed && !s.userdata && !s.complete
                            : s.phase === s.prepared && s.userdata && s.complete);
                // clang-format on
            },
            suppressed ? 1 : 0));
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                Emval.toHandle = Module.arpTypedIssuerToHandle;
                delete Module.arpTypedIssuerToHandle;
                delete Module.arpTypedIssuerFail;
                if ($0) {
                    Module.arpTypedIssuerLossStates = Module.arpTypedIssuerLossStates || [];
                    Module.arpTypedIssuerLossStates.push(...Module.arpTypedIssuerCaptured);
                }
                delete Module.arpTypedIssuerCaptured;
                // clang-format on
            },
            retained ? 1 : 0);
        // clang-format on
    }

public:
    template <class Rebuild>
    static void verifyGeneral(
        std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdCommand>& command,
        std::shared_ptr<const Custody>& custody,
        std::shared_ptr<const Issuers>& issuers,
        const rhi::OriginalTypedCallbackCustodyRequest& request,
        const std::shared_ptr<core::CpuMemoryQuota>& recording,
        const std::shared_ptr<core::CpuMemoryQuota>& submission,
        const std::shared_ptr<core::CpuMemoryQuota>& callback,
        Rebuild&& rebuild)
    {
        // Adapt shallow original references to the existing conformance helper;
        // no graph, quota, native capture algorithm or runtime actor is copied.
        struct Live {
            struct Retained {
                std::shared_ptr<core::CpuMemoryQuota> recordingQuota;
            } retained;

            std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedColdCommand>& command;
            std::shared_ptr<const Custody>& custody;
            std::shared_ptr<const Issuers>& coldIssuers;
            const rhi::OriginalTypedCallbackCustodyRequest& request;
            const std::shared_ptr<core::CpuMemoryQuota>& submission;
            const std::shared_ptr<core::CpuMemoryQuota>& callback;
            Rebuild& prepare;

            void rebuild()
            {
                coldIssuers.reset();
                custody.reset();
                prepare();
            }
        } live{{recording}, command, custody, issuers, request, submission, callback, rebuild};

        verifyCold(live, true);
        // clang-format off
        EM_ASM({
            // clang-format off
                if (Module.arpGeneralIssuerStates || Module.arpTypedIssuerLossStates.length !== 7)
                    throw new Error('Invalid original general receipt custody');
                Module.arpGeneralIssuerStates = Module.arpTypedIssuerLossStates;
                delete Module.arpTypedIssuerLossStates;
            // clang-format on
        });
        // clang-format on
    }

    static void verifyGeneralReceiptsRetired() noexcept
    {
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpGeneralIssuerStates.length === 7 &&
                       Module.arpGeneralIssuerStates.every(s => s.phase === s.suppressed && !s.userdata &&
                                                                !s.complete);
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                delete Module.arpGeneralIssuerStates;
            // clang-format on
        });
        // clang-format on
    }

    template <class Live>
    static std::array<std::uint64_t, 2u> verifyCold(Live& live, bool inspectFinalReceipts = false)
    {
        const char* id = nullptr;
        const auto request = requestFor(live);
        const auto prepareCustody = [&] {
            live.custody = Custody::prepareOriginal(live.command, live.request, id);
            assert(live.custody && !id);
        };
        const auto factory = [&] {
            return Issuers::prepareOriginal(live.custody, request, id);
        };
        if (!live.custody)
            prepareCustody();
        // Validate every independent owner/quota/forged refusal before allocation.
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{live.retained.recordingQuota, live.submission, live.callback},
            [&]() noexcept -> const char* {
                auto forged = std::shared_ptr<const Custody>(live.callback, live.custody.get());
                assert(!Issuers::prepareOriginal(forged, request, id));
                is(id, "RHI-WEBGPU-TYPED-CALLBACK-OWNER");
                auto wrong = request;
                wrong.maxRetainedOwners = live.command->retainedOwnerCount()
                    + rhi::OriginalTypedCallbackCustodyContract::additionalOwners(live.request)
                    + C::additionalOwners - 1u;
                assert(!Issuers::prepareOriginal(live.custody, wrong, id));
                is(id, "RHI-WEBGPU-TYPED-ISSUER-OWNER-CAPACITY");
                for (const auto member : std::array{&C::Request::maxRecordingCpuBytes,
                         &C::Request::maxSubmissionCpuBytes,
                         &C::Request::maxCallbackCpuBytes}) {
                    wrong = request;
                    --(wrong.*member);
                    assert(!Issuers::prepareOriginal(live.custody, wrong, id));
                    is(id, "RHI-WEBGPU-TYPED-ISSUER-QUOTA");
                }
                return nullptr;
            }));
        const auto rBase = live.retained.recordingQuota->liveBytes(),
                   sBase = live.submission->liveBytes(), cBase = live.callback->liveBytes();
        captureStart(3);
        assert(!factory());
        is(id, "RHI-WEBGPU-TYPED-ISSUER-RECEIPT-PREPARATION");
        captureEnd(3u, true);
        assert(live.retained.recordingQuota->liveBytes() == rBase
            && live.submission->liveBytes() == sBase && live.callback->liveBytes() == cBase
            && !live.custody->validate());
        std::uint64_t recordingBytes = 0u, submissionBytes = 0u;
        {
            captureStart();
            auto issuers = factory();
            assert(issuers && !id && issuers->issuersClosedForInspection());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{live.retained.recordingQuota, live.submission, live.callback},
                [&]() noexcept -> const char* {
                    for (unsigned field = 0u; field < 5u; ++field) {
                        // clang-format off
                        EM_ASM({
                            // clang-format off
                                const s = Module.arpTypedIssuerCaptured[0];
                                const key = ([ 'device', 'queue', 'userdata', 'complete', 'scope' ])[$0];
                                Module.arpTypedIssuerOriginalField = ({key : key, value : s[key]});
                                s[key] = key === 'scope' ? false : 0;
                                // clang-format on
                            },
                            field);
                        // clang-format on
                        is(issuers->validate(), "RHI-WEBGPU-TYPED-ISSUER-RECEIPT");
                        // clang-format off
                        EM_ASM({
                            // clang-format off
                                const original = Module.arpTypedIssuerOriginalField;
                                Module.arpTypedIssuerCaptured[0][original.key] = original.value;
                                delete Module.arpTypedIssuerOriginalField;
                            // clang-format on
                        });
                        // clang-format on
                    }
                    return issuers->validate();
                }));
            captureEnd(7u, false);
            recordingBytes = live.retained.recordingQuota->liveBytes() - rBase;
            submissionBytes = live.submission->liveBytes() - sBase;
            assert(recordingBytes && submissionBytes && live.callback->liveBytes() == cBase);
            assert(!issuers->nativeExecutionProven());
            is(issuers->executionDiagnostic(),
                request.contractVersion == 8u
                    ? "RHI-ORIGINAL-GRAPHICS-ISSUER-WEBGPU-RECORDING-MISSING"
                    : "RHI-TYPED-ISSUER-PREPARATION-WEBGPU-RECORDING-MISSING");
            assert(!factory());
            is(id, "RHI-WEBGPU-TYPED-ISSUER-DUPLICATE");
            issuers.reset();
            assert(!factory());
            is(id, "RHI-WEBGPU-TYPED-ISSUER-DUPLICATE");
            assert(!live.custody->cancelOriginal());
        }
        // Cancellation also destroys the original Submission-owned Graph.
        // Measure that exact retired baseline; issuer objects alone are not the
        // full cancellation footprint. No guessed Graph allocation overhead.
        const auto cancelledRecording = live.retained.recordingQuota->liveBytes(),
                   cancelledSubmission = live.submission->liveBytes();
        assert(cancelledRecording == rBase && cancelledSubmission < sBase);
        live.rebuild();
        prepareCustody();
        assert(live.retained.recordingQuota->liveBytes() == rBase
            && live.submission->liveBytes() == sBase);
        for (const bool submissionDomain : std::array{false, true}) {
            const auto quota = submissionDomain ? live.submission : live.retained.recordingQuota;
            const auto measured = submissionDomain ? submissionBytes : recordingBytes;
            const auto before = quota->liveBytes();
            assert(before == (submissionDomain ? sBase : rBase));
            for (const auto available : std::array<std::uint64_t, 2u>{0u, measured - 1u}) {
                {
                    core::CpuMemoryQuotaLease pressure(
                        quota, quota->limitBytes() - before - available);
                    assert(pressure.charged());
                    assert(!factory());
                    is(id, "RHI-WEBGPU-TYPED-ISSUER-ALLOCATION");
                }
                assert(quota->liveBytes() == before && !live.custody->validate());
            }
        }
        {
            core::CpuMemoryQuotaLease recordingPressure(live.retained.recordingQuota,
                live.retained.recordingQuota->limitBytes() - rBase - recordingBytes);
            core::CpuMemoryQuotaLease submissionPressure(
                live.submission, live.submission->limitBytes() - sBase - submissionBytes);
            core::CpuMemoryQuotaLease callbackPressure(
                live.callback, live.callback->limitBytes() - live.callback->liveBytes());
            assert(recordingPressure.charged() && submissionPressure.charged()
                && callbackPressure.charged());
            if (inspectFinalReceipts)
                captureStart();
            auto exact = factory();
            assert(exact && !id && exact->issuersClosedForInspection());
            live.coldIssuers = std::move(exact);
            if (inspectFinalReceipts)
                captureEnd(7u, false, true);
        }
        assert(live.retained.recordingQuota->liveBytes() == rBase + recordingBytes
            && live.submission->liveBytes() == sBase + submissionBytes
            && live.callback->liveBytes() == cBase);
        // Keep this exact-quota genuine candidate for the existing retirement
        // and deferred-suppression case. Its post-cancel quota assertions run
        // there, after the same real original observer has been retired.
        std::printf(
            "BINDLESS_RHI_TYPED_ISSUER_NATIVE_OK recording%llu submission%llu native-prepared7 listeners14 partial-failure3-cleared-retry exact-less-exhausted sticky-drop native-binding-mutation5 forged-budget-reject ordinary-closed hot0 no-encoder\n",
            static_cast<unsigned long long>(recordingBytes),
            static_cast<unsigned long long>(submissionBytes));
        return {cancelledRecording, cancelledSubmission};
    }

    template <class Live> static void prepareLoss(Live& live)
    {
        const char* id = nullptr;
        captureStart();
        live.coldIssuers = Issuers::prepareOriginal(live.custody, requestFor(live), id);
        assert(live.coldIssuers && !id && live.coldIssuers->issuersClosedForInspection());
        captureEnd(7u, false, true);
    }

    template <class Live> static void inspectLoss(const Live& live)
    {
        assert(live.coldIssuers && !live.coldIssuers->nativeExecutionProven());
        is(live.coldIssuers->validate(), "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST");
    }

    static void verifyLossReclaimed()
    {
        // clang-format off
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpTypedIssuerLossStates.length === 21 &&
                       Module.arpTypedIssuerLossStates.every(s => s.phase === s.suppressed && !s.userdata &&
                                                                  !s.complete);
            // clang-format on
        }) == 1);
        // clang-format on
        // clang-format off
        EM_ASM({
            // clang-format off
                delete Module.arpTypedIssuerLossStates;
            // clang-format on
        });
        // clang-format on
        std::puts(
            "BINDLESS_RHI_TYPED_ISSUER_LOSS_OK actual-physical-loss3 prepared-native-receipts21-suppressed userdata-cleared quotas-zero logical-Retired-Shutdown no-recording-completion");
    }
#endif
};
} // namespace arp::test
