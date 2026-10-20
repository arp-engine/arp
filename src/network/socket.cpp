#include "network/socket.h"

namespace arp::network {

socket_base::socket_base(io_context* ctx) : ctx_(ctx)
{}

socket_base::socket_base(io_context* ctx, socket_t fd) : ctx_(ctx), fd_(fd)
{}

socket_base::~socket_base()
{
    error_code ec;
    close(ec);
}

int socket_base::bind(error_code& ec, const endpoint& ep)
{
    sockaddr_storage local{};
    if (socketops::bind(fd_, ep.addr(), ec) != 0) {
        return SOCKET_ERROR;
    }
    if (socketops::getsockname(fd_, local, ec) != 0) {
        return SOCKET_ERROR;
    }
    local_ = local;
    clear(ec);
    return 0;
}

int socket_base::close(error_code& ec)
{
    if (fd_ == INVALID_SOCKET) {
        clear(ec);
        return 0;
    }
    const int result = socketops::close(fd_, ec);
    fd_ = INVALID_SOCKET;
    return result;
}

int socket_base::set_nonblocking(error_code& ec, bool nonblocking)
{
    return socketops::set_nonblocking(fd_, nonblocking, ec);
}

int socket_base::set_broadcast(error_code& ec, bool broadcast)
{
    return socketops::set_broadcast(fd_, broadcast, ec);
}

int socket_base::set_reuseaddr(error_code& ec, bool reuseaddr)
{
    return socketops::set_reuseaddr(fd_, reuseaddr, ec);
}

int socket_base::set_keepalive(error_code& ec, bool keepalive)
{
    return socketops::set_keepalive(fd_, keepalive, ec);
}

int socket_base::set_nodelay(error_code& ec, bool nodelay)
{
    return socketops::set_nodelay(fd_, nodelay, ec);
}

int socket_base::set_linger(error_code& ec, uint16 onoff, uint16 linger)
{
    return socketops::set_linger(fd_, onoff, linger, ec);
}

void socket_base::set_local(const endpoint& ep)
{
    local_ = ep;
}

void socket_base::set_peer(const endpoint& ep)
{
    peer_ = ep;
}

arp::core::pmr::String socket_base::local_address() const
{
    return local_.to_string();
}

arp::core::pmr::String socket_base::peer_address() const
{
    return peer_.to_string();
}

const endpoint& socket_base::local_endpoint() const
{
    return local_;
}

const endpoint& socket_base::peer_endpoint() const
{
    return peer_;
}

socket_t socket_base::native_handle() const
{
    return fd_;
}

} // namespace arp::network
