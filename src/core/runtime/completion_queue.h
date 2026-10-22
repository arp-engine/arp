#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace arp::core::runtime {

enum class CompletionStatus : std::uint8_t {
    Accepted,
    QueueFull,
    ServiceClosed,
    InvalidCallback,
    InvalidDispatchBudget,
};

struct CompletionPostResult {
    CompletionStatus status = CompletionStatus::Accepted;

    bool accepted() const noexcept { return status == CompletionStatus::Accepted; }
};

struct CompletionDispatchResult {
    CompletionStatus status = CompletionStatus::Accepted;
    std::size_t dispatched = 0;

    bool success() const noexcept { return status == CompletionStatus::Accepted; }
};

struct CompletionCallback {
    using InvokeFn = void (*)(void*);

    InvokeFn invoke = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return invoke != nullptr; }
};

struct CompletionQueueDesc {
    std::size_t capacity = 0;
};

struct CompletionQueueTelemetry {
    std::uint64_t submitted = 0;
    std::uint64_t dispatched = 0;
    std::uint64_t rejected = 0;
    std::uint64_t maxQueueDepth = 0;
};

class ARP_API CompletionQueue final {
public:
    explicit CompletionQueue(CompletionQueueDesc desc);
    CompletionQueue(const CompletionQueue&) = delete;
    CompletionQueue& operator=(const CompletionQueue&) = delete;
    CompletionQueue(CompletionQueue&&) = delete;
    CompletionQueue& operator=(CompletionQueue&&) = delete;
    ~CompletionQueue() = default;

    CompletionPostResult post(CompletionCallback callback);
    CompletionDispatchResult dispatch(std::size_t maxCallbacks);
    void shutdown() noexcept;

    bool closed() const noexcept;
    std::size_t capacity() const noexcept;
    std::size_t pending() const noexcept;
    CompletionQueueTelemetry telemetry() const noexcept;

private:
    struct Record {
        CompletionCallback callback;
    };

    CompletionQueueDesc desc_;
    mutable std::mutex mutex_;
    core::pmr::Deque<Record> records_;
    CompletionQueueTelemetry telemetry_{};
    bool closed_ = false;
};

} // namespace arp::core::runtime
