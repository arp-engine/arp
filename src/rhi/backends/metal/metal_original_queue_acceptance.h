#pragma once

#include "rhi/frontend/resource_device_lifetime.h"

namespace arp::rhi {
class OriginalCommandStreamProgram;
}

namespace arp::rhi_metal {
class MetalRhiNativeCommandPayload;
class MetalRhiResourceRegistry;
class MetalRhiCompletionState;

// Embedded in original cold SubmissionRetention and projected only through
// that exact shared owner. No public constructor/publisher or native handle.
class MetalOriginalQueueAcceptance final {
    friend class MetalRhiNativeCommandPayload;
    MetalOriginalQueueAcceptance() = default;

public:
    MetalOriginalQueueAcceptance(const MetalOriginalQueueAcceptance&) = delete;
    static const char* validateOriginal(
        const std::shared_ptr<const MetalOriginalQueueAcceptance>&) noexcept;
    const char* diagnostic() const noexcept;
    const char* precedes(const MetalOriginalQueueAcceptance& successor) const noexcept;
    bool matchesProgram(
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>&) const noexcept;

    std::uint64_t ordinal() const noexcept
    {
        return published_.load(std::memory_order_acquire) ? ordinal_ : 0u;
    }

    bool provesGpuCompletion() const noexcept { return false; }

private:
    std::weak_ptr<const MetalOriginalQueueAcceptance> self_;
    rhi::OriginalDeviceLifetime::Ticket device_;
    std::shared_ptr<void> queue_;
    std::weak_ptr<MetalRhiResourceRegistry> registry_;
    std::weak_ptr<const rhi::OriginalCommandStreamProgram> program_;
    const MetalRhiCompletionState* completion_ = nullptr;
    std::uint64_t ordinal_ = 0u;
    bool nativeAccepted_ = false;
    std::atomic<bool> published_{false};
};
} // namespace arp::rhi_metal
