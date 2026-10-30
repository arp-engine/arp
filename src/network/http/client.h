#pragma once

#include "network/endpoint.h"
#include "network/http/session.h"
#include "network/http/uri.h"
#include "network/io_context.h"
#include "network/socket.h"
#include "network/tls/ssl_stream.h"
#include "network/tls/tls_context.h"
#include "network/transport.h"

#include <type_traits>
#include <utility>

namespace arp::network::http {

enum class ClientTransportKind {
    http_cleartext,
    websocket_cleartext,
    tls_required,
};

struct ClientPrepareResult {
    ClientTransportKind transport = ClientTransportKind::http_cleartext;
    DiagnosticCode diagnostic = DiagnosticCode::none;

    bool success() const noexcept;
};

struct SecureClientPrepareResult {
    ClientTransportKind transport = ClientTransportKind::tls_required;
    DiagnosticCode diagnostic = DiagnosticCode::none;
    tls::TlsDiagnosticCode tls_diagnostic = tls::TlsDiagnosticCode::none;
    tls::TlsConfig tls_config;

    bool success() const noexcept;
};

struct ClientStatus {
    error_code transport_error{};
    DiagnosticCode diagnostic = DiagnosticCode::none;
    tls::TlsDiagnosticCode tls_diagnostic = tls::TlsDiagnosticCode::none;

    bool success() const noexcept;
};

class ARP_API ClientRequest {
public:
    ClientRequest();
    ClientRequest(Method method, StringView uri);

    static ClientRequest get(StringView uri);
    static ClientRequest post(StringView uri);

    bool success() const noexcept;
    DiagnosticCode diagnostic() const noexcept;
    DiagnosticCode set_uri(StringView uri);
    // A resolver may bind an explicit IPv4 endpoint without changing the URI,
    // Host header or TLS server name. Changing the URI clears this binding.
    DiagnosticCode set_connection_endpoint(const endpoint& remote) noexcept;
    const endpoint* connection_endpoint() const noexcept;
    void set_method(Method method) noexcept;
    DiagnosticCode add_header(StringView name, StringView value);
    DiagnosticCode set_content_type(ContentType type);
    void set_body_text(StringView text);
    tls::TlsConfig& tls_config() noexcept;
    const tls::TlsConfig& tls_config() const noexcept;

    const Uri& uri() const noexcept;
    const Request& message() const noexcept;
    Request& message() noexcept;
    StringView host_header() const noexcept;

private:
    DiagnosticCode rebuild_from_uri();

    Uri uri_;
    endpoint connection_endpoint_;
    bool has_connection_endpoint_ = false;
    Request message_;
    tls::TlsConfig tls_config_;
    core::pmr::String host_header_;
    DiagnosticCode diagnostic_ = DiagnosticCode::none;
};

class ARP_API Client {
public:
    explicit Client(io_context* context = nullptr) noexcept;

    io_context* context() const noexcept;

    static ClientPrepareResult prepareTransport(const Uri& uri) noexcept;
    static SecureClientPrepareResult prepareSecureTransport(const Uri& uri);
    static SecureClientPrepareResult propagateTlsFailure(const tls::TlsStatus& status) noexcept;

    template <typename Handler> void async_send(const ClientRequest& request, Handler&& handler);

    template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
    void async_stream(const ClientRequest& request,
        HeadersHandler&& on_headers,
        BodyHandler&& on_body,
        CompleteHandler&& on_complete);

