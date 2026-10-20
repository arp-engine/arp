#pragma once

#include "network/compat.h"

#include <system_error>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#define ARP_SOCKET_ERROR(e) WSA##e
#define ARP_NETDB_ERROR(e) WSA##e
#define ARP_GETADDRINFO_ERROR(e) WSA##e
#define ARP_WIN_OR_POSIX(e_win, e_posix) e_win
#else
#define ARP_SOCKET_ERROR(e) e
#define ARP_NETDB_ERROR(e) e
#define ARP_GETADDRINFO_ERROR(e) e
#define ARP_WIN_OR_POSIX(e_win, e_posix) e_posix
#endif

namespace arp::network {

enum inet_errors {
    access_denied = ARP_SOCKET_ERROR(EACCES),
    address_family_not_supported = ARP_SOCKET_ERROR(EAFNOSUPPORT),
    address_in_use = ARP_SOCKET_ERROR(EADDRINUSE),
    already_connected = ARP_SOCKET_ERROR(EISCONN),
    already_started = ARP_SOCKET_ERROR(EALREADY),
    broken_pipe = ARP_WIN_OR_POSIX(ERROR_BROKEN_PIPE, EPIPE),
    connection_aborted = ARP_SOCKET_ERROR(ECONNABORTED),
    connection_refused = ARP_SOCKET_ERROR(ECONNREFUSED),
    connection_reset = ARP_SOCKET_ERROR(ECONNRESET),
    bad_descriptor = ARP_SOCKET_ERROR(EBADF),
    fault = ARP_SOCKET_ERROR(EFAULT),
    host_unreachable = ARP_SOCKET_ERROR(EHOSTUNREACH),
    in_progress = ARP_SOCKET_ERROR(EINPROGRESS),
    interrupted = ARP_SOCKET_ERROR(EINTR),
    invalid_argument = ARP_SOCKET_ERROR(EINVAL),
    message_size = ARP_SOCKET_ERROR(EMSGSIZE),
    name_too_long = ARP_SOCKET_ERROR(ENAMETOOLONG),
    network_down = ARP_SOCKET_ERROR(ENETDOWN),
    network_reset = ARP_SOCKET_ERROR(ENETRESET),
    network_unreachable = ARP_SOCKET_ERROR(ENETUNREACH),
    no_descriptors = ARP_SOCKET_ERROR(EMFILE),
    no_buffer_space = ARP_SOCKET_ERROR(ENOBUFS),
    no_memory = ARP_WIN_OR_POSIX(ERROR_OUTOFMEMORY, ENOMEM),
    no_permission = ARP_WIN_OR_POSIX(ERROR_ACCESS_DENIED, EPERM),
    no_protocol_option = ARP_SOCKET_ERROR(ENOPROTOOPT),
    no_such_device = ARP_WIN_OR_POSIX(ERROR_BAD_UNIT, ENODEV),
    not_connected = ARP_SOCKET_ERROR(ENOTCONN),
    not_socket = ARP_SOCKET_ERROR(ENOTSOCK),
    operation_aborted = ARP_WIN_OR_POSIX(ERROR_OPERATION_ABORTED, ECANCELED),
    operation_not_supported = ARP_SOCKET_ERROR(EOPNOTSUPP),
    shut_down = ARP_SOCKET_ERROR(ESHUTDOWN),
    timed_out = ARP_SOCKET_ERROR(ETIMEDOUT),
    try_again = ARP_WIN_OR_POSIX(ERROR_RETRY, EAGAIN),
    would_block = ARP_SOCKET_ERROR(EWOULDBLOCK)
};

enum netdb_errors {
    host_not_found = ARP_NETDB_ERROR(HOST_NOT_FOUND),
    host_not_found_try_again = ARP_NETDB_ERROR(TRY_AGAIN),
    no_data = ARP_NETDB_ERROR(NO_DATA),
    no_recovery = ARP_NETDB_ERROR(NO_RECOVERY)
};

enum addrinfo_errors {
    service_not_found = ARP_WIN_OR_POSIX(WSATYPE_NOT_FOUND, ARP_GETADDRINFO_ERROR(EAI_SERVICE)),
    socket_type_not_supported =
        ARP_WIN_OR_POSIX(WSAESOCKTNOSUPPORT, ARP_GETADDRINFO_ERROR(EAI_SOCKTYPE))
};

using error_code = std::error_code;
using error_category = std::error_category;

ARP_API void clear(error_code& ec);
ARP_API bool get_last_error(error_code& ec, bool is_error_condition);
ARP_API error_code make_error_code(inet_errors e);
ARP_API error_code make_error_code(netdb_errors e);
ARP_API error_code make_error_code(addrinfo_errors e);
ARP_API error_code make_inet_error(int value);

} // namespace arp::network

namespace std {
template <> struct is_error_code_enum<arp::network::inet_errors> : true_type {};

template <> struct is_error_code_enum<arp::network::netdb_errors> : true_type {};

template <> struct is_error_code_enum<arp::network::addrinfo_errors> : true_type {};
} // namespace std

#undef ARP_SOCKET_ERROR
#undef ARP_NETDB_ERROR
#undef ARP_GETADDRINFO_ERROR
#undef ARP_WIN_OR_POSIX
