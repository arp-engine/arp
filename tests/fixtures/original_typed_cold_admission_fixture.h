#pragma once

#include "original_command_descriptor_state_fixture.h"
#include "original_typed_native_payload_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"
#include "rhi/backends/webgpu/webgpu_original_typed_cold_admission.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "rhi/semantics/command_lowering.h"
#include "webgpu_error_scope_handoff_fixture.h"

namespace arp::test {
class OriginalTypedColdAdmissionFixture final {
    using A = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
    using P = rhi_webgpu::WebGpuOriginalTypedNativePayload;
    using D = rhi_webgpu::WebGpuOriginalTypedDependencyDecision;
    using C = rhi::OriginalTypedNativeColdContract;
    using Closure = rhi_webgpu::WebGpuCanonicalNativeClosure;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using K = rhi::CommandUsageKind;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(
                stderr, "typed cold expected %s got %s\n", expected, actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static A::Request requestFor(
        const std::shared_ptr<const P>& payload, std::uint32_t version = 1u)
    {
        const auto required = A::lifecycleRequirements(version);
        A::Request request;
        request.contractVersion = version;
        request.prototypeOptIn = true;
        request.synchronizationVersion = 1u;
        request.synchronization =
            rhi::OriginalTypedNativeColdSynchronization::OriginalUsageScopeBoundaries1;
        request.maxWork = payload->work();
        request.maxCallbacks = required.callbacks;
        request.maxEncoders = request.maxCommandBuffers = 1u;
        request.maxRetainedOwners = payload->retainedOwnerCount() + A::additionalOwners;
        request.maxCpuMetadataBytes = payload->originalMetadataQuota()->limitBytes();
        return request;
    }

public:
    static std::uint32_t geometryExecutionOwnerDemand() noexcept
    {
        return executionOwnerDemand(4u);
    }

    static std::uint32_t executionOwnerDemand(std::uint32_t version) noexcept
    {
        const auto cold = A::lifecycleRequirements(version);
        const rhi::OriginalTypedCallbackCustodyRequest request{version,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            cold.capacity.callbackTickets,
            1u};
        return rhi::OriginalTypedNativeColdCommandContract::additionalOwners
            + rhi::OriginalTypedCallbackCustodyContract::additionalOwners(request)
            + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
    }

    static void verifyContract()
    {
        const auto required = A::lifecycleRequirements();
        assert(required.callbacks.recordingScopes == rhi_webgpu::WebGpuErrorScopeContract::count
            && required.callbacks.submissionScopes == rhi_webgpu::WebGpuErrorScopeContract::count
            && required.capacity.callbackTickets == 8u
            && required.capacity.callbackRegistrations == 8u);
        auto source = rhi::OriginalCommandAdmissionBudget{};
        source.contractVersion = 1u;
        source.prototypeOptIn = true;
        source.maxCommands = 19u;
        source.maxGraphOwners = 64u;
        source.maxCpuBytes.fill(65536u);
        auto r = required;
        r.work = OriginalTypedNativeColdContractFixture::required().work;
        auto different = r;
        ++different.work.nativeCalls;
        assert(!r.matches(different));
        different = r;
        ++different.capacity.callbackTickets;
        assert(!r.matches(different));
        source.maxNativePayloadObjects = 9u;
        source.maxTypedNativeCommandCalls = r.work.nativeCalls;
        source.maxPendingSubmissions = 1u;
        source.maxCallbackTickets = source.maxCallbackRegistrations = 8u;
        assert(!C::validateSourceAuthority(r, 7u, source));
        const auto negative = [&](auto edit, const char* expected) {
            auto bad = source;
            edit(bad);
            is(C::validateSourceAuthority(r, 7u, bad), expected);
        };
        negative(
            [](auto& b) {
                --b.maxNativePayloadObjects;
            },
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
        negative(
            [](auto& b) {
                --b.maxTypedNativeCommandCalls;
            },
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-NATIVE-CALL-CAPACITY");
        negative(
            [](auto& b) {
                b.maxTypedNativeCommandCalls = 0u;
            },
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-NATIVE-CALL-CAPACITY");
        negative(
            [](auto& b) {
                --b.maxPendingSubmissions;
            },
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-SUBMISSION-CAPACITY");
        negative(
            [](auto& b) {
                --b.maxCallbackTickets;
            },
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-CALLBACK-CAPACITY");
        negative(
            [](auto& b) {
                --b.maxCallbackRegistrations;
            },
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-REGISTRATION-CAPACITY");
        is(C::validateSourceAuthority(r, UINT64_MAX, source),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-OVERFLOW");
        const P::Record empty{{},
            *rhi::CommandScalarArguments::capture(K::Finish, rhi::CommandScalarArguments::None{})};
        std::array<P::Record, 8u> records{{empty, empty, empty, empty, empty, empty, empty, empty}};
        const std::array<K, 8u> kinds{K::BeginRenderPass,
            K::SetViewport,
            K::SetScissor,
            K::Draw,
            K::EndRenderPass,
            K::BeginRenderPass,
            K::Draw,
            K::EndRenderPass};
        for (std::size_t i = 0u; i < records.size(); ++i)
            records[i].original.kind = kinds[i];
        records[1].scalar = *rhi::CommandScalarArguments::capture(
            K::SetViewport, rhi::ViewportState{0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f});
        records[2].scalar =
            *rhi::CommandScalarArguments::capture(K::SetScissor, rhi::ScissorState{0u, 0u, 2u, 2u});
        using Render = rhi::semantics::OriginalTypedRenderStateContract;
        is(Render::validate(records, records.size()), "RHI-ORIGINAL-TYPED-RENDER-STATE-INCOMPLETE");
        records[6].original.kind = K::BindGraphicsPipeline;
        assert(!Render::validate(records, records.size())); // Empty second pass.
        records[2].original.kind = K::BindGraphicsPipeline;
        is(Render::validate(records, records.size()), "RHI-ORIGINAL-TYPED-RENDER-STATE-INCOMPLETE");
        records[2].original.kind = K::SetScissor;
        is(Render::validate(records, records.size() - 1u),
            "RHI-ORIGINAL-TYPED-RENDER-STATE-COMMAND-CAPACITY");
        for (const auto version : {1u, 2u, 3u, 4u, 5u, 6u})
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU}) {
                const auto lowering =
                    arp::rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(
                        backend, version);
                assert(lowering.admissionAvailable == (backend == rhi::Backend::WebGPU));
                assert(!lowering.recordingAvailable && !lowering.executionAvailable
                    && lowering.diagnostic);
            }
        const auto graphics =
            arp::rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(
                rhi::Backend::WebGPU, 8u);
        assert(graphics.admissionAvailable && !graphics.recordingAvailable
            && !graphics.executionAvailable);
        assert(arp::rhi::semantics::OriginalTypedNativeColdLowering::admissionLowering(
            static_cast<rhi::Backend>(255u))
                .diagnostic);
        const char* id = nullptr;
        assert(!A::prepareOriginal({}, {}, {}, id) && id && A::validateOriginal({}));
        std::puts(
            "BINDLESS_RHI_TYPED_COLD_ADMISSION_CONTRACT_OK independent1 source-payload-call-lifecycle-authority scope3-3-queue1-loss1 demand8 original-render-setters-per-consuming-pass four-execution-gates-closed");
    }
#if defined(ARP_RHI_WEBGPU_ORIGINAL_RENDER_PASS_PAYLOAD_HAS_NATIVE)
    static std::shared_ptr<const A> verifyGraphics8(const std::shared_ptr<const P>& payload,
        const rhi::GpuDevice& device,
        std::shared_ptr<core::CpuMemoryQuota> originalSubmission = {},
        std::shared_ptr<core::CpuMemoryQuota> originalCallback = {},
        std::array<std::uint64_t, 2u>* creationBaseline = nullptr)
    {
        assert(bool(originalSubmission) == bool(originalCallback));
        using Root = rhi::OriginalCommandStateTransaction;
        const auto root = payload->originalDecision()->originalClosure()->originalRoot();
        const auto source = root->originalGraph()
                                ->originalInputs()
                                ->originalScalars()
                                ->originalUsage()
                                ->originalSources();
        const auto& authored = source->originalBudget();
        const auto recording = payload->originalMetadataQuota();
        auto request = requestFor(payload, 8u);
        request.maxRetainedOwners =
            payload->originalRequest().maxRetainedOwners + A::additionalOwners;
        const auto preclaim = A::inspectOriginalPayload(payload, request);
        if (preclaim.diagnostic)
            std::fprintf(stderr, "Graphics Cold8 preclaim: %s\n", preclaim.diagnostic);
        assert(!preclaim.diagnostic && preclaim.required.contractVersion == 8u
            && preclaim.required.work.matches(payload->work())
            && preclaim.required.work.commands == 14u && preclaim.required.work.nativeCalls == 9u
            && preclaim.required.capacity.callbackTickets == 8u
            && preclaim.required.capacity.callbackRegistrations == 8u
            && root->originalContentPlanContractVersion() == 6u
            && root->originalMixedContentCounts());
        const auto reject = [&](auto change) {
            auto wrong = request;
            change(wrong);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto result = A::inspectOriginalPayload(payload, wrong);
            assert(result.diagnostic && !result.required.contractVersion
                && !result.required.work.commands && !result.required.capacity.callbackTickets
                && root->journalEmpty());
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        };
        for (std::uint32_t old = 1u; old <= 7u; ++old)
            reject([&](auto& b) {
                b.contractVersion = old;
            });
        reject([](auto& b) {
            b.prototypeOptIn = false;
        });
        reject([](auto& b) {
            b.maxWork.computePasses = 1u;
        });
        reject([](auto& b) {
            b.maxWork.dispatches = 1u;
        });
        reject([](auto& b) {
            --b.maxWork.nativeCalls;
        });
        reject([](auto& b) {
            --b.maxCallbacks.recordingScopes;
        });
        reject([](auto& b) {
            --b.maxCallbacks.submissionScopes;
        });
        reject([](auto& b) {
            --b.maxCpuMetadataBytes;
        });
        reject([&](auto& b) {
            b.maxRetainedOwners = preclaim.required.retainedOwners - 1u;
        });
        reject([](auto& b) {
            ++b.maxRetainedOwners;
        });
        const auto payloads = payload->originalDecision()->originalClosure()->nativePayloadCount();
        auto shortSource = authored;
        --shortSource.maxNativePayloadObjects;
        assert(C::validateSourceAuthority(preclaim.required, payloads, shortSource));
        shortSource = authored;
        --shortSource.maxTypedNativeCommandCalls;
        assert(C::validateSourceAuthority(preclaim.required, payloads, shortSource));
        shortSource = authored;
        --shortSource.maxCallbackTickets;
        assert(C::validateSourceAuthority(preclaim.required, payloads, shortSource));
        shortSource = authored;
        --shortSource.maxCallbackRegistrations;
        assert(C::validateSourceAuthority(preclaim.required, payloads, shortSource));
        const auto submission = originalSubmission
            ? originalSubmission
            : core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                  rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        const auto callback = originalCallback
            ? originalCallback
            : core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                  rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        assert(submission != callback && submission != recording && callback != recording);
        assert(submission->limitBytes()
                == authored.maxCpuBytes[static_cast<std::size_t>(
                    rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]
            && callback->limitBytes()
                == authored.maxCpuBytes[static_cast<std::size_t>(
                    rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        if (creationBaseline)
            *creationBaseline = {submission->liveBytes(), callback->liveBytes()};
        const auto demand = preclaim.required.capacity;
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{5u,
            true,
            demand.pendingSubmissions,
            demand.callbackTickets,
            demand.callbackRegistrations,
            request.maxRetainedOwners,
            request.maxRetainedOwners,
            request.maxRetainedOwners,
            recording->limitBytes(),
            submission->limitBytes(),
            callback->limitBytes()};
        const char* id = nullptr;
        for (std::uint32_t old = 1u; old <= 4u; ++old) {
            auto legacy = lifecycle;
            legacy.contractVersion = old;
            assert(!Seal::prepareOriginal(root, device, legacy, submission, callback, id));
            is(id, "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-CONTENT-PROFILE-UNAVAILABLE");
            assert(!root->hasOriginalRecordingSeal() && root->journalEmpty());
        }
        assert(!Seal::prepareOriginal(root, device, lifecycle, submission, submission, id) && id);
        auto seal = Seal::prepareOriginal(root, device, lifecycle, submission, callback, id);
        assert(seal && !id);
        {
            auto shortDemand = demand;
            --shortDemand.callbackTickets;
            auto shortReservation = Reservation::prepareOriginal(seal, shortDemand, id);
            assert(shortReservation && !id);
            is(A::inspectOriginal(payload, shortReservation, request).diagnostic,
                "RHI-WEBGPU-TYPED-COLD-RESERVATION-DEMAND");
        }
        auto reservation = Reservation::prepareOriginal(seal, demand, id);
        assert(reservation && !id);
        assert(source->originalReservedSubmissionCount() == 1u
            && source->originalReservedCallbackTicketCount() == 8u
            && source->originalReservedCallbackRegistrationCount() == 8u);
        const auto make = [&] {
            return A::prepareOriginal(payload, reservation, request, id);
        };
        CpuQuotaFactoryFixture pressure(recording);
        const auto peak = pressure.measureRetained(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(
                recording, recording->limitBytes() - recording->liveBytes());
            assert(full.charged() && !make());
            is(id, "RHI-WEBGPU-TYPED-COLD-ALLOCATION");
        }
        auto admission = make();
        assert(admission && !id && admission->requirements().matches(preclaim.required));
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const A>(recording, admission.get());
                    is(A::validateOriginal(forged), "RHI-WEBGPU-TYPED-COLD-OWNER");
                    auto forgedReservation =
                        std::shared_ptr<const Reservation>(submission, reservation.get());
                    is(A::inspectOriginal(payload, forgedReservation, request).diagnostic,
                        "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER");
                    for (const auto& value : source->sources())
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
                                    assert(admission->validate());
                                    native->pipeline = handle;
                                }
                            },
                            value);
                    return admission->validate();
                }));
        });
        assert(root->status() == Root::Status::Prepared && root->journalEmpty()
            && root->hasOriginalRecordingSeal());
        is(root->applyOriginalPrototypePlan(), "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
        is(root->commitOriginalPendingOwnership(), "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
        is(root->abandonOriginalPendingOwnership(), "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
        assert(!admission->nativeExecutionProven());
        is(admission->executionDiagnostic(), "RHI-WEBGPU-TYPED-COLD-LIVE-EXECUTOR-MISSING");
        std::printf(
            "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_OK Payload8 Lifecycle5 ContentPlan6 commands14 calls9 render1 compute0 callbacks8 owners%u coldBytes%llu exact-less-exhausted forged-reservation-native-mutated legacy-refused same-Prepared-root sealed-CPU-refused hot0-query0 no-encoder-or-execution\n",
            preclaim.required.retainedOwners,
            static_cast<unsigned long long>(peak));
        return admission;
    }

    static std::shared_ptr<const A> verifyComposed(const std::shared_ptr<const P>& payload,
        const rhi::GpuDevice& device,
        bool probeGenericPreparation = true)
    {
        const auto& root = payload->originalDecision()->originalClosure()->originalRoot();
        const auto& sources = root->originalGraph()
                                  ->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources();
        const auto& authored = sources->originalBudget();
        const auto recording = payload->originalMetadataQuota();
        const auto bytes = recording->limitBytes();
        auto request = requestFor(payload, 5u);
        // Reserve the authored Command5 shell's three owners in the seal;
        // Sources already includes the complete typed command owner inventory.
        const rhi::OriginalTypedCallbackCustodyRequest callbacks{5u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            12u,
            1u};
        request.maxRetainedOwners += rhi::OriginalTypedNativeColdCommandContract::additionalOwners
            + rhi::OriginalTypedCallbackCustodyContract::additionalOwners(callbacks)
            + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 1u
            + rhi::OriginalTypedIssuerPreparationContract::additionalOwners;
        request.maxTransfers = {1u, 1u, 1u};
        request.readback = {2u, true, 1u, 1u, 1u, 512u, 512u, bytes};
        request.readbackMap = {1u, true, 1u, 1u, 3u, 12u, 12u};
        const auto preclaim = A::inspectOriginalPayload(payload, request);
        if (preclaim.diagnostic)
            std::fprintf(stderr, "geometry/readback Cold5: %s\n", preclaim.diagnostic);
        assert(!preclaim.diagnostic && preclaim.required.contractVersion == 5u
            && preclaim.required.work.matches(payload->work())
            && preclaim.required.transfers.matches(request.maxTransfers)
            && preclaim.required.capacity.callbackTickets == 12u
            && preclaim.required.capacity.callbackRegistrations == 12u
            && preclaim.required.readback.resultDeclarations == 1u
            && preclaim.required.readbackMap.maps == 1u);
        const auto allocated = core::defaultCpuAllocator().stats().allocationCount;
        if (probeGenericPreparation) {
            const auto reject = [&](auto bad) {
                const auto result = A::inspectOriginalPayload(payload, bad);
                assert(result.diagnostic && !result.required.contractVersion
                    && !result.required.work.commands);
            };
            auto bad = request;
            bad.contractVersion = 4u;
            reject(bad);
            bad = request;
            bad.readback = {};
            bad.readbackMap = {};
            reject(bad);
            bad = request;
            --bad.maxWork.nativeCalls;
            reject(bad);
            bad = request;
            --bad.maxTransfers.copies;
            reject(bad);
            bad = request;
            --bad.readbackMap.maxTotalCallbackTickets;
            reject(bad);
            bad = request;
            --bad.readbackMap.maxTotalCallbackRegistrations;
            reject(bad);
            auto shortSource = authored;
            --shortSource.maxCallbackTickets;
            assert(C::validateSourceAuthority(preclaim.required,
                payload->originalDecision()->originalClosure()->nativePayloadCount(),
                shortSource));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocated);
        }
        if (!probeGenericPreparation) {
            auto shortSource = authored;
            --shortSource.maxNativePayloadObjects;
            is(C::validateSourceAuthority(preclaim.required,
                   payload->originalDecision()->originalClosure()->nativePayloadCount(),
                   shortSource),
                "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
            shortSource = authored;
            --shortSource.maxCallbackTickets;
            assert(C::validateSourceAuthority(preclaim.required,
                payload->originalDecision()->originalClosure()->nativePayloadCount(),
                shortSource));
            assert(core::defaultCpuAllocator().stats().allocationCount == allocated);
        }
        auto submission =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        auto callback =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        const auto owners = request.maxRetainedOwners;
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{2u,
            true,
            1u,
            12u,
            12u,
            owners,
            owners,
            owners,
            bytes,
            submission->limitBytes(),
            callback->limitBytes()};
        const char* id = nullptr;
        auto legacy = lifecycle;
        legacy.contractVersion = 1u;
        assert(!Seal::prepareOriginal(root, device, legacy, submission, callback, id));
        is(id, "RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE");
        auto seal = Seal::prepareOriginal(root, device, lifecycle, submission, callback, id);
        assert(seal && !id);
        auto reservation = Reservation::prepareOriginal(seal, preclaim.required.capacity, id);
        assert(reservation && !id);
        const auto factory = [&] {
            return A::prepareOriginal(payload, reservation, request, id);
        };
        CpuQuotaFactoryFixture pressure(recording);
        if (probeGenericPreparation) {
            const auto retained = pressure.measureRetained(factory);
            pressure.verify(retained, factory);
        }
        {
            core::CpuMemoryQuotaLease exhausted(
                recording, recording->limitBytes() - recording->liveBytes());
            assert(exhausted.charged() && !factory());
        }
        auto admission = factory();
        assert(admission && !id && !admission->validate());
        const auto hot = [&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept {
                    return admission->validate();
                }));
        };
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries(hot);
        assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
            && root->journalEmpty() && !admission->nativeExecutionProven());
        std::printf(
            "BINDLESS_RHI_GEOMETRY_READBACK_COLD5_OK original%llu Payload5 Lifecycle2 Result2 Map1 callbacks12 base8-map4 %s hot0-query0 no-command-map-public-authority\n",
            static_cast<unsigned long long>(request.maxWork.commands),
            probeGenericPreparation ? "exact-less-exhausted"
                                    : "generic-suite-reused exhausted-recover");
        return admission;
    }

    static void verifyGeometryRenderStateRefusal(const std::shared_ptr<const P>& payload)
    {
        const auto request = requestFor(payload, 4u);
        assert(!C::validateIntent(request));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto rejected = A::inspectOriginalPayload(payload, request);
        is(rejected.diagnostic, "RHI-ORIGINAL-TYPED-RENDER-STATE-INCOMPLETE");
        assert(!rejected.required.work.commands && !rejected.required.capacity.callbackTickets);
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::puts(
            "BINDLESS_RHI_GEOMETRY_COLD_RENDER_STATE_REJECT_OK original16 explicit-setters-required preclaim-allocation0 GPU0");
    }

    static std::shared_ptr<const A> verifyGeometry(
        const std::shared_ptr<const P>& payload, const rhi::GpuDevice& device, bool streams = false)
    {
        const auto root = payload->originalDecision()->originalClosure()->originalRoot();
        const auto& sources = root->originalGraph()
                                  ->originalInputs()
                                  ->originalScalars()
                                  ->originalUsage()
                                  ->originalSources();
        const auto& authored = sources->originalBudget();
        auto request = requestFor(payload, 4u);
        const auto admissionOwnerMinimum = request.maxRetainedOwners;
        request.maxRetainedOwners += geometryExecutionOwnerDemand();
        assert(!C::validateIntent(request));
        const auto preclaim = A::inspectOriginalPayload(payload, request);
        if (preclaim.diagnostic)
            std::fprintf(stderr, "geometry Cold4 preclaim: %s\n", preclaim.diagnostic);
        assert(!preclaim.diagnostic && preclaim.required.work.matches(payload->work()));
        assert(preclaim.required.work.commands == (streams ? 14u : 20u)
            && preclaim.required.work.nativeCalls == (streams ? 12u : 19u)
            && preclaim.required.work.indexBinds == (streams ? 1u : 2u)
            && preclaim.required.work.indexedDraws == (streams ? 1u : 3u)
            && preclaim.required.work.vertexBinds == (streams ? 2u : 0u)
            && preclaim.required.work.viewports == (streams ? 1u : 2u)
            && preclaim.required.work.scissors == (streams ? 1u : 2u)
            && preclaim.required.capacity.callbackTickets == 8u
            && preclaim.required.capacity.callbackRegistrations == 8u);
        const auto reject = [&](auto change) {
            auto wrong = request;
            change(wrong);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto rejected = A::inspectOriginalPayload(payload, wrong);
            assert(rejected.diagnostic && !rejected.required.work.commands
                && !rejected.required.capacity.callbackTickets);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        };
        reject([](auto& b) {
            b.contractVersion = 5u;
        });
        reject([](auto& b) {
            b.prototypeOptIn = false;
        });
        reject([&](auto& b) {
            b.maxRetainedOwners = admissionOwnerMinimum - 1u;
        });
        reject([](auto& b) {
            --b.maxCpuMetadataBytes;
        });
        reject([](auto& b) {
            --b.maxCallbacks.recordingScopes;
        });
        reject([](auto& b) {
            --b.maxCallbacks.submissionScopes;
        });
        reject([](auto& b) {
            --b.maxWork.indexBinds;
        });
        reject([](auto& b) {
            --b.maxWork.indexedDraws;
        });
        if (streams)
            reject([](auto& b) {
                --b.maxWork.vertexBinds;
            });
        reject([](auto& b) {
            --b.maxWork.viewports;
        });
        reject([](auto& b) {
            --b.maxWork.scissors;
        });
        reject([](auto& b) {
            --b.maxWork.nativeCalls;
        });
        reject([](auto& b) {
            b.synchronizationVersion = 0u;
        });
        for (const auto version : {1u, 2u, 3u})
            reject([&](auto& b) {
                b.contractVersion = version;
            });
        auto submission =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        auto callback =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        const rhi::OriginalCommandNativeLifecycleBudget lifecycle{1u,
            true,
            1u,
            8u,
            8u,
            request.maxRetainedOwners,
            request.maxRetainedOwners,
            request.maxRetainedOwners,
            root->originalMetadataQuota()->limitBytes(),
            submission->limitBytes(),
            callback->limitBytes()};
        const char* id = nullptr;
        auto seal = Seal::prepareOriginal(root, device, lifecycle, submission, callback, id);
        if (!seal)
            std::fprintf(stderr, "geometry Lifecycle1 seal: %s\n", id);
        assert(seal);
        {
            auto shortDemand = preclaim.required.capacity;
            --shortDemand.callbackTickets;
            auto shortReservation = Reservation::prepareOriginal(seal, shortDemand, id);
            assert(shortReservation);
            is(A::inspectOriginal(payload, shortReservation, request).diagnostic,
                "RHI-WEBGPU-TYPED-COLD-RESERVATION-DEMAND");
        }
        auto reservation = Reservation::prepareOriginal(seal, preclaim.required.capacity, id);
        assert(reservation);
        const auto make = [&] {
            return A::prepareOriginal(payload, reservation, request, id);
        };
        CpuQuotaFactoryFixture pressure(root->originalMetadataQuota());
        const auto peak = pressure.measureRetained(make);
        pressure.verify(peak, make);
        {
            core::CpuMemoryQuotaLease full(root->originalMetadataQuota(),
                root->originalMetadataQuota()->limitBytes()
                    - root->originalMetadataQuota()->liveBytes());
            assert(full.charged() && !make());
            is(id, "RHI-WEBGPU-TYPED-COLD-ALLOCATION");
        }
        auto admission = make();
        if (!admission)
            std::fprintf(stderr, "geometry Cold4 factory: %s\n", id);
        assert(admission);
        assert(admission->requirements().matches(preclaim.required)
            && !admission->nativeExecutionProven());
        const std::array quotas{root->originalMetadataQuota(), submission, callback};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged =
                        std::shared_ptr<const A>(root->originalMetadataQuota(), admission.get());
                    is(A::validateOriginal(forged), "RHI-WEBGPU-TYPED-COLD-OWNER");
                    auto forgedReservation =
                        std::shared_ptr<const Reservation>(submission, reservation.get());
                    is(A::inspectOriginal(payload, forgedReservation, request).diagnostic,
                        "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER");
                    return admission->validate();
                }));
        });
        assert(root->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
            && root->journalEmpty());
        assert(root->applyOriginalPrototypePlan()); // A seal grants no CPU replay authority.
        is(admission->executionDiagnostic(), "RHI-WEBGPU-TYPED-COLD-LIVE-EXECUTOR-MISSING");
        if (streams)
            std::printf(
                "BINDLESS_RHI_STREAM_GEOMETRY_COLD_ADMISSION4_OK commands14 calls12 vertex2 index1 indexedDraw1 callbacks8 exact-less-exhausted peak%llu\n",
                static_cast<unsigned long long>(peak));
        else
            std::printf(
                "BINDLESS_RHI_GEOMETRY_COLD_ADMISSION4_OK same-original-sources payload4 freeze4 closure6 lifecycle1 commands20 calls19 index2 indexedDraw3 renderSetters2-2 callbacks8 exact-less-exhausted forged caller-drop-required hot0-query0 no-native-command-no-receipt peak%llu\n",
                static_cast<unsigned long long>(peak));
        return admission;
    }

    template <bool Streams = false>
    static void executeGeometry(const std::shared_ptr<const A>& admission,
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalBufferUploadSource>& initialized)
    {
        using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
        using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
        using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
        using Root = rhi::OriginalCommandStateTransaction;
        using Scopes = WebGpuErrorScopeHandoffFixture<1016u>;
        const auto& root = admission->originalRoot();
        const auto& seal = admission->originalReservation()->originalSeal();
        const auto submission = seal->originalSubmissionQuota(),
                   callback = seal->originalCallbackQuota();
        const auto recording = root->originalMetadataQuota();
        const auto owners = admission->originalRequest().maxRetainedOwners;
        const auto submissionBase = submission->liveBytes(), callbackBase = callback->liveBytes();
        // Admission still owns the original submission-charged reservation.
        // Command/callback cleanup must restore that exact retained baseline.
        assert(submissionBase && !callbackBase);
        const char* id = nullptr;
        const rhi::OriginalTypedNativeColdCommandRequest commandRequest{
            4u, true, 1u, owners, recording->limitBytes()};
        for (const auto version : {1u, 2u, 3u}) {
            auto legacy = commandRequest;
            legacy.contractVersion = version;
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            assert(!Command::prepareOriginal(admission, legacy, id));
            is(id, "RHI-ORIGINAL-TYPED-COLD-COMMAND-ADMISSION-PROFILE");
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        }
        auto command = Command::prepareOriginal(admission, commandRequest, id);
        if (!command)
            std::fprintf(stderr, "geometry Command4: %s\n", id);
        assert(command);
        const rhi::OriginalTypedCallbackCustodyRequest custodyRequest{4u,
            true,
            rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            8u,
            1u,
            owners,
            submission->limitBytes(),
            callback->limitBytes()};
        auto custody = Custody::prepareOriginal(command, custodyRequest, id);
        if (!custody)
            std::fprintf(stderr, "geometry Custody4: %s\n", id);
        assert(custody);
        auto issuers = Issuers::prepareOriginal(custody,
            {4u,
                true,
                1u,
                1u,
                7u,
                owners,
                recording->limitBytes(),
                submission->limitBytes(),
                callback->limitBytes()},
            id);
        if (!issuers)
            std::fprintf(stderr, "geometry Issuer4: %s\n", id);
        assert(issuers);
        constexpr rhi::OriginalTypedNativeExecutionRequest execution{
            4u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
        for (const auto version : {1u, 2u, 3u}) {
            auto legacy = execution;
            legacy.contractVersion = version;
            is(Issuers::recordOriginal(issuers, legacy),
                "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE");
        }
        Scopes::holdScopes(true);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    return Issuers::recordOriginal(issuers, execution);
                }));
        });
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(root->status() == Root::Status::Pending && !root->journalEmpty()
            && !issuers->hasOriginalRecordedCommand());
        // The retained original seal may authenticate this immutable geometry;
        // the actor's public committed readiness/content APIs stay closed.
        assert(initialized);
        is(initialized->readyDiagnostic(), "RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE");
        is(rhi_webgpu::WebGpuCanonicalBufferUploadSource::validateInitializedIndexContent(
               initialized, initialized->originalLease(), rhi::IndexFormat::Uint16),
            "RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE");
        if constexpr (Streams) {
            for (const auto ordinal : {9u, 10u}) {
                const auto& actor =
                    admission->originalPayload()->records()[ordinal].binding->indexContent;
                assert(actor);
                is(actor->readyDiagnostic(), "RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE");
            }
        }
        Scopes::releaseScopes();
        const bool recorded = WebGpuAsyncWaitFixture::until([&] {
            return issuers->hasOriginalRecordedCommand();
        });
        if (!recorded)
            std::fprintf(stderr,
                "geometry Execution4 recording: %s pending=%u root=%u\n",
                issuers->recordingDiagnostic(),
                static_cast<unsigned>(custody->pendingOriginalRecords()),
                static_cast<unsigned>(root->status()));
        assert(recorded);
        assert(!issuers->recordingDiagnostic());
        auto& record =
            const_cast<P::Record&>(admission->originalPayload()->records()[Streams ? 9u : 6u]);
        assert(record.original.kind == (Streams ? K::BindVertexBuffer : K::BindIndexBuffer));
        ++record.bufferBindingBytes;
        assert(Issuers::submitOriginal(issuers, execution));
        --record.bufferBindingBytes;
        assert(
            root->status() == Root::Status::Pending && !issuers->hasOriginalCompletedSubmission());
        Scopes::holdScopes(true);
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    return Issuers::submitOriginal(issuers, execution);
                }));
        });
        Scopes::restoreScopes();
        Scopes::waitHeldScopes();
        assert(
            root->status() == Root::Status::Pending && !issuers->hasOriginalCompletedSubmission());
        std::weak_ptr<const Command> retainedCommand = command;
        command.reset();
        std::weak_ptr<const Issuers> retainedIssuers = issuers;
        issuers.reset();
        assert(!retainedCommand.expired() && custody->pendingOriginalRecords() == 1u);
        Scopes::releaseScopes();
        assert(WebGpuAsyncWaitFixture::until([&] {
            return root->status() == Root::Status::Committed;
        }));
        assert(root->journalEmpty() && retainedCommand.expired() && retainedIssuers.expired()
            && !custody->pendingOriginalRecords() && !custody->physicalLossArmed());
        // Expired weak observers still retain the quota-charged shared control
        // blocks. Release the observers only after proving object expiration.
        retainedCommand.reset();
        retainedIssuers.reset();
        custody.reset();
        if (submission->liveBytes() != submissionBase || callback->liveBytes() != callbackBase)
            std::fprintf(stderr,
                "geometry Execution4 cleanup: submission=%llu baseline=%llu callback=%llu baseline=%llu\n",
                static_cast<unsigned long long>(submission->liveBytes()),
                static_cast<unsigned long long>(submissionBase),
                static_cast<unsigned long long>(callback->liveBytes()),
                static_cast<unsigned long long>(callbackBase));
        assert(submission->liveBytes() == submissionBase && callback->liveBytes() == callbackBase);
        if constexpr (Streams)
            std::puts(
                "BINDLESS_RHI_STREAM_GEOMETRY_NATIVE_EXECUTION4_OK same-original14 vertex2 index1 indexedDraw1 instances2 firstInstance1 scopes3-3 queue1 actual-completion caller-drop hot0-query0 no-readback-proof no-public-promotion");
        else
            std::puts(
                "BINDLESS_RHI_GEOMETRY_NATIVE_EXECUTION4_OK same-original20 native-indexBind2 indexedDraw3 scopes3-3 queue1 Pending-before-call actual-completion caller-command-issuer-drop hot0-query0 mutation-refused legacy-profiles-refused lifecycle-reservation-baseline-retained no-public-promotion no-vertex-fetch-readback-proof");
    }

    static void verifyMetadataRefusal(const std::shared_ptr<const P>& payload, const char* label)
    {
        const auto request = requestFor(payload);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto incomplete = rhi::semantics::OriginalTypedRenderStateContract::validate(
            payload->records(), request.maxWork.commands);
        const auto inspected = A::inspectOriginalPayload(payload, request);
        assert(inspected.diagnostic && !inspected.required.work.commands
            && !inspected.required.capacity.callbackTickets);
        is(inspected.diagnostic,
            incomplete ? "RHI-ORIGINAL-TYPED-RENDER-STATE-INCOMPLETE"
                       : "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        std::printf(
            "BINDLESS_RHI_TYPED_COLD_ADMISSION_METADATA_REJECT_OK %s %s preclaim-allocation0 GPU0 original-Source-unchanged\n",
            label,
            inspected.diagnostic);
    }

    static void freezeComputeOriginal(OriginalCommandDescriptorStateFixture::Retained& actor,
        const rhi::GpuDevice& device,
        const rhi::OriginalDeviceNativeLossObservation& loss,
        const char*& id)
    {
        const auto& source =
            actor.graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
        const auto& authored = source->originalBudget();
        std::uint64_t resourceBytes = 0u;
        for (const auto& value : source->sources())
            if (const auto* buffer =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(&value))
                resourceBytes += (*buffer)->originalObject()->allocation().estimatedSizeBytes;
        const rhi::common::OriginalTypedUsageProjection::Budget usage{
            1u, true, {1u, true, 1u, 1u, true, true, 2u, 4u}, {}, 19u, 0u, 4u};
        const rhi::OriginalTypedDependencyProjectionContract::Request dependencies{
            1u, true, {1u, true, usage}, 6u, 17u, 68u, 6u, 48u, 0u};

        actor.nativeClosure = Closure::prepareOriginalTypedLossBound(actor.transaction,
            actor.stream.program,
            device,
            {2u,
                true,
                2u,
                authored.maxNativePayloadObjects,
                authored.maxGraphOwners,
                resourceBytes,
                actor.recordingQuota->limitBytes(),
                1u,
                4u},
            loss,
            id);
        assert(actor.nativeClosure && !id);
        actor.dependencyDecision = D::prepareOriginal(actor.nativeClosure,
            {1u,
                true,
                dependencies,
                actor.nativeClosure->retainedOwnerCount() + 1u,
                actor.recordingQuota->limitBytes()},
            id);
        assert(actor.dependencyDecision);
        actor.typedNativePayload = P::prepareOriginal(actor.dependencyDecision,
            OriginalTypedNativePayloadFixture::requestFor(actor.dependencyDecision),
            id);
        assert(actor.typedNativePayload);
    }

    static void verifyCompute(const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& lossQuota)
    {
        const auto& source = inputs->originalScalars()->originalUsage()->originalSources();
        const auto& authored = source->originalBudget();
        auto state = OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(
            inputs, device, true, false, false, {1u, true, 1u, 1u, true, true, 2u, 4u});
        const char* id = nullptr;
        auto loss = device.observeOriginalDeviceNativeLoss(id);
        assert(loss);
        const auto freeze = [&](auto& actor) {
            freezeComputeOriginal(actor, device, *loss, id);
        };
        freeze(state);
        auto request = requestFor(state.typedNativePayload);
        const auto preclaim = A::inspectOriginalPayload(state.typedNativePayload, request);
        if (preclaim.diagnostic)
            std::fprintf(stderr, "typed cold preclaim %s\n", preclaim.diagnostic);
        assert(!preclaim.diagnostic);
        assert(preclaim.required.work.matches(state.typedNativePayload->work())
            && preclaim.required.capacity.callbackTickets == 8u);
        const auto rejectRequest = [&](auto edit) {
            auto wrong = request;
            edit(wrong);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto refused = A::inspectOriginalPayload(state.typedNativePayload, wrong);
            assert(refused.diagnostic && !refused.required.work.commands);
            assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        };
        rejectRequest([](auto& b) {
            b.contractVersion = 0u;
        });
        rejectRequest([](auto& b) {
            b.prototypeOptIn = false;
        });
        rejectRequest([](auto& b) {
            --b.maxRetainedOwners;
        });
        rejectRequest([](auto& b) {
            --b.maxCpuMetadataBytes;
        });
        rejectRequest([](auto& b) {
            --b.maxCallbacks.recordingScopes;
        });
        rejectRequest([](auto& b) {
            --b.maxCallbacks.submissionScopes;
        });
        rejectRequest([](auto& b) {
            --b.maxWork.nativeCalls;
        });
        rejectRequest([](auto& b) {
            b.synchronizationVersion = 0u;
        });
        auto submission =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Submission)]);
        auto callback =
            core::makeShared<core::CpuMemoryQuota>(authored.maxCpuBytes[static_cast<std::size_t>(
                rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)]);
        rhi::OriginalCommandNativeLifecycleBudget budget{1u,
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
        // Wrong genuine root owns a distinct original plan/journal. Native inputs
        // remain bound to the first root and must reject its real reservation.
        {
            auto other = OriginalCommandDescriptorStateFixture::verify<rhi::ComputePipeline>(
                inputs, device, true, false, false, {1u, true, 1u, 1u, true, true, 2u, 4u});
            freeze(other);
            // A genuine seal can pass its six basic domains while an external
            // loss owner aliases Submission. Independent typed admission must
            // additionally reject that exact native/loss owner overlap.
            auto otherSeal =
                Seal::prepareOriginal(other.transaction, device, budget, lossQuota, callback, id);
            assert(otherSeal);
            auto otherReservation =
                Reservation::prepareOriginal(otherSeal, preclaim.required.capacity, id);
            assert(otherReservation);
            const auto wrong =
                A::inspectOriginal(state.typedNativePayload, otherReservation, request);
            is(wrong.diagnostic, "RHI-ORIGINAL-RECORDING-SEAL-ROOT");
            assert(!wrong.required.work.commands);
            is(A::inspectOriginal(other.typedNativePayload, otherReservation, request).diagnostic,
                "RHI-WEBGPU-TYPED-COLD-QUOTA-DOMAIN");
        }
        assert(!source->originalReservedSubmissionCount()
            && !source->originalReservedCallbackTicketCount());
        auto seal =
            Seal::prepareOriginal(state.transaction, device, budget, submission, callback, id);
        assert(seal);
        assert(state.transaction->hasOriginalRecordingSeal());
        {
            auto shortDemand = preclaim.required.capacity;
            --shortDemand.callbackTickets;
            auto shortReservation = Reservation::prepareOriginal(seal, shortDemand, id);
            assert(shortReservation);
            is(A::inspectOriginal(state.typedNativePayload, shortReservation, request).diagnostic,
                "RHI-WEBGPU-TYPED-COLD-RESERVATION-DEMAND");
        }
        auto reservation = Reservation::prepareOriginal(seal, preclaim.required.capacity, id);
        assert(reservation);
        assert(source->originalReservedSubmissionCount() == 1u
            && source->originalReservedCallbackTicketCount() == 8u
            && source->originalReservedCallbackRegistrationCount() == 8u);
        const auto factory = [&] {
            return A::prepareOriginal(state.typedNativePayload, reservation, request, id);
        };
        CpuQuotaFactoryFixture pressure(state.recordingQuota);
        const auto bytes = pressure.measureRetained(factory);
        pressure.verify(bytes, factory);
        {
            core::CpuMemoryQuotaLease full(state.recordingQuota,
                state.recordingQuota->limitBytes() - state.recordingQuota->liveBytes());
            assert(full.charged() && !factory());
            is(id, "RHI-WEBGPU-TYPED-COLD-ALLOCATION");
        }
        auto admission = factory();
        assert(admission && admission->requirements().matches(preclaim.required)
            && !admission->nativeExecutionProven());
        is(admission->executionDiagnostic(), "RHI-WEBGPU-TYPED-COLD-LIVE-EXECUTOR-MISSING");
        const std::array quotas{state.recordingQuota,
            state.stateQuota,
            state.stream.quota,
            source->originalMetadataQuota(),
            inputs->originalMetadataQuota(),
            submission,
            callback,
            lossQuota};
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    auto forged = std::shared_ptr<const A>(state.recordingQuota, admission.get());
                    is(A::validateOriginal(forged), "RHI-WEBGPU-TYPED-COLD-OWNER");
                    auto forgedReservation =
                        std::shared_ptr<const Reservation>(submission, reservation.get());
                    is(A::inspectOriginal(state.typedNativePayload, forgedReservation, request)
                            .diagnostic,
                        "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER");
                    for (const auto& value : source->sources())
                        std::visit(
                            [&](const auto& object) {
                                using T = std::remove_const_t<
                                    typename std::decay_t<decltype(object)>::element_type>;
                                if constexpr (std::is_same_v<T,
                                                  rhi::OriginalCommandComputePipelineSource>) {
                                    auto native = rhi_webgpu::webGpuNativePayloadFor(
                                        *object->originalObject());
                                    auto handle = native->pipeline;
                                    native->pipeline = nullptr;
                                    assert(admission->validate());
                                    native->pipeline = handle;
                                } else if constexpr (std::is_same_v<T,
                                                         rhi::OriginalCommandDescriptorSetSource>) {
                                    OriginalDescriptorNativeIdentityFixture::mutateActual(
                                        rhi_webgpu::webGpuNativePayloadFor(
                                            *object->originalObject()),
                                        [&] {
                                            assert(admission->validate());
                                        });
                                }
                            },
                            value);
                    return admission->validate();
                }));
        });
        assert(state.transaction->status() == rhi::OriginalCommandStateTransaction::Status::Prepared
            && state.transaction->journalEmpty());
        assert(state.transaction
                ->applyOriginalPrototypePlan()); // Public sealed CPU execution is refused.
        admission.reset();
        reservation.reset();
        assert(!source->originalReservedSubmissionCount()
            && !source->originalReservedCallbackTicketCount()
            && !source->originalReservedCallbackRegistrationCount());
        {
            auto reacquired = Reservation::prepareOriginal(seal, preclaim.required.capacity, id);
            assert(reacquired && !id);
        }
        seal.reset();
        assert(
            state.transaction->status() == rhi::OriginalCommandStateTransaction::Status::Abandoned
            && state.transaction->journalEmpty());
        assert(!Seal::prepareOriginal(state.transaction, device, budget, submission, callback, id)
            && id);
        state = {};
        assert(!submission->liveBytes() && !callback->liveBytes());
        std::printf(
            "BINDLESS_RHI_TYPED_COLD_ADMISSION_NATIVE_OK compute original19 calls14 callbacks8 same-root real-seal-reservation exact-less-exhausted forged-wrong-root-demand mutation hot0 query0 pre-record-cancel one-shot peak%llu no-GPU-work-no-receipt\n",
            static_cast<unsigned long long>(bytes));
    }
#endif
};
} // namespace arp::test
