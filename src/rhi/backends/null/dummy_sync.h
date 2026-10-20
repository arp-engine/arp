#pragma once

#include "core/api.h"
#include "rhi/gpu_device.h"

namespace arp::rhi {

/// Counter-based GpuFence for the Null/reference backend or tests. No native
/// handle is involved;
/// signal/isSignaled are pure CPU operations. This is only valid where the
/// backend explicitly publishes the reference NativeSync fallback.
class ARP_API DummyFence final : public GpuFence {
public:
    void signal() override { signaled_ = true; }

    bool isSignaled() const override { return signaled_; }

    void wait() override { /* No GPU work to wait for. */ }

    void reset() override { signaled_ = false; }

private:
    bool signaled_ = false;
};

/// Counter-based GpuSemaphore for backends without native semaphore support.
/// Used for queue-to-queue ordering in the explicit reference fallback.
class ARP_API DummySemaphore final : public GpuSemaphore {
public:
    void signal() override { signaled_ = true; }

    void wait() override { /* No GPU work to order. */ }

    void reset() override { signaled_ = false; }

private:
    bool signaled_ = false;
};

/// Counter-based GpuTimelineSemaphore dummy for the Null/reference backend.
/// signal / wait are pure CPU operations; hostWait returns immediately.
class ARP_API DummyTimelineSemaphore final : public GpuTimelineSemaphore {
public:
    explicit DummyTimelineSemaphore(uint64_t initialValue = 0) : value_(initialValue) {}

    void signal(uint64_t value) override
    {
        if (value > value_)
            value_ = value;
    }

    void wait(uint64_t value) override
    {
        if (value > value_)
            value_ = value;
    }

    uint64_t getValue() const override { return value_; }

    void hostWait(uint64_t value, uint64_t /*timeoutNs*/) override
    {
        if (value > value_)
            value_ = value;
    }

private:
    uint64_t value_ = 0;
};

/// Test/reference GpuMemoryHeap. The heap tracks size and alignment for
/// diagnostics but does not share physical memory across placed textures.
/// Production backends without a placed-resource lowering must return null
/// from their factories instead of exposing this wrapper.
class ARP_API DummyMemoryHeap final : public GpuMemoryHeap {
public:
    DummyMemoryHeap(std::uint64_t sizeBytes, std::uint64_t alignmentBytes)
        : id_(allocateGpuResourceId()), sizeBytes_(sizeBytes), alignmentBytes_(alignmentBytes)
    {}

    GpuResourceId id() const override { return id_; }

    std::uint64_t sizeBytes() const override { return sizeBytes_; }

    std::uint64_t alignmentBytes() const override { return alignmentBytes_; }

    bool supportsAliasing() const override { return false; }

private:
    GpuResourceId id_;
    std::uint64_t sizeBytes_;
    std::uint64_t alignmentBytes_;
};

} // namespace arp::rhi
