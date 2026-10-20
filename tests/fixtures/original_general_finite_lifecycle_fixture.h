#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_descriptor_buffer_binding_fixture.h"
#include "rhi/frontend/command_recording.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFiniteLifecycleFixture final {
    using Life = rhi::OriginalCommandNativeLifecycleContract;
    using Pending = rhi::OriginalInitializedBufferPendingContract;
    using Use = rhi::OriginalInitializedBufferPendingUse;
    using Texture = rhi::OriginalCompletedTextureReadPlanContract;
    using Root = rhi::OriginalCommandStateTransaction;
    using Seal = rhi::OriginalCommandRecordingSeal;
    using Reservation = rhi::OriginalCommandLifecycleReservation;

public:
    static void verifyContract()
    {
        constexpr auto use = Use::ImmutableShaderBufferRead3;
        assert(!Pending::validateUse(3u, use));
        for (const auto version : {0u, 1u, 2u, 4u, UINT32_MAX})
            assert(Pending::validateUse(version, use));
        for (const auto old :
            {Use::CopySource1, Use::ImmutableGeometryRead1, Use::ImmutableUniformRead2})
            assert(Pending::validateUse(3u, old));
        for (const bool whole : {false, true})
            for (const bool finite : {false, true})
                for (std::uint32_t content = 0u; content <= 6u; ++content) {
                    const bool selected = !whole && finite && content == 5u;
                    assert(bool(Pending::validateRootProfile(use, whole, finite, content))
                        == !selected);
                    assert(bool(Texture::validatePendingProfile(2u, true, whole, content, finite))
                        == !selected);
                    assert(bool(Texture::validatePendingProfile(1u, true, whole, content, finite))
                        == !(whole && !finite && content == 4u));
                }
        assert(Texture::validatePendingProfile(2u, false, false, 5u, true));
        assert(Texture::validatePendingProfile(3u, true, false, 5u, true));
        constexpr auto read = rhi::ResourceState::ShaderRead;
        assert(!Pending::validateStates(use, read, read, read, read, 3u));
        for (const auto wrong : {rhi::ResourceState::Undefined,
                 rhi::ResourceState::CopySrc,
                 rhi::ResourceState::CopyDst,
                 rhi::ResourceState::ShaderWrite}) {
            assert(Pending::validateStates(use, wrong, read, read, read, 3u));
            assert(Pending::validateStates(use, read, wrong, read, read, 3u));
            assert(Pending::validateStates(use, read, read, wrong, read, 3u));
            assert(Pending::validateStates(use, read, read, read, wrong, 3u));
        }
        Life::Budget budget{4u, true, 1u, 1u, 1u, 4u, 5u, 5u, 65536u, 65536u, 65536u};
        assert(!Life::validateIntent(budget) && !Life::validateContentProfile(budget, 5u));
        for (std::uint32_t content = 0u; content <= 6u; ++content)
            assert(bool(Life::validateContentProfile(budget, content)) == (content != 5u));
        for (const auto old : {1u, 2u, 3u}) {
            auto legacy = budget;
            legacy.contractVersion = old;
            assert(Life::validateContentProfile(legacy, 5u));
        }
        auto graphics = budget;
        graphics.contractVersion = 5u;
        assert(!Life::validateIntent(graphics) && Life::validateContentProfile(graphics, 5u));
        auto bad = budget;
        bad.contractVersion = 6u;
        assert(Life::validateIntent(bad));
        bad = budget;
        bad.prototypeOptIn = false;
        assert(Life::validateIntent(bad));
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend, 4u);
            assert(decision.contractVersion == 4u && decision.diagnostic
                && !decision.sealedIssuerAvailable && !decision.executionAvailable);
            assert((decision.disposition
                       == rhi::semantics::OriginalCommandNativeLifecycleLowering::Disposition::
                           ValidatorOnly)
                == (backend == rhi::Backend::Null));
        }
        std::puts(
            "BINDLESS_RHI_GENERAL_FINITE_LIFECYCLE4_CONTRACT_OK Pending3 TexturePending2 Lifecycle4 ContentPlan5 exact-finite-root legacy-isolated four-backends cpu-admission-only");
    }

