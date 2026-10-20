#include "network/socket_ops.h"

#include "network/endpoint.h"

#include <cstring>

namespace arp::network::socketops {
namespace {

void close_if_valid(socket_t& fd, error_code& ec)
{
    if (fd != INVALID_SOCKET) {
        close(fd, ec);
        fd = INVALID_SOCKET;
    }
}

bool would_block_or_retry(const error_code& ec)
{
    return ec == inet_errors::try_again || ec == inet_errors::would_block
        || ec == inet_errors::interrupted;
}

} // namespace

socket_t open(int family, int type, int protocol, error_code& ec)
{
    socket_t fd = static_cast<socket_t>(::socket(family, type, protocol));
    get_last_error(ec, fd == INVALID_SOCKET);
    return fd;
}

socket_t accept(socket_t sock, sockaddr_storage& addr, error_code& ec)
{
    socklen_t length = sizeof(addr);
    socket_t fd =
        static_cast<socket_t>(::accept(sock, reinterpret_cast<sockaddr*>(&addr), &length));
    get_last_error(ec, fd == INVALID_SOCKET);
    return fd;
}

int bind(socket_t sock, const sockaddr_storage& addr, error_code& ec)
{
    const int result = ::bind(sock,
        reinterpret_cast<const sockaddr*>(&addr),
        static_cast<socklen_t>(sockaddr_length(addr)));
    get_last_error(ec, result != 0);
    return result;
}

int listen(socket_t sock, int backlog, error_code& ec)
{
    const int result = ::listen(sock, backlog);
    get_last_error(ec, result == SOCKET_ERROR);
    return result;
}

int connect(socket_t sock, const sockaddr_storage& addr, error_code& ec)
{
    const int result = ::connect(sock,
        reinterpret_cast<const sockaddr*>(&addr),
        static_cast<socklen_t>(sockaddr_length(addr)));
    get_last_error(ec, result != 0);
    return result;
}

int shutdown(socket_t sock, int how, error_code& ec)
{
    const int result = ::shutdown(sock, how);
    get_last_error(ec, result != 0);
    return result;
}

int close(socket_t sock, error_code& ec)
{
    if (sock == INVALID_SOCKET) {
        clear(ec);
        return 0;
    }
#if defined(_WIN32)
    const int result = ::closesocket(sock);
#else
    const int result = ::close(sock);
#endif
    get_last_error(ec, result != 0);
    return result;
}

int send(socket_t sock, const void* data, socklen_t size, int flags, error_code& ec)
{
    const int bytes = ::send(sock, static_cast<const char*>(data), size, flags);
    get_last_error(ec, bytes < 0);
    return bytes;
}

int sendto(socket_t sock,
    const sockaddr_storage& addr,
    const void* data,
    socklen_t size,
    int flags,
    error_code& ec)
{
    const int bytes = ::sendto(sock,
        static_cast<const char*>(data),
        size,
        flags,
        reinterpret_cast<const sockaddr*>(&addr),
        static_cast<socklen_t>(sockaddr_length(addr)));
    get_last_error(ec, bytes < 0);
    return bytes;
}

int recv(socket_t sock, void* data, socklen_t size, int flags, error_code& ec)
{
    const int bytes = ::recv(sock, static_cast<char*>(data), size, flags);
    get_last_error(ec, bytes < 0);
    return bytes;
}

int recvfrom(
    socket_t sock, sockaddr_storage& addr, void* data, socklen_t size, int flags, error_code& ec)
{
    socklen_t length = sizeof(addr);
    const int bytes = ::recvfrom(
        sock, static_cast<char*>(data), size, flags, reinterpret_cast<sockaddr*>(&addr), &length);
    get_last_error(ec, bytes < 0);
    return bytes;
}

int setsockopt(socket_t sock, int level, int opt, const char* value, int size, error_code& ec)
{
    const int result = ::setsockopt(sock, level, opt, value, size);
    get_last_error(ec, result != 0);
    return result;
}

int getsockopt(socket_t sock, int level, int opt, char* value, int* size, error_code& ec)
{
    auto length = static_cast<socklen_t>(*size);
    const int result = ::getsockopt(sock, level, opt, value, &length);
    *size = static_cast<int>(length);
    get_last_error(ec, result != 0);
    return result;
}

int set_nonblocking(socket_t sock, bool nonblocking, error_code& ec)
{
#if defined(_WIN32)
    u_long value = nonblocking ? 1UL : 0UL;
    const int result = ::ioctlsocket(sock, FIONBIO, &value);
    get_last_error(ec, result != 0);
    return result;
#else
    const int flags = ::fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        get_last_error(ec, true);
        return SOCKET_ERROR;
    }
    const int updated = nonblocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    const int result = ::fcntl(sock, F_SETFL, updated);
    get_last_error(ec, result != 0);
    return result;
#endif
}

int set_broadcast(socket_t sock, bool broadcast, error_code& ec)
{
    int value = broadcast ? 1 : 0;
    return setsockopt(
        sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&value), sizeof(value), ec);
}

