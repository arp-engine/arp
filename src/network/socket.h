#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "network/endpoint.h"
#include "network/io_context.h"
#include "network/socket_ops.h"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace arp::network {

class ARP_API socket_base {
public:
    socket_base() = default;
    explicit socket_base(io_context* ctx);
    socket_base(io_context* ctx, socket_t fd);
    virtual ~socket_base();

    virtual int open(error_code& ec) = 0;
    virtual int bind(error_code& ec, const endpoint& ep);
    virtual int shutdown(error_code& ec, int how) = 0;
    virtual int close(error_code& ec);

    // Caller retains this object until completion; closing is reactor-owned.
    template <class Handler> void async_close(Handler&& handler)
    {
        const auto closing = fd_;
        fd_ = INVALID_SOCKET;
        if (closing == INVALID_SOCKET) {
            auto callback = core::makeShared<std::decay_t<Handler>>(std::forward<Handler>(handler));
            ctx_->post_complete([callback] {
                (*callback)(error_code{});
            });
            return;
        }
        ctx_->cancel_and_close(
            closing, core::Callback<void(const error_code&)>(std::forward<Handler>(handler)));
    }

    int set_nonblocking(error_code& ec, bool nonblocking);
    int set_broadcast(error_code& ec, bool broadcast);
    int set_reuseaddr(error_code& ec, bool reuseaddr);
    int set_keepalive(error_code& ec, bool keepalive);
    int set_nodelay(error_code& ec, bool nodelay = true);
    int set_linger(error_code& ec, uint16 onoff, uint16 linger);

    void set_local(const endpoint& ep);
    void set_peer(const endpoint& ep);
    arp::core::pmr::String local_address() const;
    arp::core::pmr::String peer_address() const;
    const endpoint& local_endpoint() const;
    const endpoint& peer_endpoint() const;
    socket_t native_handle() const;

    io_context* ctx_ = nullptr;
    socket_t fd_ = INVALID_SOCKET;
    endpoint local_{};
    endpoint peer_{};
};

template <typename Transport, typename Protocol> struct socket_traits {};

template <typename Transport, typename Protocol> class basic_socket : public socket_base {
public:
    using transport_type = Transport;
    using protocol_type = Protocol;
    using socket_impl = typename socket_traits<Transport, Protocol>::socket_impl_type;

    static constexpr int family = protocol_type::family;
    static constexpr int socktype = transport_type::socktype;
    static constexpr int protocol = transport_type::protocol;

    basic_socket();
    explicit basic_socket(io_context* ctx);
    basic_socket(io_context* ctx, socket_t fd);
    basic_socket(basic_socket&& other) noexcept;
    basic_socket(const basic_socket&) = delete;
    basic_socket& operator=(const basic_socket&) = delete;
    ~basic_socket() override = default;

    int open(error_code& ec) override;
    int shutdown(error_code& ec, int how) override;
    void set_socks5_proxy(const endpoint& ep);
    socket_impl* impl();
    basic_socket& operator=(basic_socket&& other) noexcept;

private:
    friend socket_impl;

    void move_assign(basic_socket&& other) noexcept;

protected:
    core::UniquePtr<socket_impl> impl_;
    endpoint proxy_;
};

template <typename Transport, typename Protocol>
class stream_socket : public basic_socket<Transport, Protocol> {
public:
    using basic_socket<Transport, Protocol>::basic_socket;

    int listen(error_code& ec, int backlog);
    int accecpt(error_code& ec, stream_socket& sock);
    int accept(error_code& ec, stream_socket& sock);
    int connect(error_code& ec, const endpoint& ep);
    socklen_t send(error_code& ec, const void* buffer, std::size_t size);
    socklen_t send(error_code& ec, const byte_buffer& buffer);
    socklen_t recv(error_code& ec, void* buffer, std::size_t size);
    socklen_t recv(error_code& ec, byte_buffer& buffer);
    bool recvall(error_code& ec, byte_buffer& buffer);

