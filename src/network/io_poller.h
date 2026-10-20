#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "network/io_operation.h"

#include <array>

namespace arp::network {

class ARP_API poller final {
    class poller_impl;

    struct op_descriptor_queue {
        core::pmr::Deque<io_operation*> operations{&core::defaultCpuMemoryResource()};
    };

    using op_descriptor_map = core::pmr::UnorderedMap<socket_t, op_descriptor_queue>;

public:
    poller();
    ~poller();
    poller(const poller&) = delete;
    poller& operator=(const poller&) = delete;

    bool isregistered(socket_t fd, int bit) const;
    bool register_io_event(socket_t fd, int opmask, io_operation* op, error_code& ec);
    bool deregister_io_event(socket_t fd, int opmask, error_code& ec);
    int poll_io_events(double waitUsec, error_code& ec);
    void halt_io_events();
    void cancel_io_events(socket_t fd);

private:
    bool do_register(socket_t fd, int bit, error_code& ec);
    bool do_deregister(socket_t fd, int bit, error_code& ec);
    bool deregister_io_event(socket_t fd, int opmask, io_operation* op, error_code& ec);
    void trigger_event(socket_t fd, int bit);

    core::UniquePtr<poller_impl> impl_;
    std::array<op_descriptor_map, op_bit::bit_max> op_descriptors_;
};

} // namespace arp::network