#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    // Rebuild only the same original transaction between one-shot seal probes.
    // CPU interlock/capacity admission does not invoke private native events.
    template <class Factory>
    static void verifyActual(Factory&& makeRoot,
        const rhi::GpuDevice& device,
        const std::shared_ptr<core::CpuMemoryQuota>& recording)
    {
        const char* id = nullptr;
        auto root = makeRoot();
        assert(root && !root->validate());
        const auto sources = root->originalGraph()
                                 ->originalInputs()
                                 ->originalScalars()
                                 ->originalUsage()
                                 ->originalSources();
        const auto& authored = sources->originalBudget();
        using D = rhi::OriginalCommandAdmissionBudget::CpuDomain;
        auto submission = core::makeShared<core::CpuMemoryQuota>(
            authored.maxCpuBytes[static_cast<std::size_t>(D::Submission)]);
        auto callback = core::makeShared<core::CpuMemoryQuota>(
            authored.maxCpuBytes[static_cast<std::size_t>(D::Callback)]);
        const auto sealOwners = root->retainedOwnerCount() + Seal::additionalOwnerCount;
        const auto reservationOwners = sealOwners + Reservation::additionalOwnerCount;
        const Life::Budget budget{4u,
            true,
            authored.maxPendingSubmissions,
            authored.maxCallbackTickets,
            authored.maxCallbackRegistrations,
            sealOwners,
            reservationOwners,
            reservationOwners,
            recording->limitBytes(),
            submission->limitBytes(),
            callback->limitBytes()};
        const rhi::OriginalCommandLifecycleCapacityRequest capacity{
            1u, true, 1u, authored.maxCallbackTickets, authored.maxCallbackRegistrations};
        const auto baseline = recording->liveBytes();
        const auto refuseSeal = [&](const auto& intent, const auto& s, const auto& c) {
            assert(!Seal::prepareOriginal(root, device, intent, s, c, id) && id);
            assert(recording->liveBytes() == baseline && root->journalEmpty()
                && root->status() == Root::Status::Prepared && !root->validate());
        };
        for (const auto version : {1u, 2u, 3u, 5u}) {
            auto bad = budget;
            bad.contractVersion = version;
            refuseSeal(bad, submission, callback);
        }
        auto bad = budget;
        bad.prototypeOptIn = false;
        refuseSeal(bad, submission, callback);
        bad = budget;
        --bad.maxRecordingOwners;
        refuseSeal(bad, submission, callback);
        bad = budget;
        --bad.maxRecordingCpuBytes;
        refuseSeal(bad, submission, callback);
        refuseSeal(budget, submission, submission);
        std::shared_ptr<const Seal> seal;
        const auto rebuild = [&] {
            seal.reset();
            root.reset();
            assert(!recording->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
                && !sources->originalStateTransactionCount()
                && !sources->originalReservedSubmissionCount());
            root = makeRoot();
            assert(root && !root->validate());
        };
        const auto makeSeal = [&] {
            return Seal::prepareOriginal(root, device, budget, submission, callback, id);
        };
        CpuQuotaFactoryFixture recordingPressure(recording);
        const auto sealBytes = recordingPressure.measureSingleUse(rebuild, makeSeal);
        recordingPressure.verifySingleUse(sealBytes, rebuild, makeSeal);
        rebuild();
        seal = makeSeal();
        assert(seal && !id && !seal->validate());
        assert(seal->retainedOwnerCount() == sealOwners && !seal->nativeExecutionProven());
        assert(!makeSeal() && id); // sole sticky claim
        assert(root->applyOriginalPrototypePlan() && root->commitOriginalPendingOwnership()
            && root->abandonOriginalPendingOwnership() && root->journalEmpty());
        auto forgedSeal = std::shared_ptr<const Seal>(callback, seal.get());
        assert(Seal::validateOriginal(forgedSeal));
        forgedSeal.reset();
        const auto makeReservation = [&] {
            return Reservation::prepareOriginal(seal, capacity, id);
        };
        auto shortCapacity = capacity;
        --shortCapacity.callbackTickets;
        shortCapacity.pendingSubmissions = 2u;
        assert(!Reservation::prepareOriginal(seal, shortCapacity, id) && id
            && !submission->liveBytes());
        CpuQuotaFactoryFixture submissionPressure(submission);
        const auto reservationBytes = submissionPressure.measureSingleUse([] {}, makeReservation);
        submissionPressure.verify(reservationBytes, makeReservation);
        {
            core::CpuMemoryQuotaLease full(submission, submission->limitBytes());
            assert(full.charged() && !makeReservation() && id);
        }
        auto reservation = makeReservation();
        assert(reservation && !id && !reservation->validate());
        assert(reservation->retainedOwnerCount() == reservationOwners
            && !reservation->nativeExecutionProven());
        assert(!makeReservation() && id && sources->originalReservedSubmissionCount() == 1u);
        auto forgedReservation = std::shared_ptr<const Reservation>(callback, reservation.get());
        assert(Reservation::validateOriginal(forgedReservation));
        forgedReservation.reset();
        OriginalDescriptorBufferBindingFixture::verifyNoHotQueries([&] {
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{recording, submission, callback}, [&]() noexcept -> const char* {
                    return reservation->validate();
                }));
        });
        std::weak_ptr<const Root> retainedRoot = root;
        std::weak_ptr<const Seal> retainedSeal = seal;
        root.reset();
        seal.reset();
        assert(!retainedRoot.expired() && !retainedSeal.expired() && !reservation->validate());
        reservation.reset();
        assert(retainedRoot.expired() && retainedSeal.expired());
        retainedRoot.reset();
        retainedSeal.reset();
        assert(!recording->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
            && !sources->originalStateTransactionCount()
            && !sources->originalReservedSubmissionCount()
            && !sources->originalReservedCallbackTicketCount()
            && !sources->originalReservedCallbackRegistrationCount());
        std::printf(
            "BINDLESS_RHI_GENERAL_FINITE_LIFECYCLE4_OK ContentPlan5 sealOwners%u reservationOwners%u sealBytes%llu reservationBytes%llu exact-less-exhausted sticky-claim forged-caller-drop hot0-query0 original-counts-quotas-zero cpu-admission-only\n",
            sealOwners,
            reservationOwners,
            static_cast<unsigned long long>(sealBytes),
            static_cast<unsigned long long>(reservationBytes));
    }
#endif
};
} // namespace arp::test
