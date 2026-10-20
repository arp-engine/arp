#pragma once
#include "original_typed_cold_admission_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h"
#include "rhi/frontend/command_recording.h"
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
#include "webgpu_async_wait_fixture.h"
#include "webgpu_error_scope_handoff_fixture.h"
#endif

namespace arp::test {
// Acceptance over the existing attachment actor, never a proof issuer. Only the
// actual original Recording/Submission may apply/commit its sealed journal.
class OriginalTypedNativeGraphicsExecutionFixture final {
    using Admission = rhi_webgpu::WebGpuOriginalTypedColdAdmission;
    using Issuers = rhi_webgpu::WebGpuOriginalTypedIssuerPreparation;
    using Root = rhi::OriginalCommandStateTransaction;
    static constexpr std::uint32_t owners = 128u;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        if (!actual || core::StringView(actual) != expected)
            std::fprintf(stderr,
                "typed graphics expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && core::StringView(actual) == expected);
    }

public:
    static void authorizeSource(rhi::OriginalCommandAdmissionBudget& source) noexcept
    {
        // The selected original actor retains Buffer/Texture/View/Pipeline/Set,
        // plus one encoder and one command buffer, and eleven native calls.
        source.maxNativePayloadObjects = 5u + 1u + 1u;
        const auto demand = Admission::lifecycleRequirements().capacity;
        source.maxPendingSubmissions = demand.pendingSubmissions;
        source.maxCallbackTickets = demand.callbackTickets;
        source.maxCallbackRegistrations = demand.callbackRegistrations;
    }