    template <typename Handle> void async_accecpt(stream_socket& sock, Handle&& handler);

    template <typename Handle> void async_accept(stream_socket& sock, Handle&& handler);

    template <typename Handle> void async_connect(const endpoint& ep, Handle&& handler);

    template <typename Handle>
    void async_send(const void* data, std::size_t size, Handle&& handler);

    template <typename Handle> void async_send(const byte_buffer& buffer, Handle&& handler);

    template <typename Handle> void async_recv(byte_buffer& buffer, Handle&& handler);

    void set_sni(StringView host);
    const arp::core::pmr::String& host() const;

private:
    arp::core::pmr::String host_{&arp::core::defaultCpuMemoryResource()};
};

template <typename Transport, typename Protocol>
class datagram_socket : public basic_socket<Transport, Protocol> {
public:
    using basic_socket<Transport, Protocol>::basic_socket;

    socklen_t sendto(error_code& ec, const endpoint& ep, const void* data, std::size_t size);
    socklen_t sendto(error_code& ec, const endpoint& ep, const byte_buffer& buffer);
    socklen_t recvfrom(error_code& ec, endpoint& ep, byte_buffer& buffer);

    template <typename Handle>
    void async_sendto(const endpoint& ep, const void* data, std::size_t size, Handle&& handler);

    template <typename Handle>
    void async_sendto(const endpoint& ep, const byte_buffer& buffer, Handle&& handler);

    template <typename Handle> void async_recvfrom(byte_buffer& buffer, Handle&& handler);
};

namespace detail {

inline byte_buffer copyToByteBuffer(const void* data, std::size_t size)
{
    byte_buffer buffer{&core::defaultCpuMemoryResource()};
    buffer.resize(size);
    if (size > 0) {
        std::memcpy(buffer.data(), data, size);
    }
    return buffer;
}

} // namespace detail

template <typename Transport, typename Protocol>
basic_socket<Transport, Protocol>::basic_socket() : impl_(core::makeUnique<socket_impl>())
{}

template <typename Transport, typename Protocol>
basic_socket<Transport, Protocol>::basic_socket(io_context* ctx)
    : socket_base(ctx), impl_(core::makeUnique<socket_impl>())
{}

template <typename Transport, typename Protocol>
basic_socket<Transport, Protocol>::basic_socket(io_context* ctx, socket_t fd)
    : socket_base(ctx, fd), impl_(core::makeUnique<socket_impl>())
{}

template <typename Transport, typename Protocol>
basic_socket<Transport, Protocol>::basic_socket(basic_socket&& other) noexcept
    : impl_(core::makeUnique<socket_impl>())
{
    move_assign(std::move(other));
}

template <typename Transport, typename Protocol>
int basic_socket<Transport, Protocol>::open(error_code& ec)
{
    fd_ = impl_->open(*this, family, socktype, protocol, ec);
    if (fd_ == INVALID_SOCKET) {
        return SOCKET_ERROR;
    }
    if (set_nonblocking(ec, true) != 0) {
        return SOCKET_ERROR;
    }
    if (socketops::set_nosigpipe(fd_, ec) != 0) {
        socketops::close(fd_, ec);
        fd_ = INVALID_SOCKET;
        return SOCKET_ERROR;
    }
    clear(ec);
    return 0;
}

template <typename Transport, typename Protocol>
int basic_socket<Transport, Protocol>::shutdown(error_code& ec, int how)
{
    return impl_->shutdown(*this, how, ec);
}

template <typename Transport, typename Protocol>
void basic_socket<Transport, Protocol>::set_socks5_proxy(const endpoint& ep)
{
    proxy_ = ep;
}

template <typename Transport, typename Protocol>
typename basic_socket<Transport, Protocol>::socket_impl* basic_socket<Transport, Protocol>::impl()
{
    return impl_.get();
}

