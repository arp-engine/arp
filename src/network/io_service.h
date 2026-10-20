#pragma once

#include "core/api.h"
#include "core/callback.h"
#include "core/cpu_allocator.h"
#include "network/io_operation.h"
#include "network/io_poller.h"
#include "network/timer.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace arp::network {

class ARP_API io_service final {
    class interrupter_impl;

public:
    io_service();
    ~io_service();
    io_service(const io_service&) = delete;
    io_service& operator=(const io_service&) = delete;

    void run();
    void wakeup();
    void close();
    void schedule(const deadline_timer_ptr& timer);
    void execute(io_operation* op);
    void cancel_and_close(
        io_context* context, socket_t fd, core::Callback<void(const error_code&)> callback);
    void cancel_timer(const deadline_timer* timer);
    time_point time() const;
    time_type wait_duration() const;
    bool quit() const;

private:
    class close_operation;
    bool in_loop_thread() const;
    void process_timer();
    void process_pending();

    error_code ec_;
    std::thread::id worker_id_;
    std::atomic<bool> quit_{false};

    mutable std::recursive_mutex timer_mutex_;
    time_point time_{};
    core::pmr::Vector<deadline_timer_ptr> timers_;

    mutable std::mutex op_mutex_;
    core::pmr::Vector<io_operation*> pending_;

    poller poller_;
    core::UniquePtr<interrupter_impl> interrupter_;
};

} // namespace arp::network
