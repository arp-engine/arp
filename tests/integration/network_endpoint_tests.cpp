#include "network/endpoint.h"
#include "network/socket_runtime.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace arp::network;

    SocketRuntime runtime;
    assert(runtime.valid());

    endpoint v4 = endpoint::localhost<ipv4>(3037);
    sockaddr_in v4Sock{};
    v4Sock.sin_family = ipv4::family;
    v4Sock.sin_port = htons(3037);
    assert(::inet_pton(ipv4::family, ipv4::loopback, &v4Sock.sin_addr) == 1);

    endpoint v4FromSock(reinterpret_cast<sockaddr*>(&v4Sock));
    endpoint v4Copy(v4);
    assert(v4.is_valid());
    assert(v4FromSock.is_valid());
    assert(v4Copy.is_valid());
    assert(v4.to_string()
        == arp::core::pmr::String("127.0.0.1:3037", &arp::core::defaultCpuMemoryResource()));
    assert(v4FromSock.to_string()
        == arp::core::pmr::String("127.0.0.1:3037", &arp::core::defaultCpuMemoryResource()));
    assert(v4Copy.to_string()
        == arp::core::pmr::String("127.0.0.1:3037", &arp::core::defaultCpuMemoryResource()));
    assert(v4.port() == 3037);
    assert(!v4.is_ipv6());
    assert(v4 == v4FromSock);
    assert(v4 == v4Copy);

    endpoint v6 = endpoint::localhost<ipv6>(3038);
    sockaddr_in6 v6Sock{};
    v6Sock.sin6_family = ipv6::family;
    v6Sock.sin6_port = htons(3038);
    assert(::inet_pton(ipv6::family, ipv6::loopback, &v6Sock.sin6_addr) == 1);

    endpoint v6FromSock(reinterpret_cast<sockaddr*>(&v6Sock));
    assert(v6.is_valid());
    assert(v6FromSock.is_valid());
    assert(v6.is_ipv6());
    assert(v6FromSock.is_ipv6());
    assert(v6.port() == 3038);
    assert(v6FromSock.port() == 3038);

    endpoint invalid(ipv4::family, "not-an-ip", 1);
    assert(!invalid.is_valid());

    endpoint rebound = endpoint::localhost<ipv4>(0);
    rebound.bind_port(45000);
    assert(rebound.port() == 45000);
    assert(rebound.to_string()
        == arp::core::pmr::String("127.0.0.1:45000", &arp::core::defaultCpuMemoryResource()));

    return 0;
}
