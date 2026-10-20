#pragma once

#include "rhi/backends/webgpu/webgpu_canonical_cold_admission.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_canonical_native_closure_fixture.h"

namespace arp::test {
class WebGpuCanonicalColdAdmissionFixture final {
    using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Contract = rhi::OriginalCommandNativeColdContract;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

public:
    static Contract::Budget budget()
    {
        return {1u, true, 10u, 1u, 1u, 1u, 3u, 3u, 1u, 1u, 1u, 48u, 65536u};
    }

    static void configure(rhi::OriginalCommandAdmissionBudget& b)
    {
        b.maxPendingSubmissions = 1u;
        b.maxCallbackTickets = b.maxCallbackRegistrations = 9u;
        using D = rhi::OriginalCommandAdmissionBudget::CpuDomain;
        b.maxCpuBytes[static_cast<std::size_t>(D::Submission)] = 65536u;
        b.maxCpuBytes[static_cast<std::size_t>(D::Callback)] = 65536u;
    }

    template <class Context, class Root>
    static auto prepareOriginalAdmission(
        Context& context, const Root& root, std::uint32_t owners, const char*& diagnostic)
    {
        auto closure = rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalLossBound(root,
            *context.device,
            WebGpuCanonicalNativeClosureFixture::resourceBudget(context, 43u),
            *context.nativeLossObservation,
            diagnostic);
        auto submission = core::makeShared<core::CpuMemoryQuota>(65536u),
             callback = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto seal = Seal::prepareOriginal(root,
            *context.device,
            {1u, true, 1u, 9u, 9u, owners, owners, owners, 65536u, 65536u, 65536u},
            submission,
            callback,
            diagnostic);
        auto reservation = Reservation::prepareOriginal(
            seal, Admission::selectedRequirements().capacity, diagnostic);
        auto b = budget();
        b.maxRetainedOwners = owners;
        return Admission::prepareOriginal(closure, reservation, b, diagnostic);
    }

