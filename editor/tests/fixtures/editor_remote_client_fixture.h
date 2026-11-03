#pragma once

#include "editor/remote/editor_remote_server.h"
#include "network/http/client.h"

#include <cassert>
#include <chrono>
#include <string>
#include <thread>

namespace arp::editor::tests {
// Real loopback transport; the caller retains the server and pumps its owner thread.
template <class Server> class EditorHttpClientFixture final {
public:
    EditorHttpClientFixture(Server& server, std::string_view token)
        : server_(server), token_(token), context_(&service_), client_(&context_)
    {
        assert(runtime_.valid() && server_.running());
        reactor_ = std::thread([this] {
            service_.run();
        });
    }

    ~EditorHttpClientFixture() { shutdown(); }

    EditorHttpClientFixture(const EditorHttpClientFixture&) = delete;
    EditorHttpClientFixture& operator=(const EditorHttpClientFixture&) = delete;

    void shutdown()
    {
        if (!reactor_.joinable())
            return;
        context_.shutdown();
        reactor_.join();
        context_.dispatch();
    }

    // Abort a real response before consuming its receipt body. The subsequent
    // request uses another actual connection to query the server's outcome.
    void disconnectAfterHeaders(std::string_view path, std::string_view body)
    {
        network::http::ClientRequest request(network::http::Method::Post,
            "http://127.0.0.1:" + std::to_string(server_.port()) + std::string(path));
        assert(request.add_header("Authorization", "Bearer " + token_)
            == network::http::DiagnosticCode::none);
        request.set_content_type(network::http::ContentType::Json);
        request.set_body_text(body);
        bool headers = false, done = false;
        unsigned chunks = 0;
        network::http::ClientStatus outcome;
        client_.async_stream(
            request,
            [&](const network::http::Response& response) {
                headers = true;
                assert(response.status().value() == 200);
                return false;
            },
            [&](network::StringView) {
                ++chunks;
                return false;
            },
            [&](const network::http::ClientStatus& status) {
                outcome = status;
                done = true;
            });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            server_.pump();
            context_.dispatch();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(done && headers && chunks == 0 && !outcome.success());
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
    Server& server_;
    std::string token_;
    network::SocketRuntime runtime_;
    network::io_service service_;
    network::io_context context_;
    network::http::Client client_;
    std::thread reactor_;
};

using EditorRemoteClientFixture = EditorHttpClientFixture<EditorRemoteServer>;
} // namespace arp::editor::tests