template <typename Transport, typename Protocol>
basic_socket<Transport, Protocol>& basic_socket<Transport, Protocol>::operator=(
    basic_socket&& other) noexcept
{
    move_assign(std::move(other));
    return *this;
}

template <typename Transport, typename Protocol>
void basic_socket<Transport, Protocol>::move_assign(basic_socket&& other) noexcept
{
    if (this == &other) {
        return;
    }
    error_code ec;
    close(ec);
    std::swap(ctx_, other.ctx_);
    std::swap(fd_, other.fd_);
    std::swap(local_, other.local_);
    std::swap(peer_, other.peer_);
    std::swap(proxy_, other.proxy_);
    std::swap(impl_, other.impl_);
}

template <typename Transport, typename Protocol>
int stream_socket<Transport, Protocol>::listen(error_code& ec, int backlog)
{
    return this->impl_->listen(*this, backlog, ec);
}

template <typename Transport, typename Protocol>
int stream_socket<Transport, Protocol>::accecpt(error_code& ec, stream_socket& sock)
{
    sockaddr_storage peer{};
    socket_t fd = this->impl_->accept(*this, sock, peer, ec);
    if (fd == INVALID_SOCKET) {
        return SOCKET_ERROR;
    }
    sockaddr_storage local{};
    if (socketops::getsockname(fd, local, ec) != 0) {
        socketops::close(fd, ec);
        return SOCKET_ERROR;
    }
    sock.local_ = local;
    sock.peer_ = peer;
    if (sock.set_nonblocking(ec, true) != 0 || sock.set_nodelay(ec, true) != 0
        || socketops::set_nosigpipe(sock.fd_, ec) != 0) {
        socketops::close(fd, ec);
        sock.fd_ = INVALID_SOCKET;
        return SOCKET_ERROR;
    }
    clear(ec);
    return 0;
}

template <typename Transport, typename Protocol>
int stream_socket<Transport, Protocol>::accept(error_code& ec, stream_socket& sock)
{
    return accecpt(ec, sock);
}

template <typename Transport, typename Protocol>
int stream_socket<Transport, Protocol>::connect(error_code& ec, const endpoint& ep)
{
    if (this->impl_->connect(*this, ep.addr(), ec) != 0) {
        return SOCKET_ERROR;
    }
    this->peer_ = ep;
    clear(ec);
    return 0;
}

template <typename Transport, typename Protocol>
socklen_t stream_socket<Transport, Protocol>::send(
    error_code& ec, const void* buffer, std::size_t size)
{
    int bytes = 0;
    do {
        bytes = this->impl_->send(*this, buffer, static_cast<socklen_t>(size), 0, ec);
    } while (bytes < 0 && ec == inet_errors::interrupted);
    return static_cast<socklen_t>(bytes);
}

template <typename Transport, typename Protocol>
socklen_t stream_socket<Transport, Protocol>::send(error_code& ec, const byte_buffer& buffer)
{
    return send(ec, buffer.data(), buffer.size());
}

template <typename Transport, typename Protocol>
socklen_t stream_socket<Transport, Protocol>::recv(error_code& ec, void* buffer, std::size_t size)
{
    int bytes = 0;
    do {
        bytes = this->impl_->recv(*this, buffer, static_cast<socklen_t>(size), 0, ec);
    } while (bytes < 0 && ec == inet_errors::interrupted);
    return static_cast<socklen_t>(bytes);
}

template <typename Transport, typename Protocol>
socklen_t stream_socket<Transport, Protocol>::recv(error_code& ec, byte_buffer& buffer)
{
    return recv(ec, buffer.data(), buffer.size());
}

template <typename Transport, typename Protocol>
bool stream_socket<Transport, Protocol>::recvall(error_code& ec, byte_buffer& buffer)
{
    std::size_t transferred = 0;
    while (transferred < buffer.size()) {
        const int bytes = this->impl_->recv(*this,
            buffer.data() + transferred,
            static_cast<socklen_t>(buffer.size() - transferred),
            0,
            ec);
        if (bytes <= 0) {
            if (ec == inet_errors::would_block || ec == inet_errors::try_again
                || ec == inet_errors::interrupted) {
                continue;
            }
            return false;
        }
        transferred += static_cast<std::size_t>(bytes);
    }
    clear(ec);
    return true;
}

