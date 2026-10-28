#include "../fixtures/frame_input_lifecycle_checks.h"
#include "core/memory/cpu_allocator.h"
#include "platform/frame_input_admission.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/gpu_swapchain.h"

#include <atomic>
#include <cassert>
#include <cstdio>

namespace {
class DiscardFailureSwapchain final : public arp::rhi::Swapchain {
public:
    explicit DiscardFailureSwapchain(arp::rhi::Swapchain& wrapped) : wrapped_(wrapped) {}

    std::uint32_t imageCount() const override { return wrapped_.imageCount(); }

    std::uint32_t maxFramesInFlight() const override { return wrapped_.maxFramesInFlight(); }

    arp::rhi::AcquiredFrame acquireNextFrame() override
    {
        ++acquireCalls;
        return wrapped_.acquireNextFrame();
    }

    bool isFrameAcquired(const arp::rhi::AcquiredFrame& frame) const noexcept override
    {
        return wrapped_.isFrameAcquired(frame);
    }

    arp::rhi::AcquiredSwapchainImage acquiredImage(const arp::rhi::AcquiredFrame& frame) override
    {
        return wrapped_.acquiredImage(frame);
    }

    arp::rhi::FrameDiscardResult discardFrame(const arp::rhi::AcquiredFrame& frame) override
    {
        ++discardCalls;
        if (failDiscard) {
            if (invalidateOnFailure)
                assert(wrapped_.discardFrame(frame).success);
            return arp::rhi::makeFrameDiscardFailureResult(
                "TEST-NATIVE-DISCARD-FAILED", frame.frameSerial, frame.frameIndex);
        }
        return wrapped_.discardFrame(frame);
    }

    arp::rhi::PresentResult present(const arp::rhi::AcquiredFrame& frame,
        const arp::rhi::FramePresentationCompletion& completion = {}) override
    {
        return wrapped_.present(frame, completion);
    }

    arp::rhi::SwapchainLifecycleResult setSurfaceAvailable(bool available) override
    {
        return wrapped_.setSurfaceAvailable(available);
    }

    arp::rhi::SwapchainLifecycleResult resize(const arp::rhi::Extent2D& extent) override
    {
        return wrapped_.resize(extent);
    }

    arp::rhi::SwapchainLifecycleResult reconfigure(
        const arp::rhi::SwapchainReconfigureRequest& request) override
    {
        return wrapped_.reconfigure(request);
    }

    arp::rhi::SwapchainPresentationStatus presentationStatus() const override
    {
        return wrapped_.presentationStatus();
    }

    bool failDiscard = true;
    bool invalidateOnFailure = false;
    unsigned discardCalls = 0u;
    unsigned acquireCalls = 0u;

private:
    arp::rhi::Swapchain& wrapped_;
};

class ControlledCompletionFence final : public arp::rhi::GpuFence {
public:
    void signal() override { signaled_.store(true, std::memory_order_release); }

    bool isSignaled() const override { return signaled_.load(std::memory_order_acquire); }

    void wait() override {}

