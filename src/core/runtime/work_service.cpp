#include "core/runtime/work_service.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace arp::core::runtime {

WorkService::WorkService(WorkServiceDesc desc)
    : desc_(desc), tasks_(&core::defaultCpuMemoryResource()),
      workers_(&core::defaultCpuMemoryResource())
{
    status_ = validateDesc(desc_);
    if (status_ != WorkServiceStatus::Accepted) {
        closed_ = true;
        return;
    }

    if (!desc_.inlineExecution) {
        workers_.reserve(desc_.workerCount);
        for (std::size_t index = 0; index < desc_.workerCount; ++index) {
            workers_.emplace_back([this] {
                workerLoop();
            });
        }
    }
}

WorkService::~WorkService()
{
    shutdown(WorkShutdownPolicy::CancelPending);
}

bool WorkService::valid() const noexcept
{
    return status_ == WorkServiceStatus::Accepted;
}

WorkServiceStatus WorkService::status() const noexcept
{
    return status_;
}

std::size_t WorkService::workerCount() const noexcept
{
    return workers_.size();
}

WorkSubmitResult WorkService::submit(WorkItem item)
{
    Task inlineTask{};
    bool runInline = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_ != WorkServiceStatus::Accepted) {
            ++telemetry_.rejected;
            return {status_, {}};
        }
        if (closed_) {
            ++telemetry_.rejected;
            return {WorkServiceStatus::ServiceClosed, {}};
        }
        if (!item.valid()) {
            ++telemetry_.rejected;
            return {WorkServiceStatus::InvalidWorkItem, {}};
        }
        if (item.cancellation.cancelled()) {
            ++telemetry_.cancelled;
            return {WorkServiceStatus::Cancelled, {}};
        }

        const WorkTaskHandle handle{nextId_++};
        inlineTask = Task{handle, item, std::chrono::steady_clock::now(), false};
        if (desc_.inlineExecution) {
            runInline = true;
        } else {
            if (tasks_.size() >= desc_.queueCapacity) {
                ++telemetry_.rejected;
                return {WorkServiceStatus::QueueFull, {}};
            }
            tasks_.push_back(inlineTask);
            telemetry_.maxQueueDepth = std::max<std::uint64_t>(
                telemetry_.maxQueueDepth, static_cast<std::uint64_t>(tasks_.size()));
            wake_.notify_one();
        }
        ++telemetry_.submitted;
    }

    if (runInline) {
        WorkStatus status = WorkStatus::Cancelled;
        if (!inlineTask.item.cancellation.cancelled()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ++telemetry_.started;
            }
            status = inlineTask.item.execute(inlineTask.item.executeUserData);
            if (inlineTask.item.cancellation.cancelled() && status == WorkStatus::Completed) {
                status = WorkStatus::Cancelled;
                std::lock_guard<std::mutex> lock(mutex_);
                ++telemetry_.lateCancellationCompletions;
            }
        }
        postCompletion(inlineTask, status);
    }
    return {WorkServiceStatus::Accepted, inlineTask.handle};
}

WorkCancelResult WorkService::cancelPending(WorkTaskHandle handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle.valid()) {
        return {WorkServiceStatus::NotFound};
    }
    for (auto& task : tasks_) {
        if (task.handle == handle) {
            task.cancelled = true;
            wake_.notify_one();
            return {WorkServiceStatus::Cancelled};
        }
    }
    return {WorkServiceStatus::NotFound};
}

void WorkService::shutdown(WorkShutdownPolicy policy)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_ && workers_.empty()) {
            return;
        }
        closed_ = true;
        if (policy == WorkShutdownPolicy::CancelPending) {
            for (auto& task : tasks_) {
                task.cancelled = true;
            }
        }
    }
    wake_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

WorkServiceTelemetry WorkService::telemetry() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_;
}

void WorkService::deliverCompletion(void* userData) noexcept
{
    core::UniquePtr<CompletionState> state{
        static_cast<CompletionState*>(userData),
        core::CpuAllocatedDeleter<CompletionState>{
            userData,
            &core::destroyCpuAllocatedUnique<CompletionState>,
        },
    };
    state->complete(state->completeUserData, state->result);
}

void WorkService::workerLoop() noexcept
{
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {
                return closed_ || !tasks_.empty();
            });
            if (tasks_.empty()) {
                return;
            }
            task = tasks_.front();
            tasks_.pop_front();
            const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - task.enqueueTime);
            const auto latencyNs = static_cast<std::uint64_t>(latency.count());
            telemetry_.totalQueueLatencyNs += latencyNs;
            telemetry_.maxQueueLatencyNs = std::max(telemetry_.maxQueueLatencyNs, latencyNs);
            if (!task.cancelled && !task.item.cancellation.cancelled()) {
                ++telemetry_.started;
            }
        }

        WorkStatus status = WorkStatus::Cancelled;
        if (!task.cancelled && !task.item.cancellation.cancelled()) {
            status = task.item.execute(task.item.executeUserData);
            if (task.item.cancellation.cancelled() && status == WorkStatus::Completed) {
                status = WorkStatus::Cancelled;
                std::lock_guard<std::mutex> lock(mutex_);
                ++telemetry_.lateCancellationCompletions;
            }
        }
        postCompletion(task, status);
    }
}

void WorkService::postCompletion(const Task& task, WorkStatus status) noexcept
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status == WorkStatus::Completed) {
            ++telemetry_.completed;
        } else if (status == WorkStatus::Cancelled) {
            ++telemetry_.cancelled;
        } else {
            ++telemetry_.failed;
        }
    }

    auto completion = core::makeUnique<CompletionState>();
    completion->complete = task.item.complete;
    completion->completeUserData = task.item.completeUserData;
    completion->result = WorkResult{task.handle, status};
    auto* raw = completion.get();
    const auto post = desc_.completionQueue->post(CompletionCallback{&deliverCompletion, raw});
    if (post.accepted()) {
        completion.release();
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ++telemetry_.rejected;
}

WorkServiceStatus WorkService::validateDesc(const WorkServiceDesc& desc) const noexcept
{
    if (desc.completionQueue == nullptr) {
        return WorkServiceStatus::InvalidCompletionQueue;
    }
    if (desc.workerCount == 0 && !desc.inlineExecution) {
        return WorkServiceStatus::InvalidWorkerCount;
    }
    if (desc.queueCapacity == 0) {
        return WorkServiceStatus::InvalidQueueCapacity;
    }
    return WorkServiceStatus::Accepted;
}

} // namespace arp::core::runtime
