#include "network/io_service.h"

#include "network/io_context.h"
#include "network/socket_ops.h"

#include <algorithm>

namespace arp::network {

// Serialized with all other IO operations: unregister before closing, so a
// reused descriptor cannot inherit callbacks from a previous connection.
class io_service::close_operation final : public io_operation {
public:
    close_operation(io_service* service,
        io_context* context,
        socket_t fd,
        core::Callback<void(const error_code&)> callback)
        : io_operation(context, INVALID_SOCKET, 0), service_(service), closing_(fd),
          callback_(std::move(callback))
    {}

    status perform() override
    {
        service_->poller_.cancel_io_events(closing_);
        socketops::close(closing_, ec_);
        closing_ = INVALID_SOCKET;
        return done;
    }

    void complete(void* owner, const error_code&) override
    {
        auto holder = reclaim_io_operation(this);
        if (closing_ != INVALID_SOCKET)
            socketops::close(closing_, ec_);
        if (owner && callback_) {
            auto callback =
                core::makeShared<core::Callback<void(const error_code&)>>(std::move(callback_));
            ctx_->post_complete([callback, ec = ec_] {
                (*callback)(ec);
            });
        }
    }

private:
    io_service* service_;
    socket_t closing_;
    core::Callback<void(const error_code&)> callback_;
};

void io_service::cancel_and_close(
    io_context* context, socket_t fd, core::Callback<void(const error_code&)> callback)
{
    execute(initialize_async_operation<close_operation>(this, context, fd, std::move(callback)));
}

void io_service::cancel_timer(const deadline_timer* timer)
{
    std::lock_guard<std::recursive_mutex> lock(timer_mutex_);
    timers_.erase(std::remove_if(timers_.begin(),
                      timers_.end(),
                      [timer](const auto& entry) {
                          return entry.get() == timer;
                      }),
        timers_.end());
}

class io_service::interrupter_impl final : public io_operation {
public:
    interrupter_impl() { event_.opmask = op_read; }

    ~interrupter_impl() override { destroy_descriptors(); }

    status perform() override
    {
        char buffer[256]{};
        for (;;) {
            const int bytes = socketops::recv(event_.fd, buffer, sizeof(buffer), 0, ec_);
            if (bytes > 0) {
                if (bytes == sizeof(buffer)) {
                    continue;
                }
                return not_done;
            }
            if (bytes < 0 && (ec_ == inet_errors::interrupted)) {
                continue;
            }
            if (bytes < 0 && (ec_ == inet_errors::would_block || ec_ == inet_errors::try_again)) {
                return not_done;
            }
            return halt;
        }
    }

    void complete(void*, const error_code&) override {}

    void create_descriptors()
    {
        if (pipe_[0] != INVALID_SOCKET && pipe_[1] != INVALID_SOCKET) {
            return;
        }
        if (socketops::create_pipe(pipe_, ec_) == 0) {
            event_.fd = pipe_[1];
        }
    }

    void destroy_descriptors()
    {
        if (pipe_[0] != INVALID_SOCKET) {
            socketops::close(pipe_[0], ec_);
            pipe_[0] = INVALID_SOCKET;
        }
        if (pipe_[1] != INVALID_SOCKET) {
            socketops::close(pipe_[1], ec_);
            pipe_[1] = INVALID_SOCKET;
        }
        event_.fd = INVALID_SOCKET;
    }

