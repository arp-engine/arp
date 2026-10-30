#include "core/runtime/completion_queue.h"

#include <algorithm>
#include <utility>

namespace arp::core::runtime {

CompletionQueue::CompletionQueue(CompletionQueueDesc desc)
    : desc_(desc), records_(&core::defaultCpuMemoryResource())
{
    records_.resize(desc.capacity);
}

CompletionReservation::CompletionReservation(CompletionReservation&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)), status_(other.status_)
{}

CompletionReservation& CompletionReservation::operator=(CompletionReservation&& other) noexcept
{
    if (this != &other) {
        if (owner_)
            owner_->releaseReservation();
        owner_ = std::exchange(other.owner_, nullptr);
        status_ = other.status_;
    }
    return *this;
}

CompletionReservation::~CompletionReservation()
{
    if (owner_)
        owner_->releaseReservation();
}

CompletionReservation CompletionQueue::reserve() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        ++telemetry_.rejected;
        return {nullptr, CompletionStatus::ServiceClosed};
    }
    if (size_ + reserved_ >= desc_.capacity) {
        ++telemetry_.rejected;
        return {nullptr, CompletionStatus::QueueFull};
    }
    ++reserved_;
    telemetry_.maxReservedSlots = std::max<std::uint64_t>(telemetry_.maxReservedSlots, reserved_);
    return {this, CompletionStatus::Accepted};
}

void CompletionQueue::releaseReservation() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    --reserved_;
}

void CompletionQueue::append(CompletionCallback callback) noexcept
{
    records_[(head_ + size_) % records_.size()].callback = callback;
    ++size_;
    ++telemetry_.submitted;
    telemetry_.maxQueueDepth = std::max<std::uint64_t>(telemetry_.maxQueueDepth, size_);
}

CompletionPostResult CompletionQueue::postReserved(
    CompletionReservation&& reservation, CompletionCallback callback) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (reservation.owner_ != this) {
        ++telemetry_.rejected;
        return {CompletionStatus::InvalidReservation};
    }
    if (!callback.valid()) {
        ++telemetry_.rejected;
        return {CompletionStatus::InvalidCallback};
    }
    reservation.owner_ = nullptr;
    --reserved_;
    append(callback);
    return {CompletionStatus::Accepted};
}

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
    if (size_ + reserved_ >= desc_.capacity) {
        ++telemetry_.rejected;
        return {CompletionStatus::QueueFull};
    }

    append(callback);
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
            if (!size_ || result.dispatched >= maxCallbacks) {
                telemetry_.dispatched += result.dispatched;
                return result;
            }
            callback = records_[head_].callback;
            records_[head_].callback = {};
            head_ = (head_ + 1) % records_.size();
            --size_;
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
    return size_;
}

std::size_t CompletionQueue::reserved() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return reserved_;
}

CompletionQueueTelemetry CompletionQueue::telemetry() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_;
}

} // namespace arp::core::runtime
