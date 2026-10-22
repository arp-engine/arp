#pragma once

#include "rhi/gpu_device.h"

#include <cassert>

namespace arp::tests {

// Explicit Null-only lifecycle fixture; signaling below is never native evidence.
class DelayedValidatorQueue final : public rhi::GpuQueue {
public:
    bool rejected = false, acceptedError = false, throws = false;
    rhi::GpuFence* pending = nullptr;

    rhi::GpuQueueRole role() const override { return rhi::GpuQueueRole::Graphics; }

    std::uint32_t submitCount() const override { return pending ? 1u : 0u; }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& c) override
    {
        return submit(c, nullptr);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer&, rhi::GpuFence* fence) override
    {
        assert(!pending && fence);
        if (rejected)
            return {false, "FIXTURE-REJECTED", 0u, false};
        pending = fence;
        if (throws)
            throw core::DiagnosticError("FIXTURE-SUBMIT-THREW");
        return {!acceptedError, acceptedError ? "FIXTURE-ACCEPTED-ERROR" : "", 1u, true};
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& c,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuTimelineSemaphore*,
        std::uint64_t,
        rhi::GpuFence* fence) override
    {
        return submit(c, fence);
    }

    void complete()
    {
        assert(pending);
        pending->signal();
        pending = nullptr;
    }
};

} // namespace arp::tests
