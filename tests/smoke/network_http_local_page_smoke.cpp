#include "network/endpoint.h"
#include "network/http/session.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket.h"
#include "network/socket_runtime.h"
#include "network/transport.h"

#include <cassert>
#include <thread>

namespace {

arp::network::byte_buffer bodyFromText(arp::network::StringView text)
{
    arp::network::byte_buffer body{&arp::core::defaultCpuMemoryResource()};
    for (char c : text) {
        body.push_back(static_cast<std::uint8_t>(c));
    }
    return body;
}

bool contains(arp::network::StringView text, arp::network::StringView fragment)
{
    return text.find(fragment) != arp::network::StringView::npos;
}

} // namespace

int main()
{
    using namespace arp::network;
    using namespace arp::network::http;

    SocketRuntime runtime;
    assert(runtime.valid());

    io_service service;
    io_context context(&service);
    error_code ec;

    tcp::socket<ipv4> acceptor(&context);
    tcp::socket<ipv4> serverClient(&context);
    tcp::socket<ipv4> client(&context);

    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint listenEndpoint(acceptor.local_endpoint());
    const auto hostHeader = listenEndpoint.to_string();
    assert(listenEndpoint.port() != 0);
    assert(!hostHeader.empty());
    assert(acceptor.listen(ec, 4) == 0);

    Session serverSession;
    Session clientSession;
    bool accepted = false;
    bool requestValidated = false;
    bool responseValidated = false;

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        serverSession.async_read_request(
            serverClient, [&](const SessionStatus& readStatus, const Request& request) {
                assert(readStatus.success());
                assert(request.method() == Method::Get);
                assert(request.target() == "/index.html");
                assert(request.headers().firstValue("host") == hostHeader);
                assert(request.headers().firstValue("accept") == "text/html");
                requestValidated = true;

                Response response;
                response.set_status(StatusCode(200));
                response.set_reason("OK");
                assert(response.headers().add("Content-Type", "text/html; charset=utf-8")
                    == DiagnosticCode::none);
                assert(response.headers().add("Cache-Control", "no-store") == DiagnosticCode::none);
                response.set_body(bodyFromText(
                    "<!doctype html><html><head><title>ARP Local</title></head>"
                    "<body><h1>hello local page</h1><p>served from loopback</p></body></html>"));
                serverSession.async_write_response(
                    serverClient, response, [&](const SessionStatus& writeStatus) {
                        assert(writeStatus.success());
                    });
            });
    });

    assert(client.open(ec) == 0);
    assert(client.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    client.async_connect(listenEndpoint, [&](const error_code& connectEc) {
        assert(!connectEc);

        Request request;
        request.set_method(Method::Get);
        request.set_target("/index.html");
        assert(request.headers().add("Host", hostHeader) == DiagnosticCode::none);
        assert(request.headers().add("Accept", "text/html") == DiagnosticCode::none);
        clientSession.async_write_request(client, request, [&](const SessionStatus& writeStatus) {
            assert(writeStatus.success());
            clientSession.async_read_response(
                client, [&](const SessionStatus& readStatus, const Response& response) {
                    assert(readStatus.success());
                    assert(response.status().value() == 200);
                    assert(response.headers().firstValue("content-type")
                        == "text/html; charset=utf-8");
                    assert(response.headers().firstValue("cache-control") == "no-store");

                    const StringView html(reinterpret_cast<const char*>(response.body().data()),
                        response.body().size());
                    assert(contains(html, "<title>ARP Local</title>"));
                    assert(contains(html, "<h1>hello local page</h1>"));
                    assert(contains(html, "served from loopback"));
                    responseValidated = true;
                    context.shutdown();
                });
        });
    });

    std::thread reactor([&]() {
        service.run();
    });

    while (!service.quit()) {
        context.dispatch();
    }

    reactor.join();
    context.dispatch();
    assert(accepted);
    assert(requestValidated);
    assert(responseValidated);

    return 0;
}
