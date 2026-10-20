#pragma once

#include "core/cpu_allocator.h"
#include "rhi/gpu_swapchain.h"

#include <cassert>

namespace arp::tests {
// A test acquisition retires only after real native callbacks have drained.
// Keep this lease outside all execution/resource locals in negative fixtures.
struct ConformanceNativeFrameLease final {
    rhi::Swapchain* surface = nullptr;
    void (*advance)() = nullptr;
    rhi::AcquiredFrame acquired;
    rhi::AcquiredSwapchainImage target;
    std::shared_ptr<core::CpuMemoryQuota> quota;

    void acquire(rhi::Swapchain& ownedSurface)
    {
        assert(!surface && advance);
        advance();
        surface = &ownedSurface;
        acquired = surface->acquireNextFrame();
        target = surface->acquiredImage(acquired);
        assert(target.success && target.texture);
    }

    ~ConformanceNativeFrameLease()
    {
        if (!surface && !advance)
            return;
        if (advance) {
            bool discarded = !surface || !surface->isFrameAcquired(acquired);
            for (int i = 0; i < 120; ++i) {
                if (!discarded)
                    discarded = surface->discardFrame(acquired).success;
                if (discarded)
                    target.texture.reset();
                if (discarded && (!quota || !quota->liveBytes()))
                    break;
                advance();
            }
            assert(discarded && (!quota || !quota->liveBytes()));
            advance();
        } else
            assert(surface->discardFrame(acquired).success);
    }
};
} // namespace arp::tests
