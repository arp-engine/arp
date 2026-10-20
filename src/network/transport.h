#pragma once

#include "network/socket.h"

namespace arp::network {

struct tcp {
    template <typename Protocol> using socket = typename socket_traits<tcp, Protocol>::socket_type;
    static constexpr int socktype = SOCK_STREAM;
    static constexpr int protocol = 0;
};

struct udp {
    template <typename Protocol> using socket = typename socket_traits<udp, Protocol>::socket_type;
    static constexpr int socktype = SOCK_DGRAM;
    static constexpr int protocol = 0;
};

} // namespace arp::network
