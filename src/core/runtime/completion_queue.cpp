#include "core/runtime/completion_queue.h"

#include <algorithm>

namespace arp::core::runtime {

CompletionQueue::CompletionQueue(CompletionQueueDesc desc)
    : desc_(desc), records_(&core::defaultCpuMemoryResource())
{}

CompletionPostResult CompletionQueue::post(CompletionCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!callback.valid()) {
        ++telemetry_.rejected;
        return {CompletionStatus::InvalidCallback};
    }
    if (closed_) {
        ++telemetry_.rejected;
        return {CompletionStatus::ServiceClosed};
    }
    if (desc_.capacity == 0 || records_.size() >= desc_.capacity) {
        ++telemetry_.rejected;
        return {CompletionStatus::QueueFull};
    }

    records_.push_back(Record{callback});
    ++telemetry_.submitted;
    telemetry_.maxQueueDepth = std::max<std::uint64_t>(
        telemetry_.maxQueueDepth, static_cast<std::uint64_t>(records_.size()));
    return {CompletionStatus::Accepted};
}

CompletionDispatchResult CompletionQueue::dispatch(std::size_t maxCallbacks)
{
    if (maxCallbacks == 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++telemetry_.rejected;
        return {CompletionStatus::InvalidDispatchBudget, 0};
    }

    CompletionDispatchResult result;
    for (;;) {
        CompletionCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (records_.empty() || result.dispatched >= maxCallbacks) {
                telemetry_.dispatched += result.dispatched;
                return result;
            }
            callback = records_.front().callback;
            records_.pop_front();
        }

        callback.invoke(callback.userData);
        ++result.dispatched;
    }
}

void CompletionQueue::shutdown() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
}

bool CompletionQueue::closed() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return closed_;
}

std::size_t CompletionQueue::capacity() const noexcept
{
    return desc_.capacity;
}

std::size_t CompletionQueue::pending() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return records_.size();
}

CompletionQueueTelemetry CompletionQueue::telemetry() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_;
}

} // namespace arp::core::runtime
