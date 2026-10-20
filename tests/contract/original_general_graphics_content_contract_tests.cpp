#include "../fixtures/original_general_finite_graphics_content_contract_fixture.h"
#include "../fixtures/original_general_finite_graphics_usage_fixture.h"
#include "../fixtures/original_general_finite_initialized_content_fixture.h"
#include "../fixtures/original_typed_native_cold_contract_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/backends/null/null_hidden_frame_smoke.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/frontend/command_usage_projection.h"

#ifdef NDEBUG
#error Finite Graphics content contracts require assertions
#endif

int main(int argc, char** argv)
{
    arp::test::OriginalGeneralFiniteGraphicsContentContractFixture::verify();
    arp::test::OriginalGeneralFiniteInitializedContentFixture::verifyContract(
        arp::rhi::ShaderStage::Fragment);
    {
        using namespace arp::rhi;
        using C = OriginalCommandNativeClosureContract;
        using B = OriginalCommandNativeClosureBudget;
        B intent{10u, true, 8u, 10u, 128u, 2048u, 65536u, 1u, 1u};
        intent.generalShaderResourcesVersion = 2u;
        intent.generalShaderResourcesOptIn = true;
        intent.shaderBufferContent = {2u, true, 1u, 16u, 528u};
        intent.shaderTextureContent = {1u, true, 1u, 32u, 20u, 2u, 2u, 5u};
        assert(!C::validateIntent(intent));
        for (unsigned mutation = 0u; mutation < 8u; ++mutation) {
            auto bad = intent;
            switch (mutation) {
            case 0u:
                bad.prototypeOptIn = false;
                break;
            case 1u:
                bad.generalShaderResourcesVersion = 1u;
                break;
            case 2u:
                bad.generalShaderResourcesOptIn = false;
                break;
            case 3u:
                bad.shaderBufferContent.contractVersion = 1u;
                break;
            case 4u:
                bad.shaderTextureContent = {};
                break;
            case 5u:
                bad.indexContent = {1u, true, 1u, 16u, 16u};
                break;
            case 6u:
                bad.maxReadbackSources = 1u;
                break;
            default:
                bad.maxNativePayloadObjects = 0u;
                break;
            }
            assert(C::validateIntent(bad));
        }
        OriginalFiniteGraphicsContentPlanContract::Profile profile{
            5u, 6u, 5u, 5u, 6u, 6u, 4u, 4u, 2u, true, true, false};
        assert(!C::validateFiniteGraphicsProfile(profile, 6u, 5u, false, false));
        assert(C::validateFiniteGraphicsProfile(profile, 5u, 5u, false, false));
        assert(C::validateFiniteGraphicsProfile(profile, 6u, 4u, false, false));
        assert(C::validateFiniteGraphicsProfile(profile, 6u, 5u, true, false));
        assert(C::validateFiniteGraphicsProfile(profile, 6u, 5u, false, true));
        auto compute = profile;
        compute.graphicsPipeline = false;
        compute.computePipeline = true;
        assert(C::validateFiniteGraphicsProfile(compute, 6u, 5u, false, false));
        for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d =
                semantics::OriginalCommandNativeClosureLowering::finiteGraphicsLossBoundLowering(
                    backend);
            assert(d.resourceClosureAvailable == (backend == Backend::WebGPU)
                && !d.recordingIssuerAvailable && !d.executionAvailable && d.diagnostic);
            assert(
                (d.disposition
                    == semantics::OriginalCommandNativeClosureLowering::Disposition::ValidatorOnly)
                == (backend == Backend::Null));
        }
        assert(C::lossJoinVersion(10u, false, true) == 1u
            && C::pipelineSourceVersion(10u, false, true) == 1u);
        assert(!C::lossJoinVersion(10u, true, false) && !C::lossJoinVersion(10u, true, true)
            && !C::lossJoinVersion(10u, false, false));
        for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal}) {
            assert(
                !semantics::OriginalCommandNativeClosureLowering::finiteGraphicsLossBoundLowering(
                    backend)
                    .resourceClosureAvailable);
        }
        // The existing Compute /8-/9 factory must not admit this Graphics intent.
        const char* id = nullptr;
        auto nullDevice = createNullDevice();
        assert(nullDevice);
        assert(!arp::rhi_webgpu::WebGpuCanonicalNativeClosure::
                   prepareOriginalGeneralResourcesLossBound(
                       {}, {}, *nullDevice, intent, OriginalDeviceNativeLossObservation{}, id)
            && id);
        assert(arp::core::StringView(id) == "RHI-WEBGPU-CANONICAL-CLOSURE-GENERAL-VERSION");
        assert(arp::core::StringView(OriginalDeviceNativeLossObservation{}.diagnostic())
            == "RHI-ORIGINAL-DEVICE-LOSS-OBSERVATION-UNISSUED");
        assert(
            !arp::rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalFiniteGraphicsLossBound(
                {}, {}, *nullDevice, intent, OriginalDeviceNativeLossObservation{}, id)
            && arp::core::StringView(id) == "RHI-ORIGINAL-GRAPHICS-CUSTODY-NULL-VALIDATOR-ONLY");
        auto old = intent;
        old.contractVersion = 9u;
        assert(
            !arp::rhi_webgpu::WebGpuCanonicalNativeClosure::prepareOriginalFiniteGraphicsLossBound(
                {}, {}, *nullDevice, old, OriginalDeviceNativeLossObservation{}, id)
            && arp::core::StringView(id) == "RHI-WEBGPU-CANONICAL-CLOSURE-GRAPHICS-VERSION");
        for (std::uint32_t freeze = 1u; freeze <= 7u; ++freeze)
            assert(!OriginalTypedDependencyProjectionContract::closureProfileMatches(freeze, 10u));
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_INTENT_OK ContentPlan6 Stream5 isolated-Compute-factory WebGPU-private-custody backends4-record-execution-closed");
    }
    {
        using namespace arp::rhi;
        using F = OriginalTypedDependencyProjectionContract;
        const F::FreezeRequest intent{8u,
            true,
            arp::test::OriginalGeneralFiniteGraphicsUsageFixture::dependencyBudget(true, true),
            128u,
            65536u};
        assert(!F::validateFreezeIntent(intent) && F::closureProfileMatches(8u, 10u));
        for (std::uint32_t old = 1u; old <= 9u; ++old)
            assert(!F::closureProfileMatches(8u, old));
        for (std::uint32_t old = 1u; old <= 7u; ++old)
            assert(!F::closureProfileMatches(old, 10u));
        auto bad = intent;
        bad.prototypeOptIn = false;
        assert(F::validateFreezeIntent(bad));
        bad = intent;
        bad.projection.contractVersion = 5u;
        assert(F::validateFreezeIntent(bad));
        bad = intent;
        bad.projection.usages.projection.maxAttachmentReferences = 0u;
        assert(arp::core::StringView(F::validateFreezeIntent(bad))
            == "RHI-ORIGINAL-GRAPHICS-FREEZE-ATTACHMENT-REQUIRED");
        bad = intent;
        bad.projection.usages.projection.maxReadbacks = 1u;
        assert(F::validateFreezeIntent(bad));
        bad = intent;
        bad.maxRetainedOwners = 0u;
        assert(F::validateFreezeIntent(bad));
        bad = intent;
        bad.maxCpuMetadataBytes = 0u;
        assert(F::validateFreezeIntent(bad));
        bad = intent;
        bad.contractVersion = 9u;
        assert(F::validateFreezeIntent(bad));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d =
                semantics::OriginalTypedDependencyProjectionLowering::freezeLowering(backend, 8u);
            assert(d.projectionAvailable == (backend == Backend::WebGPU) && !d.executionAvailable
                && d.diagnostic);
            assert((d.disposition
                       == semantics::OriginalTypedDependencyProjectionLowering::Disposition::
                           ValidatorOnly)
                == (backend == Backend::Null));
            assert((d.disposition
                       == semantics::OriginalTypedDependencyProjectionLowering::Disposition::
                           EquivalentCpuProjection)
                == (backend == Backend::WebGPU));
            assert(semantics::OriginalTypedNativePayloadLowering::lowering(backend, 8u)
                       .payloadAvailable
                == (backend == Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_INTENT_OK Projection6 Closure10 attachment-required legacy-isolated WebGPU-private-CPU-projection backends4-no-payload-or-execution");
    }
    {
        using namespace arp::rhi;
        using C = OriginalTypedNativePayloadContract;
        C::Work work{};
        work.renderPasses = work.pipelineBinds = work.descriptorBinds = work.draws = 1u;
        work.viewports = work.scissors = work.attachmentReferences = 1u;
        work.dependencies = 6u;
        work.implicitAccesses = 8u;
        assert(!OriginalTypedNativeColdContract::inspectWorkTotals(
            work, work.commands, work.nativeCalls));
        assert(work.commands == 14u && work.nativeCalls == 9u);
        const C::Request intent{8u, true, work, 1u, 234u, 65536u};
        assert(!C::validateIntent(intent) && !C::validateWork(work, 0u, 0u, 0u, intent));
        assert(!C::validateGraphicsProfile(work, 1u) && C::profileMatches(intent, 8u, 10u));
        for (std::uint32_t old = 1u; old <= 7u; ++old) {
            auto legacy = intent;
            legacy.contractVersion = old;
            assert(!C::profileMatches(legacy, 8u, 10u) && !C::profileMatches(intent, old, 10u));
        }
        for (std::uint32_t closure = 0u; closure <= 9u; ++closure)
            assert(!C::profileMatches(intent, 8u, closure));
        auto bad = intent;
        bad.prototypeOptIn = false;
        assert(
            arp::core::StringView(C::validateIntent(bad)) == "RHI-ORIGINAL-TYPED-PAYLOAD-OPT-IN");
        bad = intent;
        bad.maxColorAttachments = 0u;
        assert(arp::core::StringView(C::validateIntent(bad))
            == "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE");
        for (const auto member : {&C::Work::renderPasses,
                 &C::Work::draws,
                 &C::Work::viewports,
                 &C::Work::scissors,
                 &C::Work::attachmentReferences}) {
            bad = intent;
            bad.maxWork.*member = 0u;
            assert(arp::core::StringView(C::validateIntent(bad))
                == "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE");
            auto changed = work;
            changed.*member = 0u;
            assert(arp::core::StringView(C::validateWork(changed, 0u, 0u, 0u, intent))
                == "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE");
        }
        for (const auto member : {&C::Work::computePasses, &C::Work::dispatches}) {
            bad = intent;
            bad.maxWork.*member = 1u;
            assert(arp::core::StringView(C::validateIntent(bad))
                == "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE");
            auto changed = work;
            changed.*member = 1u;
            assert(arp::core::StringView(C::validateWork(changed, 0u, 0u, 0u, intent))
                == "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE");
        }
        bad = intent;
        bad.maxWork.indexBinds = bad.maxWork.indexedDraws = 1u;
        assert(arp::core::StringView(C::validateIntent(bad))
            == "RHI-ORIGINAL-TYPED-PAYLOAD-GEOMETRY-SCHEMA");
        bad = intent;
        bad.maxReadbackCopies = 1u;
        assert(arp::core::StringView(C::validateIntent(bad))
            == "RHI-ORIGINAL-TYPED-PAYLOAD-READBACK-SCHEMA");
        bad = intent;
        --bad.maxWork.nativeCalls;
        assert(arp::core::StringView(C::validateWork(work, 0u, 0u, 0u, bad))
            == "RHI-ORIGINAL-TYPED-COLD-WORK-CAPACITY");
        bad = intent;
        bad.contractVersion = 9u;
        assert(C::validateIntent(bad));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = semantics::OriginalTypedNativePayloadLowering::lowering(backend, 8u);
            assert(d.payloadAvailable == (backend == Backend::WebGPU) && !d.recordingAvailable
                && !d.executionAvailable && d.diagnostic);
            assert(
                (d.disposition
                    == semantics::OriginalCommandNativeClosureLowering::Disposition::ValidatorOnly)
                == (backend == Backend::Null));
            assert((d.disposition
                       == semantics::OriginalCommandNativeClosureLowering::Disposition::Prototype)
                == (backend == Backend::WebGPU));
            assert(semantics::OriginalTypedNativeColdCommandLowering::lowering(backend, 8u)
                       .connectionAvailable
                == (backend == Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_INTENT_OK Freeze8 Closure10 commands14 calls9 graphics-only backends4 WebGPU-private-input cold-record-execution-closed");
    }
    {
        using namespace arp::rhi;
        using C = OriginalTypedNativeColdContract;
        using L = OriginalCommandNativeLifecycleContract;
        auto required = arp::test::OriginalTypedNativeColdContractFixture::graphics8Required();
        const auto& work = required.work;
        auto intent = arp::test::OriginalTypedNativeColdContractFixture::request(required);
        assert(work.commands == 14u && work.nativeCalls == 9u && work.completeGraphicsCounts());
        assert(!C::validateRequirements(required, intent));
        assert(!C::validatePayloadProfile(intent, 8u) && !C::validateLifecycleProfile(intent, 5u));
        for (std::uint32_t old = 0u; old <= 7u; ++old)
            assert(C::validatePayloadProfile(intent, old));
        for (std::uint32_t old = 0u; old <= 4u; ++old)
            assert(C::validateLifecycleProfile(intent, old));
        for (const auto member : {&C::Work::renderPasses,
                 &C::Work::draws,
                 &C::Work::viewports,
                 &C::Work::scissors,
                 &C::Work::attachmentReferences}) {
            auto bad = intent;
            bad.maxWork.*member = 0u;
            assert(arp::core::StringView(C::validateIntent(bad))
                == "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE");
            auto forged = required;
            forged.work.*member = 0u;
            assert(arp::core::StringView(C::validateRequirements(forged, intent))
                == "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE");
        }
        for (const auto member : {&C::Work::computePasses, &C::Work::dispatches}) {
            auto bad = intent;
            bad.maxWork.*member = 1u;
            assert(arp::core::StringView(C::validateIntent(bad))
                == "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE");
            auto forged = required;
            forged.work.*member = 1u;
            assert(arp::core::StringView(C::validateRequirements(forged, intent))
                == "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE");
        }
        auto bad = intent;
        bad.maxWork.indexedDraws = bad.maxWork.indexBinds = 1u;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.readback.contractVersion = 1u;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.maxTransfers.passes = 1u;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.prototypeOptIn = false;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.contractVersion = 9u;
        assert(C::validateIntent(bad));
        bad = intent;
        --bad.maxWork.nativeCalls;
        assert(C::validateRequirements(required, bad));
        auto forged = required;
        --forged.capacity.callbackTickets;
        assert(C::validateRequirements(forged, intent));
        forged = required;
        --forged.capacity.callbackRegistrations;
        assert(C::validateRequirements(forged, intent));
        // The separate native custody ceiling never enlarges the source/root
        // owner or CPU budgets. These are hostile value fixtures, not actors.
        bad = intent;
        bad.maxRetainedOwners = 69u;
        assert(!C::validateOwnerCapacity(bad, 64u, 64u, 5u, 46u, 48u));
        --bad.maxRetainedOwners;
        assert(C::validateOwnerCapacity(bad, 64u, 64u, 5u, 46u, 48u));
        bad.maxRetainedOwners = 70u;
        assert(C::validateOwnerCapacity(bad, 64u, 64u, 5u, 46u, 48u));
        bad.maxRetainedOwners = 69u;
        assert(C::validateOwnerCapacity(bad, 64u, 64u, 5u, 49u, 48u));
        assert(C::validateOwnerCapacity(bad, UINT32_MAX, UINT32_MAX, 5u, 46u, 48u));
        const L::Budget lifecycle{5u, true, 1u, 8u, 8u, 69u, 69u, 69u, 65536u, 65536u, 65536u};
        assert(!L::validateIntent(lifecycle) && !L::validateContentProfile(lifecycle, 6u));
        for (std::uint32_t content = 0u; content <= 5u; ++content)
            assert(L::validateContentProfile(lifecycle, content));
        for (std::uint32_t old = 1u; old <= 4u; ++old) {
            auto legacy = lifecycle;
            legacy.contractVersion = old;
            assert(arp::core::StringView(L::validateContentProfile(legacy, 6u))
                == "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-CONTENT-PROFILE-UNAVAILABLE");
            auto legacyCold = intent;
            legacyCold.contractVersion = old;
            assert(C::validateLifecycleProfile(legacyCold, 5u));
        }
        auto noOptIn = lifecycle;
        noOptIn.prototypeOptIn = false;
        assert(L::validateIntent(noOptIn));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto cold =
                semantics::OriginalTypedNativeColdLowering::admissionLowering(backend, 8u);
            assert(cold.admissionAvailable == (backend == Backend::WebGPU)
                && !cold.recordingAvailable && !cold.executionAvailable && cold.diagnostic);
            assert(
                (cold.disposition
                    == semantics::OriginalCommandNativeClosureLowering::Disposition::ValidatorOnly)
                == (backend == Backend::Null));
            const auto seal =
                semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 5u);
            assert(seal.contractVersion == 5u && !seal.sealedIssuerAvailable
                && !seal.executionAvailable && seal.diagnostic);
            assert((seal.disposition
                       == semantics::OriginalCommandAdmissionLowering::Disposition::ValidatorOnly)
                == (backend == Backend::Null));
            assert(semantics::OriginalTypedNativeColdCommandLowering::lowering(backend, 8u)
                       .connectionAvailable
                == (backend == Backend::WebGPU));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_LIFECYCLE5_INTENT_OK Payload8 ContentPlan6 commands14 calls9 callbacks8 graphics-only legacy-content6-refused native-owner-source-budgets-independent backends4-WebGPU-private-admission-record-execution-closed");
    }
    {
        using namespace arp::rhi;
        using C = OriginalTypedNativeColdCommandContract;
        constexpr std::uint32_t admissionOwners = 238u;
        constexpr auto ceiling = admissionOwners + C::additionalOwners;
        const C::Request intent{8u, true, 1u, ceiling, 65536u};
        OriginalCommandAdmissionBudget source;
        source.maxGraphOwners = 48u;
        const OriginalCommandNativeLifecycleBudget lifecycle{
            5u, true, 1u, 8u, 8u, ceiling, ceiling, ceiling, 65536u, 65536u, 65536u};
        assert(!C::validateIntent(intent) && !C::validateProfile(intent, 8u));
        assert(!C::validateOwners(admissionOwners, intent, source, lifecycle, ceiling, 46u));
        for (std::uint32_t old = 0u; old <= 7u; ++old)
            assert(C::validateProfile(intent, old));
        for (std::uint32_t old = 1u; old <= 4u; ++old) {
            auto legacy = lifecycle;
            legacy.contractVersion = old;
            assert(arp::core::StringView(
                       C::validateOwners(admissionOwners, intent, source, legacy, ceiling, 46u))
                == "RHI-ORIGINAL-TYPED-COLD-COMMAND-LIFECYCLE-PROFILE");
        }
        auto bad = intent;
        bad.contractVersion = 9u;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.prototypeOptIn = false;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.maxCommandPayloads = 2u;
        assert(C::validateIntent(bad));
        bad = intent;
        bad.maxCpuMetadataBytes = 0u;
        assert(C::validateIntent(bad));
        bad = intent;
        --bad.maxRetainedOwners;
        assert(C::validateOwners(admissionOwners, bad, source, lifecycle, ceiling, 46u));
        bad = intent;
        ++bad.maxRetainedOwners;
        assert(C::validateOwners(admissionOwners, bad, source, lifecycle, ceiling, 46u));
        assert(C::validateOwners(admissionOwners, intent, source, lifecycle, admissionOwners, 46u));
        assert(C::validateOwners(admissionOwners, intent, source, lifecycle, ceiling, 49u));
        assert(C::validateOwners(UINT32_MAX, intent, source, lifecycle, ceiling, 46u));
        for (const auto member : {&OriginalCommandNativeLifecycleBudget::maxRecordingOwners,
                 &OriginalCommandNativeLifecycleBudget::maxSubmissionOwners,
                 &OriginalCommandNativeLifecycleBudget::maxCallbackOwners}) {
            auto shortLifecycle = lifecycle;
            --(shortLifecycle.*member);
            assert(
                C::validateOwners(admissionOwners, intent, source, shortLifecycle, ceiling, 46u));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto d = semantics::OriginalTypedNativeColdCommandLowering::lowering(backend, 8u);
            assert(d.connectionAvailable == (backend == Backend::WebGPU) && !d.recordingAvailable
                && !d.executionAvailable && d.diagnostic);
            assert(
                (d.disposition
                    == semantics::OriginalCommandNativeClosureLowering::Disposition::ValidatorOnly)
                == (backend == Backend::Null));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_INTENT_OK Cold8 Lifecycle5 ContentPlan6 command1 owner-add3 exact-predeclared-ceiling legacy-refused source-root-unchanged WebGPU-private-connection-record-execution-closed");
    }
    {
        using namespace arp::rhi;
        using C = OriginalTypedCallbackCustodyContract;
        const auto cold = arp::test::OriginalTypedNativeColdContractFixture::graphics8Required();
        const C::Request intent{8u,
            true,
            OriginalTypedLossObserverPolicy::RetainPhysicalLossUntilLocalDrain1,
            1u,
            cold.capacity.callbackTickets,
            1u,
            252u,
            65536u,
            65536u};
        C::Participants participants;
        assert(!C::inspect(cold, intent, participants) && C::additionalOwners(intent) == 11u
            && participants.commandNativeCallbacks == 7u && participants.reservedCallbacks == 8u
            && participants.sharedNativeLossCallbacks == 1u
            && participants.localObserverCallbacks == 1u);
        const auto refused = [&](const auto& required, const auto& request) {
            participants = {1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
            assert(C::inspect(required, request, participants) && !participants.reservedCallbacks
                && !participants.sharedNativeLossCallbacks
                && !participants.commandNativeRegistrations);
        };
        for (std::uint32_t old = 0u; old < 8u; ++old) {
            auto legacy = intent;
            legacy.contractVersion = old;
            refused(cold, legacy);
        }
        auto bad = intent;
        bad.contractVersion = 9u;
        refused(cold, bad);
        bad = intent;
        bad.prototypeOptIn = false;
        refused(cold, bad);
        bad = intent;
        bad.lossPolicy = OriginalTypedLossObserverPolicy::Unspecified;
        refused(cold, bad);
        bad = intent;
        bad.readbackMap = {1u, true};
        refused(cold, bad);
        for (const auto member : {&C::Request::maxPendingRecords,
                 &C::Request::maxCallbackSlots,
                 &C::Request::maxLocalLossObservers}) {
            bad = intent;
            --(bad.*member);
            refused(cold, bad);
            bad = intent;
            ++(bad.*member);
            refused(cold, bad);
        }
        for (const auto member : {&OriginalTypedNativeColdContract::Work::renderPasses,
                 &OriginalTypedNativeColdContract::Work::draws,
                 &OriginalTypedNativeColdContract::Work::viewports,
                 &OriginalTypedNativeColdContract::Work::scissors,
                 &OriginalTypedNativeColdContract::Work::attachmentReferences}) {
            auto forged = cold;
            forged.work.*member = 0u;
            refused(forged, intent);
        }
        auto mixed = cold;
        mixed.work.computePasses = mixed.work.dispatches = 1u;
        refused(mixed, intent);
        mixed = cold;
        mixed.transfers = {1u, 1u, 1u};
        refused(mixed, intent);
        mixed = cold;
        --mixed.existingDeviceLossCallbacks;
        refused(mixed, intent);
        OriginalCommandAdmissionBudget source;
        source.maxGraphOwners = 48u;
        OriginalCommandNativeLifecycleBudget lifecycle{
            5u, true, 1u, 8u, 8u, 252u, 252u, 252u, 65536u, 65536u, 65536u};
        assert(!C::validateOwners(241u, 0u, intent, 252u, 252u, source, lifecycle, 46u));
        bad = intent;
        --bad.maxRetainedOwners;
        assert(C::validateOwners(241u, 0u, bad, 252u, 252u, source, lifecycle, 46u));
        bad = intent;
        ++bad.maxRetainedOwners;
        assert(C::validateOwners(241u, 0u, bad, 252u, 252u, source, lifecycle, 46u));
        assert(C::validateOwners(241u, 0u, intent, 241u, 252u, source, lifecycle, 46u));
        assert(C::validateOwners(241u, 0u, intent, 252u, 241u, source, lifecycle, 46u));
        assert(C::validateOwners(241u, 0u, intent, 252u, 252u, source, lifecycle, 49u));
        assert(C::validateOwners(UINT32_MAX, 0u, intent, 252u, 252u, source, lifecycle, 46u));
        for (std::uint32_t old = 1u; old < 5u; ++old) {
            auto wrong = lifecycle;
            wrong.contractVersion = old;
            assert(C::validateOwners(241u, 0u, intent, 252u, 252u, source, wrong, 46u));
        }
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision =
                semantics::OriginalTypedCallbackCustodyLowering::lowering(backend, 8u);
            assert(decision.validatorOnly == (backend == Backend::Null)
                && decision.observerPreparationAvailable == (backend == Backend::WebGPU)
                && !decision.recordingAvailable && !decision.executionAvailable
                && decision.diagnostic);
            const auto issuer =
                semantics::OriginalTypedIssuerPreparationLowering::lowering(backend, 8u);
            assert(issuer.preparationAvailable == (backend == Backend::WebGPU)
                && !issuer.recordingAvailable && !issuer.submissionAvailable && issuer.diagnostic);
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_INTENT_OK Command8 Cold8 Lifecycle5 ContentPlan6 callbacks8 owner-add11 exact-predeclared-native-ceiling source-root-unchanged WebGPU-private-observer-record-execution-closed");
    }
    if (argc == 2 && arp::core::StringView(argv[1]) == "--smoke") {
        auto device = arp::rhi::createNullDevice();
        assert(device);
        const auto decision =
            arp::rhi::OriginalFiniteGraphicsContentPlanContract::lowering(device->backend());
        assert(decision.validatorOnly && !decision.rootAvailable && !decision.executionAvailable);
        using namespace arp;
        using Root = rhi::OriginalCommandStateTransaction;
        const rhi::OriginalFiniteGraphicsContentPlanBudget content{
            6u, true, 1u, 1u, 1u, 2u, 2u, 42u, 256u};
        const auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const auto reject = [&](const auto& value, const char* expected) {
            const char* id = nullptr;
            auto root =
                Root::prepareOriginalGraphicsOwnership<rhi::common::OriginalTypedUsageProjection,
                    rhi::common::OriginalTypedDependencyProjection>(
                    std::shared_ptr<const rhi::OriginalCommandResourceStates>{},
                    std::shared_ptr<const rhi::OriginalCommandStreamProgram>{},
                    *device,
                    {},
                    {},
                    value,
                    {},
                    quota,
                    id);
            assert(!root && id && core::StringView(id) == expected && !quota->liveBytes());
        };
        reject(content, decision.diagnostic);
        auto changed = content;
        changed.prototypeOptIn = false;
        reject(changed, "RHI-FINITE-GRAPHICS-CONTENT-OPT-IN");
        changed = content;
        changed.contractVersion = 5u;
        reject(changed, "RHI-FINITE-GRAPHICS-CONTENT-VERSION");
        changed = content;
        changed.maxProjectionVisits = 0u;
        reject(changed, "RHI-FINITE-GRAPHICS-CONTENT-BUDGET");
        arp::rhi::NullHiddenFrameSmokeRunner runner(*device);
        const auto report = runner.run({"FiniteGraphicsContent6Schema",
            {320u, 180u},
            arp::rhi::TextureFormat::Bgra8Unorm,
            2u});
        if (!report.success)
            std::fprintf(stderr, "Null hidden-frame: %s\n", report.diagnostics.c_str());
        assert(report.success && report.submittedFrameCount == 2u
            && report.descriptorCompletenessValidated && !report.nativeHandleExposedToUpperLayer
            && !report.resourceStats.pendingDestroyCount);
        std::puts(
            "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_NULL_SMOKE_OK frames2 validator-only no-mixed-root-or-native-evidence");
    }
}
