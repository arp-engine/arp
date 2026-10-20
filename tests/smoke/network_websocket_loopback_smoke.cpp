#include "network/endpoint.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket.h"
#include "network/socket_runtime.h"
#include "network/transport.h"
#include "network/websocket/stream.h"

#include <cassert>
#include <thread>

namespace {

arp::network::byte_buffer bytesFromText(arp::network::StringView text)
{
    arp::network::byte_buffer bytes{&arp::core::defaultCpuMemoryResource()};
    for (char c : text) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    return bytes;
}

} // namespace

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
    tcp::socket<ipv4> clientSocket(&context);

    assert(acceptor.open(ec) == 0);
    assert(acceptor.set_reuseaddr(ec, true) == 0);
    assert(acceptor.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    endpoint listenEndpoint(acceptor.local_endpoint());
    assert(listenEndpoint.port() != 0);
    assert(acceptor.listen(ec, 4) == 0);

    websocket::Stream server(websocket::StreamRole::server);
    websocket::Stream client(websocket::StreamRole::client);
    bool accepted = false;
    bool completed = false;

    acceptor.async_accept(serverClient, [&](const error_code& acceptEc) {
        assert(!acceptEc);
        accepted = true;
        server.async_server_accept(
            serverClient, StringView(), [&](const websocket::StreamStatus& acceptStatus) {
                assert(acceptStatus.success());
                server.async_read_frame(serverClient,
                    [&](const websocket::StreamStatus& readTextStatus,
                        const websocket::Frame& textFrame) {
                        assert(readTextStatus.success());
                        assert(textFrame.opcode() == websocket::Opcode::text);
                        server.async_write_text(serverClient,
                            "hello",
                            [&](const websocket::StreamStatus& writeTextStatus) {
                                assert(writeTextStatus.success());
                                server.async_read_frame(serverClient,
                                    [&](const websocket::StreamStatus& readBinaryStatus,
                                        const websocket::Frame& binaryFrame) {
                                        assert(readBinaryStatus.success());
                                        assert(binaryFrame.opcode() == websocket::Opcode::binary);
                                        server.async_write_binary(serverClient,
                                            binaryFrame.payload(),
                                            [&](const websocket::StreamStatus& writeBinaryStatus) {
                                                assert(writeBinaryStatus.success());
                                                server.async_read_frame(serverClient,
                                                    [&](const websocket::StreamStatus&
                                                            readPingStatus,
                                                        const websocket::Frame& pingFrame) {
                                                        assert(readPingStatus.success());
                                                        assert(pingFrame.opcode()
                                                            == websocket::Opcode::ping);
                                                        server.async_write_pong(serverClient,
                                                            "ok",
                                                            [&](const websocket::StreamStatus&
                                                                    writePongStatus) {
                                                                assert(writePongStatus.success());
                                                                server.async_read_frame(
                                                                    serverClient,
                                                                    [&](const websocket::
                                                                            StreamStatus&
                                                                                readCloseStatus,
                                                                        const websocket::Frame&
                                                                            closeFrame) {
                                                                        assert(readCloseStatus
                                                                                .success());
                                                                        assert(closeFrame.opcode()
                                                                            == websocket::Opcode::
                                                                                close);
                                                                        server.async_write_close(
                                                                            serverClient,
                                                                            1000,
                                                                            "bye",
                                                                            [&](const websocket::
                                                                                    StreamStatus&
                                                                                        writeCloseStatus) {
                                                                                assert(
                                                                                    writeCloseStatus
                                                                                        .success());
                                                                            });
                                                                    });
                                                            });
                                                    });
                                            });
                                    });
                            });
                    });
            });
    });

    assert(clientSocket.open(ec) == 0);
    assert(clientSocket.bind(ec, endpoint::localhost<ipv4>(0)) == 0);
    clientSocket.async_connect(listenEndpoint, [&](const error_code& connectEc) {
        assert(!connectEc);
        client.async_client_upgrade(clientSocket,
            "localhost",
            "/ws",
            "dGhlIHNhbXBsZSBub25jZQ==",
            [&](const websocket::StreamStatus& upgradeStatus) {
                assert(upgradeStatus.success());
                client.async_write_text(
                    clientSocket, "hello", [&](const websocket::StreamStatus& writeTextStatus) {
                        assert(writeTextStatus.success());
                        client.async_read_frame(clientSocket,
                            [&](const websocket::StreamStatus& readTextStatus,
                                const websocket::Frame& textFrame) {
                                assert(readTextStatus.success());
                                assert(textFrame.opcode() == websocket::Opcode::text);
                                client.async_write_binary(clientSocket,
                                    bytesFromText("bin"),
                                    [&](const websocket::StreamStatus& writeBinaryStatus) {
                                        assert(writeBinaryStatus.success());
                                        client.async_read_frame(clientSocket,
                                            [&](const websocket::StreamStatus& readBinaryStatus,
                                                const websocket::Frame& binaryFrame) {
                                                assert(readBinaryStatus.success());
                                                assert(binaryFrame.opcode()
                                                    == websocket::Opcode::binary);
                                                client.async_write_ping(clientSocket,
                                                    "ok",
                                                    [&](const websocket::StreamStatus&
                                                            writePingStatus) {
                                                        assert(writePingStatus.success());
                                                        client.async_read_frame(clientSocket,
                                                            [&](const websocket::StreamStatus&
                                                                    readPongStatus,
                                                                const websocket::Frame& pongFrame) {
                                                                assert(readPongStatus.success());
                                                                assert(pongFrame.opcode()
                                                                    == websocket::Opcode::pong);
                                                                client.async_write_close(
                                                                    clientSocket,
                                                                    1000,
                                                                    "bye",
                                                                    [&](const websocket::
                                                                            StreamStatus&
                                                                                writeCloseStatus) {
                                                                        assert(writeCloseStatus
                                                                                .success());
                                                                        client.async_read_frame(
                                                                            clientSocket,
                                                                            [&](const websocket::
                                                                                    StreamStatus&
                                                                                        readCloseStatus,
                                                                                const websocket::
                                                                                    Frame&
                                                                                        closeFrame) {
                                                                                assert(
                                                                                    readCloseStatus
                                                                                        .success());
                                                                                assert(closeFrame
                                                                                           .opcode()
                                                                                    == websocket::
                                                                                        Opcode::
                                                                                            close);
                                                                                completed = true;
                                                                                context.shutdown();
                                                                            });
                                                                    });
                                                            });
                                                    });
                                            });
                                    });
                            });
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
