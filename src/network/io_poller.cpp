#include "network/io_poller.h"

#include <utility>

namespace arp::network {

#if !defined(__linux__) && !defined(__APPLE__)

class poller::poller_impl final {
public:
    poller_impl()
    {
        for (auto& set : fd_sets) {
            FD_ZERO(&set);
        }
    }

    fd_set fd_sets[op_bit::bit_max];
    socket_t largest = INVALID_SOCKET;
    int writeCount = 0;
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
    fd_set errorFds = impl_->fd_sets[error_bit];
    fd_set readFds = impl_->fd_sets[read_bit];
    fd_set writeFds = impl_->fd_sets[write_bit];

    if (waitUsec < 0.0) {
        waitUsec = 0.0;
    }

    timeval timeout{};
    timeout.tv_sec = static_cast<long>(waitUsec / 1000000.0);
    timeout.tv_usec = static_cast<long>(waitUsec) % 1000000L;

#if defined(_WIN32)
    const int ready =
        ::select(0, &readFds, impl_->writeCount > 0 ? &writeFds : nullptr, &errorFds, &timeout);
#else
    const int ready = ::select(static_cast<int>(impl_->largest + 1),
        &readFds,
        impl_->writeCount > 0 ? &writeFds : nullptr,
        &errorFds,
        &timeout);
#endif
    get_last_error(ec, ready == SOCKET_ERROR);
    if (ready <= 0) {
        return ready;
    }

    core::pmr::Vector<std::pair<socket_t, int>> readyEvents{&core::defaultCpuMemoryResource()};
    readyEvents.reserve(static_cast<std::size_t>(ready));
    for (const auto& item : op_descriptors_[read_bit]) {
        if (FD_ISSET(item.first, &readFds)) {
            readyEvents.emplace_back(item.first, read_bit);
        }
    }
    for (const auto& item : op_descriptors_[write_bit]) {
        if (FD_ISSET(item.first, &writeFds)) {
            readyEvents.emplace_back(item.first, write_bit);
        }
    }
    for (const auto& item : op_descriptors_[error_bit]) {
        if (FD_ISSET(item.first, &errorFds)) {
            readyEvents.emplace_back(item.first, error_bit);
        }
    }

    for (const auto& event : readyEvents) {
        trigger_event(event.first, event.second);
    }
    return ready;
}

bool poller::do_register(socket_t fd, int bit, error_code& ec)
{
#if defined(_WIN32)
    if (impl_->fd_sets[bit].fd_count >= FD_SETSIZE) {
        ec = inet_errors::no_descriptors;
        return false;
    }
#else
    if (fd < 0 || fd >= FD_SETSIZE) {
        ec = inet_errors::no_descriptors;
        return false;
    }
#endif
    if (FD_ISSET(fd, &impl_->fd_sets[bit])) {
        ec = inet_errors::bad_descriptor;
        return false;
    }
    FD_SET(fd, &impl_->fd_sets[bit]);
    if (impl_->largest == INVALID_SOCKET || fd > impl_->largest) {
        impl_->largest = fd;
    }
    if (bit == write_bit) {
        ++impl_->writeCount;
    }
    clear(ec);
    return true;
}

bool poller::do_deregister(socket_t fd, int bit, error_code& ec)
{
    FD_CLR(fd, &impl_->fd_sets[bit]);
    if (bit == write_bit && impl_->writeCount > 0) {
        --impl_->writeCount;
    }

    socket_t largest = INVALID_SOCKET;
    for (const auto& descriptors : op_descriptors_) {
        for (const auto& item : descriptors) {
            if (largest == INVALID_SOCKET || item.first > largest) {
                largest = item.first;
            }
        }
    }
    impl_->largest = largest;
    clear(ec);
    return true;
}

#endif

} // namespace arp::network
