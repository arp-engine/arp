#pragma once
#include "rhi/gpu_resources.h"

namespace arp::rhi {
// GraphicsStorageWriteSequence/1: value-only command order. Resource state is
// owned exclusively by the resource snapshot/journal, not this finite sequence.
class GraphicsStorageWriteSequence final {
public:
    enum class Phase {
        Empty,
        ProducerOpen,
        ProducerEnded,
        DependencyConsumed,
        ConsumerOpen,
        ConsumerConsumed
    };

    const char* begin(GpuResourceId resource) noexcept
    {
        if (!resource.isValid() || phase_ != Phase::Empty)
            return "RHI-GRAPHICS-WRITE-SEQUENCE-BEGIN";
        resource_ = resource;
        phase_ = Phase::ProducerOpen;
        return nullptr;
    }

    const char* draw(GpuResourceId resource) noexcept
    {
        if (resource != resource_ || phase_ != Phase::ProducerOpen)
            return "RHI-GRAPHICS-WRITE-SEQUENCE-DRAW";
        drew_ = true;
        return nullptr;
    }

    const char* canEnd(GpuResourceId resource) const noexcept
    {
        if (resource != resource_ || phase_ != Phase::ProducerOpen)
            return "RHI-GRAPHICS-WRITE-SEQUENCE-END";
        return drew_ ? nullptr : "RHI-GRAPHICS-WRITE-SEQUENCE-DRAW-REQUIRED";
    }

    const char* end(GpuResourceId resource) noexcept
    {
        if (const auto* id = canEnd(resource))
            return id;
        phase_ = Phase::ProducerEnded;
        return nullptr;
    }

    const char* canDepend(GpuResourceId resource) const noexcept
    {
        return resource == resource_ && phase_ == Phase::ProducerEnded
            ? nullptr
            : "RHI-GRAPHICS-WRITE-SEQUENCE-DEPENDENCY";
    }

    const char* depend(GpuResourceId resource) noexcept
    {
        if (const auto* id = canDepend(resource))
            return id;
        phase_ = Phase::DependencyConsumed;
        return nullptr;
    }

    const char* canConsume(GpuResourceId resource) const noexcept
    {
        return resource == resource_ && phase_ == Phase::DependencyConsumed
            ? nullptr
            : "RHI-GRAPHICS-WRITE-SEQUENCE-CONSUMER";
    }

    const char* consume(GpuResourceId resource) noexcept
    {
        if (const auto* id = canConsume(resource))
            return id;
        phase_ = Phase::ConsumerConsumed;
        return nullptr;
    }

    const char* beginConsumer(GpuResourceId resource) noexcept
    {
        if (const auto* id = canConsume(resource))
            return id;
        phase_ = Phase::ConsumerOpen;
        drew_ = false;
        return nullptr;
    }

    const char* drawConsumer(GpuResourceId resource) noexcept
    {
        if (resource != resource_ || phase_ != Phase::ConsumerOpen)
            return "RHI-GRAPHICS-WRITE-SEQUENCE-CONSUMER-DRAW";
        drew_ = true;
        return nullptr;
    }

    const char* canEndConsumer(GpuResourceId resource) const noexcept
    {
        if (resource != resource_ || phase_ != Phase::ConsumerOpen)
            return "RHI-GRAPHICS-WRITE-SEQUENCE-CONSUMER-END";
        return drew_ ? nullptr : "RHI-GRAPHICS-WRITE-SEQUENCE-CONSUMER-DRAW-REQUIRED";
    }

    const char* endConsumer(GpuResourceId resource) noexcept
    {
        if (const auto* id = canEndConsumer(resource))
            return id;
        phase_ = Phase::ConsumerConsumed;
        return nullptr;
    }

    const char* finish() const noexcept
    {
        return phase_ == Phase::ConsumerConsumed ? nullptr
                                                 : "RHI-GRAPHICS-WRITE-SEQUENCE-INCOMPLETE";
    }

    Phase phase() const noexcept { return phase_; }

private:
    Phase phase_ = Phase::Empty;
    GpuResourceId resource_;
    bool drew_ = false;
};
} // namespace arp::rhi
