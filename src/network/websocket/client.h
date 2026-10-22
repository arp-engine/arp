#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "network/endpoint.h"
#include "network/http/client.h"
#include "network/io_context.h"
#include "network/tls/tls_context.h"
#include "network/transport.h"
#include "network/websocket/stream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace arp::network::websocket {

struct ClientStatus {
    error_code transport_error{};
    DiagnosticCode diagnostic = DiagnosticCode::none;
    http::DiagnosticCode http_diagnostic = http::DiagnosticCode::none;
    tls::TlsDiagnosticCode tls_diagnostic = tls::TlsDiagnosticCode::none;
    std::size_t bytes = 0;

    bool success() const noexcept;
};

class ARP_API ClientRequest {
public:
    ClientRequest();

    static ClientRequest connect(StringView uri);

    bool success() const noexcept;
    http::DiagnosticCode diagnostic() const noexcept;
    http::DiagnosticCode set_uri(StringView uri);

    tls::TlsConfig& tls_config() noexcept;
    const tls::TlsConfig& tls_config() const noexcept;

    const http::Uri& uri() const noexcept;
    StringView host_header() const noexcept;
    StringView resource() const noexcept;
    StringView key() const noexcept;

private:
    http::DiagnosticCode rebuild_from_uri();

    http::Uri uri_;
    tls::TlsConfig tls_config_;
    core::pmr::String host_header_;
    core::pmr::String resource_;
    core::pmr::String key_;
    http::DiagnosticCode diagnostic_ = http::DiagnosticCode::none;
};

namespace detail {

inline void appendUnsignedDecimal(core::pmr::String& out, std::size_t value)
{
    char digits[32]{};
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value > 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

inline core::pmr::String serviceNameFromPort(uint16 port)
{
    core::pmr::String service{&core::defaultCpuMemoryResource()};
    appendUnsignedDecimal(service, port);
    return service;
}

} // namespace detail

class ARP_API Client {
public:
    explicit Client(io_context* context);
    Client(io_context* context, const ClientRequest& request);
    Client(io_context* context, const ClientRequest& request, StreamConfig config);
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    io_context* context() const noexcept;
    const ClientRequest& request() const noexcept;
    void set_request(const ClientRequest& request);
    StreamState state() const noexcept;

    template <typename Handler> void connect(Handler&& handler);

    template <typename Handler> void async_connect(const ClientRequest& request, Handler&& handler);

    template <typename Handler> void send(StringView text, Handler&& handler);

    template <typename Handler> void send_text(StringView text, Handler&& handler);

    template <typename Handler> void async_write_text(StringView text, Handler&& handler);

    template <typename Handler>
    void async_write_binary(const byte_buffer& payload, Handler&& handler);

    template <typename Handler> void receive(Handler&& handler);

    template <typename Handler> void async_read_frame(Handler&& handler);

    template <typename Handler>
    void close(std::uint16_t code, StringView reason, Handler&& handler);

    template <typename Handler>
    void async_close(std::uint16_t code, StringView reason, Handler&& handler);

private:
    enum class ActiveTransport {
        none,
        direct,
    };

    void reset_transport();
    ClientStatus open_socket();
    static ClientStatus fromStreamStatus(const StreamStatus& status) noexcept;
    static ClientStatus fromStreamAndTlsStatus(
        const StreamStatus& streamStatus, const tls::TlsStatus& tlsStatus) noexcept;
    static ClientStatus fromHttpDiagnostic(http::DiagnosticCode diagnostic) noexcept;
    static ClientStatus fromTransportError(const error_code& ec) noexcept;
    static ClientStatus fromTlsStatus(const tls::TlsStatus& status) noexcept;
    static ClientStatus fromSecurePrepare(const SecureStreamPrepareResult& prepare) noexcept;

    io_context* context_ = nullptr;
    ClientRequest request_;
    bool has_request_ = false;
    StreamConfig stream_config_{};
    tcp::socket<ipv4> socket_;
    core::UniquePtr<Stream> stream_;
    core::UniquePtr<tls::TlsContext> tls_context_;
    core::UniquePtr<tls::SslStream> tls_stream_;
    core::UniquePtr<http::detail::TlsHttpStreamAdapter> tls_transport_;
    ActiveTransport active_transport_ = ActiveTransport::none;
};

template <typename Handler> void Client::connect(Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    if (!has_request_) {
        (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::malformed_uri));
        return;
    }
    async_connect(request_, [handlerState](const ClientStatus& status) {
        (*handlerState)(status);
    });
}

