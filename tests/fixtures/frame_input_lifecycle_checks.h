#pragma once

#include "platform/frame_input_admission.h"

#include <cassert>
#include <cstdio>

namespace arp::tests {

// Uses the caller's actual swapchain. Native and metadata callers must label
// their evidence separately; this fixture never substitutes a surface payload.
inline void verifyFrameInputLifecycle(
    rhi::Swapchain& swapchain, const rhi::SwapchainDesc& desc, bool imageAvailable = true)
{
    const auto frame = swapchain.acquireNextFrame();
    assert(frame.success && swapchain.isFrameAcquired(frame));
    auto wrongSerial = frame;
    ++wrongSerial.frameSerial;
    assert(!swapchain.isFrameAcquired(wrongSerial));
    auto wrongSlot = frame;
    wrongSlot.frameIndex = swapchain.maxFramesInFlight();
    assert(!swapchain.isFrameAcquired(wrongSlot));
    assert(!swapchain.resize({0u, desc.extent.height}).success);
    for (unsigned i = 0; i != 8u; ++i)
        assert(swapchain.isFrameAcquired(frame));
    assert(swapchain.discardFrame(frame).success);
    assert(!swapchain.isFrameAcquired(frame));

    for (unsigned event = 0u; event != 3u; ++event) {
        platform::FrameInputAdmission admission;
        unsigned queried = 0u;
        const auto pending = admission.acquire(swapchain, [&](std::uint32_t) {
            ++queried;
            return false;
        });
        assert(pending.status == rhi::AcquireStatus::NotReady && queried == 1u);
        // Surface mutation cannot dispose of an acquired native owner. Explicitly
        // discard the original acquisition; admission must detect its stale copy.
        assert(swapchain
                .discardFrame(
                    rhi::makeAcquireSuccessResult(pending.frameSerial, pending.frameIndex))
                .success);
        if (event == 0u) {
            assert(swapchain.setSurfaceAvailable(false).success);
            assert(swapchain.setSurfaceAvailable(true).success);
        } else if (event == 1u) {
            assert(swapchain.resize(desc.extent).success);
        } else {
            assert(swapchain.reconfigure({desc}).success);
        }
        const auto invalidated = admission.acquire(swapchain, [&](std::uint32_t) {
            ++queried;
            return true;
        });
        assert(!invalidated.success && invalidated.status == rhi::AcquireStatus::NotReady);
        assert(invalidated.diagnostics == "PLATFORM-FRAME-INPUT-ACQUISITION-INVALIDATED");
        assert(!invalidated.swapchainRebuildRequired && invalidated.recoverable);
        assert(!admission.pending() && queried == 1u);
        const auto resumed = admission.acquire(swapchain, [](std::uint32_t) {
            return true;
        });
        assert(resumed.success && resumed.frameSerial != pending.frameSerial);
        assert(swapchain.isFrameAcquired(resumed));
        auto image = swapchain.acquiredImage(resumed);
        assert(image.success == imageAvailable);
        assert(swapchain.discardFrame(resumed).success);
        assert(!swapchain.isFrameAcquired(resumed));
    }

    platform::FrameInputAdmission cancel;
    const auto cancelPending = cancel.acquire(swapchain, [](std::uint32_t) {
        return false;
    });
    assert(cancelPending.status == rhi::AcquireStatus::NotReady);
    assert(swapchain
            .discardFrame(
                rhi::makeAcquireSuccessResult(cancelPending.frameSerial, cancelPending.frameIndex))
            .success);
    assert(swapchain.setSurfaceAvailable(false).success);
    const auto cancelled = cancel.cancel();
    assert(cancelled.success && cancelled.status == rhi::FrameDiscardStatus::AlreadyInvalidated);
    assert(!cancel.pending() && cancel.cancel().success);
    const auto unavailable = cancel.acquire(swapchain, [](std::uint32_t) {
        return true;
    });
    assert(!unavailable.success && unavailable.surfaceLost);
    assert(swapchain.setSurfaceAvailable(true).success);
    const auto restored = cancel.acquire(swapchain, [](std::uint32_t) {
        return true;
    });
    assert(restored.success && swapchain.isFrameAcquired(restored));
    assert(swapchain.present(restored).success);
    assert(!swapchain.isFrameAcquired(restored));
    std::puts("Frame input lifecycle: loss/resize/reconfigure invalidation, cancel, recovery PASS");
}

} // namespace arp::tests
