#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_callback_custody.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_canonical_cold_command_fixture.h"

namespace arp::test {
class WebGpuCanonicalCallbackCustodyFixture final {
    using Custody = rhi_webgpu::WebGpuCanonicalCallbackCustody;
    using Command = rhi_webgpu::WebGpuCanonicalColdCommand;
    using Contract = rhi::OriginalCommandCallbackCustodyContract;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    class Live final {
    public:
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 9> quotas;
        core::UniquePtr<rhi::GpuDevice> device;
        std::weak_ptr<rhi::OriginalCommandStateTransaction> root;
        std::shared_ptr<rhi::OriginalCommandStateTransaction> callerRoot;
        std::shared_ptr<const Custody> custody;
    };

    static inline std::array<core::UniquePtr<Live>, 3> live_;

    static void remember(const std::shared_ptr<const Custody>& custody, unsigned index)
    {
        assert(custody->originalLossStateForInspection());
        EM_ASM(
            {
                // clang-format off
                    Module.arpCanonicalCallbackLossStates = Module.arpCanonicalCallbackLossStates || new Array(5);
                    if (Module.arpCanonicalCallbackLossStates[$1])
                        throw new Error('Duplicate canonical callback loss evidence');
                    Module.arpCanonicalCallbackLossStates[$1] = Emval.toValue($0);
                // clang-format on
            },
            custody->originalLossStateForInspection(),
            index);
    }
#endif
public:
    static Custody::Budget budget() { return {1u, true, 1u, 8u, 62u, 65536u, 65536u}; }

