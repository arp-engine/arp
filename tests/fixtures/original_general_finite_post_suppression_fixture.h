#pragma once
#if defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
#include "webgpu_post_suppression_failure_probe_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalGeneralFinitePostSuppressionFixture final {
public:
    using Probe = WebGpuPostSuppressionFailureProbeFixture;
    using Failure = Probe::Failure;

    static const char* name(Failure failure) noexcept
    {
        return failure == Failure::Commit ? "commit" : "publication";
    }

    template <class Custody>
    static void arm(const std::shared_ptr<const Custody>& custody,
        Failure failure,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root)
    {
        assert(root && root->status() == rhi::OriginalCommandStateTransaction::Status::Pending
            && !root->journalEmpty());
        const auto* diagnostic = Probe::arm(custody, failure);
        if (diagnostic)
            std::fprintf(stderr, "Post-suppression arm: %s\n", diagnostic);
        assert(!diagnostic);
        assert(Probe::arm(custody, failure)); // Cannot re-arm or choose another completion.
        const auto held = Probe::inspect(custody);
        assert(held.failure == failure && !held.suppressionAttempts && !held.detachedDrainRevisits
            && !held.injectedAfterSuppression && !held.lossDetached && !held.physicalLossDelivered
            && !held.originalRecordReleased);
        std::printf(
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_HELD_OK %s original-submitted-root-Pending original-loss-Armed no-injection native-receipts-pending output-unpublished public-closed\n",
            name(failure));
    }

    template <class Custody>
    static void verifyTerminal(const std::shared_ptr<const Custody>& custody,
        Failure failure,
        const std::shared_ptr<rhi::OriginalCommandStateTransaction>& root,
        const char* diagnostic)
    {
        const auto facts = Probe::inspect(custody);
        assert(facts.failure == failure && facts.suppressionAttempts == 1u
            && facts.detachedDrainRevisits == 1u && facts.injectedAfterSuppression
            && facts.commitAttempted && facts.nativeQueueReceipt && facts.foreignCallbacksDrained
            && facts.lossDetached && facts.lossUserdataCleared && !facts.physicalLossDelivered
            && facts.originalRecordReleased
            && root->status() == rhi::OriginalCommandStateTransaction::Status::Invalidated
            && root->journalEmpty());
        const bool publication = failure == Failure::Publication;
        assert(facts.publicationAttempted == publication
            && facts.committedBeforePublicationFailure == publication);
        assert(diagnostic
            && core::StringView(diagnostic)
                == (publication ? "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN"
                                : "RHI-GENERAL-SUBMISSION-COMPLETION-PROFILE"));
        std::printf(
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_DIAGNOSTIC %s %s native-post-suppression-rejection\n",
            name(failure),
            diagnostic);
        for (const auto* cpu : {root->applyOriginalPrototypePlan(),
                 root->commitOriginalPendingOwnership(),
                 root->abandonOriginalPendingOwnership()})
            assert(cpu && core::StringView(cpu) == "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED");
        std::printf(
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_TERMINAL_OK %s original-root-Invalidated journal-empty suppression1 detached-revisit1 real-queue-receipt foreign-callbacks-drained loss-userdata-cleared physical-loss-undelivered committed-before-failure%u unpublished no-retry public-closed\n",
            name(failure),
            unsigned(publication));
    }
};
} // namespace arp::test
#endif
