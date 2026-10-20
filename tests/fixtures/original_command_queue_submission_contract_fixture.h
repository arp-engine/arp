#pragma once

#include "rhi/frontend/command_recording.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandQueueSubmissionContractFixture final {
public:
    static void verifyContract()
    {
        using Contract = rhi::OriginalCommandQueueSubmissionContract;
        assert(!Contract::validateIntent({1u, true}));
        assert(std::string_view(Contract::validateIntent({}))
            == "RHI-ORIGINAL-QUEUE-SUBMISSION-VERSION");
        assert(std::string_view(Contract::validateIntent({2u, true}))
            == "RHI-ORIGINAL-QUEUE-SUBMISSION-VERSION");
        assert(std::string_view(Contract::validateIntent({1u, false}))
            == "RHI-ORIGINAL-QUEUE-SUBMISSION-OPT-IN");
        for (const auto backend : {rhi::Backend::Null,
                 rhi::Backend::Vulkan,
                 rhi::Backend::Metal,
                 rhi::Backend::WebGPU,
                 static_cast<rhi::Backend>(255)}) {
            const auto d =
                arp::rhi::semantics::OriginalCommandQueueSubmissionLowering::lowering(backend);
            assert(d.diagnostic && !d.nativeSyncAvailable);
            assert(d.submissionAvailable == (backend == rhi::Backend::WebGPU));
            assert(d.completionCommitAvailable == (backend == rhi::Backend::WebGPU));
        }
        using Life = rhi::OriginalCommandNativeLifecycleContract;
        using P = Life::Phase;
        using E = Life::Event;
        using A = Life::RootAction;
        const auto reserve = Life::inspectTransition(P::Recorded, E::ReserveSubmission);
        assert(reserve.accepted() && reserve.next == P::SubmitReserved
            && reserve.action == A::KeepPending);
        assert(!Life::inspectTransition(P::SubmitReserved, E::CompletionProven).accepted());
        const auto may = Life::inspectTransition(reserve.next, E::MaySubmit);
        assert(may.accepted() && may.next == P::SubmitPending && may.action == A::KeepPending);
        const auto cancel = Life::inspectTransition(may.next, E::Cancel);
        assert(cancel.accepted() && cancel.next == P::QuarantinedAfterSubmit
            && cancel.action == A::InvalidateAndRetain);
        assert(!Life::inspectTransition(cancel.next, E::CompletionProven).accepted());
        assert(!Life::inspectTransition(cancel.next, E::BeforeSubmitScopesDrained).accepted());
        const auto drained =
            Life::inspectTransition(cancel.next, E::SubmittedQueueAndCallbacksDrained);
        assert(drained.accepted() && drained.action == A::InvalidateAndRelease);
        assert(!Life::inspectTransition(drained.next, E::MaySubmit).accepted());
        std::puts(
            "BINDLESS_RHI_CANONICAL_QUEUE_SUBMISSION_CONTRACT_OK explicit-opt-in MaySubmit-before-queue no-pre-submit-commit no-post-submit-rollback webgpu-explicit-prototype other-execution-gates-closed");
    }
};
} // namespace arp::test
