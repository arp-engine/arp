#include "network/network_module.h"

#include <algorithm>

namespace arp::scriptvm {
namespace {

NetworkResult serverResult(const network::http::SessionStatus& status)
{
    NetworkResult result;
    result.success = status.success();
    result.code = status.transport_error ? status.transport_error.value()
                                         : static_cast<int>(status.diagnostic);
    result.bytes = static_cast<int>(status.bytes);
    result.message.assign(network::http::diagnosticName(status.diagnostic));
    if (status.transport_error)
        result.message += ": transport_error";
    return result;
}

NetworkResult serverResult(network::http::DiagnosticCode code)
{
    network::http::SessionStatus status;
    status.diagnostic = code;
    return serverResult(status);
}

struct HttpAwaitState {
    bool complete = false;
    bool success = false;
    Value result;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};

    void fail(network::http::DiagnosticCode code)
    {
        complete = true;
        success = false;
        diagnostic.assign(network::http::diagnosticName(code));
    }
};

Value serverAwaitable(
    const core::SharedPtr<HttpAwaitState>& state, const reflection::object& operation)
{
    ScriptAwaitable awaitable;
    awaitable.operation = operation;
    awaitable.ready = [state] {
        return state->complete;
    };
    awaitable.poll = [state](Value& value, core::pmr::String& diagnostic) {
        if (!state->complete)
            return ScriptAwaitStatus::Pending;
        if (!state->success) {
            diagnostic = state->diagnostic;
            return ScriptAwaitStatus::Failed;
        }
        value = state->result;
        return ScriptAwaitStatus::Ready;
    };
    return Value{std::move(awaitable)};
}

bool validConfig(const HttpScriptServerConfig& c)
{
    return c.max_connections > 0 && c.max_pending_requests > 0 && c.max_routes > 0
        && c.read_timeout_ms > 0 && c.response_timeout_ms > 0 && c.write_timeout_ms > 0
        && c.read_buffer_size > 0 && c.max_header_bytes > 0 && c.max_header_count > 0
        && c.max_body_bytes >= 0 && c.max_buffer_bytes > 0
        && c.read_buffer_size <= c.max_buffer_bytes;
}

network::http::ServerConfig nativeConfig(const HttpScriptServerConfig& c)
{
    network::http::ServerConfig result;
    result.max_connections = static_cast<std::size_t>(c.max_connections);
    result.max_pending_requests = static_cast<std::size_t>(c.max_pending_requests);
    result.max_routes = static_cast<std::size_t>(c.max_routes);
    result.read_timeout = std::chrono::milliseconds(c.read_timeout_ms);
    result.response_timeout = std::chrono::milliseconds(c.response_timeout_ms);
    result.write_timeout = std::chrono::milliseconds(c.write_timeout_ms);
    result.session.read_buffer_size = static_cast<std::size_t>(c.read_buffer_size);
    result.session.parser_limits.max_header_bytes = static_cast<std::size_t>(c.max_header_bytes);
    result.session.parser_limits.max_header_count = static_cast<std::size_t>(c.max_header_count);
    result.session.parser_limits.max_body_bytes = static_cast<std::size_t>(c.max_body_bytes);
    result.session.parser_limits.max_buffer_bytes = static_cast<std::size_t>(c.max_buffer_bytes);
    return result;
}

core::pmr::String copyText(network::StringView text)
{
    return core::pmr::String(text, &core::defaultCpuMemoryResource());
}

} // namespace

struct HttpServerScriptState {
    core::UniquePtr<network::http::Server> server;
    NetworkRuntime* owner = nullptr;
    bool destroyed = false;
    std::size_t retained_requests = 0;
    std::size_t max_retained_requests = 0;
};

NetworkResult HttpScriptResponse::addHeader(
    const core::pmr::String& name, const core::pmr::String& value)
{
    return serverResult(headers.add(name, value));
}

HttpScriptServer::HttpScriptServer(
    network::io_context* context, NetworkRuntime* owner, const HttpScriptServerConfig& config)
    : owner_(owner), state_(core::makeShared<HttpServerScriptState>()),
      reflectionObject_(reflection::object::native_reference(this))
{
    state_->owner = owner;
    if (validConfig(config)) {
        state_->server = core::makeUnique<network::http::Server>(context, nativeConfig(config));
        state_->max_retained_requests = static_cast<std::size_t>(config.max_connections);
    }
}

