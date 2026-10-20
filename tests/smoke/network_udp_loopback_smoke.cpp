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

    udp::socket<ipv4> receiver(&context);
    udp::socket<ipv4> sender(&context);

    assert(receiver.open(ec) == 0);
    assert(receiver.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint receiverEndpoint(receiver.local_endpoint());
    assert(receiverEndpoint.port() != 0);

    assert(sender.open(ec) == 0);
    assert(sender.bind(ec, endpoint::localhost<ipv4>(0)) == 0);

    byte_buffer recvBuffer(32, &arp::core::defaultCpuMemoryResource());
    const char message[] = "datagram";
    bool received = false;
    endpoint senderEndpoint;

    receiver.async_recvfrom(
        recvBuffer, [&](const error_code& recvEc, const endpoint& from, std::size_t bytes) {
            assert(!recvEc);
            assert(bytes == 8);
            senderEndpoint = from;
            received = std::memcmp(recvBuffer.data(), message, 8) == 0;
            context.shutdown();
        });

    sender.async_sendto(
        receiverEndpoint, message, 8, [&](const error_code& sendEc, std::size_t bytes) {
            assert(!sendEc);
            assert(bytes == 8);
        });

    std::thread reactor([&]() {
        service.run();
    });

    while (!service.quit()) {
        context.dispatch();
    }

    reactor.join();
    context.dispatch();
    assert(received);
    assert(senderEndpoint.is_valid());

    return 0;
}
