#pragma once

#if !defined(ARP_RHI_WEBGPU_POST_SUPPRESSION_CONTRACT_PROBES)
#error "Post-suppression failure probes require the explicit browser contract build"
#endif

#include "rhi/backends/webgpu/webgpu_original_typed_callback_custody.h"

namespace arp::test {
// Test-only negative choices. No native receiver, mutable state or proof escapes.
// Implemented by the smoke's instrumented original command-preparation object;
// no definitions are linked into the ordinary backend library.
class WebGpuPostSuppressionFailureProbeFixture final {
public:
    enum class Failure { None, Commit, Publication };

    struct Facts {
        Failure failure = Failure::None;
        std::uint32_t suppressionAttempts = 0u, detachedDrainRevisits = 0u;
        bool injectedAfterSuppression = false, commitAttempted = false;
        bool committedBeforePublicationFailure = false, publicationAttempted = false;
        bool nativeQueueReceipt = false, foreignCallbacksDrained = false;
        bool lossDetached = false, lossUserdataCleared = false;
        bool physicalLossDelivered = false, originalRecordReleased = false;
    };

    static const char* arm(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedCallbackCustody>&,
        Failure) noexcept;
    static const char* arm(
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCallbackCustody>&, Failure) noexcept;
    static Facts inspect(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalTypedCallbackCustody>&) noexcept;
    static Facts inspect(
        const std::shared_ptr<const rhi_webgpu::WebGpuCanonicalCallbackCustody>&) noexcept;

private:
    template <class State>
    static const char* armState(const std::shared_ptr<State>&, Failure) noexcept;
    template <class State> static Facts inspectState(const std::shared_ptr<State>&) noexcept;
};
} // namespace arp::test
