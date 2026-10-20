#pragma once

#include "network/error.h"
#include "network/tls/tls_context.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace arp::network::tls {

namespace detail {

template <typename Stream, typename Handler> class SslHandshakeOperation;

template <typename Stream, typename Handler> class SslReadOperation;

template <typename Stream, typename Handler> class SslWriteOperation;

template <typename Stream, typename Handler> class SslShutdownOperation;

} // namespace detail

enum class TlsStreamState {
    idle,
    handshaking,
    established,
    failed,
    shutdown,
};

class ARP_API SslStream {
public:
    explicit SslStream(TlsContext& context);
    ~SslStream();

    enum class IoAction {
        done,
        need_read,
        need_write,
        failed,
    };

    struct IoResult {
        IoAction action = IoAction::failed;
        TlsStatus status{};
        std::size_t bytes = 0;
        byte_buffer output{&core::defaultCpuMemoryResource()};
    };

    TlsStreamState state() const noexcept;
    const TlsStatus& status() const noexcept;

    template <typename Stream, typename Handler>
    void async_handshake(Stream& stream, Handler&& handler);

    template <typename Stream, typename Handler>
    void async_recv(Stream& stream, byte_buffer& buffer, Handler&& handler);

    template <typename Stream, typename Handler>
    void async_send(Stream& stream, const void* data, std::size_t size, Handler&& handler);

    template <typename Stream, typename Handler>
    void async_shutdown(Stream& stream, Handler&& handler);

private:
    template <typename Stream, typename Handler> friend class detail::SslHandshakeOperation;
    template <typename Stream, typename Handler> friend class detail::SslReadOperation;
    template <typename Stream, typename Handler> friend class detail::SslWriteOperation;
    template <typename Stream, typename Handler> friend class detail::SslShutdownOperation;

    TlsStatus disabledStatus() const noexcept;
    TlsStatus transportStatus() noexcept;
    TlsStatus consume_ciphertext(const std::uint8_t* data, std::size_t size);
    IoResult handshake_step();
    IoResult recv_step(byte_buffer& buffer);
    IoResult send_step(const void* data, std::size_t size);
    IoResult shutdown_step();

    class Impl;
    TlsContext* context_ = nullptr;
    TlsStatus status_;
    TlsStreamState state_ = TlsStreamState::idle;
    core::UniquePtr<Impl> impl_;
};

