#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/runtime/cancellation_token.h"
#include "core/runtime/completion_queue.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace arp::core::runtime {

enum class WorkStatus : std::uint8_t {
    Completed,
    Cancelled,
    Failed,
};

enum class WorkServiceStatus : std::uint8_t {
    Accepted,
    QueueFull,
    ServiceClosed,
    InvalidWorkerCount,
    InvalidQueueCapacity,
    InvalidCompletionQueue,
    InvalidWorkItem,
    Cancelled,
    NotFound,
};

enum class WorkShutdownPolicy : std::uint8_t {
    Drain,
    CancelPending,
};

struct WorkTaskHandle {
    std::uint64_t id = 0;

    bool valid() const noexcept { return id != 0; }

    friend bool operator==(WorkTaskHandle lhs, WorkTaskHandle rhs) noexcept
    {
        return lhs.id == rhs.id;
    }
};

struct WorkResult {
    WorkTaskHandle handle;
    WorkStatus status = WorkStatus::Completed;
};

using WorkExecuteFn = WorkStatus (*)(void*);
using WorkCompleteFn = void (*)(void*, WorkResult);

struct WorkItem {
    WorkExecuteFn execute = nullptr;
    void* executeUserData = nullptr;
    WorkCompleteFn complete = nullptr;
    void* completeUserData = nullptr;
    CancellationToken cancellation;

    bool valid() const noexcept { return execute != nullptr && complete != nullptr; }
};

struct WorkServiceDesc {
    CompletionQueue* completionQueue = nullptr;
    std::size_t workerCount = 0;
    std::size_t queueCapacity = 0;
    // Browser runtimes can retain the same submit/completion semantics without
    // creating native threads. Inline work executes synchronously and posts
    // its completion through the normal CompletionQueue.
    bool inlineExecution = false;
};

struct WorkSubmitResult {
    WorkServiceStatus status = WorkServiceStatus::Accepted;
    WorkTaskHandle handle;

    bool accepted() const noexcept { return status == WorkServiceStatus::Accepted; }
};

struct WorkCancelResult {
    WorkServiceStatus status = WorkServiceStatus::Cancelled;
};

struct WorkServiceTelemetry {
    std::uint64_t submitted = 0;
    std::uint64_t started = 0;
    std::uint64_t completed = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t failed = 0;
    std::uint64_t rejected = 0;
    std::uint64_t maxQueueDepth = 0;
    std::uint64_t lateCancellationCompletions = 0;
    std::uint64_t totalQueueLatencyNs = 0;
    std::uint64_t maxQueueLatencyNs = 0;
};

class ARP_API WorkService final {
public:
    explicit WorkService(WorkServiceDesc desc);
    WorkService(const WorkService&) = delete;
    WorkService& operator=(const WorkService&) = delete;
    WorkService(WorkService&&) = delete;
    WorkService& operator=(WorkService&&) = delete;
    ~WorkService();

    bool valid() const noexcept;
    WorkServiceStatus status() const noexcept;
    std::size_t workerCount() const noexcept;

    WorkSubmitResult submit(WorkItem item);
    WorkCancelResult cancelPending(WorkTaskHandle handle);
    void shutdown(WorkShutdownPolicy policy = WorkShutdownPolicy::CancelPending);

    WorkServiceTelemetry telemetry() const noexcept;

private:
    struct Task {
        WorkTaskHandle handle;
        WorkItem item;
        std::chrono::steady_clock::time_point enqueueTime{};
        bool cancelled = false;
    };

    struct CompletionState {
        WorkCompleteFn complete = nullptr;
        void* completeUserData = nullptr;
        WorkResult result;
    };

    static void deliverCompletion(void* userData) noexcept;

    void workerLoop() noexcept;
    void postCompletion(const Task& task, WorkStatus status) noexcept;
    WorkServiceStatus validateDesc(const WorkServiceDesc& desc) const noexcept;

    WorkServiceDesc desc_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    core::pmr::Deque<Task> tasks_;
    core::pmr::Vector<std::thread> workers_;
    WorkServiceTelemetry telemetry_{};
    WorkServiceStatus status_ = WorkServiceStatus::Accepted;
    std::uint64_t nextId_ = 1;
    bool closed_ = false;
};

} // namespace arp::core::runtime
