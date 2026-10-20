#pragma once

#include "core/api.h"
#include "core/callback.h"
#include "core/cpu_allocator.h"

#include <atomic>
#include <chrono>
#include <functional>

namespace arp::network {

class io_context;
class io_service;

using clock = std::chrono::steady_clock;
using time_point = clock::time_point;
using duration = clock::duration;
using time_type = double;
using task = core::SharedCallback<void()>;

class deadline_timer;

namespace detail {
template <typename T> using SharedPtr = decltype(core::makeShared<T>());
} // namespace detail

using deadline_timer_ptr = detail::SharedPtr<deadline_timer>;

class ARP_API deadline_timer final {
public:
    deadline_timer(io_service* service, const time_point& deadline, task callback);
    deadline_timer(io_service* service, const duration& interval, task callback, bool repeat);
    deadline_timer(
        io_context* context, io_service* service, const time_point& deadline, task callback);
    deadline_timer(io_context* context,
        io_service* service,
        const duration& interval,
        task callback,
        bool repeat);

    bool cancelled() const;
    bool repeat() const;
    time_point deadline() const;
    duration interval() const;
    void cancel();
    void trigger();
    void reschedule(const time_point& now);

private:
    io_context* context_ = nullptr;
    io_service* service_ = nullptr;
    bool repeat_ = false;
    std::atomic<bool> cancelled_{false};
    duration interval_{};
    time_point deadline_{};
    task callback_;
};

} // namespace arp::network
