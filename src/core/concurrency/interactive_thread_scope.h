#pragma once

#include "core/api.h"

#include <array>
#include <cstdint>

namespace arp::core {
enum class ThreadExecutionQualityStatus { Applied, Unsupported, Failed };

struct InteractiveThreadResult {
    ThreadExecutionQualityStatus quality = ThreadExecutionQualityStatus::Unsupported;
    std::uint32_t nativeError = 0;
};

// InteractiveThread/1: an execution-quality hint for the current thread.
// Windows explicitly disables execution-speed throttling; other platforms
// report Unsupported. Priority, affinity and CPU-set selection stay under
// their existing owners. Construct and restore on the same thread; nested
// scopes restore the exact prior quality control and state masks.
class ARP_API InteractiveThreadScope final {
public:
    InteractiveThreadScope() noexcept;
    ~InteractiveThreadScope();
    InteractiveThreadScope(const InteractiveThreadScope&) = delete;
    InteractiveThreadScope& operator=(const InteractiveThreadScope&) = delete;

    const InteractiveThreadResult& result() const noexcept { return result_; }

    bool restore() noexcept;

private:
    InteractiveThreadResult result_;
    std::array<std::uint32_t, 3> priorQuality_{};
    std::uint32_t ownerThread_ = 0;
    bool applied_ = false;
};
} // namespace arp::core
