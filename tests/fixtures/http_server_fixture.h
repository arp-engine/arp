#pragma once

#include "network/http/server.h"
#include "network/io_service.h"
#include "network/socket_runtime.h"
#include "network/transport.h"

#include <cassert>
#include <thread>

namespace arp::network::http::test {

class RawClient {
public:
    explicit RawClient(io_context* context) : socket(context) {}

    void connect(const endpoint& address, StringView request, bool receive = true)
    {
        error_code ec;
        assert(socket.open(ec) == 0);
        wire_request.assign(request);
        socket.async_connect(address, [this, receive](const error_code& ec) {
            error = ec;
            if (ec) {
                done = true;
                return;
            }
            connected = true;
            socket.async_send(wire_request.data(),
                wire_request.size(),
                [this, receive](const error_code& ec, std::size_t) {
                    error = ec;
                    sent = true;
                    if (ec)
                        done = true;
                    else if (receive)
                        read();
                });
        });
    }

    void close()
    {
        done = true;
        socket.async_close([this](const error_code&) {
            closed = true;
        });
    }

    void read()
    {
        buffer.assign(4096, 0);
        socket.async_recv(buffer, [this](const error_code& ec, std::size_t size) {
            error = ec;
            if (done || ec || size == 0) {
                done = true;
                return;
            }
            response.append(reinterpret_cast<const char*>(buffer.data()), size);
            read();
        });
    }

    tcp::socket<ipv4> socket;
    byte_buffer buffer{&core::defaultCpuMemoryResource()};
    core::pmr::String wire_request{&core::defaultCpuMemoryResource()};
    core::pmr::String response{&core::defaultCpuMemoryResource()};
    error_code error;
    bool connected = false, sent = false, done = false, closed = false;
};

class ServerFixture {
public:
    ServerFixture()
        : context(&service), reactor([this] {
              service.run();
          })
    {
        assert(runtime.valid());
    }

    ~ServerFixture()
    {
        for (auto& server : servers)
            server->stop();
        for (auto& client : clients)
            client->close();
        until([this] {
            for (const auto& server : servers)
                if (!server->stopped())
                    return false;
            for (const auto& client : clients)
                if (!client->closed)
                    return false;
            return true;
        });
        context.shutdown();
        reactor.join();
        context.dispatch();
    }

    template <class Predicate>
    void until(Predicate&& predicate, duration timeout = std::chrono::seconds(5))
    {
        const auto deadline = clock::now() + timeout;
        while (!predicate() && clock::now() < deadline) {
            context.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        context.dispatch();
        assert(predicate());
    }

    Server& make_server(ServerConfig config = {})
    {
        auto server = core::makeUnique<Server>(&context, config);
        auto& reference = *server;
        servers.push_back(std::move(server));
        return reference;
    }

    RawClient& client(const Server& server, StringView request, bool receive = true)
    {
        auto client = core::makeUnique<RawClient>(&context);
        auto& reference = *client;
        clients.push_back(std::move(client));
        reference.connect(server.local_endpoint(), request, receive);
        return reference;
    }

    SocketRuntime runtime;
    io_service service;
    io_context context;
    core::pmr::Vector<core::UniquePtr<Server>> servers{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<RawClient>> clients{&core::defaultCpuMemoryResource()};
    std::thread reactor;
};

} // namespace arp::network::http::test