HttpScriptServer::~HttpScriptServer()
{
    state_->destroyed = true;
    state_->owner = nullptr;
    if (state_->server)
        state_->server->stop();
    reflectionObject_.invalidate_native_reference();
}

NetworkResult HttpScriptServer::route(
    const core::pmr::String& method, const core::pmr::String& pattern)
{
    if (!state_->server)
        return serverResult(network::http::DiagnosticCode::invalid_server_config);
    auto parsed = network::http::parseMethod(method);
    if (!parsed.success())
        return serverResult(parsed.diagnostic);
    return serverResult(state_->server->route(parsed.method, pattern));
}

NetworkResult HttpScriptServer::start(const core::pmr::String& host, int port)
{
    if (!state_->server)
        return serverResult(network::http::DiagnosticCode::invalid_server_config);
    if (port < 0 || port > 65535 || host.find('\0') != host.npos)
        return serverResult(network::http::DiagnosticCode::invalid_server_config);
    const network::endpoint address(host, static_cast<network::uint16>(port));
    if (!address.is_valid())
        return serverResult(network::http::DiagnosticCode::invalid_server_config);
    return serverResult(state_->server->start(address));
}

core::pmr::String HttpScriptServer::localEndpoint() const
{
    return state_->server ? state_->server->local_endpoint().to_string() : copyText("");
}

int HttpScriptServer::localPort() const
{
    return state_->server ? state_->server->local_endpoint().port() : 0;
}

bool HttpScriptServer::isRunning() const
{
    return state_->server && state_->server->running();
}

bool HttpScriptServer::isStopped() const
{
    return !state_->server || state_->server->stopped();
}

NetworkResult HttpScriptServer::stop()
{
    if (state_->server)
        state_->server->stop();
    return serverResult(network::http::DiagnosticCode::none);
}

bool HttpScriptServer::destroy()
{
    return owner_ && owner_->destroyHttpServer(this);
}

Value HttpScriptServer::nextRequest()
{
    auto operation = core::makeShared<HttpAwaitState>();
    if (!state_->server)
        operation->fail(network::http::DiagnosticCode::invalid_server_config);
    else if (state_->retained_requests >= state_->max_retained_requests)
        operation->fail(network::http::DiagnosticCode::server_busy);
    else
        state_->server->async_next_request(
            [state = state_, operation](const network::http::SessionStatus& status,
                network::http::IncomingRequestPtr request) {
                if (state->destroyed || !state->owner) {
                    operation->fail(network::http::DiagnosticCode::server_stopped);
                    return;
                }
                if (!status.success()) {
                    operation->complete = true;
                    operation->diagnostic = serverResult(status).message;
                    return;
                }
                auto* native = state->owner->createHttpServerRequest(state, std::move(request));
                if (!native) {
                    operation->fail(network::http::DiagnosticCode::server_busy);
                    return;
                }
                operation->result = native->nativeReflectionObject();
                operation->success = operation->complete = true;
            });
    return serverAwaitable(operation, reflectionObject_);
}

HttpScriptIncomingRequest::HttpScriptIncomingRequest(NetworkRuntime* owner,
    core::SharedPtr<HttpServerScriptState> state,
    network::http::IncomingRequestPtr request)
    : owner_(owner), state_(std::move(state)), request_(std::move(request)),
      reflectionObject_(reflection::object::native_reference(this))
{
    ++state_->retained_requests;
}

HttpScriptIncomingRequest::~HttpScriptIncomingRequest()
{
    request_->abandon();
    --state_->retained_requests;
    reflectionObject_.invalidate_native_reference();
}

core::pmr::String HttpScriptIncomingRequest::method() const
{
    return copyText(network::http::methodText(request_->request().method()));
}

core::pmr::String HttpScriptIncomingRequest::target() const
{
    return copyText(request_->request().target());
}

core::pmr::String HttpScriptIncomingRequest::path() const
{
    return copyText(request_->route_match().path);
}

