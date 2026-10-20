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
    assert(listenEndpoint.port() != 0);
    assert(acceptor.listen(ec, 4) == 0);

    Session serverSession;
    Session clientSession;
    bool accepted = false;
    bool completed = false;

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        serverSession.async_read_request(
            serverClient, [&](const SessionStatus& readStatus, const Request& request) {
                assert(readStatus.success());
                assert(request.method() == Method::Get);
                assert(request.target() == "/loopback");

                Response response;
                response.set_status(StatusCode(200));
                response.set_reason("OK");
                response.set_body(bodyFromText("ok"));
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
        request.set_target("/loopback");
        clientSession.async_write_request(client, request, [&](const SessionStatus& writeStatus) {
            assert(writeStatus.success());
            clientSession.async_read_response(
                client, [&](const SessionStatus& readStatus, const Response& response) {
                    assert(readStatus.success());
                    assert(response.status().value() == 200);
                    assert(response.body().size() == 2);
                    completed = response.body()[0] == static_cast<std::uint8_t>('o');
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
    assert(completed);

    return 0;
}