    static void verifyContract()
    {
        auto b = budget();
        assert(!Contract::validateIntent(b));
        b.contractVersion = 0u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-CALLBACK-CUSTODY-VERSION");
        b = budget();
        b.prototypeOptIn = false;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-CALLBACK-CUSTODY-OPT-IN");
        for (auto member :
            {&Custody::Budget::maxPendingRecords, &Custody::Budget::maxCallbackSlots})
            for (const int difference : {-1, 1}) {
                b = budget();
                b.*member += difference;
                is(Contract::validateIntent(b), "RHI-ORIGINAL-CALLBACK-CUSTODY-COUNT");
            }
        b = budget();
        b.maxCpuCallbackBytes = 0u;
        is(Contract::validateIntent(b), "RHI-ORIGINAL-CALLBACK-CUSTODY-BUDGET");
        assert(Contract::additionalOwnerCount == 11u);
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandCallbackCustodyLowering::lowering(backend);
            assert(d.diagnostic && !d.recordingAvailable && !d.executionAvailable);
            assert(d.nativeLossObservationAvailable == (backend == rhi::Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_CANONICAL_CALLBACK_CONTRACT_OK slots8 owner-add11 exact-submission-callback-quotas loss1-independent four-record-submit-gates-closed");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(
            create,
            WebGpuCanonicalColdAdmissionFixture::configure,
            [&](auto& context, auto& root) {
                const char* diagnostic = nullptr;
                const auto b = budget();
                const auto graph = root->originalGraph();
                const auto source =
                    graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
                std::shared_ptr<const Command> command;
                std::shared_ptr<core::CpuMemoryQuota> submission, callback;
                const auto prepareCommand = [&] {
                    auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                        context, root, 62u, diagnostic);
                    assert(admission);
                    submission = admission->originalSubmissionQuota();
                    callback = admission->originalCallbackQuota();
                    command = Command::prepareOriginal(
                        admission, {1u, true, 1u, 62u, 65536u}, diagnostic);
                    assert(command);
                };
                const auto rebuild = [&] {
                    command.reset();
                    root.reset();
                    assert(!source->originalStateTransactionCount());
                    root = OriginalCommandAccessFixture::prepareOriginalPlan(
                        graph, *context.device, context.quotas[5], diagnostic);
                    assert(root);
                    prepareCommand();
                };
                prepareCommand();
                const auto make = [&] {
                    return Custody::prepareOriginal(command, b, diagnostic);
                };
                std::uint64_t submissionPeak = 0u, callbackPeak = 0u;
                {
                    core::CpuMemoryQuotaLease sMarker(
                        submission, submission->peakBytes() - submission->liveBytes() + 1u);
                    core::CpuMemoryQuotaLease cMarker(
                        callback, callback->peakBytes() - callback->liveBytes() + 1u);
                    assert(sMarker.charged() && cMarker.charged());
                    const auto sBase = submission->liveBytes(), cBase = callback->liveBytes();
                    auto custody = make();
                    assert(custody && !custody->validate());
                    submissionPeak = submission->peakBytes() - sBase;
                    callbackPeak = callback->peakBytes() - cBase;
                }
                assert(submissionPeak && callbackPeak);
                for (const bool callbackDomain : {false, true})
                    for (const int difference : {0, -1}) {
                        rebuild();
                        auto quota = callbackDomain ? callback : submission;
                        const auto peak = callbackDomain ? callbackPeak : submissionPeak;
                        const auto baseline = quota->liveBytes();
                        {
                            core::CpuMemoryQuotaLease pressure(
                                quota, quota->limitBytes() - baseline - peak - difference);
                            assert(pressure.charged());
                            auto custody = make();
                            assert(bool(custody) == (difference == 0));
                            if (!custody)
                                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-ALLOCATION");
                        }
                        assert(quota->liveBytes() == baseline);
                        if (difference == -1) {
                            auto retry = make();
                            assert(retry);
                        } // Failed cold allocations publish no claim.
                    }
                for (const bool callbackDomain : {false, true}) {
                    rebuild();
                    auto quota = callbackDomain ? callback : submission;
                    core::CpuMemoryQuotaLease pressure(
                        quota, quota->limitBytes() - quota->liveBytes());
                    assert(pressure.charged());
                    assert(!make());
                    is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-ALLOCATION");
                }
                rebuild();
                auto invalid = b;
                --invalid.maxRetainedOwners;
                assert(!Custody::prepareOriginal(command, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER-CAPACITY");
                invalid = b;
                --invalid.maxCpuSubmissionBytes;
                assert(!Custody::prepareOriginal(command, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-QUOTA");
                invalid = b;
                --invalid.maxCpuCallbackBytes;
                assert(!Custody::prepareOriginal(command, invalid, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-QUOTA");
                EM_ASM({
                    // clang-format off
                        Module.arpCanonicalCallbackBindingFailureReceipt = 0;
                        Module.arpCanonicalCallbackThen = Module.preinitializedWebGPUDevice.lost.then;
                        Module.preinitializedWebGPUDevice.lost.then = function(callback) {
                            Module.arpCanonicalCallbackThen.call(this, function(info) {
                                ++Module.arpCanonicalCallbackBindingFailureReceipt;
                                callback(info);
                            });
                            throw new Error('Authored canonical loss binding failure after actual registration');
                        };
                    // clang-format on
                });
                const auto sBase = submission->liveBytes(), cBase = callback->liveBytes();
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-BINDING");
                assert(submission->liveBytes() == sBase && callback->liveBytes() == cBase);
                EM_ASM({
                    // clang-format off
                        Module.preinitializedWebGPUDevice.lost.then = Module.arpCanonicalCallbackThen;
                        delete Module.arpCanonicalCallbackThen;
                    // clang-format on
                });
                auto custody = make();
                assert(custody && !custody->validate());
                remember(custody, 0u);
                auto forged = std::shared_ptr<const Custody>(callback, custody.get());
                is(Custody::validateOriginal(forged), "RHI-WEBGPU-CANONICAL-CALLBACK-OWNER");
                forged.reset();
                auto forgedCommand = std::shared_ptr<const Command>(callback, command.get());
                assert(!Custody::prepareOriginal(forgedCommand, b, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-COMMAND-OWNER");
                forgedCommand.reset();
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-DUPLICATE");
                std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    submission,
                    callback};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    assert(custody->originalNativeLossArmed()
                        && !custody->originalNativeLossDelivered()
                        && custody->pendingOriginalRecords() == 1u);
                    assert(!custody->nativeExecutionProven() && custody->executionDiagnostic());
                    return Custody::validateOriginal(custody);
                }));
                assert(!custody->cancelOriginal());
                is(custody->validate(), "RHI-WEBGPU-CANONICAL-CALLBACK-CANCELLED");
                assert(!custody->originalNativeLossArmed() && !custody->pendingOriginalRecords());
                is(custody->cancelOriginal(), "RHI-WEBGPU-CANONICAL-CALLBACK-RETIRED");
                custody.reset();
                assert(!make());
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CALLBACK-DUPLICATE");
                rebuild();
                custody = make();
                assert(custody);
                remember(custody, 1u);
                quotas[7] = submission;
                quotas[8] = callback;
                std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = root;
                root.reset();
                command.reset();
                context.nativeLossObservation.reset();
                context.device.reset();
                assert(!retained.expired());
                is(custody->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                assert(custody->originalNativeLossArmed() && !custody->originalNativeLossDelivered()
                    && custody->pendingOriginalRecords() == 1u);
                assert(source->originalReservedSubmissionCount() == 1u
                    && source->originalReservedCallbackTicketCount() == 8u
                    && source->originalReservedCallbackRegistrationCount() == 8u);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    custody.reset();
                    assert(retained.expired());
                    retained.reset();
                    return static_cast<const char*>(nullptr);
                }));
                assert(!source->originalStateTransactionCount()
                    && !source->originalReservedSubmissionCount()
                    && !source->originalReservedCallbackTicketCount()
                    && !source->originalReservedCallbackRegistrationCount());
                assert(!context.quotas[5]->liveBytes() && !submission->liveBytes()
                    && !callback->liveBytes() && !context.nativeLossQuota->liveBytes());
                std::printf(
                    "BINDLESS_RHI_CANONICAL_CALLBACK_OK owner62 submission%llu callback%llu slots8 loss1-native exact-less-exhausted failure-unwind-no-claim forged-budget-duplicate-refuse caller-drop shutdown-not-loss pre-record-native-suppression hot-allocation0 execution-closed\n",
                    static_cast<unsigned long long>(submissionPeak),
                    static_cast<unsigned long long>(callbackPeak));
            },
            5u,
            true);
        for (unsigned index = 0u; index < 3u; ++index)
            OriginalCommandAccessFixture::withConfiguredOriginalPlan(
                create,
                WebGpuCanonicalColdAdmissionFixture::configure,
                [&](auto& context, auto& root) {
                    const char* diagnostic = nullptr;
                    auto admission = WebGpuCanonicalColdAdmissionFixture::prepareOriginalAdmission(
                        context, root, 62u, diagnostic);
                    assert(admission);
                    auto command = Command::prepareOriginal(
                        admission, {1u, true, 1u, 62u, 65536u}, diagnostic);
                    assert(command);
                    auto custody = Custody::prepareOriginal(command, budget(), diagnostic);
                    assert(custody);
                    remember(custody, index + 2u);
                    auto live = core::makeUnique<Live>();
                    live->quotas = {context.quotas[0],
                        context.quotas[1],
                        context.quotas[2],
                        context.quotas[3],
                        context.quotas[4],
                        context.quotas[5],
                        context.nativeLossQuota,
                        admission->originalSubmissionQuota(),
                        admission->originalCallbackQuota()};
                    live->root = root;
                    live->custody = std::move(custody);
                    if (index == 2u) {
                        // Test-only native suppression failure. Keep the actual promise
                        // and original Emval state untouched: only the detach lookup
                        // is refused until restoration, then genuine device loss must
                        // release the runtime's bounded retention cycle.
                        EM_ASM(
                            {
                                // clang-format off
                                    Module.arpCanonicalDetachToValue = Emval.toValue;
                                    Module.arpCanonicalDetachHandle = $0;
                                    Emval.toValue = function(handle) {
                                        if (handle === Module.arpCanonicalDetachHandle)
                                            return {phase : 0};
                                        return Module.arpCanonicalDetachToValue(handle);
                                    };
                                // clang-format on
                            },
                            live->custody->originalLossStateForInspection());
                        is(live->custody->cancelOriginal(),
                            "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-DETACH");
                        is(live->custody->validate(),
                            "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-DETACH");
                        assert(live->custody->originalNativeLossArmed()
                            && live->custody->pendingOriginalRecords() == 1u);
                        live->custody.reset();
                        assert(!live->root.expired());
                        EM_ASM({
                            // clang-format off
                                Emval.toValue = Module.arpCanonicalDetachToValue;
                                delete Module.arpCanonicalDetachToValue;
                                delete Module.arpCanonicalDetachHandle;
                            // clang-format on
                        });
                    }
                    if (index == 0u) {
                        live->device = std::move(context.device);
                        live->callerRoot = root;
                    } else
                        context.device.reset();
                    live_[index] = std::move(live);
                },
                5u,
                true);
#else
        (void)create;
#endif
    }

    static void verifyActualLoss()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        assert(WebGpuAsyncWaitFixture::until([] {
            return EM_ASM_INT({
                // clang-format off
                    return Module.arpCanonicalCallbackLossStates.every(function(s) {
                        return s && s.observed;
                    });
                // clang-format on
            }) != 0;
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpCanonicalCallbackLossStates.every(function(s, i) {
                    return !s.userdata && !s.callback && s.deliveries === (i < 2 ? 0 : 1) &&
                           s.phase === (i < 2 ? 3 : 2);
                });
            // clang-format on
        }) != 0);
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpCanonicalCallbackBindingFailureReceipt;
            // clang-format on
        }) == 1);
        for (auto& live : live_) {
            assert(live);
            if (live->callerRoot) {
                using Root = rhi::OriginalCommandStateTransaction;
                assert(live->callerRoot->status() == Root::Status::Invalidated
                    && live->callerRoot->journalEmpty());
                is(live->callerRoot->validate(), "RHI-ORIGINAL-TRANSACTION-INVALIDATED");
                is(live->callerRoot->applyOriginalPrototypePlan(),
                    "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                is(live->callerRoot->commitOriginalPendingOwnership(),
                    "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
                live->callerRoot.reset();
            }
            assert(live->root.expired());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(live->quotas, [&]() noexcept {
                if (live->custody) {
                    assert(live->custody->originalNativeLossDelivered()
                        && !live->custody->originalNativeLossArmed());
                    assert(!live->custody->pendingOriginalRecords()
                        && !live->custody->nativeExecutionProven());
                    is(live->custody->validate(), "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-LOST");
                }
                live->custody.reset();
                live->device.reset();
                live->root.reset();
                return static_cast<const char*>(nullptr);
            }));
            for (const auto& quota : live->quotas)
                assert(!quota->liveBytes());
            live.reset();
        }
        EM_ASM({
            // clang-format off
                delete Module.arpCanonicalCallbackLossStates;
                delete Module.arpCanonicalCallbackBindingFailureReceipt;
            // clang-format on
        });
        std::puts(
            "BINDLESS_RHI_CANONICAL_CALLBACK_LOSS_OK actual-browser-loss live-and-shutdown-facades independent-runtime-delivery3 suppression-failure-quarantine-released detached-delivery0 native-userdata-cleared same-root-invalidated-and-released all-nine-quotas-zero hot-allocation0 no-queue-completion-proof");
#endif
    }
};
} // namespace arp::test