    void reset() override { signaled_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> signaled_{false};
};

void verifyPresentationCompletionRetiresTheExactSlot()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    auto swapchain = device->createSwapchain({
        "PresentationCompletion",
        {16u, 16u},
        rhi::TextureFormat::Bgra8Unorm,
        2u,
    });
    assert(swapchain);
    auto pendingFence = core::makeShared<ControlledCompletionFence>();
    const auto first = swapchain->acquireNextFrame();
    assert(first.success && first.frameIndex == 0u);
    assert(swapchain->present(first, {pendingFence}).success);
    const auto second = swapchain->acquireNextFrame();
    assert(second.success && second.frameIndex == 1u);
    const auto blocked = swapchain->acquireNextFrame();
    assert(!blocked.success && blocked.status == rhi::AcquireStatus::NotReady);
    pendingFence->signal();
    const auto recycled = swapchain->acquireNextFrame();
    assert(recycled.success && recycled.frameIndex == first.frameIndex);
    assert(swapchain->discardFrame(recycled).success);
    assert(swapchain->discardFrame(second).success);
}

void verifySharedReadinessBeforeAcquisition()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    auto swapchain = device->createSwapchain(
        {"SharedInputReadiness", {16u, 16u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    assert(swapchain);
    DiscardFailureSwapchain observed(*swapchain);
    observed.failDiscard = false;
    platform::FrameInputAdmission admission;
    bool sharedReady = false;
    bool slotReady = false;
    unsigned slotQueries = 0u;
    unsigned sharedQueries = 0u;
    const auto poll = [&] {
        return admission.acquire(
            observed,
            [&](std::uint32_t) {
                ++slotQueries;
                return slotReady;
            },
            [&] {
                ++sharedQueries;
                return sharedReady;
            });
    };
    for (unsigned i = 0u; i < 8u; ++i) {
        const auto pending = poll();
        assert(!pending.success && pending.status == rhi::AcquireStatus::NotReady
            && pending.recoverable && !pending.swapchainRebuildRequired
            && pending.frameSerial == 0u);
    }
    assert(!admission.pending() && observed.acquireCalls == 0u && slotQueries == 0u);
    sharedReady = true;
    const auto slotPending = poll();
    assert(!slotPending.success && slotPending.status == rhi::AcquireStatus::NotReady
        && slotPending.frameSerial != 0u && admission.pending());
    assert(observed.acquireCalls == 1u && slotQueries == 1u);
    sharedReady = false;
    for (unsigned i = 0u; i < 8u; ++i) {
        const auto retained = poll();
        assert(retained.frameSerial == slotPending.frameSerial
            && retained.frameIndex == slotPending.frameIndex && !retained.success);
    }
    assert(observed.acquireCalls == 1u && slotQueries == 1u);
    assert(observed
            .discardFrame(
                rhi::makeAcquireSuccessResult(slotPending.frameSerial, slotPending.frameIndex))
            .success);
    const auto beforeInvalidation = sharedQueries;
    const auto invalidated = poll();
    assert(!invalidated.success && invalidated.status == rhi::AcquireStatus::NotReady
        && invalidated.diagnostics == "PLATFORM-FRAME-INPUT-ACQUISITION-INVALIDATED");
    assert(!admission.pending() && sharedQueries == beforeInvalidation);
    sharedReady = slotReady = true;
    const auto first = poll();
    assert(first.success && !admission.pending() && observed.acquireCalls == 2u);
    const auto second = poll();
    assert(second.success && second.frameIndex != first.frameIndex && observed.acquireCalls == 3u);
    assert(observed.discardFrame(first).success && observed.discardFrame(second).success);
    slotReady = false;
    assert(poll().status == rhi::AcquireStatus::NotReady && admission.pending());
    sharedReady = false;
    assert(admission.cancel().success && !admission.pending());
}

void verifyDiscardFailureRetention()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    auto swapchain =
        device->createSwapchain({"DiscardFailure", {16u, 16u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    DiscardFailureSwapchain injected(*swapchain);
    platform::FrameInputAdmission admission;
    assert(admission
               .acquire(injected,
                   [](std::uint32_t) {
                       return false;
                   })
               .status
        == rhi::AcquireStatus::NotReady);
    const auto failed = admission.cancel();
    assert(!failed.success && failed.diagnostics == "TEST-NATIVE-DISCARD-FAILED");
    assert(admission.pending() && injected.discardCalls == 1u);
    injected.failDiscard = false;
    assert(admission.cancel().success && !admission.pending());
    assert(injected.discardCalls == 2u);
    injected.failDiscard = true;
    injected.invalidateOnFailure = true;
    assert(admission
               .acquire(injected,
                   [](std::uint32_t) {
                       return false;
                   })
               .status
        == rhi::AcquireStatus::NotReady);
    const auto releasedButFailed = admission.cancel();
    assert(!releasedButFailed.success && admission.pending());
    const auto alreadyInvalidated = admission.cancel();
    assert(alreadyInvalidated.success
        && alreadyInvalidated.status == rhi::FrameDiscardStatus::AlreadyInvalidated);
    assert(!admission.pending() && injected.discardCalls == 3u);
}

void verifyPendingSurfaceInvalidation()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    auto swapchain =
        device->createSwapchain({"PendingLoss", {16u, 16u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    assert(swapchain);
    platform::FrameInputAdmission admission;
    const auto pending = admission.acquire(*swapchain, [](std::uint32_t) {
        return false;
    });
    assert(pending.status == rhi::AcquireStatus::NotReady);
    assert(swapchain->setSurfaceAvailable(false).success);
    assert(swapchain->setSurfaceAvailable(true).success);
    const auto resumed = admission.acquire(*swapchain, [](std::uint32_t) {
        return true;
    });
    const auto image = swapchain->acquiredImage(resumed);
    std::fprintf(stderr,
        "pending surface loss: admitted=%d oldSerial=%llu returnedSerial=%llu imageValid=%d\n",
        resumed.success,
        static_cast<unsigned long long>(pending.frameSerial),
        static_cast<unsigned long long>(resumed.frameSerial),
        image.success);
    // A lifecycle-invalidated acquisition must never be handed to a consumer.
    assert(!resumed.success);
    assert(resumed.status == rhi::AcquireStatus::NotReady && !admission.pending());
    const auto recovered = admission.acquire(*swapchain, [](std::uint32_t) {
        return true;
    });
    assert(recovered.success && recovered.frameSerial != pending.frameSerial);
    assert(swapchain->acquiredImage(recovered).success);
    assert(swapchain->discardFrame(recovered).success);
    tests::verifyFrameInputLifecycle(
        *swapchain, {"PendingLoss", {16u, 16u}, rhi::TextureFormat::Bgra8Unorm, 2u});
}

void verifyInputAdmission()
{
    using namespace arp;
    auto device = rhi::createNullDevice();
    auto swapchain =
        device->createSwapchain({"Admission", {16u, 16u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    auto foreign = device->createSwapchain(
        {"ForeignAdmission", {16u, 16u}, rhi::TextureFormat::Bgra8Unorm, 2u});
    assert(swapchain && foreign);
    bool ready = false;
    const auto readiness = [&](std::uint32_t) {
        return ready;
    };
    platform::FrameInputAdmission admission;
    const auto pending = admission.acquire(*swapchain, readiness);
    assert(!pending.success && pending.status == rhi::AcquireStatus::NotReady);
    assert(pending.recoverable && !pending.swapchainRebuildRequired && !pending.surfaceLost);
    assert(admission.pending());
    const auto other = swapchain->acquireNextFrame();
    assert(other.success && other.frameIndex != pending.frameIndex);
    assert(!swapchain->acquireNextFrame().success);
    for (unsigned attempt = 0u; attempt != 4u; ++attempt) {
        const auto retry = admission.acquire(*swapchain, readiness);
        assert(retry.status == rhi::AcquireStatus::NotReady);
        assert(retry.frameIndex == pending.frameIndex && retry.frameSerial == pending.frameSerial);
    }
    const auto wrongOwner = admission.acquire(*foreign, readiness);
    assert(!wrongOwner.success && wrongOwner.diagnostics == "PLATFORM-FRAME-INPUT-OWNER-MISMATCH");
    ready = true;
    const auto admitted = admission.acquire(*swapchain, readiness);
    assert(admitted.success && admitted.frameSerial == pending.frameSerial);
    assert(admitted.frameIndex == pending.frameIndex && !admission.pending());
    assert(swapchain->discardFrame(admitted).success);
    assert(swapchain->discardFrame(other).success);
    ready = false;
    assert(admission.acquire(*swapchain, readiness).status == rhi::AcquireStatus::NotReady);
    assert(admission.cancel().success && !admission.pending());
    assert(admission.cancel().success);
    {
        platform::FrameInputAdmission abandoned;
        assert(abandoned.acquire(*swapchain, readiness).status == rhi::AcquireStatus::NotReady);
    }
    // Cancel and destruction each released their exact acquired slot.
    const auto a = swapchain->acquireNextFrame();
    const auto b = swapchain->acquireNextFrame();
    assert(a.success && b.success && a.frameIndex != b.frameIndex);
    assert(swapchain->discardFrame(a).success && swapchain->discardFrame(b).success);
    assert(swapchain->setSurfaceAvailable(false).success);
    const auto lost = admission.acquire(*swapchain, readiness);
    assert(!lost.success && lost.status == rhi::AcquireStatus::SurfaceLost);
    assert(!admission.pending());
}
} // namespace

int main()
{
    verifySharedReadinessBeforeAcquisition();
    verifyPresentationCompletionRetiresTheExactSlot();
    verifyDiscardFailureRetention();
    verifyPendingSurfaceInvalidation();
    verifyInputAdmission();
    auto device = arp::rhi::createNullDevice();
    auto swapchain = device->createSwapchain({
        "MainSurface",
        {1280, 720},
        arp::rhi::TextureFormat::Bgra8Unorm,
        2u,
    });
    assert(swapchain);
    assert(swapchain->imageCount() == 2u);
    assert(swapchain->maxFramesInFlight() == 2u);

    auto frame0 = swapchain->acquireNextFrame();
    assert(frame0.success);
    assert(frame0.frameIndex == 0u);

    auto frame1 = swapchain->acquireNextFrame();
    assert(frame1.success);
    assert(frame1.frameIndex == 1u);

    auto frame2 = swapchain->acquireNextFrame();
    assert(!frame2.success);
    assert(frame2.diagnostics.find("frame slot") != std::pmr::string::npos);

    const auto presented0 = swapchain->present(frame0);
    assert(presented0.success);
    assert(presented0.frameIndex == frame0.frameIndex);

    auto frame3 = swapchain->acquireNextFrame();
    assert(frame3.success);
    assert(frame3.frameIndex == frame0.frameIndex);

    const auto discarded3 = swapchain->discardFrame(frame3);
    assert(discarded3.success);
    assert(discarded3.frameIndex == frame3.frameIndex);

    auto frame4 = swapchain->acquireNextFrame();
    assert(frame4.success);
    assert(frame4.frameIndex == frame3.frameIndex);
    const auto presented4 = swapchain->present(frame4);
    assert(presented4.success);

    arp::rhi::AcquiredFrame invalidFrame;
    invalidFrame.frameIndex = 99;
    const auto invalidPresent = swapchain->present(invalidFrame);
    assert(!invalidPresent.success);
    assert(invalidPresent.diagnostics.find("present") != std::pmr::string::npos);
    const auto invalidDiscard = swapchain->discardFrame(invalidFrame);
    assert(!invalidDiscard.success);
    assert(invalidDiscard.diagnostics.find("discard") != std::pmr::string::npos);

    auto surfaceLost = swapchain->setSurfaceAvailable(false);
    assert(surfaceLost.success);
    assert(!surfaceLost.acquireAllowed);
    assert(!surfaceLost.presentAllowed);
    assert(surfaceLost.releaseSurfaceResources);

    auto acquireWhileLost = swapchain->acquireNextFrame();
    assert(!acquireWhileLost.success);
    assert(acquireWhileLost.diagnostics.find("surface") != std::pmr::string::npos);

    const auto presentWhileLost = swapchain->present(frame1);
    assert(!presentWhileLost.success);
    assert(presentWhileLost.diagnostics.find("surface") != std::pmr::string::npos);

    auto surfaceRestored = swapchain->setSurfaceAvailable(true);
    assert(surfaceRestored.success);
    assert(surfaceRestored.acquireAllowed);
    assert(surfaceRestored.presentAllowed);
    assert(surfaceRestored.swapchainRebuildRequired);

    auto restoredFrame = swapchain->acquireNextFrame();
    assert(restoredFrame.success);
    assert(restoredFrame.frameIndex == 0u);

    auto invalidResize = swapchain->resize({0, 480});
    assert(!invalidResize.success);
    assert(invalidResize.diagnostics.find("extent") != std::pmr::string::npos);
    assert(invalidResize.extent.width == 1280);
    assert(invalidResize.extent.height == 720);

    auto imageAfterInvalidResize = swapchain->acquiredImage(restoredFrame);
    assert(imageAfterInvalidResize.success);
    assert(imageAfterInvalidResize.ref.extent.width == 1280);
    assert(imageAfterInvalidResize.ref.extent.height == 720);

    auto resize = swapchain->resize({640, 480});
    assert(resize.success);
    assert(resize.swapchainRebuildRequired);
    assert(resize.releaseSurfaceResources);
    assert(resize.acquireAllowed);
    assert(resize.presentAllowed);

    auto resizedFrame = swapchain->acquireNextFrame();
    assert(resizedFrame.success);
    assert(resizedFrame.frameIndex == 0u);
    return 0;
}