namespace detail {

template <typename Stream, typename Handler> class SslHandshakeOperation final {
public:
    SslHandshakeOperation(SslStream* owner, Stream* stream, Handler&& handler)
        : owner_(owner), stream_(stream), handler_(std::move(handler)),
          buffer_(&core::defaultCpuMemoryResource())
    {}

    template <typename Self> void step(const Self& self)
    {
        auto result = owner_->handshake_step();
        flush_or_continue(self, std::move(result));
    }

private:
    template <typename Self> void flush_or_continue(const Self& self, SslStream::IoResult&& result)
    {
        if (!result.output.empty()) {
            auto output = core::makeShared<byte_buffer>(std::move(result.output));
            stream_->async_send(output->data(),
                output->size(),
                [self, output, result](const error_code& ec, std::size_t) mutable {
                    if (ec) {
                        self->owner_->status_ = self->owner_->transportStatus();
                        self->owner_->state_ = TlsStreamState::failed;
                        self->handler_(self->owner_->status_);
                        return;
                    }
                    if (result.action == SslStream::IoAction::done
                        || result.action == SslStream::IoAction::failed) {
                        self->handler_(result.status);
                        return;
                    }
                    self->step(self);
                });
            return;
        }
        if (result.action == SslStream::IoAction::done
            || result.action == SslStream::IoAction::failed) {
            handler_(result.status);
            return;
        }
        buffer_.assign(16384, 0);
        stream_->async_recv(buffer_, [self](const error_code& ec, std::size_t bytes) {
            if (ec || bytes == 0) {
                self->owner_->status_ = ec ? self->owner_->transportStatus()
                                           : TlsStatus{TlsDiagnosticCode::connection_closed};
                self->owner_->state_ = TlsStreamState::failed;
                self->handler_(self->owner_->status_);
                return;
            }
            auto status = self->owner_->consume_ciphertext(self->buffer_.data(), bytes);
            if (!status.success()) {
                self->handler_(status);
                return;
            }
            self->step(self);
        });
    }

    SslStream* owner_ = nullptr;
    Stream* stream_ = nullptr;
    Handler handler_;
    byte_buffer buffer_;
};

template <typename Stream, typename Handler> class SslReadOperation final {
public:
    SslReadOperation(SslStream* owner, Stream* stream, byte_buffer* output, Handler&& handler)
        : owner_(owner), stream_(stream), output_(output), handler_(std::move(handler)),
          buffer_(&core::defaultCpuMemoryResource())
    {}

    template <typename Self> void step(const Self& self)
    {
        auto result = owner_->recv_step(*output_);
        flush_or_continue(self, std::move(result));
    }

private:
    template <typename Self> void flush_or_continue(const Self& self, SslStream::IoResult&& result)
    {
        if (!result.output.empty()) {
            auto output = core::makeShared<byte_buffer>(std::move(result.output));
            stream_->async_send(
                output->data(), output->size(), [self, output](const error_code& ec, std::size_t) {
                    if (ec) {
                        self->owner_->status_ = self->owner_->transportStatus();
                        self->owner_->state_ = TlsStreamState::failed;
                        self->handler_(self->owner_->status_, 0U);
                        return;
                    }
                    self->step(self);
                });
            return;
        }
        if (result.action == SslStream::IoAction::done
            || result.action == SslStream::IoAction::failed) {
            handler_(result.status, result.bytes);
            return;
        }
        buffer_.assign(16384, 0);
        stream_->async_recv(buffer_, [self](const error_code& ec, std::size_t bytes) {
            if (ec || bytes == 0) {
                self->owner_->status_ = ec ? self->owner_->transportStatus()
                                           : TlsStatus{TlsDiagnosticCode::connection_closed};
                self->owner_->state_ = TlsStreamState::failed;
                self->handler_(self->owner_->status_, 0U);
                return;
            }
            auto status = self->owner_->consume_ciphertext(self->buffer_.data(), bytes);
            if (!status.success()) {
                self->handler_(status, 0U);
                return;
            }
            self->step(self);
        });
    }

    SslStream* owner_ = nullptr;
    Stream* stream_ = nullptr;
    byte_buffer* output_ = nullptr;
    Handler handler_;
    byte_buffer buffer_;
};

template <typename Stream, typename Handler> class SslWriteOperation final {
public:
    SslWriteOperation(
        SslStream* owner, Stream* stream, const void* data, std::size_t size, Handler&& handler)
        : owner_(owner), stream_(stream), data_(data), size_(size), handler_(std::move(handler)),
          buffer_(&core::defaultCpuMemoryResource())
    {}

    template <typename Self> void step(const Self& self)
    {
        auto result = owner_->send_step(data_, size_);
        flush_or_continue(self, std::move(result));
    }

private:
    template <typename Self> void flush_or_continue(const Self& self, SslStream::IoResult&& result)
    {
        if (!result.output.empty()) {
            auto output = core::makeShared<byte_buffer>(std::move(result.output));
            stream_->async_send(output->data(),
                output->size(),
                [self, output, result](const error_code& ec, std::size_t) mutable {
                    if (ec) {
                        self->owner_->status_ = self->owner_->transportStatus();
                        self->owner_->state_ = TlsStreamState::failed;
                        self->handler_(self->owner_->status_, 0U);
                        return;
                    }
                    if (result.action == SslStream::IoAction::done
                        || result.action == SslStream::IoAction::failed) {
                        self->handler_(result.status, result.bytes);
                        return;
                    }
                    self->step(self);
                });
            return;
        }
        if (result.action == SslStream::IoAction::done
            || result.action == SslStream::IoAction::failed) {
            handler_(result.status, result.bytes);
            return;
        }
        buffer_.assign(16384, 0);
        stream_->async_recv(buffer_, [self](const error_code& ec, std::size_t bytes) {
            if (ec || bytes == 0) {
                self->owner_->status_ = ec ? self->owner_->transportStatus()
                                           : TlsStatus{TlsDiagnosticCode::connection_closed};
                self->owner_->state_ = TlsStreamState::failed;
                self->handler_(self->owner_->status_, 0U);
                return;
            }
            auto status = self->owner_->consume_ciphertext(self->buffer_.data(), bytes);
            if (!status.success()) {
                self->handler_(status, 0U);
                return;
            }
            self->step(self);
        });
    }

    SslStream* owner_ = nullptr;
    Stream* stream_ = nullptr;
    const void* data_ = nullptr;
    std::size_t size_ = 0;
    Handler handler_;
    byte_buffer buffer_;
};

template <typename Stream, typename Handler> class SslShutdownOperation final {
public:
    SslShutdownOperation(SslStream* owner, Stream* stream, Handler&& handler)
        : owner_(owner), stream_(stream), handler_(std::move(handler)),
          buffer_(&core::defaultCpuMemoryResource())
    {}

    template <typename Self> void step(const Self& self)
    {
        auto result = owner_->shutdown_step();
        flush_or_continue(self, std::move(result));
    }

private:
    template <typename Self> void flush_or_continue(const Self& self, SslStream::IoResult&& result)
    {
        if (!result.output.empty()) {
            auto output = core::makeShared<byte_buffer>(std::move(result.output));
            stream_->async_send(output->data(),
                output->size(),
                [self, output, result](const error_code& ec, std::size_t) mutable {
                    if (ec) {
                        self->owner_->status_ = self->owner_->transportStatus();
                        self->owner_->state_ = TlsStreamState::failed;
                        self->handler_(self->owner_->status_);
                        return;
                    }
                    if (result.action == SslStream::IoAction::done
                        || result.action == SslStream::IoAction::failed) {
                        self->handler_(result.status);
                        return;
                    }
                    self->step(self);
                });
            return;
        }
        if (result.action == SslStream::IoAction::done
            || result.action == SslStream::IoAction::failed) {
            handler_(result.status);
            return;
        }
        buffer_.assign(16384, 0);
        stream_->async_recv(buffer_, [self](const error_code& ec, std::size_t bytes) {
            if (ec || bytes == 0) {
                self->owner_->status_ = ec ? self->owner_->transportStatus()
                                           : TlsStatus{TlsDiagnosticCode::connection_closed};
                self->owner_->state_ = TlsStreamState::shutdown;
                self->handler_(self->owner_->status_);
                return;
            }
            auto status = self->owner_->consume_ciphertext(self->buffer_.data(), bytes);
            if (!status.success()) {
                self->handler_(status);
                return;
            }
            self->step(self);
        });
    }

    SslStream* owner_ = nullptr;
    Stream* stream_ = nullptr;
    Handler handler_;
    byte_buffer buffer_;
};

} // namespace detail