    static void verifyContract()
    {
        assert(!Contract::validateLegacyProfiles(1u, 1u));
        for (const auto profiles : std::array{std::pair{2u, 1u},
                 std::pair{1u, 2u},
                 std::pair{2u, 2u},
                 std::pair{0u, 1u},
                 std::pair{1u, 0u}})
            is(Contract::validateLegacyProfiles(profiles.first, profiles.second),
                "RHI-ORIGINAL-NATIVE-COLD-TYPED-PROFILE-REQUIRED");
        const auto required = Admission::selectedRequirements();
        assert(required.originalCommands == 10u && required.encoders == 1u
            && required.commandBuffers == 1u && required.renderPasses == 1u
            && required.recordingScopeCallbacks == 3u && required.submissionScopeCallbacks == 3u
            && required.queueCallbacks == 1u && required.runtimeLossCallbacks == 1u
            && required.runtimeLossRegistrations == 1u && required.existingDeviceLossCallbacks == 1u
            && required.existingDeviceLossRegistrations == 1u
            && required.capacity.pendingSubmissions == 1u && required.capacity.callbackTickets == 8u
            && required.capacity.callbackRegistrations == 8u);
        auto b = budget();
        assert(!Contract::validateRequirements(required, b));
        auto forgedRequirements = required;
        forgedRequirements.recordingScopeCallbacks = UINT32_MAX;
        is(Contract::validateRequirements(forgedRequirements, b),
            "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS-CAPACITY");
        forgedRequirements = required;
        --forgedRequirements.capacity.callbackTickets;
        is(Contract::validateRequirements(forgedRequirements, b),
            "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS-CAPACITY");
        forgedRequirements = required;
        forgedRequirements.runtimeLossRegistrations = 0u;
        is(Contract::validateRequirements(forgedRequirements, b),
            "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS");
        auto invalid = b;
        invalid.contractVersion = 0u;
        is(Contract::validateRequirements(required, invalid), "RHI-ORIGINAL-NATIVE-COLD-VERSION");
        invalid = b;
        invalid.prototypeOptIn = false;
        is(Contract::validateRequirements(required, invalid), "RHI-ORIGINAL-NATIVE-COLD-OPT-IN");
        invalid = b;
        --invalid.maxOriginalCommands;
        is(Contract::validateRequirements(required, invalid),
            "RHI-ORIGINAL-NATIVE-COLD-COMMAND-CAPACITY");
        for (auto member : {&Contract::Budget::maxEncoders,
                 &Contract::Budget::maxCommandBuffers,
                 &Contract::Budget::maxRenderPasses}) {
            invalid = b;
            --(invalid.*member);
            is(Contract::validateRequirements(required, invalid),
                "RHI-ORIGINAL-NATIVE-COLD-COMMAND-BUDGET");
        }
        for (auto member : {&Contract::Budget::maxRecordingScopeCallbacks,
                 &Contract::Budget::maxSubmissionScopeCallbacks}) {
            invalid = b;
            --(invalid.*member);
            is(Contract::validateRequirements(required, invalid),
                "RHI-ORIGINAL-NATIVE-COLD-CALLBACK-CAPACITY");
        }
        for (auto member : {&Contract::Budget::maxQueueCallbacks,
                 &Contract::Budget::maxRuntimeLossCallbacks,
                 &Contract::Budget::maxRuntimeLossRegistrations}) {
            invalid = b;
            --(invalid.*member);
            is(Contract::validateRequirements(required, invalid),
                "RHI-ORIGINAL-NATIVE-COLD-CALLBACK-BUDGET");
        }
        auto actual = required.capacity;
        assert(!Contract::compareCapacity(required, actual));
        for (auto member : {&rhi::OriginalCommandLifecycleCapacityRequest::pendingSubmissions,
                 &rhi::OriginalCommandLifecycleCapacityRequest::callbackTickets,
                 &rhi::OriginalCommandLifecycleCapacityRequest::callbackRegistrations}) {
            actual = required.capacity;
            --(actual.*member);
            is(Contract::compareCapacity(required, actual),
                "RHI-ORIGINAL-NATIVE-COLD-RESERVATION-REQUIREMENTS");
            actual = required.capacity;
            ++(actual.*member);
            is(Contract::compareCapacity(required, actual),
                "RHI-ORIGINAL-NATIVE-COLD-RESERVATION-REQUIREMENTS");
        }
        actual = required.capacity;
        actual.prototypeOptIn = false;
        assert(Contract::compareCapacity(required, actual));
        actual = required.capacity;
        actual.contractVersion = 2u;
        assert(Contract::compareCapacity(required, actual));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto result =
                arp::rhi::semantics::OriginalCommandNativeColdLowering::lowering(backend);
            assert(
                !result.sealedIssuerAvailable && !result.executionAvailable && result.diagnostic);
            const auto upload =
                arp::rhi::semantics::OriginalCommandNativeColdLowering::uploadLowering(backend);
            assert(!upload.recordingAvailable && !upload.executionAvailable && upload.diagnostic
                && upload.coldJoinAvailable == (backend == rhi::Backend::WebGPU));
            const auto typed =
                arp::rhi::semantics::OriginalCommandNativeColdLowering::typedLowering(backend);
            assert(!typed.coldJoinAvailable && !typed.recordingAvailable
                && !typed.executionAvailable && typed.diagnostic);
            assert((typed.disposition
                       == arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        auto uploads = required;
        uploads.contractVersion = 3u;
        uploads.renderPasses = 0u;
        uploads.bufferUploads = 2u;
        uploads.nativeCopyCommands = 2u;
        uploads.bufferUploadBytes = uploads.totalUploadBytes = 32u;
        auto uploadBudget = b;
        uploadBudget.contractVersion = 3u;
        uploadBudget.maxRenderPasses = 0u;
        uploadBudget.maxBufferUploads = uploadBudget.maxNativeCopyCommands = 2u;
        uploadBudget.maxBufferUploadBytes = uploadBudget.maxTotalUploadBytes = 32u;
        assert(!Contract::validateRequirements(uploads, uploadBudget));
        for (const auto version : {1u, 2u}) {
            auto forbidden = uploadBudget;
            forbidden.contractVersion = version;
            is(Contract::validateIntent(forbidden), "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-PROFILE");
        }
        for (auto member : {&Contract::Budget::maxBufferUploads,
                 &Contract::Budget::maxNativeCopyCommands,
                 &Contract::Budget::maxBufferUploadBytes,
                 &Contract::Budget::maxTotalUploadBytes}) {
            auto less = uploadBudget;
            --(less.*member);
            assert(Contract::validateRequirements(uploads, less));
        }
        auto inconsistent = uploads;
        --inconsistent.totalUploadBytes;
        is(Contract::validateRequirements(inconsistent, uploadBudget),
            "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-REQUIREMENTS");
        inconsistent = required;
        inconsistent.bufferUploads = 1u;
        is(Contract::validateRequirements(inconsistent, b),
            "RHI-ORIGINAL-NATIVE-COLD-UPLOAD-REQUIREMENTS-PROFILE");
        std::puts(
            "BINDLESS_RHI_CANONICAL_UPLOAD_COLD_CONTRACT_OK version3 explicit-upload-work scopes-and-loss-derived legacy1-2-profile-closed no-recording-or-execution four-backends");
        std::puts(
            "BINDLESS_RHI_CANONICAL_COLD_REQUIREMENTS_CONTRACT_OK original10 encoder1 command-buffer1 render-pass1 scopes3-3 queue1 runtime-loss1 existing-loss1-separate exact-request1-8-8 four-native-gates-closed");
    }

    template <class Factory> static void verify(Factory&& create)
    {
        verifyContract();
        OriginalCommandAccessFixture::withOriginalPlan(create, [&](auto& context, auto& root) {
            assert(!Admission::inspectOriginalPlan(root));
            const char* diagnostic = nullptr;
            assert(!Admission::prepareOriginal({}, {}, budget(), diagnostic));
            is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-OWNER");
            assert(!root->applyOriginalPrototypePlan());
            is(Admission::inspectOriginalPlan(root), "RHI-WEBGPU-CANONICAL-COLD-PLAN-STATUS");
            root->abandonOriginalPendingOwnership();
        });
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        OriginalCommandAccessFixture::withConfiguredOriginalPlan(
            create,
            configure,
            [&](auto& context, auto& root) {
                const char* diagnostic = nullptr;
                auto b = budget();
                auto closure =
                    rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalLossBound(root,
                        *context.device,
                        WebGpuCanonicalNativeClosureFixture::resourceBudget(context, 43u),
                        *context.nativeLossObservation,
                        diagnostic);
                assert(closure);
                auto unbound = rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginal(root,
                    *context.device,
                    WebGpuCanonicalNativeClosureFixture::resourceBudget(context, 43u),
                    diagnostic);
                assert(unbound);
                rhi::OriginalCommandNativeLifecycleBudget lifecycle{
                    1u, true, 1u, 9u, 9u, 48u, 48u, 48u, 65536u, 65536u, 65536u};
                auto submission = core::makeShared<core::CpuMemoryQuota>(65536u),
                     callback = core::makeShared<core::CpuMemoryQuota>(65536u);
                auto seal = Seal::prepareOriginal(
                    root, *context.device, lifecycle, submission, callback, diagnostic);
                assert(seal);
                auto demand = Admission::selectedRequirements().capacity;
                for (auto member : {&rhi::OriginalCommandLifecycleCapacityRequest::callbackTickets,
                         &rhi::OriginalCommandLifecycleCapacityRequest::callbackRegistrations})
                    for (const auto difference : {-1, 1}) {
                        auto wrong = demand;
                        wrong.*member += difference;
                        auto reservation = Reservation::prepareOriginal(seal, wrong, diagnostic);
                        assert(reservation);
                        assert(!Admission::prepareOriginal(closure, reservation, b, diagnostic));
                        is(diagnostic, "RHI-ORIGINAL-NATIVE-COLD-RESERVATION-REQUIREMENTS");
                    }
                auto reservation = Reservation::prepareOriginal(seal, demand, diagnostic);
                assert(reservation);
                const auto source = root->originalGraph()
                                        ->originalInputs()
                                        ->originalScalars()
                                        ->originalUsage()
                                        ->originalSources();
                assert(source->originalReservedSubmissionCount() == 1u
                    && source->originalReservedCallbackTicketCount() == 8u
                    && source->originalReservedCallbackRegistrationCount() == 8u);
                assert(!Admission::prepareOriginal(unbound, reservation, b, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-CLOSURE-LOSS-UNBOUND");
                unbound.reset();
                const auto make = [&] {
                    return Admission::prepareOriginal(closure, reservation, b, diagnostic);
                };
                CpuQuotaFactoryFixture pressure(context.quotas[5]);
                const auto peak = pressure.measure(make);
                pressure.verify(peak, make);
                auto wrong = b;
                --wrong.maxRetainedOwners;
                assert(!Admission::prepareOriginal(closure, reservation, wrong, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-OWNER-CAPACITY");
                wrong = b;
                --wrong.maxCpuMetadataBytes;
                assert(!Admission::prepareOriginal(closure, reservation, wrong, diagnostic));
                is(diagnostic, "RHI-WEBGPU-CANONICAL-COLD-QUOTA");
                auto admission = make();
                assert(admission && admission->retainedOwnerCount() == 48u);
                auto forged = std::shared_ptr<const Admission>(callback, admission.get());
                is(Admission::validateOriginal(forged), "RHI-WEBGPU-CANONICAL-COLD-OWNER");
                forged.reset();
                assert(core::SharedOwnerProjection::sameOwner(admission->originalRoot(), root)
                    && core::SharedOwnerProjection::sameOwner(admission->originalClosure(), closure)
                    && core::SharedOwnerProjection::sameOwner(
                        admission->originalReservation(), reservation)
                    && core::SharedOwnerProjection::sameOwner(
                        admission->originalCallbackQuota(), callback)
                    && core::SharedOwnerProjection::sameOwner(
                        admission->originalSubmissionQuota(), submission));
                const std::array quotas{context.quotas[0],
                    context.quotas[1],
                    context.quotas[2],
                    context.quotas[3],
                    context.quotas[4],
                    context.quotas[5],
                    context.nativeLossQuota,
                    submission,
                    callback};
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    if (const auto* id = Admission::validateOriginal(admission))
                        return id;
                    if (admission->nativeExecutionProven() || !admission->executionDiagnostic())
                        return "TEST-COLD-EXECUTION";
                    return root->journalEmpty() && root->hasOriginalRecordingSeal()
                        ? nullptr
                        : "TEST-COLD-ROOT";
                }));
                callback->setLimitBytes(65535u);
                is(admission->validate(), "RHI-ORIGINAL-NATIVE-LIFECYCLE-QUOTA");
                callback->setLimitBytes(65536u);
                std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = root;
                root.reset();
                closure.reset();
                reservation.reset();
                seal.reset();
                context.nativeLossObservation.reset();
                assert(!retained.expired() && !admission->validate());
                context.device.reset();
                is(admission->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                assert(source->originalReservedCallbackTicketCount() == 8u
                    && source->originalReservedCallbackRegistrationCount() == 8u);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                    admission.reset();
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
                    "BINDLESS_RHI_CANONICAL_COLD_ADMISSION_OK owner48 cold%llu exact-root-closure-seal-reservation requirements1-8-8 quota-independent mismatch-under-over unbound-forged-budget-refusal exact-less-exhausted caller-drop shutdown-retains-counts hot-allocation0 no-native-callback-issuance\n",
                    static_cast<unsigned long long>(peak));
            },
            5u,
            true);
#endif
    }
};
} // namespace arp::test