template <typename Handler>
void Client::async_connect(const ClientRequest& request, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));

    if (!request.success()) {
        (*handlerState)(fromHttpDiagnostic(request.diagnostic()));
        return;
    }
    const bool secure = request.uri().scheme() == "wss";
    if (request.uri().scheme() != "ws" && !secure) {
        (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::unsupported_scheme));
        return;
    }

    if (secure) {
        const auto prepared = Stream::prepareSecureClient(request.uri());
        if (!prepared.success()) {
            ClientStatus status;
            status.diagnostic = prepared.diagnostic;
            status.http_diagnostic = prepared.http_diagnostic;
            status.tls_diagnostic = prepared.tls_diagnostic;
            (*handlerState)(status);
            return;
        }
    }

    if (context_ == nullptr) {
        (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::malformed_uri));
        return;
    }

    endpoint remote(request.uri().host(), request.uri().port());
    if (!remote.is_valid()) {
        (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::malformed_uri));
        return;
    }

    auto requestState = core::makeShared<ClientRequest>(request);
    request_ = *requestState;
    has_request_ = true;
    reset_transport();

    const auto openStatus = open_socket();
    if (!openStatus.success()) {
        (*handlerState)(openStatus);
        return;
    }
    active_transport_ = ActiveTransport::direct;

    socket_.async_connect(remote, [this, handlerState, requestState, secure](const error_code& ec) {
        if (ec) {
            active_transport_ = ActiveTransport::none;
            (*handlerState)(fromTransportError(ec));
            return;
        }

        auto upgrade = [this, handlerState, requestState](auto& transport) {
            stream_->async_client_upgrade(transport,
                requestState->host_header(),
                requestState->resource(),
                requestState->key(),
                [this, handlerState](const StreamStatus& status) {
                    const auto tlsStatus =
                        tls_stream_ != nullptr ? tls_stream_->status() : tls::TlsStatus{};
                    (*handlerState)(fromStreamAndTlsStatus(status, tlsStatus));
                });
        };

        if (!secure) {
            upgrade(socket_);
            return;
        }

        tls_context_ = core::makeUnique<tls::TlsContext>(requestState->tls_config());
        if (!tls_context_->status().success()) {
            (*handlerState)(fromTlsStatus(tls_context_->status()));
            return;
        }
        tls_stream_ = core::makeUnique<tls::SslStream>(*tls_context_);
        tls_transport_ =
            core::makeUnique<http::detail::TlsHttpStreamAdapter>(&socket_, tls_stream_.get());
        tls_stream_->async_handshake(
            socket_, [this, handlerState, upgrade](const tls::TlsStatus& status) mutable {
                if (!status.success()) {
                    (*handlerState)(fromStreamAndTlsStatus(StreamStatus{}, status));
                    return;
                }
                upgrade(*tls_transport_);
            });
    });
}

template <typename Handler> void Client::send(StringView text, Handler&& handler)
{
    async_write_text(text, std::forward<Handler>(handler));
}

template <typename Handler> void Client::send_text(StringView text, Handler&& handler)
{
    async_write_text(text, std::forward<Handler>(handler));
}

template <typename Handler> void Client::async_write_text(StringView text, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    if (active_transport_ == ActiveTransport::direct && stream_) {
        auto complete = [this, handlerState](const StreamStatus& status) {
            const auto tlsStatus =
                tls_stream_ != nullptr ? tls_stream_->status() : tls::TlsStatus{};
            (*handlerState)(fromStreamAndTlsStatus(status, tlsStatus));
        };
        if (tls_transport_) {
            stream_->async_write_text(*tls_transport_, text, std::move(complete));
        } else {
            stream_->async_write_text(socket_, text, std::move(complete));
        }
        return;
    }
    (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::connection_closed));
}

template <typename Handler>
void Client::async_write_binary(const byte_buffer& payload, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    if (active_transport_ == ActiveTransport::direct && stream_) {
        auto complete = [this, handlerState](const StreamStatus& status) {
            const auto tlsStatus =
                tls_stream_ != nullptr ? tls_stream_->status() : tls::TlsStatus{};
            (*handlerState)(fromStreamAndTlsStatus(status, tlsStatus));
        };
        if (tls_transport_) {
            stream_->async_write_binary(*tls_transport_, payload, std::move(complete));
        } else {
            stream_->async_write_binary(socket_, payload, std::move(complete));
        }
        return;
    }
    (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::connection_closed));
}

template <typename Handler> void Client::receive(Handler&& handler)
{
    async_read_frame(std::forward<Handler>(handler));
}

template <typename Handler> void Client::async_read_frame(Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    if (active_transport_ == ActiveTransport::direct && stream_) {
        auto complete = [this, handlerState](const StreamStatus& status, const Frame& frame) {
            const auto tlsStatus =
                tls_stream_ != nullptr ? tls_stream_->status() : tls::TlsStatus{};
            (*handlerState)(fromStreamAndTlsStatus(status, tlsStatus), frame);
        };
        if (tls_transport_) {
            stream_->async_read_frame(*tls_transport_, std::move(complete));
        } else {
            stream_->async_read_frame(socket_, std::move(complete));
        }
        return;
    }
    (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::connection_closed), Frame{});
}

template <typename Handler>
void Client::close(std::uint16_t code, StringView reason, Handler&& handler)
{
    async_close(code, reason, std::forward<Handler>(handler));
}

template <typename Handler>
void Client::async_close(std::uint16_t code, StringView reason, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    if (active_transport_ == ActiveTransport::direct && stream_) {
        auto complete = [this, handlerState](const StreamStatus& status) {
            if (!status.success() || tls_stream_ == nullptr) {
                const auto tlsStatus =
                    tls_stream_ != nullptr ? tls_stream_->status() : tls::TlsStatus{};
                (*handlerState)(fromStreamAndTlsStatus(status, tlsStatus));
                return;
            }
            tls_stream_->async_shutdown(
                socket_, [this, handlerState, status](const tls::TlsStatus& tlsStatus) {
                    active_transport_ = ActiveTransport::none;
                    (*handlerState)(fromStreamAndTlsStatus(status, tlsStatus));
                });
        };
        if (tls_transport_) {
            stream_->async_write_close(*tls_transport_, code, reason, std::move(complete));
        } else {
            stream_->async_write_close(socket_, code, reason, std::move(complete));
        }
        return;
    }
    (*handlerState)(fromHttpDiagnostic(http::DiagnosticCode::connection_closed));
}

} // namespace arp::network::websocket