    void interrupt()
    {
        if (pipe_[0] == INVALID_SOCKET) {
            return;
        }
        const char value = 0;
        socketops::send(pipe_[0], &value, 1, 0, ec_);
    }

private:
    socket_t pipe_[2]{INVALID_SOCKET, INVALID_SOCKET};
};

io_service::io_service()
    : time_(clock::now()), timers_(&core::defaultCpuMemoryResource()),
      pending_(&core::defaultCpuMemoryResource()),
      interrupter_(core::makeUnique<interrupter_impl>())
{}

io_service::~io_service()
{
    close();
    for (auto* op : pending_)
        if (op)
            op->destroy();
    pending_.clear();
}

void io_service::run()
{
    quit_ = false;
    worker_id_ = std::this_thread::get_id();
    interrupter_->create_descriptors();
    if (interrupter_->event().fd != INVALID_SOCKET) {
        poller_.register_io_event(
            interrupter_->event().fd, interrupter_->event().opmask, interrupter_.get(), ec_);
    }

    while (!quit_) {
        process_pending();
        process_timer();
        const time_type waitUsec = wait_duration();
        poller_.poll_io_events(waitUsec, ec_);
        process_timer();
        process_pending();
    }

    process_pending();
    poller_.halt_io_events();
    interrupter_->destroy_descriptors();
    worker_id_ = std::thread::id{};
}

void io_service::wakeup()
{
    interrupter_->interrupt();
}

void io_service::close()
{
    quit_ = true;
    if (!in_loop_thread()) {
        wakeup();
    }
}

void io_service::schedule(const deadline_timer_ptr& timer)
{
    bool shouldWake = false;
    {
        std::lock_guard<std::recursive_mutex> lock(timer_mutex_);
        timers_.push_back(timer);
        std::sort(timers_.begin(), timers_.end(), [](const auto& lhs, const auto& rhs) {
            return lhs->deadline() < rhs->deadline();
        });
        shouldWake = !timers_.empty() && timers_.front() == timer;
    }
    if (shouldWake && !in_loop_thread()) {
        wakeup();
    }
}

void io_service::execute(io_operation* op)
{
    {
        std::lock_guard<std::mutex> lock(op_mutex_);
        pending_.push_back(op);
    }
    if (!in_loop_thread()) {
        wakeup();
    }
}

time_point io_service::time() const
{
    return time_;
}

time_type io_service::wait_duration() const
{
    constexpr time_type defaultUsec = 5000000.0;
    std::lock_guard<std::recursive_mutex> lock(timer_mutex_);
    if (timers_.empty()) {
        return defaultUsec;
    }
    const auto now = clock::now();
    const auto delta = timers_.front()->deadline() - now;
    const auto usec = std::chrono::duration_cast<std::chrono::microseconds>(delta).count();
    return usec > 0 ? static_cast<time_type>(usec) : 0.0;
}

bool io_service::quit() const
{
    return quit_;
}

bool io_service::in_loop_thread() const
{
    return worker_id_ == std::this_thread::get_id();
}

void io_service::process_timer()
{
    time_ = clock::now();
    core::pmr::Vector<deadline_timer_ptr> due{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<deadline_timer_ptr> repeat{&core::defaultCpuMemoryResource()};

    {
        std::lock_guard<std::recursive_mutex> lock(timer_mutex_);
        while (!timers_.empty() && timers_.front()->deadline() <= time_) {
            auto timer = timers_.front();
            timers_.erase(timers_.begin());
            if (!timer->cancelled()) {
                due.push_back(timer);
            }
        }
    }

    for (auto& timer : due) {
        timer->trigger();
        if (timer->repeat() && !timer->cancelled()) {
            timer->reschedule(time_);
            repeat.push_back(timer);
        }
    }

    for (auto& timer : repeat) {
        schedule(timer);
    }
}

void io_service::process_pending()
{
    core::pmr::Vector<io_operation*> operations{&core::defaultCpuMemoryResource()};
    {
        std::lock_guard<std::mutex> lock(op_mutex_);
        operations.swap(pending_);
    }

    for (auto* op : operations) {
        if (op == nullptr) {
            continue;
        }
        switch (op->perform()) {
        case io_operation::not_done:
            if (!poller_.register_io_event(op->event().fd, op->event().opmask, op, ec_)) {
                op->complete(this, ec_);
            }
            break;
        case io_operation::done:
        case io_operation::halt:
            op->complete(this, error_code());
            break;
        }
    }
}

} // namespace arp::network
