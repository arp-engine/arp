#include "network/socket_runtime.h"

#include "network/compat.h"

#include <cstdio>

namespace arp::network {

SocketRuntime::SocketRuntime()
{
#if defined(_WIN32)
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    valid_ = result == 0;
    if (!valid_) {
        char buffer[96]{};
        std::snprintf(buffer, sizeof(buffer), "WSAStartup failed with code %d", result);
        diagnostics_ = buffer;
    }
#else
    valid_ = true;
#endif
}

SocketRuntime::~SocketRuntime()
{
#if defined(_WIN32)
    if (valid_) {
        WSACleanup();
    }
#endif
}

bool SocketRuntime::valid() const
{
    return valid_;
}

const arp::core::pmr::String& SocketRuntime::diagnostics() const
{
    return diagnostics_;
}

} // namespace arp::network