template <typename Stream, typename Handler>
void SslStream::async_handshake(Stream& stream, Handler&& handler)
{
    if (context_ == nullptr || !context_->status().success()) {
        (void)stream;
        status_ = disabledStatus();
        state_ = TlsStreamState::failed;
        std::forward<Handler>(handler)(status_);
        return;
    }
    using operation = detail::SslHandshakeOperation<Stream, std::decay_t<Handler>>;
    auto op = core::makeShared<operation>(this, &stream, std::forward<Handler>(handler));
    op->step(op);
}

template <typename Stream, typename Handler>
void SslStream::async_recv(Stream& stream, byte_buffer& buffer, Handler&& handler)
{
    if (context_ == nullptr || !context_->status().success()) {
        (void)stream;
        (void)buffer;
        status_ = disabledStatus();
        state_ = TlsStreamState::failed;
        std::forward<Handler>(handler)(status_, 0U);
        return;
    }
    using operation = detail::SslReadOperation<Stream, std::decay_t<Handler>>;
    auto op = core::makeShared<operation>(this, &stream, &buffer, std::forward<Handler>(handler));
    op->step(op);
}

template <typename Stream, typename Handler>
void SslStream::async_send(Stream& stream, const void* data, std::size_t size, Handler&& handler)
{
    if (context_ == nullptr || !context_->status().success()) {
        (void)stream;
        (void)data;
        (void)size;
        status_ = disabledStatus();
        state_ = TlsStreamState::failed;
        std::forward<Handler>(handler)(status_, 0U);
        return;
    }
    using operation = detail::SslWriteOperation<Stream, std::decay_t<Handler>>;
    auto op =
        core::makeShared<operation>(this, &stream, data, size, std::forward<Handler>(handler));
    op->step(op);
}

template <typename Stream, typename Handler>
void SslStream::async_shutdown(Stream& stream, Handler&& handler)
{
    if (context_ == nullptr || !context_->status().success()) {
        (void)stream;
        status_ = disabledStatus();
        state_ = TlsStreamState::shutdown;
        std::forward<Handler>(handler)(status_);
        return;
    }
    using operation = detail::SslShutdownOperation<Stream, std::decay_t<Handler>>;
    auto op = core::makeShared<operation>(this, &stream, std::forward<Handler>(handler));
    op->step(op);
}

} // namespace arp::network::tls
