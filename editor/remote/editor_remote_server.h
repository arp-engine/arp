#pragma once

#include "editor/remote/editor_remote_protocol.h"
#include "network/http/server.h"
#include "network/io_context.h"
#include "network/io_service.h"
#include "network/socket_runtime.h"

#include <thread>

namespace arp::editor {
class EditorRemoteServer final {
public:
    EditorRemoteServer();
    ~EditorRemoteServer();
    EditorRemoteServer(const EditorRemoteServer&) = delete;
    EditorRemoteServer& operator=(const EditorRemoteServer&) = delete;

    bool start(EditorShell& shell,
        std::string_view token,
        network::uint16 port,
        std::string_view bindAddress = "127.0.0.1");
    static bool isLoopbackAddress(std::string_view ipv4Address) noexcept;
    void pump();
    void stop();
    bool running() const noexcept;

    bool hasPendingResponses() const noexcept { return pendingResponses_ != 0; }

    network::uint16 port() const noexcept;
    const core::pmr::String& diagnostic() const noexcept;

private:
    static network::http::ServerConfig serverConfig();

    network::SocketRuntime socketRuntime_;
    network::io_service service_;
    network::io_context context_;
    network::http::Server server_;
    core::UniquePtr<EditorRemoteProtocol> protocol_;
    core::pmr::String diagnostic_{&core::defaultCpuMemoryResource()};
    std::thread reactor_;
    EditorShell* shell_ = nullptr;
    bool requestPending_ = false;
    std::size_t pendingResponses_ = 0;
};
} // namespace arp::editor
