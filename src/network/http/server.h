#pragma once

#include "core/callback.h"
#include "network/endpoint.h"
#include "network/http/router.h"
#include "network/http/session.h"
#include "network/io_context.h"

namespace arp::network::http {

struct ServerConfig {
    std::size_t max_connections = 16;
    std::size_t max_pending_requests = 16;
    std::size_t max_routes = 64;
    duration read_timeout = std::chrono::seconds(10);
    duration response_timeout = std::chrono::seconds(30);
    duration write_timeout = std::chrono::seconds(10);
    SessionConfig session{};
};

namespace detail {
class ServerConnection;
class ServerState;
} // namespace detail

class ARP_API IncomingRequest {
public:
    IncomingRequest(core::SharedPtr<detail::ServerConnection> connection,
        const Request& request,
        RouteMatch match);

    const Request& request() const noexcept { return request_; }

    const RouteMatch& route_match() const noexcept { return match_; }

    StringView param(StringView name) const noexcept { return match_.param(name); }

    bool active() const noexcept;
    void abandon();
    void async_respond(
        const Response& response, core::Callback<void(const SessionStatus&)> handler);

private:
    core::SharedPtr<detail::ServerConnection> connection_;
    Request request_;
    RouteMatch match_;
};

using IncomingRequestPtr = core::SharedPtr<IncomingRequest>;

// All public operations and callbacks run on the context's dispatch thread.
// The host owns/drives the context and retains it until stopped() is true.
class ARP_API Server {
public:
    explicit Server(io_context* context, ServerConfig config = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    DiagnosticCode route(Method method, StringView pattern);
    SessionStatus start(const endpoint& address);
    endpoint local_endpoint() const;
    void async_next_request(core::Callback<void(const SessionStatus&, IncomingRequestPtr)> handler);
    void stop();
    bool running() const noexcept;
    bool stopped() const noexcept;
    const ServerConfig& config() const noexcept;

private:
    core::SharedPtr<detail::ServerState> state_;
};

} // namespace arp::network::http