template <typename Transport, typename Protocol>
template <typename Handle>
void stream_socket<Transport, Protocol>::async_accecpt(stream_socket& sock, Handle&& handler)
{
    this->impl_->async_accecpt(*this, sock, std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void stream_socket<Transport, Protocol>::async_accept(stream_socket& sock, Handle&& handler)
{
    async_accecpt(sock, std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void stream_socket<Transport, Protocol>::async_connect(const endpoint& ep, Handle&& handler)
{
    this->impl_->async_connect(*this, ep, std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void stream_socket<Transport, Protocol>::async_send(
    const void* data, std::size_t size, Handle&& handler)
{
    this->impl_->async_send(
        *this, detail::copyToByteBuffer(data, size), std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void stream_socket<Transport, Protocol>::async_send(const byte_buffer& buffer, Handle&& handler)
{
    this->impl_->async_send(*this,
        byte_buffer(buffer, &core::defaultCpuMemoryResource()),
        std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void stream_socket<Transport, Protocol>::async_recv(byte_buffer& buffer, Handle&& handler)
{
    this->impl_->async_recv(*this, buffer, std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
void stream_socket<Transport, Protocol>::set_sni(StringView host)
{
    host_.assign(host.data(), host.size());
}

template <typename Transport, typename Protocol>
const arp::core::pmr::String& stream_socket<Transport, Protocol>::host() const
{
    return host_;
}

template <typename Transport, typename Protocol>
socklen_t datagram_socket<Transport, Protocol>::sendto(
    error_code& ec, const endpoint& ep, const void* data, std::size_t size)
{
    int bytes = 0;
    do {
        bytes = this->impl_->sendto(*this, ep.addr(), data, static_cast<socklen_t>(size), 0, ec);
    } while (bytes < 0 && ec == inet_errors::interrupted);
    return static_cast<socklen_t>(bytes);
}

template <typename Transport, typename Protocol>
socklen_t datagram_socket<Transport, Protocol>::sendto(
    error_code& ec, const endpoint& ep, const byte_buffer& buffer)
{
    return sendto(ec, ep, buffer.data(), buffer.size());
}

template <typename Transport, typename Protocol>
socklen_t datagram_socket<Transport, Protocol>::recvfrom(
    error_code& ec, endpoint& ep, byte_buffer& buffer)
{
    sockaddr_storage from{};
    const int bytes = this->impl_->recvfrom(
        *this, from, buffer.data(), static_cast<socklen_t>(buffer.size()), 0, ec);
    if (bytes > 0) {
        ep = from;
    }
    return static_cast<socklen_t>(bytes);
}

template <typename Transport, typename Protocol>
template <typename Handle>
void datagram_socket<Transport, Protocol>::async_sendto(
    const endpoint& ep, const void* data, std::size_t size, Handle&& handler)
{
    this->impl_->async_sendto(
        *this, ep, detail::copyToByteBuffer(data, size), std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void datagram_socket<Transport, Protocol>::async_sendto(
    const endpoint& ep, const byte_buffer& buffer, Handle&& handler)
{
    this->impl_->async_sendto(*this,
        ep,
        byte_buffer(buffer, &core::defaultCpuMemoryResource()),
        std::forward<Handle>(handler));
}

template <typename Transport, typename Protocol>
template <typename Handle>
void datagram_socket<Transport, Protocol>::async_recvfrom(byte_buffer& buffer, Handle&& handler)
{
    this->impl_->async_recvfrom(*this, buffer, std::forward<Handle>(handler));
}

} // namespace arp::network

#include "network/internal/datagram_socket_impl.h"
#include "network/internal/stream_socket_impl.h"
