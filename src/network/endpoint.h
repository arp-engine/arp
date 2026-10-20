#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "network/compat.h"
#include "network/error.h"

#include <utility>

namespace arp::network {

class ARP_API endpoint {
public:
    endpoint() noexcept;
    endpoint(int family, StringView ip, uint16 port) noexcept;
    endpoint(StringView ip, uint16 port) noexcept;
    endpoint(const endpoint& other) noexcept;
    endpoint(endpoint&& other) noexcept;
    explicit endpoint(const sockaddr* addr) noexcept;
    explicit endpoint(const sockaddr_storage& addr) noexcept;
    ~endpoint() = default;

    arp::core::pmr::String to_string() const;
    bool is_valid() const noexcept;
    bool is_ipv6() const noexcept;
    const sockaddr_storage& addr() const noexcept;
    uint16 port() const noexcept;
    endpoint& bind_port(uint16 port);
    bool operator==(const endpoint& other) const;
    bool operator!=(const endpoint& other) const;
    endpoint& operator=(const endpoint& other);
    endpoint& operator=(endpoint&& other) noexcept;
    endpoint& operator=(const sockaddr_storage& addr);
    endpoint& operator=(const sockaddr* addr);

    static bool to_sockaddr_storage(
        int family, const char* ip, uint16 port, sockaddr_storage& addr);
    static bool resolve(error_code& ec,
        const char* node,
        const char* service,
        int family,
        int socktype,
        int protocol,
        arp::core::pmr::Vector<endpoint>& out);

    template <typename Domain> static endpoint localhost(uint16 port)
    {
        return endpoint{Domain::family, Domain::loopback, port};
    }

    template <typename Domain> static endpoint anyaddr(uint16 port)
    {
        return endpoint{Domain::family, Domain::anyaddr, port};
    }

private:
    void copy_assign(const endpoint& other);
    void copy_assign(const sockaddr* addr);
    void move_assign(endpoint& other) noexcept;

    sockaddr_storage addr_{};
    uint16 port_ = 0;
};

} // namespace arp::network
