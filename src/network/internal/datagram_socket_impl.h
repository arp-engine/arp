#pragma once

#include "network/io_operation.h"
#include "network/socket_ops.h"

namespace arp::network {

template <typename Handle> class socket_sendto_op final : public basic_io_operation<Handle> {
    using base = basic_io_operation<Handle>;

public:
    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    socket_sendto_op(io_context* ctx,
        socket_t fd,
        const endpoint& peer,
        byte_buffer&& buffer,
        int flags,
        U&& handle)
        : base(ctx, fd, op_write, std::forward<U>(handle)), peer_(peer), buffer_(std::move(buffer)),
          flags_(flags)
    {}

    io_operation::status perform() override
    {
        const int bytes = socketops::sendto(this->event_.fd,
            peer_.addr(),
            buffer_.data(),
            static_cast<socklen_t>(buffer_.size()),
            flags_,
            this->ec_);
        if (bytes < 0) {
            if (this->ec_ == inet_errors::interrupted || this->ec_ == inet_errors::try_again
                || this->ec_ == inet_errors::would_block
                || this->ec_ == inet_errors::no_buffer_space) {
                return io_operation::not_done;
            }
            return io_operation::halt;
        }
        transferred_ = static_cast<std::size_t>(bytes);
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
    endpoint peer_;
    byte_buffer buffer_;
    std::size_t transferred_ = 0;
    int flags_ = 0;
};

template <typename Handle> class socket_recvfrom_op final : public basic_io_operation<Handle> {
    using base = basic_io_operation<Handle>;

public:
    template <typename U, std::enable_if_t<std::is_same_v<std::decay_t<U>, Handle>, int> = 0>
    socket_recvfrom_op(io_context* ctx, socket_t fd, byte_buffer& buffer, int flags, U&& handle)
        : base(ctx, fd, op_read, std::forward<U>(handle)), data_(buffer.data()),
          size_(buffer.size()), flags_(flags)
    {}

    io_operation::status perform() override
    {
        sockaddr_storage from{};
        const int bytes = socketops::recvfrom(
            this->event_.fd, from, data_, static_cast<socklen_t>(size_), flags_, this->ec_);
        if (bytes < 0) {
            if (this->ec_ == inet_errors::interrupted || this->ec_ == inet_errors::try_again
                || this->ec_ == inet_errors::would_block) {
                return io_operation::not_done;
            }
            return io_operation::halt;
        }
        if (bytes == 0) {
            return io_operation::not_done;
        }
        peer_ = from;
        transferred_ = static_cast<std::size_t>(bytes);
        return io_operation::done;
    }

    void complete(void* owner, const error_code& ec) override
    {
        auto holder = reclaim_io_operation(this);
        if (owner != nullptr) {
            this->ctx_->post_complete(
                std::move(this->handle_), ec ? ec : this->ec_, peer_, transferred_);
        }
    }

private:
    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    endpoint peer_;
    std::size_t transferred_ = 0;
    int flags_ = 0;
};

class datagram_socket_impl {
public:
    template <typename DatagramSocket>
    int open(DatagramSocket&, int family, int socktype, int protocol, error_code& ec)
    {
        return socketops::open(family, socktype, protocol, ec);
    }

    template <typename DatagramSocket> int shutdown(DatagramSocket& sock, int how, error_code& ec)
    {
        return socketops::shutdown(sock.fd_, how, ec);
    }

    template <typename DatagramSocket>
    int sendto(DatagramSocket& sock,
        const sockaddr_storage& addr,
        const void* data,
        socklen_t size,
        int flags,
        error_code& ec)
    {
        return socketops::sendto(sock.fd_, addr, data, size, flags, ec);
    }

    template <typename DatagramSocket>
    int recvfrom(DatagramSocket& sock,
        sockaddr_storage& addr,
        void* data,
        socklen_t size,
        int flags,
        error_code& ec)
    {
        return socketops::recvfrom(sock.fd_, addr, data, size, flags, ec);
    }

    template <typename DatagramSocket, typename Handle>
    void async_sendto(
        DatagramSocket& sock, const endpoint& ep, byte_buffer&& buffer, Handle&& handler)
    {
        using handler_type = std::decay_t<Handle>;
        using op = socket_sendto_op<handler_type>;
        sock.ctx_->start_op(initialize_async_operation<op>(
            sock.ctx_, sock.fd_, ep, std::move(buffer), 0, std::forward<Handle>(handler)));
    }

    template <typename DatagramSocket, typename Handle>
    void async_recvfrom(DatagramSocket& sock, byte_buffer& buffer, Handle&& handler)
    {
        using handler_type = std::decay_t<Handle>;
        using op = socket_recvfrom_op<handler_type>;
        sock.ctx_->start_op(initialize_async_operation<op>(
            sock.ctx_, sock.fd_, buffer, 0, std::forward<Handle>(handler)));
    }
};

struct udp;

template <typename Protocol> struct socket_traits<udp, Protocol> {
    using socket_type = datagram_socket<udp, Protocol>;
    using socket_impl_type = datagram_socket_impl;
};

} // namespace arp::network
