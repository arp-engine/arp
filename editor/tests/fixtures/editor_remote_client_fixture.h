#pragma once

#include "editor/remote/editor_remote_server.h"
#include "network/http/client.h"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

namespace arp::editor::tests {
// Real loopback transport; the caller retains the server and pumps its owner thread.
class EditorRemoteClientFixture final {
public:
    EditorRemoteClientFixture(EditorRemoteServer& server, std::string_view token)
        : server_(server), token_(token), context_(&service_), client_(&context_)
    {
        assert(runtime_.valid() && server_.running());
        reactor_ = std::thread([this] {
            service_.run();
        });
    }

    ~EditorRemoteClientFixture() { shutdown(); }

    EditorRemoteClientFixture(const EditorRemoteClientFixture&) = delete;
    EditorRemoteClientFixture& operator=(const EditorRemoteClientFixture&) = delete;

    void shutdown()
    {
        if (!reactor_.joinable())
            return;
        context_.shutdown();
        reactor_.join();
        context_.dispatch();
    }

    std::pair<std::uint16_t, std::string> send(network::http::Method method,
        std::string_view path,
        std::string_view body,
        bool authorized,
        std::string_view protocolVersion = {})
    {
        network::http::ClientRequest request(
            method, "http://127.0.0.1:" + std::to_string(server_.port()) + std::string(path));
        if (authorized)
            assert(request.add_header("Authorization", "Bearer " + token_)
                == network::http::DiagnosticCode::none);
        if (!body.empty()) {
            request.set_content_type(network::http::ContentType::Json);
            request.set_body_text(body);
        }
        assert(request.add_header("Accept", "application/json, text/event-stream")
            == network::http::DiagnosticCode::none);
        if (!protocolVersion.empty())
            assert(request.add_header("MCP-Protocol-Version", protocolVersion)
                == network::http::DiagnosticCode::none);
        bool done = false;
        network::http::ClientStatus resultStatus;
        std::uint16_t statusCode = 0;
        std::string responseBody;
        client_.async_send(request,
            [&](const network::http::ClientStatus& status,
                const network::http::Response& response) {
                resultStatus = status;
                statusCode = response.status().value();
                responseBody.assign(response.body_text());
                done = true;
            });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            server_.pump();
            context_.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(done && resultStatus.success());
        return {statusCode, std::move(responseBody)};
    }

private:
    EditorRemoteServer& server_;
    std::string token_;
    network::SocketRuntime runtime_;
    network::io_service service_;
    network::io_context context_;
    network::http::Client client_;
    std::thread reactor_;
};
} // namespace arp::editor::tests
