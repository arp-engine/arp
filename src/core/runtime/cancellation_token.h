#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"

#include <atomic>

namespace arp::core::runtime {

class CancellationToken final {
public:
    CancellationToken() noexcept = default;

    bool cancellable() const noexcept { return state_ != nullptr; }

    bool cancelled() const noexcept
    {
        return state_ != nullptr && state_->cancelled.load(std::memory_order_acquire);
    }

private:
    struct State {
        std::atomic_bool cancelled{false};
    };

    friend class CancellationSource;

    explicit CancellationToken(State* state) noexcept : state_(state) {}

    State* state_ = nullptr;
};

class ARP_API CancellationSource final {
public:
    CancellationSource() : state_(core::makeUnique<CancellationToken::State>()) {}

    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) = delete;
    CancellationSource& operator=(CancellationSource&&) = delete;
    ~CancellationSource() = default;

    CancellationToken token() const noexcept { return CancellationToken(state_.get()); }

    void cancel() noexcept { state_->cancelled.store(true, std::memory_order_release); }

private:
    core::UniquePtr<CancellationToken::State> state_;
};

} // namespace arp::core::runtime
