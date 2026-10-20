#include "network/endpoint.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket.h"
#include "network/socket_runtime.h"
#include "network/transport.h"

#include <cassert>
#include <cstring>
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
    tcp::socket<ipv4> client(&context);

    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint listenEndpoint(acceptor.local_endpoint());
    assert(listenEndpoint.port() != 0);
    assert(acceptor.listen(ec, 4) == 0);

    bool accepted = false;
    bool connected = false;
    bool echoed = false;
    byte_buffer serverBuffer(16, &arp::core::defaultCpuMemoryResource());
    byte_buffer clientBuffer(16, &arp::core::defaultCpuMemoryResource());
    const char message[] = "ping";

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        serverClient.async_recv(serverBuffer, [&](const error_code& recvEc, std::size_t bytes) {
            assert(!recvEc);
            assert(bytes == 4);
            serverClient.async_send(
                serverBuffer.data(), bytes, [&](const error_code& sendEc, std::size_t sent) {
                    assert(!sendEc);
                    assert(sent == 4);
                });
        });
    });

    assert(client.open(ec) == 0);
    assert(client.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    client.async_connect(listenEndpoint, [&](const error_code& connectEc) {
        assert(!connectEc);
        connected = true;
        client.async_send(message, 4, [&](const error_code& sendEc, std::size_t sent) {
            assert(!sendEc);
            assert(sent == 4);
            client.async_recv(clientBuffer, [&](const error_code& recvEc, std::size_t bytes) {
                assert(!recvEc);
                assert(bytes == 4);
                echoed = std::memcmp(clientBuffer.data(), message, 4) == 0;
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
    assert(connected);
    assert(echoed);

    return 0;
}
