#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winerror.h>
#include <winsock2.h>
#include <ws2def.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
using arp_socket_t = SOCKET;
#ifndef socklen_t
using socklen_t = int;
#endif
#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#define SHUT_RDWR SD_BOTH
#endif
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using arp_socket_t = int;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#endif

namespace arp::network {

using socket_t = arp_socket_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using byte_buffer = arp::core::pmr::Vector<std::uint8_t>;
using StringView = std::basic_string_view<char>;

struct ipv4 {
    static constexpr int family = AF_INET;
    static constexpr const char* anyaddr = "0.0.0.0";
    static constexpr const char* broadcast = "255.255.255.255";
    static constexpr const char* loopback = "127.0.0.1";
    using sockaddr_in_t = sockaddr_in;
    using in_addr_t = in_addr;
    static constexpr std::size_t sockaddrlen = sizeof(sockaddr_in);
    static constexpr std::size_t in_addrlen = sizeof(in_addr);
};

struct ipv6 {
    static constexpr int family = AF_INET6;
    static constexpr const char* anyaddr = "::";
    static constexpr const char* loopback = "::1";
    using sockaddr_in_t = sockaddr_in6;
    using in_addr_t = in6_addr;
    static constexpr std::size_t sockaddrlen = sizeof(sockaddr_in6);
    static constexpr std::size_t in_addrlen = sizeof(in6_addr);
};

template <int Family> struct inet_traits;

template <> struct inet_traits<AF_INET> {
    using ip = ipv4;
};

template <> struct inet_traits<AF_INET6> {
    using ip = ipv6;
};

template <typename T, std::enable_if_t<std::is_same_v<T, ipv4> || std::is_same_v<T, ipv6>, int> = 0>
typename T::sockaddr_in_t* sockaddr_cast(sockaddr_storage& addr) noexcept
{
    return reinterpret_cast<typename T::sockaddr_in_t*>(&addr);
}

template <typename T, std::enable_if_t<std::is_same_v<T, ipv4> || std::is_same_v<T, ipv6>, int> = 0>
const typename T::sockaddr_in_t* sockaddr_cast(const sockaddr_storage& addr) noexcept
{
    return reinterpret_cast<const typename T::sockaddr_in_t*>(&addr);
}

inline std::size_t sockaddr_length(const sockaddr_storage& addr) noexcept
{
    if (addr.ss_family == ipv4::family) {
        return ipv4::sockaddrlen;
    }
    if (addr.ss_family == ipv6::family) {
        return ipv6::sockaddrlen;
    }
    return sizeof(sockaddr_storage);
}

} // namespace arp::network
