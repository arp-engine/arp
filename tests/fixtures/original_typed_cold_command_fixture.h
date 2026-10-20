#pragma once

#include "original_typed_cold_admission_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_command.h"
#include "rhi/semantics/command_lowering.h"

namespace arp::test {
class OriginalTypedColdCommandFixture final {
    using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
    using C = rhi::OriginalTypedNativeColdCommandContract;
    using Admission = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Root = rhi::OriginalCommandStateTransaction;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "typed shell expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void verifyContract()
    {
        C::Request request;
        is(C::validateIntent(request), "RHI-ORIGINAL-TYPED-COLD-COMMAND-VERSION");
        request.contractVersion = 1u;
        is(C::validateIntent(request), "RHI-ORIGINAL-TYPED-COLD-COMMAND-OPT-IN");
        request.prototypeOptIn = true;
        is(C::validateIntent(request), "RHI-ORIGINAL-TYPED-COLD-COMMAND-COUNT");
        request.maxCommandPayloads = 1u;
        is(C::validateIntent(request), "RHI-ORIGINAL-TYPED-COLD-COMMAND-BUDGET");
        request = {1u, true, 1u, 13u, 65536u};
        assert(!C::validateIntent(request));
        for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u}) {
            auto paired = request;
            paired.contractVersion = version;
            assert(!C::validateProfile(paired, version));
            is(C::validateProfile(paired, version == 1u ? 2u : 1u),
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-ADMISSION-PROFILE");
            paired.prototypeOptIn = false;
            is(C::validateProfile(paired, version), "RHI-ORIGINAL-TYPED-COLD-COMMAND-OPT-IN");
        }
        auto unknown = request;
        unknown.contractVersion = 9u;
        is(C::validateIntent(unknown), "RHI-ORIGINAL-TYPED-COLD-COMMAND-VERSION");
        auto source = rhi::OriginalCommandAdmissionBudget{};
        source.maxGraphOwners = 13u;
        auto lifecycle = rhi::OriginalCommandNativeLifecycleBudget{};
        lifecycle.maxRecordingOwners = lifecycle.maxSubmissionOwners = lifecycle.maxCallbackOwners =
            13u;
        assert(!C::validateOwners(10u, request, source, lifecycle));
        auto shortRequest = request;
        --shortRequest.maxRetainedOwners;
        is(C::validateOwners(10u, shortRequest, source, lifecycle),
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        --source.maxGraphOwners;
        is(C::validateOwners(10u, request, source, lifecycle),
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        ++source.maxGraphOwners;
        for (auto member :
            std::array{&rhi::OriginalCommandNativeLifecycleBudget::maxRecordingOwners,
                &rhi::OriginalCommandNativeLifecycleBudget::maxSubmissionOwners,
                &rhi::OriginalCommandNativeLifecycleBudget::maxCallbackOwners}) {
            auto shortLifecycle = lifecycle;
            --(shortLifecycle.*member);
            is(C::validateOwners(10u, request, source, shortLifecycle),
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        }
        is(C::validateOwners(UINT32_MAX, request, source, lifecycle),
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-OVERFLOW");
        auto general = request;
        general.contractVersion = 6u;
        general.maxRetainedOwners = 237u;
        source.maxGraphOwners = 48u;
        lifecycle.contractVersion = 3u;
        lifecycle.prototypeOptIn = true;
        lifecycle.maxRecordingOwners = lifecycle.maxSubmissionOwners = lifecycle.maxCallbackOwners =
            237u;
        assert(!C::validateOwners(223u, general, source, lifecycle, 237u, 46u));
        auto shortGeneral = general;
        shortGeneral.maxRetainedOwners = 225u;
        is(C::validateOwners(223u, shortGeneral, source, lifecycle, 237u, 46u),
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        shortGeneral = general;
        ++shortGeneral.maxRetainedOwners;
        is(C::validateOwners(223u, shortGeneral, source, lifecycle, 237u, 46u),
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        assert(C::validateOwners(223u, general, source, lifecycle, 237u, 49u));
        assert(C::validateOwners(223u, general, source, lifecycle));
        assert(C::validateOwners(UINT32_MAX, general, source, lifecycle, 237u, 46u));
        for (auto member :
            std::array{&rhi::OriginalCommandNativeLifecycleBudget::maxRecordingOwners,
                &rhi::OriginalCommandNativeLifecycleBudget::maxSubmissionOwners,
                &rhi::OriginalCommandNativeLifecycleBudget::maxCallbackOwners}) {
            auto shortLifecycle = lifecycle;
            shortLifecycle.*member = 225u;
            assert(C::validateOwners(223u, general, source, shortLifecycle, 237u, 46u));
        }
        for (const auto old : {1u, 2u}) {
            auto oldLifecycle = lifecycle;
            oldLifecycle.contractVersion = old;
            is(C::validateOwners(223u, general, source, oldLifecycle, 237u, 46u),
                "RHI-ORIGINAL-TYPED-COLD-COMMAND-LIFECYCLE-PROFILE");
        }
        for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u})
            for (auto backend : std::array{rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU}) {
                const auto decision =
                    arp::rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(
                        backend, version);
                assert(decision.connectionAvailable == (backend == rhi::Backend::WebGPU));
                assert(!decision.recordingAvailable && !decision.executionAvailable
                    && decision.diagnostic);
            }
        const auto graphics = arp::rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(
            rhi::Backend::WebGPU, 8u);
        assert(graphics.connectionAvailable && !graphics.recordingAvailable
            && !graphics.executionAvailable);
        assert(arp::rhi::semantics::OriginalTypedNativeColdCommandLowering::lowering(
            static_cast<rhi::Backend>(255u))
                .diagnostic);
        const char* id = nullptr;
        assert(!Command::prepareOriginal({}, request, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-OWNER");
        is(Command::validateOriginal({}), "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
        std::puts(
            "BINDLESS_RHI_TYPED_COLD_COMMAND_CONTRACT_OK independent1 command1 owner-add3 exact-Source-lifecycle-ceilings overflow four-execution-gates-closed no-live-authority");
    }
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
    static std::shared_ptr<const Command> verifyComposed(
        const std::shared_ptr<const Admission>& admission, bool graphics = false)
    {
        const auto& root = admission->originalRoot();
        const auto recording = root->originalMetadataQuota();
        const auto& seal = admission->originalReservation()->originalSeal();
        const auto callback = seal->originalCallbackQuota(),
                   submission = seal->originalSubmissionQuota();
        const auto commandOwners = admission->retainedOwnerCount() + C::additionalOwners;
        // The shell charges its own minimum, but must carry the predeclared
        // complete graph ceiling through callback/result/issuer preparation.
        const C::Request request{graphics ? 8u : 5u,
            true,
            1u,
            admission->originalRequest().maxRetainedOwners,
            recording->limitBytes()};
        assert(admission->originalRequest().contractVersion == request.contractVersion);
        const char* id = nullptr;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        for (std::uint32_t version = 1u; version < (graphics ? 8u : 5u); ++version) {
            auto legacy = request;
            legacy.contractVersion = version;
            assert(!Command::prepareOriginal(admission, legacy, id));
            is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-ADMISSION-PROFILE");
        }
        auto less = request;
        less.maxRetainedOwners = commandOwners - 1u;
        assert(!Command::prepareOriginal(admission, less, id));
        is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        if (graphics) {
            auto enlarged = request;
            ++enlarged.maxRetainedOwners;
            assert(!Command::prepareOriginal(admission, enlarged, id));
            is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        }
        less = request;
        less.prototypeOptIn = false;
        assert(!Command::prepareOriginal(admission, less, id));
        is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-OPT-IN");
        less = request;
        --less.maxCpuMetadataBytes;
        assert(!Command::prepareOriginal(admission, less, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA");
        less = request;
        less.maxCommandPayloads = 2u;
        assert(!Command::prepareOriginal(admission, less, id));
        is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-COUNT");
        auto forgedAdmission = std::shared_ptr<const Admission>(callback, admission.get());
        assert(!Command::prepareOriginal(forgedAdmission, request, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-OWNER");
        forgedAdmission.reset();
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        const auto factory = [&] {
            return Command::prepareOriginal(admission, request, id);
        };
        {
            core::CpuMemoryQuotaLease exhausted(
                recording, recording->limitBytes() - recording->liveBytes());
            assert(exhausted.charged() && !factory());
            is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-ALLOCATION");
        }
        auto command = factory();
        assert(command && !id && !command->validate());
        assert(command->retainedOwnerCount() == commandOwners
            && command->originalRequest().maxRetainedOwners
                == admission->originalRequest().maxRetainedOwners
            && !command->nativeExecutionProven());
        is(command->executionDiagnostic(),
            graphics ? "RHI-ORIGINAL-GRAPHICS-COLD-COMMAND-WEBGPU-CONNECTION-ONLY"
                     : "RHI-WEBGPU-TYPED-COLD-COMMAND-LIVE-EXECUTOR-MISSING");
        assert(!factory());
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        if (graphics) {
            auto peer = Admission::prepareOriginal(admission->originalPayload(),
                admission->originalReservation(),
                admission->originalRequest(),
                id);
            assert(peer && !id && !Command::prepareOriginal(peer, request, id));
            is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
            peer.reset();
        }
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const Command>(callback, command.get());
                    is(Command::validateOriginal(forged), "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
                    const auto& sources = root->originalGraph()
                                              ->originalInputs()
                                              ->originalScalars()
                                              ->originalUsage()
                                              ->originalSources();
                    for (const auto& entry : sources->sources())
                        std::visit(
                            [&](const auto& object) {
                                using T = std::remove_const_t<
                                    typename std::decay_t<decltype(object)>::element_type>;
                                if constexpr (std::is_same_v<T,
                                                  rhi::OriginalCommandGraphicsPipelineSource>) {
                                    auto native = rhi_webgpu::webGpuNativePayloadFor(
                                        *object->originalObject());
                                    const auto handle = native->pipeline;
                                    native->pipeline = nullptr;
                                    assert(command->validate());
                                    native->pipeline = handle;
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandDescriptorSetSource>)
                                    OriginalDescriptorNativeIdentityFixture::mutateActual(
                                        rhi_webgpu::webGpuNativePayloadFor(
                                            *object->originalObject()),
                                        [&] {
                                            assert(command->validate());
                                        });
                            },
                            entry);
                    return command->validate();
                }));
        });
        assert(root->status() == Root::Status::Prepared && root->journalEmpty());
        if (graphics) {
            assert(admission->originalPayload()->work().commands == 14u
                && root->originalContentPlanContractVersion() == 6u
                && root->hasOriginalRecordingSeal());
            is(root->applyOriginalPrototypePlan(), "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
            is(root->commitOriginalPendingOwnership(), "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
            is(root->abandonOriginalPendingOwnership(),
                "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
            std::printf(
                "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_OK Cold8 Payload8 Lifecycle5 ContentPlan6 commands14 calls9 command1 owners%u owner-add3 legacy-refused short-exhausted-recover duplicate-peer-forged-native-mutated-refused sealed-CPU-refused hot0-query0 no-encoder-callback-or-execution\n",
                commandOwners);
        } else
            std::printf(
                "BINDLESS_RHI_GEOMETRY_READBACK_COMMAND5_OK original%llu Cold5 Lifecycle2 callbacks12 owner-add3 legacy-refused exhausted-recover duplicate-forged-refused hot0-query0 no-encoder-map-public-authority\n",
                static_cast<unsigned long long>(admission->originalPayload()->work().commands));
        return command;
    }

    static void verifyCompute(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& lossQuota)
    {
        auto state = OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(
            inputs, device, true, false, false, {1u, true, 1u, 1u, true, true, 2u, 4u});
        const auto& sources = inputs->originalScalars()->originalUsage()->originalSources();
        const auto& source = sources->originalBudget();
        const char* id = nullptr;
        const auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss);
        OriginalTypedColdAdmissionFixture::freezeComputeOriginal(state, device, *loss, id);
        const auto admissionRequest =
            OriginalTypedColdAdmissionFixture::requestFor(state.typedNativePayload);
        const auto demand =
            Admission::inspectOriginalPayload(state.typedNativePayload, admissionRequest)
                .required.capacity;
        const C::Request request{1u,
            true,
            1u,
            admissionRequest.maxRetainedOwners + C::additionalOwners,
            state.recordingQuota->limitBytes()};
        auto submission =
            core::makeShared<core::CpuMemoryQuota>(source.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        auto callback =
            core::makeShared<core::CpuMemoryQuota>(source.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{1u,
            true,
            1u,
            8u,
            8u,
            request.maxRetainedOwners,
            request.maxRetainedOwners,
            request.maxRetainedOwners,
            state.recordingQuota->limitBytes(),
            submission->limitBytes(),
            callback->limitBytes()};
        std::shared_ptr<const Seal> seal;
        std::shared_ptr<const Reservation> reservation;
        std::shared_ptr<const Admission> admission;
        const auto bind = [&] {
            seal = Seal::prepareOriginal(
                state.transaction, device, lifecycle, submission, callback, id);
            assert(seal);
            reservation = Reservation::prepareOriginal(seal, demand, id);
            assert(reservation);
            admission = Admission::prepareOriginal(
                state.typedNativePayload, reservation, admissionRequest, id);
            assert(admission);
        };
        bind();
        const auto rebuild = [&] {
            admission.reset();
            reservation.reset();
            seal.reset();
            assert(state.transaction->status() == Root::Status::Abandoned
                && state.transaction->journalEmpty());
            state.typedNativePayload.reset();
            state.dependencyDecision.reset();
            state.nativeClosure.reset();
            state.transaction.reset();
            assert(!sources->originalStateTransactionCount()
                && !sources->originalReservedSubmissionCount());
            OriginalCommandDescriptorStateFixture::prepareRetainedPlan(state, device, id);
            OriginalTypedColdAdmissionFixture::freezeComputeOriginal(state, device, *loss, id);
            bind();
        };
        const auto factory = [&] {
            return Command::prepareOriginal(admission, request, id);
        };
        CpuQuotaFactoryFixture pressure(state.recordingQuota);
        const auto bytes = pressure.measure(factory);
        pressure.verifySingleUse(bytes, rebuild, factory);
        // Last exhausted probe publishes no claim; the same genuine candidate
        // must recover when its independent CPU pressure is released.
        assert(!id || core::StringView(id) == "RHI-WEBGPU-TYPED-COLD-COMMAND-ALLOCATION");
        const auto negative = [&](auto edit, const char* expected) {
            auto wrong = request;
            edit(wrong);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Command::prepareOriginal(admission, wrong, id));
            is(id, expected);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        };
        negative(
            [](auto& r) {
                --r.maxRetainedOwners;
            },
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-OWNER-CAPACITY");
        negative(
            [](auto& r) {
                --r.maxCpuMetadataBytes;
            },
            "RHI-WEBGPU-TYPED-COLD-COMMAND-QUOTA");
        negative(
            [](auto& r) {
                r.maxCommandPayloads = 2u;
            },
            "RHI-ORIGINAL-TYPED-COLD-COMMAND-COUNT");
        auto forgedAdmission = std::shared_ptr<const Admission>(submission, admission.get());
        assert(!Command::prepareOriginal(forgedAdmission, request, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-OWNER");
        forgedAdmission.reset();
        auto command = factory();
        assert(command && !id && !command->validate()
            && command->retainedOwnerCount() == request.maxRetainedOwners);
        is(command->executionDiagnostic(), "RHI-WEBGPU-TYPED-COLD-COMMAND-LIVE-EXECUTOR-MISSING");
        assert(!command->nativeExecutionProven());
        assert(!factory());
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        auto peer =
            Admission::prepareOriginal(state.typedNativePayload, reservation, admissionRequest, id);
        assert(peer);
        assert(!Command::prepareOriginal(peer, request, id));
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        peer.reset();
        const std::array quotas{state.recordingQuota,
            state.stateQuota,
            state.stream.quota,
            sources->originalMetadataQuota(),
            inputs->originalMetadataQuota(),
            submission,
            callback,
            lossQuota};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const Command>(callback, command.get());
                    is(Command::validateOriginal(forged), "RHI-WEBGPU-TYPED-COLD-COMMAND-OWNER");
                    for (const auto& entry : sources->sources())
                        std::visit(
                            [&](const auto& object) {
                                using T = std::remove_const_t<
                                    typename std::decay_t<decltype(object)>::element_type>;
                                if constexpr (std::is_same_v<T,
                                                  rhi::OriginalCommandComputePipelineSource>) {
                                    auto native = rhi_webgpu::webGpuNativePayloadFor(
                                        *object->originalObject());
                                    const auto handle = native->pipeline;
                                    native->pipeline = nullptr;
                                    assert(command->validate());
                                    native->pipeline = handle;
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandDescriptorSetSource>)
                                    OriginalDescriptorNativeIdentityFixture::mutateActual(
                                        rhi_webgpu::webGpuNativePayloadFor(
                                            *object->originalObject()),
                                        [&] {
                                            assert(command->validate());
                                        });
                            },
                            entry);
                    return command->validate();
                }));
        });
        assert(state.transaction->status() == Root::Status::Prepared
            && state.transaction->journalEmpty()
            && state.transaction->applyOriginalPrototypePlan());
        std::weak_ptr<const Admission> retained = admission;
        admission.reset();
        reservation.reset();
        seal.reset();
        assert(!retained.expired() && !command->validate()
            && sources->originalReservedCallbackTicketCount() == 8u);
        admission = command->originalAdmission();
        seal = admission->originalReservation()->originalSeal();
        command.reset();
        assert(!factory());
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        admission.reset();
        assert(!sources->originalReservedSubmissionCount());
        reservation = Reservation::prepareOriginal(seal, demand, id);
        assert(reservation);
        admission =
            Admission::prepareOriginal(state.typedNativePayload, reservation, admissionRequest, id);
        assert(admission);
        assert(!factory());
        is(id, "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE");
        admission.reset();
        reservation.reset();
        seal.reset();
        assert(state.transaction->status() == Root::Status::Abandoned
            && state.transaction->journalEmpty());
        retained.reset();
        state = {};
        assert(!submission->liveBytes() && !callback->liveBytes()
            && !sources->originalStateTransactionCount());
        std::printf(
            "BINDLESS_RHI_TYPED_COLD_COMMAND_NATIVE_OK original19 actual-device-queue same-seal-root demand8 owner-add3 exact-less-exhausted%llu failed-claim-recover peer-replacement-reservation-sticky forged-native-mutation hot0 query0 caller-drop pre-record-cancel no-encoder-callback-receipt\n",
            static_cast<unsigned long long>(bytes));
    }
#endif
};
} // namespace arp::test
