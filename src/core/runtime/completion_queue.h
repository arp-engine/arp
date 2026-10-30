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
    InvalidReservation,
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
    std::uint64_t maxReservedSlots = 0;
};

class CompletionQueue;

// An accepted producer owns one terminal callback slot. The queue must outlive
// this token; destroying an unused token returns its reservation.
class ARP_API CompletionReservation final {
public:
    CompletionReservation() noexcept = default;
    CompletionReservation(const CompletionReservation&) = delete;
    CompletionReservation& operator=(const CompletionReservation&) = delete;
    CompletionReservation(CompletionReservation&& other) noexcept;
    CompletionReservation& operator=(CompletionReservation&& other) noexcept;
    ~CompletionReservation();

    bool valid() const noexcept { return owner_ != nullptr; }

    CompletionStatus status() const noexcept { return status_; }

private:
    friend class CompletionQueue;

    CompletionReservation(CompletionQueue* owner, CompletionStatus status) noexcept
        : owner_(owner), status_(status)
    {}

    CompletionQueue* owner_ = nullptr;
    CompletionStatus status_ = CompletionStatus::InvalidReservation;
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
    CompletionReservation reserve() noexcept;
    CompletionPostResult postReserved(
        CompletionReservation&& reservation, CompletionCallback callback) noexcept;
    CompletionDispatchResult dispatch(std::size_t maxCallbacks);
    void shutdown() noexcept;

    bool closed() const noexcept;
    std::size_t capacity() const noexcept;
    std::size_t pending() const noexcept;
    std::size_t reserved() const noexcept;
    CompletionQueueTelemetry telemetry() const noexcept;

private:
    friend class CompletionReservation;
    void releaseReservation() noexcept;
    void append(CompletionCallback callback) noexcept;

    struct Record {
        CompletionCallback callback;
    };

    CompletionQueueDesc desc_;
    mutable std::mutex mutex_;
    core::pmr::Vector<Record> records_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::size_t reserved_ = 0;
    CompletionQueueTelemetry telemetry_{};
    bool closed_ = false;
};

} // namespace arp::core::runtime
