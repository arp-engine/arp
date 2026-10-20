#include "network/endpoint.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket.h"
#include "network/socket_runtime.h"
#include "network/transport.h"
#include "network/websocket/client.h"

#include <cassert>
#include <thread>

int main()
{
    using namespace arp::network;

    SocketRuntime runtime;
    assert(runtime.valid());

    io_service service;
    io_context context(&service);
    error_code ec;

    tcp::socket<ipv4> acceptor(&context);
    tcp::socket<ipv4> serverClient(&context);

    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint listenEndpoint(acceptor.local_endpoint());
    assert(listenEndpoint.port() != 0);
    assert(acceptor.listen(ec, 4) == 0);

    websocket::Stream server(websocket::StreamRole::server);
    bool accepted = false;
    bool completed = false;

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        server.async_server_accept(
            serverClient, StringView(), [&](const websocket::StreamStatus& acceptStatus) {
                assert(acceptStatus.success());
                server.async_read_frame(serverClient,
                    [&](const websocket::StreamStatus& readStatus, const websocket::Frame& frame) {
                        assert(readStatus.success());
                        assert(frame.opcode() == websocket::Opcode::text);
                        server.async_write_text(
                            serverClient, "pong", [&](const websocket::StreamStatus& writeStatus) {
                                assert(writeStatus.success());
                            });
                    });
            });
    });

    arp::core::pmr::String uri{&arp::core::defaultCpuMemoryResource()};
    uri += "ws://";
    uri += listenEndpoint.to_string();
    uri += "/chat";
    auto request = websocket::ClientRequest::connect(uri);
    websocket::Client client(&context, request);

    client.connect([&](const websocket::ClientStatus& connectStatus) {
        assert(connectStatus.success());
        client.send("ping", [&](const websocket::ClientStatus& writeStatus) {
            assert(writeStatus.success());
            client.receive(
                [&](const websocket::ClientStatus& readStatus, const websocket::Frame& frame) {
                    assert(readStatus.success());
                    assert(frame.opcode() == websocket::Opcode::text);
                    assert(frame.payload_text() == "pong");
                    completed = true;
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
