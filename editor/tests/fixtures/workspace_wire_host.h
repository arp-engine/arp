#pragma once

#include "core/io/workspace_file_wire.h"
#include "core/text/number_format.h"
#include "editor/remote/editor_workspace_protocol.h"
#include "editor/tests/fixtures/workspace_service_fixture.h"
#include "network/http/server.h"
#include "network/io_service.h"
#include "network/socket_runtime.h"

namespace arp::editor::tests {

// Real engine HTTP + actual Windows directory, with deliberately faulty wire
// delivery. Faults happen after the production protocol finishes real IO.
class WorkspaceWireHost final {
public:
    WorkspaceWireHost()
    {
        network::http::ServerConfig config;
        config.max_connections = config.max_pending_requests = 4;
        config.session.parser_limits.max_body_bytes = 131072;
        config.session.parser_limits.max_buffer_bytes =
            131072 + config.session.parser_limits.max_header_bytes;
        server = core::makeUnique<network::http::Server>(&context, config);
        assert(server->route(network::http::Method::Post, "/workspace/v2")
            == network::http::DiagnosticCode::none);
        assert(server->start(network::endpoint::localhost<network::ipv4>(0)).success());
        auto d = workspace::config(host.mount);
        d.expectedHost = "127.0.0.1:" + core::formatUnsigned(server->local_endpoint().port());
        protocol = core::makeUnique<EditorWorkspaceProtocol>(host.files, std::move(d));
        assert(protocol->valid());
        reactor = std::thread([this] {
            service.run();
        });
    }

    ~WorkspaceWireHost()
    {
        protocol->shutdown();
        host.mounts.shutdown();
        host.backend->shutdown();
        while (host.queue.pending())
            host.queue.dispatch(8);
        server->stop();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!server->stopped() && std::chrono::steady_clock::now() < deadline) {
            context.dispatch();
            std::this_thread::yield();
        }
        assert(server->stopped());
        context.shutdown();
        reactor.join();
        context.dispatch();
    }

    core::pmr::String origin() const
    {
        return "http://127.0.0.1:" + core::formatUnsigned(server->local_endpoint().port());
    }

    void pump()
    {
        host.queue.dispatch(8);
        context.dispatch();
        if (receiving)
            return;
        receiving = true;
        server->async_next_request([this](const network::http::SessionStatus& status,
                                       network::http::IncomingRequestPtr incoming) {
            receiving = false;
            if (!status.success() || !incoming || !incoming->active())
                return;
            core::json::JsonDocument request;
            request.Parse(
                incoming->request().body_text().data(), incoming->request().body_text().size());
            assert(!request.HasParseError());
            using Wire = core::io::WorkspaceFileWire;
            const core::pmr::String operation(Wire::text(request, "operation"));
            const auto* pathValue = core::json::jsonMember(request, "path");
            const core::pmr::String path(
                pathValue ? core::json::jsonText(*pathValue) : std::string_view{});
            const auto id = core::json::jsonMember(request, "id") ? Wire::number(request, "id") : 0;
            protocol->handle(incoming->request(),
                [this, incoming, operation, path, id](network::http::Response response) {
                    if (operation == "replace" && path == "folder/lost.arp") {
                        core::json::JsonDocument result;
                        result.Parse(response.body_text().data(), response.body_text().size());
                        assert(Wire::text(result, "commit") == "committed");
                        if (corruptLostReceipt)
                            badId = id;
                        ++dropped;
                        incoming->abandon();
                        return;
                    }
                    if (operation == "replace" && path == "folder/bad.arp")
                        badId = id;
                    if (operation == "replace" && path == "folder/cancel-after.arp") {
                        core::json::JsonDocument result;
                        result.Parse(response.body_text().data(), response.body_text().size());
                        committedBeforeCancel = Wire::text(result, "commit") == "committed";
                    }
                    if ((operation == "read" && path == "folder/bad.arp")
                        || (operation == "receipt" && id == badId)) {
                        core::json::JsonDocument result;
                        result.Parse(response.body_text().data(), response.body_text().size());
                        if (operation == "read")
                            result["data"].SetString("!!!!", result.GetAllocator());
                        else
                            result["requestVersion"]["digest"].SetString(
                                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
                                result.GetAllocator());
                        response.set_body_text(Wire::encode(result, 131072));
                        ++corrupted;
                    }
                    incoming->async_respond(response, [](const network::http::SessionStatus&) {});
                });
        });
    }

    workspace::Host host{true};
    unsigned dropped = 0, corrupted = 0;
    bool committedBeforeCancel = false, corruptLostReceipt = false;

private:
    network::SocketRuntime sockets;
    network::io_service service;
    network::io_context context{&service};
    core::UniquePtr<network::http::Server> server;
    core::UniquePtr<EditorWorkspaceProtocol> protocol;
    std::thread reactor;
    std::uint64_t badId = 0;
    bool receiving = false;
};
} // namespace arp::editor::tests
