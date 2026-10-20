#pragma once
#include "original_command_native_lifecycle_fixture.h"
#include "rhi/frontend/command_recording.h"

namespace arp::test {
class OriginalCommandLifecycleReservationFixture final {
    using Reservation = rhi::OriginalCommandLifecycleReservation;
    using Seal = rhi::OriginalCommandRecordingSeal;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    struct Case {
        std::uint32_t submissions, tickets, registrations;
        const char* refusal;
    };

public:
    template <class Factory> static void verify(Factory&& create)
    {
        const std::array<Case, 4> cases{
            {{1u, 4u, 2u, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SUBMISSION-EXHAUSTED"},
                {2u, 3u, 2u, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CALLBACK-EXHAUSTED"},
                {2u, 4u, 1u, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REGISTRATION-EXHAUSTED"},
                {2u, 4u, 2u, nullptr}}};
        for (const auto& limits : cases)
            OriginalCommandAccessFixture::withConfiguredOriginalPlan(
                create,
                [&](auto& b) {
                    OriginalCommandNativeLifecycleFixture::configureSourceBudget(b);
                    b.maxStateTransactions = 2u;
                    b.maxPendingSubmissions = limits.submissions;
                    b.maxCallbackTickets = limits.tickets;
                    b.maxCallbackRegistrations = limits.registrations;
                },
                [&](auto& context, auto& root) {
                    const auto graph = root->originalGraph();
                    const auto& source = graph->originalInputs()
                                             ->originalScalars()
                                             ->originalUsage()
                                             ->originalSources();
                    rhi::OriginalCommandNativeLifecycleBudget budget{
                        1u, true, 1u, 2u, 1u, 33u, 34u, 1u, 65536u, 65536u, 65536u};
                    rhi::OriginalCommandLifecycleCapacityRequest request{1u, true, 1u, 2u, 1u};
                    auto submission =
                        core::makeShared<core::CpuMemoryQuota>(budget.maxSubmissionCpuBytes);
                    auto callback =
                        core::makeShared<core::CpuMemoryQuota>(budget.maxCallbackCpuBytes);
                    const char* diagnostic = nullptr;
                    auto invalidBudget = budget;
                    --invalidBudget.maxSubmissionOwners;
                    auto seal = Seal::prepareOriginal(
                        root, *context.device, invalidBudget, submission, callback, diagnostic);
                    assert(seal);
                    assert(!Reservation::prepareOriginal(seal, request, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER-BUDGET");
                    assert(!source->originalReservedSubmissionCount()
                        && !source->originalReservedCallbackTicketCount()
                        && !source->originalReservedCallbackRegistrationCount());
                    seal.reset();
                    root.reset();
                    assert(!source->originalStateTransactionCount());
                    root = OriginalCommandAccessFixture::prepareOriginalPlan(
                        graph, *context.device, context.quotas[5], diagnostic);
                    assert(root);
                    seal = Seal::prepareOriginal(
                        root, *context.device, budget, submission, callback, diagnostic);
                    assert(seal);
                    const auto make = [&] {
                        return Reservation::prepareOriginal(seal, request, diagnostic);
                    };
                    CpuQuotaFactoryFixture pressure(submission);
                    const auto peak = pressure.measure(make);
                    pressure.verify(peak, make);
                    {
                        core::CpuMemoryQuotaLease full(
                            submission, submission->limitBytes() - submission->liveBytes());
                        assert(full.charged() && !make());
                        is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-ALLOCATION");
                    }
                    assert(!source->originalReservedSubmissionCount()
                        && !source->originalReservedCallbackTicketCount()
                        && !source->originalReservedCallbackRegistrationCount()
                        && !submission->liveBytes());
                    auto invalid = request;
                    invalid.contractVersion = 0u;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-VERSION");
                    invalid = request;
                    invalid.prototypeOptIn = false;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OPT-IN");
                    invalid = request;
                    invalid.pendingSubmissions = 2u;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REQUEST");
                    invalid = request;
                    invalid.callbackTickets = 0u;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REQUEST");
                    invalid = request;
                    invalid.callbackRegistrations = 0u;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REQUEST");
                    invalid = request;
                    ++invalid.callbackTickets;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-CALLBACK-BUDGET");
                    invalid = request;
                    ++invalid.callbackRegistrations;
                    assert(!Reservation::prepareOriginal(seal, invalid, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-REGISTRATION-BUDGET");
                    auto forgedSeal = std::shared_ptr<const Seal>(callback, seal.get());
                    assert(!Reservation::prepareOriginal(forgedSeal, request, diagnostic));
                    is(diagnostic, "RHI-ORIGINAL-RECORDING-SEAL-OWNER");
                    forgedSeal.reset();
                    auto first = make();
                    assert(first && !Reservation::validateOriginal(first)
                        && first->retainedOwnerCount() == 34u);
                    assert(!make());
                    is(diagnostic, "RHI-ORIGINAL-LIFECYCLE-CAPACITY-DUPLICATE");
                    auto forged = std::shared_ptr<const Reservation>(callback, first.get());
                    is(Reservation::validateOriginal(forged),
                        "RHI-ORIGINAL-LIFECYCLE-CAPACITY-OWNER");
                    forged.reset();
                    forgedSeal = std::shared_ptr<const Seal>(callback, seal.get());
                    is(first->compareOriginalSeal(forgedSeal),
                        "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SEAL");
                    forgedSeal.reset();
                    auto peerRoot = OriginalCommandAccessFixture::prepareOriginalPlan(
                        graph, *context.device, context.quotas[5], diagnostic);
                    assert(peerRoot);
                    auto peerSubmission =
                        core::makeShared<core::CpuMemoryQuota>(budget.maxSubmissionCpuBytes);
                    auto peerCallback =
                        core::makeShared<core::CpuMemoryQuota>(budget.maxCallbackCpuBytes);
                    auto peerSeal = Seal::prepareOriginal(peerRoot,
                        *context.device,
                        budget,
                        peerSubmission,
                        peerCallback,
                        diagnostic);
                    assert(peerSeal);
                    is(first->compareOriginalSeal(peerSeal),
                        "RHI-ORIGINAL-LIFECYCLE-CAPACITY-SEAL");
                    auto second = Reservation::prepareOriginal(peerSeal, request, diagnostic);
                    if (limits.refusal) {
                        assert(!second);
                        is(diagnostic, limits.refusal);
                    } else {
                        assert(second && !second->validate());
                        second.reset();
                    }
                    assert(source->originalStateTransactionCount() == 2u
                        && source->originalReservedSubmissionCount() == 1u
                        && source->originalReservedCallbackTicketCount() == 2u
                        && source->originalReservedCallbackRegistrationCount() == 1u);
                    const std::array quotas{context.quotas[0],
                        context.quotas[1],
                        context.quotas[2],
                        context.quotas[3],
                        context.quotas[4],
                        context.quotas[5],
                        submission,
                        callback,
                        peerSubmission,
                        peerCallback};
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                        if (const auto* id = first->validate())
                            return id;
                        for (const auto backend : {rhi::Backend::Null,
                                 rhi::Backend::Vulkan,
                                 rhi::Backend::Metal,
                                 rhi::Backend::WebGPU})
                            if (first->nativeExecutionProven()
                                || !first->executionDiagnostic(backend))
                                return "TEST-CAPACITY-EXECUTION";
                        if (!root->journalEmpty() || !peerRoot->journalEmpty())
                            return "TEST-CAPACITY-JOURNAL";
                        first.reset();
                        return !source->originalReservedSubmissionCount()
                                && !source->originalReservedCallbackTicketCount()
                                && !source->originalReservedCallbackRegistrationCount()
                            ? nullptr
                            : "TEST-CAPACITY-RELEASE";
                    }));
                    second = Reservation::prepareOriginal(peerSeal, request, diagnostic);
                    assert(second && !second->validate());
                    seal.reset();
                    root.reset();
                    assert(source->originalStateTransactionCount() == 1u);
                    std::weak_ptr<rhi::OriginalCommandStateTransaction> retained = peerRoot;
                    peerRoot.reset();
                    peerSeal.reset();
                    assert(!retained.expired() && !second->validate());
                    context.device.reset();
                    is(second->validate(), "RHI-ORIGINAL-DEVICE-LIFETIME-SHUTDOWN");
                    assert(source->originalReservedSubmissionCount() == 1u
                        && source->originalReservedCallbackTicketCount() == 2u
                        && source->originalReservedCallbackRegistrationCount() == 1u);
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(quotas, [&]() noexcept {
                        second.reset();
                        if (!retained.expired())
                            return "TEST-CAPACITY-RETAINED";
                        retained.reset();
                        return !source->originalStateTransactionCount()
                                && !source->originalReservedSubmissionCount()
                                && !source->originalReservedCallbackTicketCount()
                                && !source->originalReservedCallbackRegistrationCount()
                            ? nullptr
                            : "TEST-CAPACITY-TERMINAL-RELEASE";
                    }));
                    assert(!submission->liveBytes() && !peerSubmission->liveBytes()
                        && !context.quotas[5]->liveBytes());
                    std::printf(
                        "BINDLESS_RHI_CANONICAL_LIFECYCLE_CAPACITY_OK source%u-%u-%u owner34 cold%llu original-source-alias actual-counts duplicate-cross-root-exhaustion-recovery exact-less-exhausted forged-budget-refusal caller-drop shutdown-retains-until-pre-record-release hot-allocation0 native-closed\n",
                        limits.submissions,
                        limits.tickets,
                        limits.registrations,
                        static_cast<unsigned long long>(peak));
                });
    }
};
} // namespace arp::test