int set_reuseaddr(socket_t sock, bool reuseaddr, error_code& ec)
{
    int value = reuseaddr ? 1 : 0;
    return setsockopt(
        sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&value), sizeof(value), ec);
}

int set_keepalive(socket_t sock, bool keepalive, error_code& ec)
{
    int value = keepalive ? 1 : 0;
    return setsockopt(
        sock, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char*>(&value), sizeof(value), ec);
}

int set_nodelay(socket_t sock, bool nodelay, error_code& ec)
{
    int value = nodelay ? 1 : 0;
    return setsockopt(
        sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&value), sizeof(value), ec);
}

int set_nosigpipe(socket_t sock, error_code& ec)
{
#if defined(__APPLE__)
    int value = 1;
    return setsockopt(
        sock, SOL_SOCKET, SO_NOSIGPIPE, reinterpret_cast<char*>(&value), sizeof(value), ec);
#else
    (void)sock;
    clear(ec);
    return 0;
#endif
}

int set_linger(socket_t sock, uint16 onoff, uint16 lingerValue, error_code& ec)
{
    linger value{};
    value.l_onoff = static_cast<decltype(value.l_onoff)>(onoff);
    value.l_linger = static_cast<decltype(value.l_linger)>(lingerValue);
    return setsockopt(
        sock, SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&value), sizeof(value), ec);
}

uint32 rtt(socket_t, error_code& ec)
{
    clear(ec);
    return 0;
}

bool getbuffersize(socket_t sock, int optname, int* size, error_code& ec)
{
    if (optname != SO_SNDBUF && optname != SO_RCVBUF) {
        ec = inet_errors::invalid_argument;
        return false;
    }
    int length = sizeof(int);
    return getsockopt(sock, SOL_SOCKET, optname, reinterpret_cast<char*>(size), &length, ec) == 0
        && length == sizeof(int);
}

bool setbuffersize(socket_t sock, int optname, int size, error_code& ec)
{
    if (optname != SO_SNDBUF && optname != SO_RCVBUF) {
        ec = inet_errors::invalid_argument;
        return false;
    }
    if (setsockopt(
            sock, SOL_SOCKET, optname, reinterpret_cast<const char*>(&size), sizeof(size), ec)
        != 0) {
        return false;
    }
    int confirmed = 0;
    return getbuffersize(sock, optname, &confirmed, ec) && confirmed >= size;
}

int getsockname(socket_t sock, sockaddr_storage& addr, error_code& ec)
{
    std::memset(&addr, 0, sizeof(addr));
    socklen_t length = sizeof(addr);
    const int result = ::getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &length);
    get_last_error(ec, result != 0);
    return result;
}

int getpeername(socket_t sock, sockaddr_storage& addr, error_code& ec)
{
    std::memset(&addr, 0, sizeof(addr));
    socklen_t length = sizeof(addr);
    const int result = ::getpeername(sock, reinterpret_cast<sockaddr*>(&addr), &length);
    get_last_error(ec, result != 0);
    return result;
}

int create_pipe(socket_t fd[2], error_code& ec)
{
    socket_t acceptor = INVALID_SOCKET;
    socket_t client = INVALID_SOCKET;
    socket_t server = INVALID_SOCKET;
    sockaddr_storage bound{};
    sockaddr_storage peer{};

    acceptor = open(AF_INET, SOCK_STREAM, 0, ec);
    if (acceptor == INVALID_SOCKET) {
        goto failed;
    }
    if (set_reuseaddr(acceptor, true, ec) != 0) {
        goto failed;
    }
    if (bind(acceptor, endpoint::localhost<ipv4>(0).addr(), ec) != 0) {
        goto failed;
    }
    if (getsockname(acceptor, bound, ec) != 0) {
        goto failed;
    }
    if (listen(acceptor, SOMAXCONN, ec) != 0) {
        goto failed;
    }

    client = open(AF_INET, SOCK_STREAM, 0, ec);
    if (client == INVALID_SOCKET) {
        goto failed;
    }
    if (connect(client, bound, ec) != 0 && !would_block_or_retry(ec)
        && ec != inet_errors::in_progress) {
        goto failed;
    }

    server = accept(acceptor, peer, ec);
    if (server == INVALID_SOCKET) {
        goto failed;
    }

    if (set_nonblocking(client, true, ec) != 0 || set_nonblocking(server, true, ec) != 0) {
        goto failed;
    }
    if (set_nodelay(client, true, ec) != 0 || set_nodelay(server, true, ec) != 0) {
        goto failed;
    }

    close(acceptor, ec);
    fd[0] = client;
    fd[1] = server;
    clear(ec);
    return 0;

failed:
    close_if_valid(acceptor, ec);
    close_if_valid(client, ec);
    close_if_valid(server, ec);
    fd[0] = INVALID_SOCKET;
    fd[1] = INVALID_SOCKET;
    return SOCKET_ERROR;
}

} // namespace arp::network::socketops