    template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
    void async_stream(const ClientRequest& request,
        SessionConfig config,
        HeadersHandler&& on_headers,
        BodyHandler&& on_body,
        CompleteHandler&& on_complete);

private:
    io_context* context_ = nullptr;
};

namespace detail {

inline ClientStatus clientStatusFromSession(const SessionStatus& sessionStatus) noexcept
{
    ClientStatus status;
    status.transport_error = sessionStatus.transport_error;
    status.diagnostic = sessionStatus.diagnostic;
    return status;
}

inline ClientStatus clientStatusFromDiagnostic(DiagnosticCode diagnostic) noexcept
{
    ClientStatus status;
    status.diagnostic = diagnostic;
    return status;
}

inline ClientStatus clientStatusFromTransportError(const error_code& ec) noexcept
{
    ClientStatus status;
    status.transport_error = ec;
    return status;
}

inline ClientStatus clientStatusFromTlsStatus(const tls::TlsStatus& tlsStatus) noexcept
{
    ClientStatus status;
    if (!tlsStatus.success()) {
        status.diagnostic = DiagnosticCode::tls_unavailable;
        status.tls_diagnostic = tlsStatus.diagnostic;
    }
    return status;
}

inline ClientStatus clientStatusFromSessionAndTls(
    const SessionStatus& sessionStatus, const tls::TlsStatus& tlsStatus) noexcept
{
    if (!tlsStatus.success()) {
        return clientStatusFromTlsStatus(tlsStatus);
    }
    return clientStatusFromSession(sessionStatus);
}

class TlsHttpStreamAdapter {
public:
    TlsHttpStreamAdapter() = default;

    TlsHttpStreamAdapter(tcp::socket<ipv4>* socket, tls::SslStream* tlsStream)
        : socket_(socket), tls_stream_(tlsStream)
    {}

    const tls::TlsStatus& last_status() const noexcept { return last_status_; }

    template <typename Handler> void async_recv(byte_buffer& buffer, Handler&& handler)
    {
        using HandlerType = std::decay_t<Handler>;
        auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
        tls_stream_->async_recv(*socket_,
            buffer,
            [this, handlerState](const tls::TlsStatus& status, std::size_t bytes) {
                last_status_ = status;
                const auto ec = status.success() ? error_code()
                                                 : make_error_code(inet_errors::operation_aborted);
                (*handlerState)(ec, bytes);
            });
    }

    template <typename Handler>
    void async_send(const void* data, std::size_t size, Handler&& handler)
    {
        using HandlerType = std::decay_t<Handler>;
        auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
        tls_stream_->async_send(*socket_,
            data,
            size,
            [this, handlerState](const tls::TlsStatus& status, std::size_t bytes) {
                last_status_ = status;
                const auto ec = status.success() ? error_code()
                                                 : make_error_code(inet_errors::operation_aborted);
                (*handlerState)(ec, bytes);
            });
    }

private:
    tcp::socket<ipv4>* socket_ = nullptr;
    tls::SslStream* tls_stream_ = nullptr;
    tls::TlsStatus last_status_{};
};

template <typename Handler> struct ClientAsyncState {
    ClientAsyncState(
        io_context* context, const ClientRequest& clientRequest, Handler&& clientHandler)
        : request(clientRequest), handler(std::forward<Handler>(clientHandler)), socket(context)
    {}

    ClientRequest request;
    Handler handler;
    tcp::socket<ipv4> socket;
    Session session;
};

template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
struct ClientStreamState {
    ClientStreamState(io_context* context,
        const ClientRequest& clientRequest,
        HeadersHandler headers,
        BodyHandler body,
        CompleteHandler complete,
        SessionConfig config)
        : request(clientRequest), on_headers(std::move(headers)), on_body(std::move(body)),
          on_complete(std::move(complete)), socket(context), session(config)
    {}

    ClientRequest request;
    HeadersHandler on_headers;
    BodyHandler on_body;
    CompleteHandler on_complete;
    tcp::socket<ipv4> socket;
    Session session;
};

template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
struct ClientTlsStreamState {
    ClientTlsStreamState(io_context* context,
        const ClientRequest& clientRequest,
        HeadersHandler headers,
        BodyHandler body,
        CompleteHandler complete,
        SessionConfig config)
        : request(clientRequest), on_headers(std::move(headers)), on_body(std::move(body)),
          on_complete(std::move(complete)), socket(context), tls_context(request.tls_config()),
          tls_stream(tls_context), tls_adapter(&socket, &tls_stream), session(config)
    {}