    static void verifyContract()
    {
        rhi::OriginalCommandAdmissionBudget source;
        source.contractVersion = 1u;
        source.prototypeOptIn = true;
        source.maxCommands = 16u;
        source.maxGraphOwners = owners;
        source.maxCpuBytes.fill(bytes);
        source.maxTypedNativeCommandCalls = 11u;
        authorizeSource(source);
        auto demand = Admission::lifecycleRequirements();
        demand.work.nativeCalls = 11u;
        assert(!rhi::OriginalTypedNativeColdContract::validateSourceAuthority(demand, 5u, source));
        auto wrong = source;
        --wrong.maxNativePayloadObjects;
        is(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(demand, 5u, wrong),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-PAYLOAD-CAPACITY");
        wrong = source;
        --wrong.maxCallbackRegistrations;
        is(rhi::OriginalTypedNativeColdContract::validateSourceAuthority(demand, 5u, wrong),
            "RHI-ORIGINAL-TYPED-COLD-SOURCE-REGISTRATION-CAPACITY");
        std::puts(
            "BINDLESS_RHI_TYPED_NATIVE_GRAPHICS_CONTRACT_OK original-attachment-actor explicit-source7 calls11 pending1 callback8 no-metadata-promotion");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <bool finite = false, class Context>
    static void execute(OriginalCommandDescriptorStateFixture::Retained& actor, Context& context)
    {
        using Seal = rhi::OriginalCommandRecordingSeal;
        using Reservation = rhi::OriginalCommandLifecycleReservation;
        using Command = rhi_webgpu::WebGpuOriginalTypedColdCommand;
        using Custody = rhi_webgpu::WebGpuOriginalTypedCallbackCustody;
        using Scopes = WebGpuErrorScopeHandoffFixture<24u>;
        constexpr rhi::OriginalTypedNativeExecutionContract::Request intent{
            1u, true, rhi::OriginalTypedSynchronizationPolicy::FrozenOriginalScopeVisibility1};
        const char* id = nullptr;
        actor.typedNativePayload =
            rhi_webgpu::WebGpuOriginalTypedNativePayload::prepareOriginal(actor.dependencyDecision,
                OriginalTypedNativePayloadFixture::requestFor(actor.dependencyDecision),
                id);
        assert(actor.typedNativePayload && !id);
        auto request = OriginalTypedColdAdmissionFixture::requestFor(actor.typedNativePayload);
        request.maxRetainedOwners = owners;
        const auto inspected = Admission::inspectOriginalPayload(actor.typedNativePayload, request);
        if (inspected.diagnostic)
            std::fprintf(stderr, "typed graphics admission: %s\n", inspected.diagnostic);
        assert(!inspected.diagnostic);
        auto submission = core::makeShared<core::CpuMemoryQuota>(bytes),
             callback = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            auto seal = Seal::prepareOriginal(actor.transaction,
                *context.device,
                {1u, true, 1u, 8u, 8u, owners, owners, owners, bytes, bytes, bytes},
                submission,
                callback,
                id);
            assert(seal);
            auto reservation = Reservation::prepareOriginal(seal, inspected.required.capacity, id);
            assert(reservation);
            auto admission =
                Admission::prepareOriginal(actor.typedNativePayload, reservation, request, id);
            assert(admission);
            auto command = Command::prepareOriginal(admission, {1u, true, 1u, owners, bytes}, id);
            assert(command);
            auto custody = Custody::prepareOriginal(command,
                {1u,
                    true,
                    rhi::OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
                    1u,
                    8u,
                    1u,
                    owners,
                    bytes,
                    bytes},
                id);
            assert(custody);
            auto issuers = Issuers::prepareOriginal(
                custody, {1u, true, 1u, 1u, 7u, owners, bytes, bytes, bytes}, id);
            assert(issuers);
            Scopes::holdScopes();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{actor.recordingQuota, submission, callback},
                [&]() noexcept -> const char* {
                    return Issuers::recordOriginal(issuers, intent);
                }));
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            assert(actor.transaction->status() == Root::Status::Pending
                && !actor.transaction->journalEmpty() && !issuers->hasOriginalRecordedCommand());
            assert(actor.nativeClosure->validate() && actor.dependencyDecision->validate()
                && actor.typedNativePayload->validate() && admission->validate()
                && command->validate() && custody->validate() && issuers->validate());
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuers->hasOriginalRecordedCommand();
            }));
            if constexpr (finite) {
                using Source = rhi::OriginalCommandDescriptorSetSource;
                const auto& sources = actor.stream.program->originalInputs()
                                          ->originalScalars()
                                          ->originalUsage()
                                          ->originalSources()
                                          ->sources();
                const auto& original = std::get<std::shared_ptr<const Source>>(sources[4]);
                assert(original->sourceContractVersion() == 3u
                    && original->frozenFiniteDeclarations().size() == 2u);
                auto& element = const_cast<Source::FrozenElement&>(original->frozenElements()[0]);
                ++element.declaration.bufferAccessBytes;
                is(Issuers::submitOriginal(issuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-COVERAGE");
                --element.declaration.bufferAccessBytes;
                ++element.declaration.bufferAccessOffset;
                is(Issuers::submitOriginal(issuers, intent), "RHI-ORIGINAL-FINITE-ACCESS-COVERAGE");
                --element.declaration.bufferAccessOffset;
                const auto stages = element.declaration.stages;
                element.declaration.stages = rhi::PipelineStage::None;
                is(Issuers::submitOriginal(issuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-STAGES");
                element.declaration.stages = stages;
                const auto access = element.declaration.access;
                element.declaration.access = rhi::ResourceAccess::None;
                is(Issuers::submitOriginal(issuers, intent),
                    "RHI-ORIGINAL-FINITE-SOURCE-FOOTPRINT-ACCESS");
                element.declaration.access = access;
            }
            const auto& passes = actor.typedNativePayload->passes();
            assert(passes.size() == 2u);
            auto& color = const_cast<WGPURenderPassColorAttachment&>(passes[1].colors.values()[0]);
            const auto load = color.loadOp;
            color.loadOp = WGPULoadOp_Clear;
            is(Issuers::submitOriginal(issuers, intent), "RHI-WEBGPU-TYPED-PAYLOAD-PASS-FIELDS");
            color.loadOp = load;
            auto& nativePass = const_cast<rhi_webgpu::WebGpuOriginalRenderPassPayload&>(passes[0]);
            ++nativePass.maxDraw.maxDrawCount;
            is(Issuers::submitOriginal(issuers, intent), "RHI-WEBGPU-TYPED-PAYLOAD-PASS-FIELDS");
            --nativePass.maxDraw.maxDrawCount;
            auto& dependency =
                const_cast<rhi_webgpu::WebGpuOriginalTypedDependencyDecision::Record&>(
                    actor.dependencyDecision->records()[2]);
            assert(dependency.original.following);
            const auto following = *dependency.original.following;
            ++dependency.original.following->scope.first;
            is(Issuers::submitOriginal(issuers, intent),
                "RHI-ORIGINAL-DEPENDENCY-PROJECTION-FROZEN-BINDING");
            dependency.original.following = following;
            assert(actor.transaction->status() == Root::Status::Pending
                && !issuers->hasOriginalCompletedSubmission());
            Scopes::holdScopes();
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{actor.recordingQuota, submission, callback},
                [&]() noexcept -> const char* {
                    return Issuers::submitOriginal(issuers, intent);
                }));
            Scopes::restoreScopes();
            Scopes::waitHeldScopes();
            assert(actor.transaction->status() == Root::Status::Pending
                && !issuers->hasOriginalCompletedSubmission());
            Scopes::releaseScopes();
            assert(WebGpuAsyncWaitFixture::until([&] {
                return issuers->hasOriginalCompletedSubmission();
            }));
            assert(actor.transaction->status() == Root::Status::Committed
                && actor.transaction->journalEmpty() && !custody->pendingOriginalRecords()
                && !custody->physicalLossArmed());
            if constexpr (!finite)
                std::puts(
                    "BINDLESS_RHI_TYPED_NATIVE_GRAPHICS_EXECUTION_OK original16 render2 ClearStore-LoadStore real-scopes3-3 queue1 original-Pending-commit cold-guards7 mutation3 independent-hot0");
            else
                std::puts(
                    "BINDLESS_RHI_TYPED_NATIVE_FINITE_GRAPHICS_EXECUTION_OK Source3 original16 render2 target-minimum-views real-scopes3-3 queue1 original-Pending-commit cold-guards7 mutation7 independent-hot0");
        }
        assert(!submission->liveBytes() && !callback->liveBytes());
    }
#endif
};
} // namespace arp::test
