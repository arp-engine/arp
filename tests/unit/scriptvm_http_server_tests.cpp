#include "tests/fixtures/script_http_server_fixture.h"

using namespace arp::scriptvm;
using arp::core::pmr::String;

namespace {
ScriptAwaitStatus poll(const Value& awaitable, Value& value, String& diagnostic)
{
    assert(awaitable.is<ScriptAwaitable>());
    return awaitable.ptr<ScriptAwaitable>()->poll(value, diagnostic);
}
} // namespace

int main()
{
    test::ScriptServerFixture f;
    auto runtime = arp::core::makeUnique<NetworkRuntime>(&f.context);
    HttpScriptServerConfig config;
    config.max_connections = 1;
    config.response_timeout_ms = 150;
    auto* server = runtime->createHttpServerWithConfig(config);
    auto serverRef = server->nativeReflectionObject();
    assert(server->route(String("GET"), String("/users/:id")).success);
    assert(!server->start(String("localhost"), 0).success);
    assert(server->start(String("127.0.0.1"), 0).success);
    assert(server->localPort() > 0 && !server->localEndpoint().empty());
    Value result;
    String diagnostic;
    auto next = server->nextRequest();
    assert(poll(next, result, diagnostic) == ScriptAwaitStatus::Pending);
    auto duplicate = server->nextRequest();
    assert(poll(duplicate, result, diagnostic) == ScriptAwaitStatus::Failed
        && diagnostic == "server_busy");
    auto client = arp::core::makeUnique<arp::network::http::test::RawClient>(&f.context);
    auto* raw = client.get();
    f.clients.push_back(std::move(client));
    raw->connect(
        arp::network::endpoint("127.0.0.1", static_cast<arp::network::uint16>(server->localPort())),
        "GET /users/a%2Fb?q=raw HTTP/1.1\r\nHost: test\r\nX-Check: yes\r\n\r\n");
    f.until([&] {
        return poll(next, result, diagnostic) == ScriptAwaitStatus::Ready;
    });
    auto requestRef = result;
    auto* request = result.ptr<HttpScriptIncomingRequest>();
    assert(request->method() == "GET" && request->target() == "/users/a%2Fb?q=raw");
    assert(request->path() == "/users/a%2Fb" && request->query() == "q=raw");
    assert(request->header(String("x-check")) == "yes"
        && request->headers().find("Host: test") != String::npos);
    assert(request->routePattern() == "/users/:id" && request->param(String("id")) == "a%2Fb");
    assert(request->body().empty() && request->isActive());
    auto retained = server->nextRequest();
    assert(poll(retained, result, diagnostic) == ScriptAwaitStatus::Failed
        && diagnostic == "server_busy");
    HttpScriptResponse response;
    response.body = "bound";
    response.content_type = "text/custom";
    assert(response.addHeader(String("X-Bound"), String("yes")).success);
    assert(!response.addHeader(String("bad\r\n"), String("no")).success);
    auto write = request->respond(response);
    auto repeated = request->respond(response);
    assert(poll(repeated, result, diagnostic) == ScriptAwaitStatus::Ready);
    assert(!result.cast<NetworkResult>().success
        && result.cast<NetworkResult>().message == "response_already_sent");
    f.until([&] {
        return poll(write, result, diagnostic) == ScriptAwaitStatus::Ready && raw->done;
    });
    assert(result.cast<NetworkResult>().success);
    assert(raw->response.find("Content-Type: text/custom") != String::npos);
    assert(raw->response.find("X-Bound: yes") != String::npos);
    assert(request->destroy() && !requestRef.native_reference_alive());

    // Destroying an unanswered reflected request abandons its connection.
    next = server->nextRequest();
    auto abandonedClient = arp::core::makeUnique<arp::network::http::test::RawClient>(&f.context);
    auto* abandonedRaw = abandonedClient.get();
    f.clients.push_back(std::move(abandonedClient));
    abandonedRaw->connect(
        arp::network::endpoint("127.0.0.1", static_cast<arp::network::uint16>(server->localPort())),
        "GET /users/abandoned HTTP/1.1\r\nHost: test\r\n\r\n");
    f.until([&] {
        return poll(next, result, diagnostic) == ScriptAwaitStatus::Ready;
    });
    requestRef = result;
    request = result.ptr<HttpScriptIncomingRequest>();
    assert(request->destroy() && !requestRef.native_reference_alive());
    f.until([&] {
        return abandonedRaw->done;
    });
    assert(abandonedRaw->response.empty());

    // Retained expired requests remain readable and respond returns diagnostics.
    next = server->nextRequest();
    auto expiredClient = arp::core::makeUnique<arp::network::http::test::RawClient>(&f.context);
    auto* expiredRaw = expiredClient.get();
    f.clients.push_back(std::move(expiredClient));
    expiredRaw->connect(
        arp::network::endpoint("127.0.0.1", static_cast<arp::network::uint16>(server->localPort())),
        "GET /users/expired HTTP/1.1\r\nHost: test\r\n\r\n");
    f.until([&] {
        return poll(next, result, diagnostic) == ScriptAwaitStatus::Ready;
    });
    requestRef = result;
    request = result.ptr<HttpScriptIncomingRequest>();
    f.until([&] {
        return expiredRaw->done && !request->isActive();
    });
    assert(request->param(String("id")) == "expired");
    auto expiredWrite = request->respond(response);
    assert(poll(expiredWrite, result, diagnostic) == ScriptAwaitStatus::Ready);
    assert(!result.cast<NetworkResult>().success
        && result.cast<NetworkResult>().message.find("request_expired") == 0);
    assert(request->destroy());

    next = server->nextRequest();
    assert(server->stop().success);
    assert(poll(next, result, diagnostic) == ScriptAwaitStatus::Failed
        && diagnostic == "server_stopped");
    config.max_connections = 0;
    auto* invalid = runtime->createHttpServerWithConfig(config);
    assert(!invalid->start(String("127.0.0.1"), 0).success);
    auto invalidNext = invalid->nextRequest();
    assert(poll(invalidNext, result, diagnostic) == ScriptAwaitStatus::Failed
        && diagnostic == "invalid_server_config");
    assert(invalid->destroy());
    f.until([&] {
        return server->isStopped();
    });
    assert(server->start(String("127.0.0.1"), 0).success);
    next = server->nextRequest();
    assert(server->destroy());
    assert(!serverRef.native_reference_alive());
    assert(poll(next, result, diagnostic) == ScriptAwaitStatus::Failed
        && diagnostic == "server_stopped");

    // Runtime destruction invalidates references and deterministically fails waits.
    server = runtime->createHttpServer();
    serverRef = server->nativeReflectionObject();
    assert(server->start(String("127.0.0.1"), 0).success);
    next = server->nextRequest();
    runtime.reset();
    assert(!serverRef.native_reference_alive());
    assert(poll(next, result, diagnostic) == ScriptAwaitStatus::Failed
        && diagnostic == "server_stopped");
    bool drained = false;
    f.context.run_after(std::chrono::milliseconds(40), [&] {
        drained = true;
    });
    f.until([&] {
        return drained;
    });
}
