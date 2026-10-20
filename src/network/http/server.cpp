#include "network/http/server.h"

#include "network/socket_runtime.h"
#include "network/transport.h"

#include <algorithm>
#include <limits>

namespace arp::network::http {
namespace detail {

using Completion = core::Callback<void(const error_code&)>;
using Transfer = core::Callback<void(const error_code&, std::size_t)>;

// Protocol code uses one stream interface; address-family selection remains
// inside this transport, driven solely by the explicit endpoint metadata.
class ServerTransport {
public:
    virtual ~ServerTransport() = default;
    virtual int prepare(const endpoint&, int, error_code&) = 0;
    virtual endpoint local_endpoint() const = 0;
    virtual void async_accept(ServerTransport&, Completion) = 0;
    virtual void async_recv(byte_buffer&, Transfer) = 0;
    virtual void async_send(const void*, std::size_t, Transfer) = 0;
    virtual void async_close(Completion) = 0;
};

template <class Ip> class TcpTransport final : public ServerTransport {
public:
    explicit TcpTransport(io_context* context) : socket_(context) {}

    int prepare(const endpoint& address, int backlog, error_code& ec) override
    {
        if (socket_.open(ec) != 0 || socket_.set_reuseaddr(ec, true) != 0
            || socket_.bind(ec, address) != 0 || socket_.listen(ec, backlog) != 0) {
            error_code ignored;
            socket_.close(ignored);
            return -1;
        }
        return 0;
    }

    endpoint local_endpoint() const override { return socket_.local_endpoint(); }

    void async_accept(ServerTransport& peer, Completion callback) override
    {
        socket_.async_accept(static_cast<TcpTransport&>(peer).socket_, std::move(callback));
    }

    void async_recv(byte_buffer& buffer, Transfer callback) override
    {
        socket_.async_recv(buffer, std::move(callback));
    }

    void async_send(const void* data, std::size_t size, Transfer callback) override
    {
        socket_.async_send(data, size, std::move(callback));
    }

    void async_close(Completion callback) override { socket_.async_close(std::move(callback)); }

private:
    tcp::socket<Ip> socket_;
};

core::UniquePtr<ServerTransport> makeTransport(io_context* context, const endpoint& address)
{
    if (address.is_ipv6())
        return core::makeUnique<TcpTransport<ipv6>>(context);
    return core::makeUnique<TcpTransport<ipv4>>(context);
}

class ServerConnection {
public:
    enum class Phase { reading, waiting, writing, closing, closed };

    ServerConnection(io_context* context, const endpoint& address, ServerConfig config)
        : context(context), config(config), transport(makeTransport(context, address)),
          session(serverSession(config))
    {}

    static SessionConfig serverSession(ServerConfig config)
    {
        config.session.parser_limits.server_request_validation = true;
        return config.session;
    }

    void read();
    void respond(const Response& response, core::Callback<void(const SessionStatus&)> handler);
    void finish(DiagnosticCode diagnostic = DiagnosticCode::request_expired, error_code ec = {});
    void arm(duration timeout);
    void monitor();
    void send_error(std::uint16_t status, StringView reason, StringView allow = {});

    io_context* context;
    ServerConfig config;
    core::UniquePtr<ServerTransport> transport;
    Session session;
    core::WeakPtr<ServerConnection> self;
    core::WeakPtr<ServerState> owner;
    deadline_timer_ptr timer;
    std::uint64_t timer_generation = 0;
    Phase phase = Phase::reading;
    Method method = Method::Get;
    byte_buffer probe{&core::defaultCpuMemoryResource()};
    SessionStatus terminal;
    core::Callback<void(const SessionStatus&)> writing;
};

class ServerState {
public:
    ServerState(io_context* context, ServerConfig config)
        : context(context), config(config),
          router(config.max_routes, config.session.parser_limits.max_header_bytes)
    {}

    void accept_next();
    void enqueue(const core::SharedPtr<ServerConnection>& connection, const Request& request);
    void retired(ServerConnection* connection);
    void stop();

    bool stopped() const
    {
        return !running && !accepting && !listener_closing && connections.empty();
    }

    void next(core::Callback<void(const SessionStatus&, IncomingRequestPtr)> handler);