core::pmr::String HttpScriptIncomingRequest::query() const
{
    return copyText(request_->route_match().query);
}

core::pmr::String HttpScriptIncomingRequest::body() const
{
    return copyText(request_->request().body_text());
}

core::pmr::String HttpScriptIncomingRequest::routePattern() const
{
    return copyText(request_->route_match().pattern);
}

core::pmr::String HttpScriptIncomingRequest::header(const core::pmr::String& name) const
{
    return copyText(request_->request().header(name));
}

core::pmr::String HttpScriptIncomingRequest::param(const core::pmr::String& name) const
{
    return copyText(request_->param(name));
}

core::pmr::String HttpScriptIncomingRequest::headers() const
{
    core::pmr::String result{&core::defaultCpuMemoryResource()};
    for (std::size_t i = 0; i < request_->request().headers().size(); ++i) {
        const auto& item = request_->request().headers().at(i);
        result += item.name();
        result += ": ";
        result += item.value();
        result += "\n";
    }
    return result;
}

bool HttpScriptIncomingRequest::isActive() const
{
    return request_->active();
}

bool HttpScriptIncomingRequest::destroy()
{
    return owner_ && owner_->destroyHttpServerRequest(this);
}

Value HttpScriptIncomingRequest::respond(const HttpScriptResponse& response)
{
    auto operation = core::makeShared<HttpAwaitState>();
    auto reject = [operation](network::http::DiagnosticCode code) {
        operation->result = Value{serverResult(code)};
        operation->success = operation->complete = true;
    };
    if (state_->destroyed)
        reject(network::http::DiagnosticCode::server_stopped);
    else if (response.status < 200 || response.status > 599)
        reject(network::http::DiagnosticCode::invalid_response);
    else {
        network::http::Response native;
        native.set_status(network::http::StatusCode(static_cast<std::uint16_t>(response.status)));
        native.set_reason(response.reason);
        native.set_body_text(response.body);
        native.headers() = response.headers;
        auto diagnostic = native.add_header("Content-Type", response.content_type);
        if (diagnostic != network::http::DiagnosticCode::none)
            reject(diagnostic);
        else
            request_->async_respond(
                native, [operation](const network::http::SessionStatus& status) {
                    auto result = serverResult(status);
                    operation->complete = true;
                    operation->success = true;
                    operation->diagnostic = result.message;
                    operation->result = Value{std::move(result)};
                });
    }
    return serverAwaitable(operation, reflectionObject_);
}

HttpScriptResponse NetworkRuntime::createHttpResponse()
{
    return {};
}

HttpScriptServer* NetworkRuntime::createHttpServer()
{
    return createHttpServerWithConfig({});
}

HttpScriptServer* NetworkRuntime::createHttpServerWithConfig(const HttpScriptServerConfig& config)
{
    if (!context_)
        return nullptr;
    auto object = core::makeUnique<HttpScriptServer>(context_, this, config);
    auto* result = object.get();
    httpServers_.push_back(std::move(object));
    return result;
}

HttpScriptIncomingRequest* NetworkRuntime::createHttpServerRequest(
    core::SharedPtr<HttpServerScriptState> state, network::http::IncomingRequestPtr request)
{
    if (state->retained_requests >= state->max_retained_requests) {
        request->abandon();
        return nullptr;
    }
    auto object =
        core::makeUnique<HttpScriptIncomingRequest>(this, std::move(state), std::move(request));
    auto* result = object.get();
    httpServerRequests_.push_back(std::move(object));
    return result;
}

bool NetworkRuntime::destroyHttpServer(HttpScriptServer* value)
{
    auto found =
        std::find_if(httpServers_.begin(), httpServers_.end(), [value](const auto& object) {
            return object.get() == value;
        });
    if (found == httpServers_.end())
        return false;
    httpServers_.erase(found);
    return true;
}

bool NetworkRuntime::destroyHttpServerRequest(HttpScriptIncomingRequest* value)
{
    auto found = std::find_if(
        httpServerRequests_.begin(), httpServerRequests_.end(), [value](const auto& object) {
            return object.get() == value;
        });
    if (found == httpServerRequests_.end())
        return false;
    httpServerRequests_.erase(found);
    return true;
}

} // namespace arp::scriptvm
