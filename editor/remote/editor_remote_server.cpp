#include "editor/remote/editor_remote_server.h"

#include "editor/workspace/editor_shell.h"

#include <chrono>

namespace arp::editor {
network::http::ServerConfig EditorRemoteServer::serverConfig()
{
    network::http::ServerConfig config;
    config.max_connections = 2;
    config.max_pending_requests = 2;
    config.session.parser_limits.max_body_bytes = EditorRemoteProtocol::maxRequestBytes;
    config.session.parser_limits.max_buffer_bytes =
        EditorRemoteProtocol::maxRequestBytes + config.session.parser_limits.max_header_bytes;
    config.response_timeout = std::chrono::seconds(10);
    return config;
}

EditorRemoteServer::EditorRemoteServer() : context_(&service_), server_(&context_, serverConfig())
{}

EditorRemoteServer::~EditorRemoteServer()
{
    stop();
}

bool EditorRemoteServer::isLoopbackAddress(std::string_view ipv4Address) noexcept
{
    const network::endpoint candidate(ipv4Address, 1);
    return candidate.is_valid() && !candidate.is_ipv6()
        && candidate == network::endpoint::localhost<network::ipv4>(1);
}

bool EditorRemoteServer::start(
    EditorShell& shell, std::string_view token, network::uint16 port, std::string_view bindAddress)
{
    if (server_.running()) {
        diagnostic_ = "remote service already running";
        return false;
    }
    if (!socketRuntime_.valid()) {
        diagnostic_ = socketRuntime_.diagnostics();
        return false;
    }
    if (token.size() < 32 || token.size() > 256) {
        diagnostic_ = "ARP_EDITOR_REMOTE_TOKEN must contain 32 to 256 bytes";
        return false;
    }
    const network::endpoint endpoint(bindAddress, port);
    if (!endpoint.is_valid() || endpoint.is_ipv6()) {
        diagnostic_ = "remote bind address must be a numeric IPv4 address";
        return false;
    }
    if (!isLoopbackAddress(bindAddress)) {
        diagnostic_ =
            "non-loopback remote access requires HTTPS; the HTTP listener only permits 127.0.0.1";
        return false;
    }
    protocol_ = core::makeUnique<EditorRemoteProtocol>(token);
    if (server_.route(network::http::Method::Get, "/api/v1/discovery")
            != network::http::DiagnosticCode::none
        || server_.route(network::http::Method::Post, "/api/v1/commands")
            != network::http::DiagnosticCode::none
        || server_.route(network::http::Method::Post, "/mcp") != network::http::DiagnosticCode::none
        || server_.route(network::http::Method::Get, "/mcp") != network::http::DiagnosticCode::none
        || server_.route(network::http::Method::Delete, "/mcp")
            != network::http::DiagnosticCode::none) {
        diagnostic_ = "remote route registration failed";
        return false;
    }
    const auto status = server_.start(endpoint);
    if (!status.success()) {
        diagnostic_ = "remote listener could not bind the requested IPv4 address";
        return false;
    }
    shell_ = &shell;
    reactor_ = std::thread([this] {
        service_.run();
    });
    return true;
}

void EditorRemoteServer::pump()
{
    context_.dispatch();
    if (!server_.running() || requestPending_ || !shell_ || shell_->exitRequested())
        return;
    requestPending_ = true;
    server_.async_next_request([this](const network::http::SessionStatus& status,
                                   network::http::IncomingRequestPtr incoming) {
        requestPending_ = false;
        if (!status.success() || !incoming || !incoming->active() || !shell_)
            return;
        const auto response = protocol_->handle(incoming->request(), *shell_);
        ++pendingResponses_;
        incoming->async_respond(response, [this](const network::http::SessionStatus&) {
            --pendingResponses_;
        });
    });
}

void EditorRemoteServer::stop()
{
    if (server_.running()) {
        server_.stop();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!server_.stopped() && std::chrono::steady_clock::now() < deadline) {
            context_.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    shell_ = nullptr;
    if (reactor_.joinable()) {
        context_.shutdown();
        reactor_.join();
        context_.dispatch();
    }
    protocol_.reset();
}

bool EditorRemoteServer::running() const noexcept
{
    return server_.running();
}

network::uint16 EditorRemoteServer::port() const noexcept
{
    return server_.local_endpoint().port();
}

const core::pmr::String& EditorRemoteServer::diagnostic() const noexcept
{
    return diagnostic_;
}
} // namespace arp::editor
