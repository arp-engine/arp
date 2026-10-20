#include "network/error.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace arp::network {
namespace {

using ErrorMessage = decltype(std::declval<const error_category&>().message(0));

class InetErrorCategory final : public error_category {
public:
    const char* name() const noexcept override { return "arp.network.inet"; }

    ErrorMessage message(int value) const override
    {
#if defined(_WIN32)
        char* messageText = nullptr;
        const DWORD length = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER
                | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            static_cast<DWORD>(value),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<char*>(&messageText),
            0,
            nullptr);
        if (length != 0 && messageText != nullptr) {
            while (*messageText != '\0') {
                const auto textLength = std::strlen(messageText);
                if (textLength == 0
                    || (messageText[textLength - 1] != '\n'
                        && messageText[textLength - 1] != '\r')) {
                    break;
                }
                messageText[textLength - 1] = '\0';
            }
            ErrorMessage result = messageText;
            ::LocalFree(messageText);
            return result;
        }
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "win socket error %d", value);
        return buffer;
#else
        return ::strerror(value);
#endif
    }
};

class NetdbErrorCategory final : public error_category {
public:
    const char* name() const noexcept override { return "arp.network.netdb"; }

    ErrorMessage message(int value) const override
    {
        switch (value) {
        case netdb_errors::host_not_found:
            return "host not found";
        case netdb_errors::host_not_found_try_again:
            return "host not found, try again";
        case netdb_errors::no_data:
            return "no address data";
        case netdb_errors::no_recovery:
            return "non recoverable name service failure";
        default:
            return "unknown name service error";
        }
    }
};

class AddrinfoErrorCategory final : public error_category {
public:
    const char* name() const noexcept override { return "arp.network.addrinfo"; }

    ErrorMessage message(int value) const override
    {
        switch (value) {
        case addrinfo_errors::service_not_found:
            return "service not found";
        case addrinfo_errors::socket_type_not_supported:
            return "socket type not supported";
        default:
            return "unknown address info error";
        }
    }
};

const error_category& inetCategory()
{
    static InetErrorCategory category;
    return category;
}

const error_category& netdbCategory()
{
    static NetdbErrorCategory category;
    return category;
}

const error_category& addrinfoCategory()
{
    static AddrinfoErrorCategory category;
    return category;
}

} // namespace

void clear(error_code& ec)
{
    ec.clear();
}

bool get_last_error(error_code& ec, bool is_error_condition)
{
    if (!is_error_condition) {
        clear(ec);
        return false;
    }
#if defined(_WIN32)
    ec = error_code(WSAGetLastError(), inetCategory());
#else
    ec = error_code(errno, inetCategory());
#endif
    return true;
}

error_code make_error_code(inet_errors e)
{
    return error_code(static_cast<int>(e), inetCategory());
}

error_code make_error_code(netdb_errors e)
{
    return error_code(static_cast<int>(e), netdbCategory());
}

error_code make_error_code(addrinfo_errors e)
{
    return error_code(static_cast<int>(e), addrinfoCategory());
}

error_code make_inet_error(int value)
{
    return error_code(value, inetCategory());
}

} // namespace arp::network
