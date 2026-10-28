#pragma once

#include "rhi/gpu_swapchain.h"

#include <cstdio>
#include <utility>

namespace arp::platform {

// Platform-owned acquisition, pending until the renderer's input requirement
// is ready. Swapchain must outlive this helper. See FrameInputReadiness/1.
class FrameInputAdmission final {
public:
    FrameInputAdmission() = default;
    FrameInputAdmission(const FrameInputAdmission&) = delete;
    FrameInputAdmission& operator=(const FrameInputAdmission&) = delete;

    ~FrameInputAdmission()
    {
        const auto result = cancel();
        if (!result.success)
            std::fprintf(
                stderr, "[frame-input-admission] discard failed: %s\n", result.diagnostics.c_str());
    }

    template <typename ReadyFn>
    rhi::AcquiredFrame acquire(rhi::Swapchain& swapchain, ReadyFn&& ready)
    {
        return acquire(swapchain, std::forward<ReadyFn>(ready), [] {
            return true;
        });
    }

    template <typename ReadyFn, typename SharedReadyFn>
    rhi::AcquiredFrame acquire(
        rhi::Swapchain& swapchain, ReadyFn&& ready, SharedReadyFn&& sharedReady)
    {
        if (owner_ && owner_ != &swapchain)
            return rhi::makeAcquireFailureResult(
                "PLATFORM-FRAME-INPUT-OWNER-MISMATCH", pending_.frameSerial, pending_.frameIndex);
        if (pending_.success && !swapchain.isFrameAcquired(pending_)) {
            auto invalidated =
                rhi::makeAcquireFailureResult("PLATFORM-FRAME-INPUT-ACQUISITION-INVALIDATED",
                    pending_.frameSerial,
                    pending_.frameIndex,
                    rhi::AcquireStatus::NotReady);
            pending_ = {};
            owner_ = nullptr;
            return invalidated;
        }
        // Shared input dependencies do not need a drawable identity. Poll
        // them before native acquisition, preserving any existing acquisition
        // and its cancellation ownership. No slot or serial is invented.
        if (!sharedReady())
            return rhi::makeAcquireFailureResult({},
                pending_.success ? pending_.frameSerial : 0u,
                pending_.success ? pending_.frameIndex : 0u,
                rhi::AcquireStatus::NotReady);
        if (!pending_.success) {
            auto acquired = swapchain.acquireNextFrame();
            if (!acquired.success)
                return acquired;
            owner_ = &swapchain;
            pending_ = std::move(acquired);
        }
        if (!ready(pending_.frameIndex))
            return rhi::makeAcquireFailureResult(
                {}, pending_.frameSerial, pending_.frameIndex, rhi::AcquireStatus::NotReady);
        auto acquired = std::move(pending_);
        pending_ = {};
        owner_ = nullptr;
        return acquired;
    }

    rhi::FrameDiscardResult cancel()
    {
        if (!pending_.success) {
            rhi::FrameDiscardResult result;
            result.success = true;
            result.status = rhi::FrameDiscardStatus::Discarded;
            return result;
        }
        if (!owner_->isFrameAcquired(pending_)) {
            auto invalidated =
                rhi::makeFrameDiscardSuccessResult(pending_.frameSerial, pending_.frameIndex);
            invalidated.status = rhi::FrameDiscardStatus::AlreadyInvalidated;
            pending_ = {};
            owner_ = nullptr;
            return invalidated;
        }
        const auto discarded = owner_->discardFrame(pending_);
        if (discarded.success) {
            pending_ = {};
            owner_ = nullptr;
        }
        return discarded;
    }

    bool pending() const noexcept { return pending_.success; }

private:
    rhi::Swapchain* owner_ = nullptr;
    rhi::AcquiredFrame pending_;
};

} // namespace arp::platform
