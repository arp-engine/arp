#include "network/io_poller.h"

#if defined(__APPLE__)

#include <cerrno>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

namespace arp::network {

class poller::poller_impl final {
public:
    poller_impl() : kqueue_fd_(::kqueue()) {}

    ~poller_impl()
    {
        if (kqueue_fd_ != -1) {
            ::close(kqueue_fd_);
        }
    }

    bool valid(error_code& ec) const
    {
        if (kqueue_fd_ != -1) {
            clear(ec);
            return true;
        }
        ec = make_inet_error(errno);
        return false;
    }

    bool update(socket_t fd, int bit, bool add, error_code& ec)
    {
        if (!valid(ec)) {
            return false;
        }

        int filter = 0;
        if (bit == read_bit) {
            filter = EVFILT_READ;
        } else if (bit == write_bit) {
            filter = EVFILT_WRITE;
        } else {
            clear(ec);
            return true;
        }

        struct kevent event{};
        EV_SET(&event, static_cast<uintptr_t>(fd), filter, add ? EV_ADD : EV_DELETE, 0, 0, nullptr);
        return !get_last_error(ec, ::kevent(kqueue_fd_, &event, 1, nullptr, 0, nullptr) < 0);
    }

    int kqueue_fd() const { return kqueue_fd_; }

private:
    int kqueue_fd_ = -1;
};

poller::poller()
    : impl_(core::makeUnique<poller_impl>()),
      op_descriptors_{
          op_descriptor_map{&core::defaultCpuMemoryResource()},
          op_descriptor_map{&core::defaultCpuMemoryResource()},
          op_descriptor_map{&core::defaultCpuMemoryResource()},
      }
{}

poller::~poller() = default;

int poller::poll_io_events(double waitUsec, error_code& ec)
{
    constexpr int maxEvents = 2000;
    struct kevent events[maxEvents];

    if (!impl_->valid(ec)) {
        return SOCKET_ERROR;
    }

    if (waitUsec < 0.0) {
        waitUsec = 0.0;
    }
    timespec timeout{};
    timeout.tv_sec = static_cast<time_t>(waitUsec / 1000000.0);
    timeout.tv_nsec = static_cast<long>(static_cast<long long>(waitUsec) % 1000000LL) * 1000L;

    const int ready = ::kevent(impl_->kqueue_fd(), nullptr, 0, events, maxEvents, &timeout);
    get_last_error(ec, ready < 0);
    if (ready <= 0) {
        return ready;
    }

    for (int index = 0; index < ready; ++index) {
        if ((events[index].flags & EV_ERROR) != 0) {
            trigger_event(static_cast<socket_t>(events[index].ident), error_bit);
        }
        if (events[index].filter == EVFILT_READ) {
            trigger_event(static_cast<socket_t>(events[index].ident), read_bit);
        }
        if (events[index].filter == EVFILT_WRITE) {
            trigger_event(static_cast<socket_t>(events[index].ident), write_bit);
        }
    }
    return ready;
}

bool poller::do_register(socket_t fd, int bit, error_code& ec)
{
    return impl_->update(fd, bit, true, ec);
}

bool poller::do_deregister(socket_t fd, int bit, error_code& ec)
{
    return impl_->update(fd, bit, false, ec);
}

} // namespace arp::network

#endif
