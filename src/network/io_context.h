#pragma once

#include "core/api.h"
#include "core/callback.h"
#include "core/cpu_allocator.h"
#include "network/io_operation.h"
#include "network/timer.h"

#include <functional>
#include <mutex>
#include <utility>

namespace arp::network {

class io_service;

class ARP_API io_context final {
public:
    io_context();
    explicit io_context(io_service* service);
    ~io_context();
    io_context(const io_context&) = delete;
    io_context& operator=(const io_context&) = delete;

    void init();
    void destroy();
    time_point now() const;
    void start_op(io_operation* op);
    void dispatch();
    void shutdown();
    void cancel_and_close(socket_t fd, core::Callback<void(const error_code&)> callback);

    template <typename F, typename... Args> void post_complete(F&& callback, Args&&... args)
    {
        core::Callback<void()> item(
            std::bind(std::forward<F>(callback), std::forward<Args>(args)...));
        std::lock_guard<std::mutex> lock(complete_mutex_);
        complete_queue_.push_back(std::move(item));
    }

    deadline_timer_ptr run_at(const time_point& time, task callback);
    deadline_timer_ptr run_after(const duration& delay, task callback);
    deadline_timer_ptr run_every(const duration& interval, task callback);
    io_service* service() const;

private:
    io_service* service_ = nullptr;
    core::UniquePtr<io_service> owned_service_;
    std::mutex complete_mutex_;
    core::pmr::Vector<core::Callback<void()>> complete_queue_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::network