    SocketRuntime runtime;
    io_context* context;
    ServerConfig config;
    Router router;
    core::WeakPtr<ServerState> self;
    core::UniquePtr<ServerTransport> listener;
    endpoint address;
    bool running = false;
    bool accepting = false;
    bool listener_closing = false;
    core::pmr::Vector<core::SharedPtr<ServerConnection>> connections{
        &core::defaultCpuMemoryResource()};
    core::pmr::Deque<IncomingRequestPtr> queue{&core::defaultCpuMemoryResource()};
    core::Callback<void(const SessionStatus&, IncomingRequestPtr)> waiting;
};

void ServerConnection::arm(duration timeout)
{
    if (timer)
        timer->cancel();
    const auto generation = ++timer_generation;
    timer = context->run_after(timeout, [weak = self, generation] {
        auto connection = weak.lock();
        if (connection && connection->timer_generation == generation)
            connection->finish(
                DiagnosticCode::request_expired, make_error_code(inet_errors::timed_out));
    });
}

void ServerConnection::read()
{
    arm(config.read_timeout);
    auto connection = self.lock();
    session.async_read_request(
        *transport, [connection](const SessionStatus& status, const Request& request) {
            if (connection->phase != Phase::reading)
                return;
            if (!status.success()) {
                if (status.transport_error
                    || status.diagnostic == DiagnosticCode::connection_closed)
                    connection->finish(status.diagnostic, status.transport_error);
                else {
                    std::uint16_t code = 400;
                    StringView reason = "Bad Request";
                    if (status.diagnostic == DiagnosticCode::body_too_large
                        || status.diagnostic == DiagnosticCode::buffer_too_large) {
                        code = 413;
                        reason = "Content Too Large";
                    }
                    if (status.diagnostic == DiagnosticCode::header_too_large
                        || status.diagnostic == DiagnosticCode::too_many_headers) {
                        code = 431;
                        reason = "Request Header Fields Too Large";
                    }
                    if (status.diagnostic == DiagnosticCode::expectation_not_supported) {
                        code = 417;
                        reason = "Expectation Failed";
                    }
                    if (status.diagnostic == DiagnosticCode::unsupported_http_version) {
                        code = 505;
                        reason = "HTTP Version Not Supported";
                    }
                    connection->send_error(code, reason);
                }
                return;
            }
            connection->method = request.method();
            connection->phase = Phase::waiting;
            connection->arm(connection->config.response_timeout);
            if (auto state = connection->owner.lock())
                state->enqueue(connection, request);
            else
                connection->finish(DiagnosticCode::server_stopped);
            if (connection->phase == Phase::waiting)
                connection->monitor();
        });
}

void ServerConnection::monitor()
{
    probe.assign(1, 0);
    auto connection = self.lock();
    transport->async_recv(probe, [connection](const error_code& ec, std::size_t bytes) {
        if (connection->phase != Phase::waiting && connection->phase != Phase::writing)
            return;
        // A second request on this one-request connection is unsupported.
        connection->finish(
            bytes ? DiagnosticCode::invalid_request_target : DiagnosticCode::connection_closed, ec);
    });
}

void ServerConnection::send_error(std::uint16_t status, StringView reason, StringView allow)
{
    Response response;
    response.set_status(StatusCode(status));
    response.set_reason(reason);
    if (!allow.empty())
        response.add_header("Allow", allow);
    response.set_content_type(ContentType::Plain);
    response.set_body_text(reason);
    respond(response, [](const SessionStatus&) {});
}

void ServerConnection::respond(
    const Response& response, core::Callback<void(const SessionStatus&)> handler)
{
    if (phase == Phase::writing) {
        SessionStatus status;
        status.diagnostic = DiagnosticCode::response_already_sent;
        handler(status);
        return;
    }
    if (phase == Phase::closing || phase == Phase::closed) {
        handler(terminal);
        return;
    }
    if (response.body().size() > config.session.parser_limits.max_body_bytes
        || response.headers().size() > config.session.parser_limits.max_header_count
        || response.status().value() < 200 || response.version().major != 1
        || response.version().minor != 1) {
        SessionStatus status;
        status.diagnostic = DiagnosticCode::invalid_response;
        handler(status);
        return;
    }
    const auto headerBudget = config.session.parser_limits.max_header_bytes;
    std::size_t headerBytes = response.reason().size();
    for (std::size_t i = 0; i < response.headers().size(); ++i) {
        const auto& header = response.headers().at(i);
        for (auto text : {header.name(), header.value()}) {
            if (headerBytes > headerBudget || text.size() > headerBudget - headerBytes) {
                SessionStatus status;
                status.diagnostic = DiagnosticCode::invalid_response;
                handler(status);
                return;
            }
            headerBytes += text.size();
        }
    }
    if (headerBytes > headerBudget) {
        SessionStatus status;
        status.diagnostic = DiagnosticCode::invalid_response;
        handler(status);
        return;
    }
    SerializationOptions options;
    options.head_response = method == Method::Head;
    options.close_connection = true;
    auto serialized = Serializer::serializeResponse(response, options);
    const auto headerEnd = serialized.text.find("\r\n\r\n");
    if (!serialized.success() || headerEnd == StringView::npos
        || headerEnd + 4 > config.session.parser_limits.max_header_bytes) {
        SessionStatus status;
        status.diagnostic = DiagnosticCode::invalid_response;
        handler(status);
        return;
    }
    phase = Phase::writing;
    writing = std::move(handler);
    arm(config.write_timeout);
    auto connection = self.lock();
    transport->async_send(serialized.text.data(),
        serialized.text.size(),
        [connection](const error_code& ec, std::size_t bytes) {
            if (connection->phase != Phase::writing)
                return;
            SessionStatus status;
            status.transport_error = ec;
            status.bytes = bytes;
            auto callback = std::move(connection->writing);
            connection->finish(DiagnosticCode::response_already_sent, ec);
            if (callback)
                callback(status);
        });
}

void ServerConnection::finish(DiagnosticCode diagnostic, error_code ec)
{
    if (phase == Phase::closing || phase == Phase::closed)
        return;
    phase = Phase::closing;
    terminal.diagnostic = diagnostic;
    terminal.transport_error = ec;
    ++timer_generation;
    if (timer) {
        timer->cancel();
        timer.reset();
    }
    auto callback = std::move(writing);
    auto connection = self.lock();
    transport->async_close([connection](const error_code&) {
        connection->phase = Phase::closed;
        if (auto state = connection->owner.lock())
            state->retired(connection.get());
    });
    if (callback)
        callback(terminal);
}

void ServerState::accept_next()
{
    if (!running || accepting || connections.size() >= config.max_connections)
        return;
    auto state = self.lock();
    auto connection = core::makeShared<ServerConnection>(context, address, config);
    connection->self = connection;
    connection->owner = state;
    accepting = true;
    listener->async_accept(*connection->transport, [state, connection](const error_code& ec) {
        state->accepting = false;
        if (ec) {
            if (state->running)
                state->stop();
            return;
        }
        state->connections.push_back(connection);
        if (!state->running)
            connection->finish(DiagnosticCode::server_stopped);
        else {
            connection->read();
            state->accept_next();
        }
    });
}

void ServerState::enqueue(
    const core::SharedPtr<ServerConnection>& connection, const Request& request)
{
    if (!running) {
        connection->finish(DiagnosticCode::server_stopped);
        return;
    }
    auto match = router.match(request);
    if (!match.success()) {
        connection->send_error(match.status,
            match.status == 405       ? "Method Not Allowed"
                : match.status == 404 ? "Not Found"
                                      : "Bad Request",
            match.allow);
        return;
    }
    if (!waiting && queue.size() >= config.max_pending_requests) {
        connection->send_error(503, "Service Unavailable");
        return;
    }
    auto incoming = core::makeShared<IncomingRequest>(connection, request, std::move(match));
    if (waiting) {
        auto callback = std::move(waiting);
        callback(SessionStatus{}, std::move(incoming));
    } else
        queue.push_back(std::move(incoming));
}

void ServerState::next(core::Callback<void(const SessionStatus&, IncomingRequestPtr)> handler)
{
    SessionStatus status;
    if (!running) {
        status.diagnostic = DiagnosticCode::server_stopped;
        handler(status, {});
        return;
    }
    if (waiting) {
        status.diagnostic = DiagnosticCode::server_busy;
        handler(status, {});
        return;
    }
    while (!queue.empty()) {
        auto incoming = std::move(queue.front());
        queue.pop_front();
        if (incoming->active()) {
            handler(status, std::move(incoming));
            return;
        }
    }
    waiting = std::move(handler);
}

void ServerState::retired(ServerConnection* connection)
{
    queue.erase(std::remove_if(queue.begin(),
                    queue.end(),
                    [](const auto& incoming) {
                        return !incoming->active();
                    }),
        queue.end());
    connections.erase(std::remove_if(connections.begin(),
                          connections.end(),
                          [connection](const auto& entry) {
                              return entry.get() == connection;
                          }),
        connections.end());
    accept_next();
}

void ServerState::stop()
{
    if (!running)
        return;
    running = false;
    queue.clear();
    auto callback = std::move(waiting);
    auto state = self.lock();
    listener_closing = true;
    listener->async_close([state](const error_code&) {
        state->listener_closing = false;
    });
    for (const auto& connection : connections)
        connection->finish(DiagnosticCode::server_stopped);
    if (callback) {
        SessionStatus status;
        status.diagnostic = DiagnosticCode::server_stopped;
        callback(status, {});
    }
}

} // namespace detail

IncomingRequest::IncomingRequest(
    core::SharedPtr<detail::ServerConnection> connection, const Request& request, RouteMatch match)
    : connection_(std::move(connection)), request_(request), match_(std::move(match))
{}

bool IncomingRequest::active() const noexcept
{
    return connection_->phase == detail::ServerConnection::Phase::waiting;
}

void IncomingRequest::abandon()
{
    connection_->finish();
}

void IncomingRequest::async_respond(
    const Response& response, core::Callback<void(const SessionStatus&)> handler)
{
    connection_->respond(response, std::move(handler));
}

Server::Server(io_context* context, ServerConfig config)
    : state_(core::makeShared<detail::ServerState>(context, config))
{
    state_->self = state_;
}

Server::~Server()
{
    stop();
}

DiagnosticCode Server::route(Method method, StringView pattern)
{
    return state_->router.route(method, pattern);
}

SessionStatus Server::start(const endpoint& address)
{
    SessionStatus status;
#if defined(__EMSCRIPTEN__)
    status.diagnostic = DiagnosticCode::unsupported_platform;
    return status;
#else
    const auto& config = state_->config;
    const auto maxTransfer = static_cast<std::size_t>(std::numeric_limits<socklen_t>::max());
    if (!state_->stopped()) {
        status.diagnostic = DiagnosticCode::server_busy;
        return status;
    }
    if (!state_->runtime.valid()) {
        status.transport_error = make_error_code(inet_errors::network_down);
        return status;
    }
    if (!state_->context || !address.is_valid() || config.max_connections == 0
        || config.max_pending_requests == 0 || config.max_routes == 0
        || config.max_connections > static_cast<std::size_t>(std::numeric_limits<int>::max())
        || config.session.read_buffer_size > maxTransfer
        || config.session.parser_limits.max_header_bytes > maxTransfer
        || config.session.parser_limits.max_body_bytes
            > maxTransfer - std::min(maxTransfer, config.session.parser_limits.max_header_bytes)
        || config.read_timeout <= duration::zero() || config.response_timeout <= duration::zero()
        || config.write_timeout <= duration::zero() || config.session.read_buffer_size == 0
        || config.session.read_buffer_size > config.session.parser_limits.max_buffer_bytes
        || config.session.parser_limits.max_header_bytes == 0
        || config.session.parser_limits.max_header_count == 0
        || config.session.parser_limits.max_buffer_bytes == 0) {
        status.diagnostic = DiagnosticCode::invalid_server_config;
        return status;
    }
    state_->listener = detail::makeTransport(state_->context, address);
    if (state_->listener->prepare(
            address, static_cast<int>(config.max_connections), status.transport_error)
        != 0)
        return status;
    state_->address = state_->listener->local_endpoint();
    state_->running = true;
    state_->router.freeze();
    state_->accept_next();
    return status;
#endif
}

endpoint Server::local_endpoint() const
{
    return state_->address;
}

void Server::async_next_request(
    core::Callback<void(const SessionStatus&, IncomingRequestPtr)> handler)
{
    state_->next(std::move(handler));
}

void Server::stop()
{
    state_->stop();
}

bool Server::running() const noexcept
{
    return state_->running;
}

bool Server::stopped() const noexcept
{
    return state_->stopped();
}

const ServerConfig& Server::config() const noexcept
{
    return state_->config;
}

} // namespace arp::network::http
