#include "network/io_context.h"

#include "network/io_service.h"

namespace arp::network {

io_context::io_context()
{
    init();
}

io_context::io_context(io_service* service) : service_(service)
{
    init();
}

io_context::~io_context()
{
    destroy();
}

void io_context::init()
{
    if (service_ == nullptr) {
        owned_service_ = core::makeUnique<io_service>();
        service_ = owned_service_.get();
    }
}

void io_context::destroy()
{
    if (owned_service_) {
        owned_service_->close();
    }
}

time_point io_context::now() const
{
    return service_ != nullptr ? service_->time() : clock::now();
}

void io_context::start_op(io_operation* op)
{
    if (service_ != nullptr) {
        service_->execute(op);
    } else if (op != nullptr) {
        op->complete(nullptr, make_error_code(inet_errors::operation_aborted));
    }
}

void io_context::dispatch()
{
    core::pmr::Vector<core::Callback<void()>> tasks{&core::defaultCpuMemoryResource()};
    {
        std::lock_guard<std::mutex> lock(complete_mutex_);
        tasks.swap(complete_queue_);
    }

    for (auto& item : tasks) {
        if (item) {
            item();
        }
    }
}

std::size_t io_context::pending_completions() const noexcept
{
    std::lock_guard lock(complete_mutex_);
    return complete_queue_.size();
}

bool io_context::wait_for_completion(std::chrono::milliseconds timeout) noexcept
{
    std::unique_lock lock(complete_mutex_);
    return complete_ready_.wait_for(lock, timeout, [this] {
        return !complete_queue_.empty();
    });
}

void io_context::shutdown()
{
    if (service_ != nullptr) {
        service_->close();
    }
    complete_ready_.notify_all();
}

void io_context::cancel_and_close(socket_t fd, core::Callback<void(const error_code&)> callback)
{
    service_->cancel_and_close(this, fd, std::move(callback));
}

deadline_timer_ptr io_context::run_at(const time_point& time, task callback)
{
    auto timer = core::makeShared<deadline_timer>(this, service_, time, std::move(callback));
    service_->schedule(timer);
    return timer;
}

deadline_timer_ptr io_context::run_after(const duration& delay, task callback)
{
    auto timer =
        core::makeShared<deadline_timer>(this, service_, delay, std::move(callback), false);
    service_->schedule(timer);
    return timer;
}

deadline_timer_ptr io_context::run_every(const duration& interval, task callback)
{
    auto timer =
        core::makeShared<deadline_timer>(this, service_, interval, std::move(callback), true);
    service_->schedule(timer);
    return timer;
}

io_service* io_context::service() const
{
    return service_;
}

} // namespace arp::network
