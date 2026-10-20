#include "network/endpoint.h"

#include <cstdio>
#include <cstring>

namespace arp::network {
namespace {

uint16 networkToHost(uint16 value)
{
    return ntohs(value);
}

uint16 hostToNetwork(uint16 value)
{
    return htons(value);
}

bool copyViewToBuffer(StringView text, char* buffer, std::size_t capacity)
{
    if (text.size() + 1 > capacity) {
        return false;
    }
    std::memcpy(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';
    return true;
}

error_code translate_addrinfo_error(int error)
{
    switch (error) {
    case 0:
        return error_code();
    case EAI_AGAIN:
        return netdb_errors::host_not_found_try_again;
    case EAI_BADFLAGS:
        return inet_errors::invalid_argument;
    case EAI_FAIL:
        return netdb_errors::no_recovery;
    case EAI_FAMILY:
        return inet_errors::address_family_not_supported;
    case EAI_MEMORY:
        return inet_errors::no_memory;
    case EAI_NONAME:
#if defined(EAI_ADDRFAMILY)
    case EAI_ADDRFAMILY:
#endif
#if defined(EAI_NODATA) && (EAI_NODATA != EAI_NONAME)
    case EAI_NODATA:
#endif
        return netdb_errors::host_not_found;
    case EAI_SERVICE:
        return addrinfo_errors::service_not_found;
    case EAI_SOCKTYPE:
        return addrinfo_errors::socket_type_not_supported;
    default:
        return make_inet_error(error);
    }
}

} // namespace

endpoint::endpoint() noexcept = default;

endpoint::endpoint(int family, StringView ip, uint16 port) noexcept : port_(port)
{
    char buffer[INET6_ADDRSTRLEN]{};
    if (!copyViewToBuffer(ip, buffer, sizeof(buffer))
        || !to_sockaddr_storage(family, buffer, port, addr_)) {
        port_ = 0;
    }
}

endpoint::endpoint(StringView ip, uint16 port) noexcept : port_(port)
{
    char buffer[INET6_ADDRSTRLEN]{};
    if (!copyViewToBuffer(ip, buffer, sizeof(buffer))
        || (!to_sockaddr_storage(ipv4::family, buffer, port, addr_)
            && !to_sockaddr_storage(ipv6::family, buffer, port, addr_))) {
        port_ = 0;
    }
}

endpoint::endpoint(const endpoint& other) noexcept
{
    copy_assign(other);
}

endpoint::endpoint(endpoint&& other) noexcept
{
    move_assign(other);
}

endpoint::endpoint(const sockaddr* addr) noexcept
{
    copy_assign(addr);
}

endpoint::endpoint(const sockaddr_storage& addr) noexcept
    : endpoint(reinterpret_cast<const sockaddr*>(&addr))
{}

arp::core::pmr::String endpoint::to_string() const
{
    arp::core::pmr::String result{&arp::core::defaultCpuMemoryResource()};
    if (!is_valid()) {
        return result;
    }

    char address[INET6_ADDRSTRLEN]{};
    const void* source = is_ipv6()
        ? static_cast<const void*>(&sockaddr_cast<ipv6>(addr_)->sin6_addr)
        : static_cast<const void*>(&sockaddr_cast<ipv4>(addr_)->sin_addr);
    if (::inet_ntop(addr_.ss_family, source, address, sizeof(address)) == nullptr) {
        return result;
    }

    char formatted[INET6_ADDRSTRLEN + 16]{};
    if (is_ipv6()) {
        std::snprintf(
            formatted, sizeof(formatted), "[%s]:%u", address, static_cast<unsigned>(port_));
    } else {
        std::snprintf(formatted, sizeof(formatted), "%s:%u", address, static_cast<unsigned>(port_));
    }
    result = formatted;
    return result;
}

bool endpoint::is_valid() const noexcept
{
    return addr_.ss_family == ipv4::family || addr_.ss_family == ipv6::family;
}

bool endpoint::is_ipv6() const noexcept
{
    return addr_.ss_family == ipv6::family;
}

const sockaddr_storage& endpoint::addr() const noexcept
{
    return addr_;
}

uint16 endpoint::port() const noexcept
{
    return port_;
}

endpoint& endpoint::bind_port(uint16 port)
{
    if (is_ipv6()) {
        sockaddr_cast<ipv6>(addr_)->sin6_port = hostToNetwork(port);
    } else if (is_valid()) {
        sockaddr_cast<ipv4>(addr_)->sin_port = hostToNetwork(port);
    }
    port_ = port;
    return *this;
}

bool endpoint::operator==(const endpoint& other) const
{
    if (addr_.ss_family != other.addr_.ss_family || port_ != other.port_) {
        return false;
    }
    if (!is_valid()) {
        return !other.is_valid();
    }
    if (is_ipv6()) {
        return std::memcmp(&sockaddr_cast<ipv6>(addr_)->sin6_addr,
                   &sockaddr_cast<ipv6>(other.addr_)->sin6_addr,
                   ipv6::in_addrlen)
            == 0;
    }
    return std::memcmp(&sockaddr_cast<ipv4>(addr_)->sin_addr,
               &sockaddr_cast<ipv4>(other.addr_)->sin_addr,
               ipv4::in_addrlen)
        == 0;
}

bool endpoint::operator!=(const endpoint& other) const
{
    return !(*this == other);
}

endpoint& endpoint::operator=(const endpoint& other)
{
    copy_assign(other);
    return *this;
}

endpoint& endpoint::operator=(endpoint&& other) noexcept
{
    move_assign(other);
    return *this;
}

endpoint& endpoint::operator=(const sockaddr_storage& addr)
{
    copy_assign(reinterpret_cast<const sockaddr*>(&addr));
    return *this;
}

endpoint& endpoint::operator=(const sockaddr* addr)
{
    copy_assign(addr);
    return *this;
}

bool endpoint::to_sockaddr_storage(int family, const char* ip, uint16 port, sockaddr_storage& addr)
{
    std::memset(&addr, 0, sizeof(sockaddr_storage));
    if (family == ipv4::family) {
        auto* sin4 = sockaddr_cast<ipv4>(addr);
        if (::inet_pton(family, ip, &sin4->sin_addr) != 1) {
            std::memset(&addr, 0, sizeof(sockaddr_storage));
            return false;
        }
        sin4->sin_family = static_cast<decltype(sin4->sin_family)>(family);
        sin4->sin_port = hostToNetwork(port);
        return true;
    }
    if (family == ipv6::family) {
        auto* sin6 = sockaddr_cast<ipv6>(addr);
        if (::inet_pton(family, ip, &sin6->sin6_addr) != 1) {
            std::memset(&addr, 0, sizeof(sockaddr_storage));
            return false;
        }
        sin6->sin6_family = static_cast<decltype(sin6->sin6_family)>(family);
        sin6->sin6_port = hostToNetwork(port);
        return true;
    }
    return false;
}

bool endpoint::resolve(error_code& ec,
    const char* node,
    const char* service,
    int family,
    int socktype,
    int protocol,
    arp::core::pmr::Vector<endpoint>& out)
{
    addrinfo* addresses = nullptr;
    addrinfo hints{};
    hints.ai_family = family;
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;

    const int result = ::getaddrinfo(node, service, &hints, &addresses);
    if (result != 0) {
        ec = translate_addrinfo_error(result);
        return false;
    }

    for (auto* item = addresses; item != nullptr; item = item->ai_next) {
        out.emplace_back(reinterpret_cast<const sockaddr*>(item->ai_addr));
    }
    ::freeaddrinfo(addresses);
    clear(ec);
    return true;
}

void endpoint::copy_assign(const endpoint& other)
{
    std::memcpy(&addr_, &other.addr_, sizeof(sockaddr_storage));
    port_ = other.port_;
}

void endpoint::copy_assign(const sockaddr* addr)
{
    std::memset(&addr_, 0, sizeof(sockaddr_storage));
    port_ = 0;
    if (addr == nullptr) {
        return;
    }
    if (addr->sa_family == ipv4::family) {
        std::memcpy(&addr_, addr, ipv4::sockaddrlen);
        port_ = networkToHost(sockaddr_cast<ipv4>(addr_)->sin_port);
    } else if (addr->sa_family == ipv6::family) {
        std::memcpy(&addr_, addr, ipv6::sockaddrlen);
        port_ = networkToHost(sockaddr_cast<ipv6>(addr_)->sin6_port);
    }
}

void endpoint::move_assign(endpoint& other) noexcept
{
    std::swap(addr_, other.addr_);
    std::swap(port_, other.port_);
}

} // namespace arp::network
