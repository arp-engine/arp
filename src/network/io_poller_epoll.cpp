#include "network/io_poller.h"

#if defined(__linux__)

#include <cerrno>
#include <cmath>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

namespace arp::network {

class poller::poller_impl final {
public:
    explicit poller_impl(poller* owner) : owner_(owner), epoll_fd_(::epoll_create(20000)) {}

    ~poller_impl()
    {
        if (epoll_fd_ != -1) {
            ::close(epoll_fd_);
        }
    }

    bool valid(error_code& ec) const
    {
        if (epoll_fd_ != -1) {
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

        const int otherBit = bit == read_bit ? write_bit : read_bit;
        const bool otherRegistered = owner_->isregistered(fd, otherBit);

        epoll_event event{};
        event.data.fd = fd;
        if ((bit == read_bit && add) || (otherBit == read_bit && otherRegistered)) {
            event.events |= EPOLLIN;
        }
        if ((bit == write_bit && add) || (otherBit == write_bit && otherRegistered)) {
            event.events |= EPOLLOUT;
        }
        if (bit == error_bit && add) {
            event.events |= EPOLLERR | EPOLLHUP;
        }

        int op = EPOLL_CTL_ADD;
        if (add && otherRegistered) {
            op = EPOLL_CTL_MOD;
        } else if (!add && otherRegistered) {
            op = EPOLL_CTL_MOD;
        } else if (!add) {
            op = EPOLL_CTL_DEL;
        }

        if (op == EPOLL_CTL_DEL) {
            return !get_last_error(ec, ::epoll_ctl(epoll_fd_, op, fd, nullptr) < 0);
        }
        return !get_last_error(ec, ::epoll_ctl(epoll_fd_, op, fd, &event) < 0);
    }

    int epoll_fd() const { return epoll_fd_; }

private:
    poller* owner_ = nullptr;
    int epoll_fd_ = -1;
};

poller::poller()
    : impl_(core::makeUnique<poller_impl>(this)),
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
    epoll_event events[maxEvents];

    if (!impl_->valid(ec)) {
        return SOCKET_ERROR;
    }

    const int waitMs = waitUsec < 0.0 ? 0 : static_cast<int>(std::ceil(waitUsec / 1000.0));
    const int ready = ::epoll_wait(impl_->epoll_fd(), events, maxEvents, waitMs);
    get_last_error(ec, ready < 0);
    if (ready <= 0) {
        return ready;
    }

    for (int index = 0; index < ready; ++index) {
        if ((events[index].events & (EPOLLERR | EPOLLHUP)) != 0) {
            trigger_event(events[index].data.fd, error_bit);
        }
        if ((events[index].events & EPOLLIN) != 0) {
            trigger_event(events[index].data.fd, read_bit);
        }
        if ((events[index].events & EPOLLOUT) != 0) {
            trigger_event(events[index].data.fd, write_bit);
        }
    }
    return ready;
}

bool poller::do_register(socket_t fd, int bit, error_code& ec)
{
    if (bit == error_bit) {
        clear(ec);
        return true;
    }
    return impl_->update(fd, bit, true, ec);
}

bool poller::do_deregister(socket_t fd, int bit, error_code& ec)
{
    if (bit == error_bit) {
        clear(ec);
        return true;
    }
    return impl_->update(fd, bit, false, ec);
}

} // namespace arp::network

#endif