    ClientRequest request;
    HeadersHandler on_headers;
    BodyHandler on_body;
    CompleteHandler on_complete;
    tcp::socket<ipv4> socket;
    tls::TlsContext tls_context;
    tls::SslStream tls_stream;
    TlsHttpStreamAdapter tls_adapter;
    Session session;
};

template <typename Handler> struct BufferedClientState {
    explicit BufferedClientState(Handler&& clientHandler)
        : handler(std::move(clientHandler)), body(&core::defaultCpuMemoryResource())
    {}

    Handler handler;
    Response response;
    byte_buffer body;
};

} // namespace detail

template <typename Handler> void Client::async_send(const ClientRequest& request, Handler&& handler)
{
    using HandlerType = std::decay_t<Handler>;
    auto handlerState = core::makeShared<HandlerType>(std::forward<Handler>(handler));
    auto state =
        core::makeShared<detail::BufferedClientState<HandlerType>>(std::move(*handlerState));
    async_stream(
        request,
        [state](const Response& response) {
            state->response.set_status(response.status());
            state->response.set_reason(response.reason());
            state->response.set_version(response.version());
            state->response.headers() = response.headers();
        },
        [state](StringView chunk) {
            state->body.insert(state->body.end(),
                reinterpret_cast<const std::uint8_t*>(chunk.data()),
                reinterpret_cast<const std::uint8_t*>(chunk.data()) + chunk.size());
        },
        [state](const ClientStatus& status) {
            state->response.set_body(state->body);
            state->handler(status, state->response);
        });
}

template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
void Client::async_stream(const ClientRequest& request,
    HeadersHandler&& on_headers,
    BodyHandler&& on_body,
    CompleteHandler&& on_complete)
{
    async_stream(request,
        SessionConfig{},
        std::forward<HeadersHandler>(on_headers),
        std::forward<BodyHandler>(on_body),
        std::forward<CompleteHandler>(on_complete));
}

template <typename HeadersHandler, typename BodyHandler, typename CompleteHandler>
void Client::async_stream(const ClientRequest& request,
    SessionConfig config,
    HeadersHandler&& on_headers,
    BodyHandler&& on_body,
    CompleteHandler&& on_complete)
{
    if (!request.success()) {
        std::forward<CompleteHandler>(on_complete)(
            detail::clientStatusFromDiagnostic(request.diagnostic()));
        return;
    }
    if (context_ == nullptr) {
        std::forward<CompleteHandler>(on_complete)(
            detail::clientStatusFromDiagnostic(DiagnosticCode::malformed_uri));
        return;
    }

    const auto prepare = prepareTransport(request.uri());
    if (!prepare.success()) {
        ClientStatus status = detail::clientStatusFromDiagnostic(prepare.diagnostic);
        if (prepare.diagnostic == DiagnosticCode::tls_unavailable) {
            const auto secure = prepareSecureTransport(request.uri());
            status.tls_diagnostic = secure.tls_diagnostic;
        }
        std::forward<CompleteHandler>(on_complete)(status);
        return;
    }
    if (prepare.transport == ClientTransportKind::websocket_cleartext) {
        std::forward<CompleteHandler>(on_complete)(
            detail::clientStatusFromDiagnostic(DiagnosticCode::unsupported_scheme));
        return;
    }

    const endpoint remote = request.connection_endpoint()
        ? *request.connection_endpoint()
        : endpoint(request.uri().host(), request.uri().port());
    if (!remote.is_valid()) {
        std::forward<CompleteHandler>(on_complete)(
            detail::clientStatusFromDiagnostic(DiagnosticCode::malformed_uri));
        return;
    }

    if (prepare.transport == ClientTransportKind::http_cleartext) {
        using State = detail::ClientStreamState<std::decay_t<HeadersHandler>,
            std::decay_t<BodyHandler>,
            std::decay_t<CompleteHandler>>;
        auto state = core::makeShared<State>(context_,
            request,
            std::forward<HeadersHandler>(on_headers),
            std::forward<BodyHandler>(on_body),
            std::forward<CompleteHandler>(on_complete),
            config);

        error_code ec;
        if (state->socket.open(ec) != 0) {
            state->on_complete(detail::clientStatusFromTransportError(ec));
            return;
        }
        if (state->socket.bind(ec, endpoint::anyaddr<ipv4>(0)) != 0) {
            state->on_complete(detail::clientStatusFromTransportError(ec));
            return;
        }

        state->socket.async_connect(remote, [state](const error_code& connectEc) {
            if (connectEc) {
                state->on_complete(detail::clientStatusFromTransportError(connectEc));
                return;
            }

            Request wireRequest = state->request.message();
            if (wireRequest.headers().count("Host") == 0) {
                const auto diagnostic =
                    wireRequest.add_header("Host", state->request.host_header());
                if (diagnostic != DiagnosticCode::none) {
                    state->on_complete(detail::clientStatusFromDiagnostic(diagnostic));
                    return;
                }
            }

            state->session.async_write_request(
                state->socket, wireRequest, [state](const SessionStatus& writeStatus) {
                    if (!writeStatus.success()) {
                        state->on_complete(detail::clientStatusFromSession(writeStatus));
                        return;
                    }

                    state->session.async_read_response_stream(
                        state->socket,
                        [state](const Response& response) {
                            return state->on_headers(response);
                        },
                        [state](StringView chunk) {
                            return state->on_body(chunk);
                        },
                        [state](const SessionStatus& readStatus) {
                            state->on_complete(detail::clientStatusFromSession(readStatus));
                        });
                });
        });
        return;
    }

    using State = detail::ClientTlsStreamState<std::decay_t<HeadersHandler>,
        std::decay_t<BodyHandler>,
        std::decay_t<CompleteHandler>>;
    auto state = core::makeShared<State>(context_,
        request,
        std::forward<HeadersHandler>(on_headers),
        std::forward<BodyHandler>(on_body),
        std::forward<CompleteHandler>(on_complete),
        config);

    if (!state->tls_context.status().success()) {
        state->on_complete(detail::clientStatusFromTlsStatus(state->tls_context.status()));
        return;
    }

    error_code ec;
    if (state->socket.open(ec) != 0) {
        state->on_complete(detail::clientStatusFromTransportError(ec));
        return;
    }
    if (state->socket.bind(ec, endpoint::anyaddr<ipv4>(0)) != 0) {
        state->on_complete(detail::clientStatusFromTransportError(ec));
        return;
    }

    state->socket.async_connect(remote, [state](const error_code& connectEc) {
        if (connectEc) {
            state->on_complete(detail::clientStatusFromTransportError(connectEc));
            return;
        }

        state->tls_stream.async_handshake(
            state->socket, [state](const tls::TlsStatus& handshakeStatus) {
                if (!handshakeStatus.success()) {
                    state->on_complete(detail::clientStatusFromTlsStatus(handshakeStatus));
                    return;
                }

                Request wireRequest = state->request.message();
                if (wireRequest.headers().count("Host") == 0) {
                    const auto diagnostic =
                        wireRequest.add_header("Host", state->request.host_header());
                    if (diagnostic != DiagnosticCode::none) {
                        state->on_complete(detail::clientStatusFromDiagnostic(diagnostic));
                        return;
                    }
                }

                state->session.async_write_request(
                    state->tls_adapter, wireRequest, [state](const SessionStatus& writeStatus) {
                        if (!writeStatus.success()) {
                            state->on_complete(detail::clientStatusFromSessionAndTls(
                                writeStatus, state->tls_adapter.last_status()));
                            return;
                        }

                        state->session.async_read_response_stream(
                            state->tls_adapter,
                            [state](const Response& response) {
                                return state->on_headers(response);
                            },
                            [state](StringView chunk) {
                                return state->on_body(chunk);
                            },
                            [state](const SessionStatus& readStatus) {
                                state->on_complete(detail::clientStatusFromSessionAndTls(
                                    readStatus, state->tls_adapter.last_status()));
                            });
                    });
            });
    });
}

} // namespace arp::network::http
