#pragma once

#include "rhi/gpu_device.h"

namespace arp::tests {

// Test-only outcome injection around the original queue. Accepted failure
// preserves the real submission/fence; it never manufactures acceptance.
class SubmissionOutcomeQueue final : public rhi::GpuQueue {
public:
    explicit SubmissionOutcomeQueue(rhi::GpuQueue& inner) : inner_(inner) {}

    rhi::GpuQueueRole role() const override { return inner_.role(); }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commands) override
    {
        return submit(commands, nullptr);
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commands, rhi::GpuFence* fence) override
    {
        if (reject)
            return {false, "test-submit-rejected", 0u};
        return report(inner_.submit(commands, fence));
    }

    rhi::QueueSubmitResult submit(const rhi::CommandBuffer& commands,
        rhi::GpuTimelineSemaphore* wait,
        std::uint64_t waitValue,
        rhi::GpuTimelineSemaphore* signal,
        std::uint64_t signalValue,
        rhi::GpuFence* fence) override
    {
        if (reject)
            return {false, "test-submit-rejected", 0u};
        return report(inner_.submit(commands, wait, waitValue, signal, signalValue, fence));
    }

    std::uint32_t submitCount() const override { return inner_.submitCount(); }

    bool reject = false;
    bool failAfterAcceptance = false;

private:
    rhi::QueueSubmitResult report(rhi::QueueSubmitResult result) const
    {
        if (failAfterAcceptance && result.hasAcceptedWork())
            return {false, "test-post-submit-report-failed", result.submittedCommandCount, true};
        return result;
    }

    rhi::GpuQueue& inner_;
};

} // namespace arp::tests
