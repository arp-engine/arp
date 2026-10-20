#pragma once

#include "network/endpoint.h"
#include "network/io_operation.h"
#include "network/socket.h"
#include "network/socket_ops.h"

namespace arp::network {

template <typename Handle, typename SocketType>
class socket_accept_op final : public basic_io_operation<Handle> {
    using base = basic_io_operation<Handle>;

public:
    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    socket_accept_op(io_context* ctx, socket_t fd, SocketType& newsock, U&& handle)
        : base(ctx, fd, op_read, std::forward<U>(handle)), newsock_(newsock)
    {}

    io_operation::status perform() override
    {
        sockaddr_storage peer{};
        const socket_t accepted = socketops::accept(this->event_.fd, peer, this->ec_);
        if (accepted == INVALID_SOCKET) {
            if (this->ec_ == inet_errors::try_again || this->ec_ == inet_errors::would_block
                || this->ec_ == inet_errors::interrupted) {
                return io_operation::not_done;
            }
            return io_operation::halt;
        }

        if (socketops::set_nonblocking(accepted, true, this->ec_) != 0
            || socketops::set_nodelay(accepted, true, this->ec_) != 0
            || socketops::set_nosigpipe(accepted, this->ec_) != 0) {
            error_code ignored;
            socketops::close(accepted, ignored);
            return io_operation::halt;
        }

        sockaddr_storage local{};
        if (socketops::getsockname(accepted, local, this->ec_) != 0) {
            socketops::close(accepted, this->ec_);
            return io_operation::halt;
        }

        SocketType replacement(this->ctx_, accepted);
        newsock_ = std::move(replacement);
        newsock_.local_ = local;
        newsock_.peer_ = peer;
        return io_operation::done;
    }

    void complete(void* owner, const error_code& ec) override
    {
        auto holder = reclaim_io_operation(this);
        if (owner != nullptr) {
            this->ctx_->post_complete(std::move(this->handle_), ec ? ec : this->ec_);
        }
    }

private:
    SocketType& newsock_;
};

template <typename Handle, typename SocketType>
class socket_connect_op final : public basic_io_operation<Handle> {
    using base = basic_io_operation<Handle>;

public:
    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    socket_connect_op(
        io_context* ctx, socket_t fd, SocketType& sock, const endpoint& ep, U&& handle)
        : base(ctx, fd, op_error | op_write, std::forward<U>(handle)), peer_(ep), sock_(sock)
    {}

    io_operation::status perform() override
    {
        if (!connecting_) {
            connecting_ = true;
            if (socketops::connect(this->event_.fd, peer_.addr(), this->ec_) != 0) {
                if (this->ec_ == inet_errors::try_again || this->ec_ == inet_errors::would_block
                    || this->ec_ == inet_errors::in_progress
                    || this->ec_ == inet_errors::already_started) {
                    return io_operation::not_done;
                }
                return io_operation::halt;
            }
            sock_.set_peer(peer_);
            return io_operation::done;
        }

        int socketError = 0;
        int length = sizeof(socketError);
        error_code optEc;
        if (socketops::getsockopt(this->event_.fd,
                SOL_SOCKET,
                SO_ERROR,
                reinterpret_cast<char*>(&socketError),
                &length,
                optEc)
            != 0) {
            this->ec_ = optEc;
            return io_operation::halt;
        }
        if (socketError != 0) {
            this->ec_ = make_inet_error(socketError);
            return io_operation::halt;
        }

        sockaddr_storage peer{};
        if (socketops::getpeername(this->event_.fd, peer, this->ec_) != 0) {
            return io_operation::halt;
        }
        sock_.set_peer(peer_);
        return io_operation::done;
    }

