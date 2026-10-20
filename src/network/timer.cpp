#include "network/timer.h"

#include "network/io_context.h"
#include "network/io_service.h"

#include <utility>

namespace arp::network {

deadline_timer::deadline_timer(io_service* service, const time_point& deadline, task callback)
    : service_(service), deadline_(deadline), callback_(std::move(callback))
{}

deadline_timer::deadline_timer(
    io_service* service, const duration& interval, task callback, bool repeat)
    : service_(service), repeat_(repeat), interval_(interval), deadline_(clock::now() + interval),
      callback_(std::move(callback))
{}

deadline_timer::deadline_timer(
    io_context* context, io_service* service, const time_point& deadline, task callback)
    : context_(context), service_(service), deadline_(deadline), callback_(std::move(callback))
{}

deadline_timer::deadline_timer(
    io_context* context, io_service* service, const duration& interval, task callback, bool repeat)
    : context_(context), service_(service), repeat_(repeat), interval_(interval),
      deadline_(clock::now() + interval), callback_(std::move(callback))
{}

bool deadline_timer::cancelled() const
{
    return cancelled_;
}

bool deadline_timer::repeat() const
{
    return repeat_;
}

time_point deadline_timer::deadline() const
{
    return deadline_;
}

duration deadline_timer::interval() const
{
    return interval_;
}

void deadline_timer::cancel()
{
    cancelled_ = true;
    if (service_)
        service_->cancel_timer(this);
}

void deadline_timer::trigger()
{
    if (cancelled_ || !callback_) {
        return;
    }
    if (context_ != nullptr) {
        context_->post_complete(callback_);
    } else {
        callback_();
    }
}

void deadline_timer::reschedule(const time_point& now)
{
    deadline_ = now + interval_;
}

} // namespace arp::network