    void complete(void* owner, const error_code& ec) override
    {
        auto holder = reclaim_io_operation(this);
        if (owner != nullptr) {
            this->ctx_->post_complete(std::move(this->handle_), ec ? ec : this->ec_);
        }
    }

private:
    endpoint peer_;
    bool connecting_ = false;
    SocketType& sock_;
};

template <typename Handle> class socket_send_op final : public basic_io_operation<Handle> {
    using base = basic_io_operation<Handle>;

public:
    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    socket_send_op(io_context* ctx, socket_t fd, byte_buffer&& buffer, int flags, U&& handle)
        : base(ctx, fd, op_write, std::forward<U>(handle)), buffer_(std::move(buffer)),
          flags_(flags)
    {}

    io_operation::status perform() override
    {
        while (transferred_ < buffer_.size()) {
            const int bytes = socketops::send(this->event_.fd,
                buffer_.data() + transferred_,
                static_cast<socklen_t>(buffer_.size() - transferred_),
                flags_,
                this->ec_);
            if (bytes < 0) {
                if (this->ec_ == inet_errors::interrupted) {
                    continue;
                }
                if (this->ec_ == inet_errors::try_again || this->ec_ == inet_errors::would_block
                    || this->ec_ == inet_errors::no_buffer_space) {
                    return io_operation::not_done;
                }
                return io_operation::halt;
            }
            if (bytes == 0) {
                return io_operation::not_done;
            }
            transferred_ += static_cast<std::size_t>(bytes);
        }
        return io_operation::done;
    }

    void complete(void* owner, const error_code& ec) override
    {
        auto holder = reclaim_io_operation(this);
        if (owner != nullptr) {
            this->ctx_->post_complete(std::move(this->handle_), ec ? ec : this->ec_, transferred_);
        }
    }

private:
    byte_buffer buffer_;
    std::size_t transferred_ = 0;
    int flags_ = 0;
};

template <typename Handle> class socket_recv_op final : public basic_io_operation<Handle> {
    using base = basic_io_operation<Handle>;

public:
    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    socket_recv_op(io_context* ctx, socket_t fd, byte_buffer& buffer, int flags, U&& handle)
        : base(ctx, fd, op_read, std::forward<U>(handle)), data_(buffer.data()),
          size_(buffer.size()), flags_(flags)
    {}

    io_operation::status perform() override
    {
        for (;;) {
            const int bytes = socketops::recv(
                this->event_.fd, data_, static_cast<socklen_t>(size_), flags_, this->ec_);
            if (bytes < 0 && this->ec_ == inet_errors::interrupted) {
                continue;
            }
            if (bytes < 0
                && (this->ec_ == inet_errors::try_again || this->ec_ == inet_errors::would_block)) {
                return io_operation::not_done;
            }
            if (bytes <= 0) {
                return io_operation::halt;
            }
            transferred_ = static_cast<std::size_t>(bytes);
            return io_operation::done;
        }
    }

    void complete(void* owner, const error_code& ec) override
    {
        auto holder = reclaim_io_operation(this);
        if (owner != nullptr) {
            this->ctx_->post_complete(std::move(this->handle_), ec ? ec : this->ec_, transferred_);
        }
    }

private:
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t transferred_ = 0;
    int flags_ = 0;
};

class stream_socket_impl {
public:
    template <typename StreamSocket>
    int open(StreamSocket&, int family, int socktype, int protocol, error_code& ec)
    {
        return socketops::open(family, socktype, protocol, ec);
    }

    template <typename StreamSocket> int shutdown(StreamSocket& sock, int how, error_code& ec)
    {
        return socketops::shutdown(sock.fd_, how, ec);
    }

    template <typename StreamSocket> int listen(StreamSocket& sock, int backlog, error_code& ec)
    {
        return socketops::listen(sock.fd_, backlog, ec);
    }

    template <typename StreamSocket>
    socket_t accept(
        StreamSocket& sock, StreamSocket& newsock, sockaddr_storage& addr, error_code& ec)
    {
        const socket_t fd = socketops::accept(sock.fd_, addr, ec);
        if (fd != INVALID_SOCKET) {
            StreamSocket replacement(sock.ctx_, fd);
            newsock = std::move(replacement);
        }
        return fd;
    }

    template <typename StreamSocket>
    int connect(StreamSocket& sock, const sockaddr_storage& addr, error_code& ec)
    {
        return socketops::connect(sock.fd_, addr, ec);
    }

    template <typename StreamSocket>
    int send(StreamSocket& sock, const void* data, socklen_t size, int flags, error_code& ec)
    {
        return socketops::send(sock.fd_, data, size, flags, ec);
    }

    template <typename StreamSocket>
    int recv(StreamSocket& sock, void* data, socklen_t size, int flags, error_code& ec)
    {
        return socketops::recv(sock.fd_, data, size, flags, ec);
    }

    template <typename StreamSocket, typename Handle>
    void async_accecpt(StreamSocket& sock, StreamSocket& newsock, Handle&& handler)
    {
        using handler_type = std::decay_t<Handle>;
        using op = socket_accept_op<handler_type, StreamSocket>;
        sock.ctx_->start_op(initialize_async_operation<op>(
            sock.ctx_, sock.fd_, newsock, std::forward<Handle>(handler)));
    }

    template <typename StreamSocket, typename Handle>
    void async_connect(StreamSocket& sock, const endpoint& ep, Handle&& handler)
    {
        using handler_type = std::decay_t<Handle>;
        using op = socket_connect_op<handler_type, StreamSocket>;
        sock.ctx_->start_op(initialize_async_operation<op>(
            sock.ctx_, sock.fd_, sock, ep, std::forward<Handle>(handler)));
    }

    template <typename StreamSocket, typename Handle>
    void async_send(StreamSocket& sock, byte_buffer&& buffer, Handle&& handler)
    {
        using handler_type = std::decay_t<Handle>;
        using op = socket_send_op<handler_type>;
        sock.ctx_->start_op(initialize_async_operation<op>(
            sock.ctx_, sock.fd_, std::move(buffer), 0, std::forward<Handle>(handler)));
    }

    template <typename StreamSocket, typename Handle>
    void async_recv(StreamSocket& sock, byte_buffer& buffer, Handle&& handler)
    {
        using handler_type = std::decay_t<Handle>;
        using op = socket_recv_op<handler_type>;
        sock.ctx_->start_op(initialize_async_operation<op>(
            sock.ctx_, sock.fd_, buffer, 0, std::forward<Handle>(handler)));
    }
};

struct tcp;

template <typename Protocol> struct socket_traits<tcp, Protocol> {
    using socket_type = stream_socket<tcp, Protocol>;
    using socket_impl_type = stream_socket_impl;
};

} // namespace arp::network
