#pragma once

#include "core/memory/cpu_allocator.h"
#include "network/http/message.h"

#include <cstdint>
#include <string_view>

namespace arp::editor {
class EditorShell;

class EditorRemoteProtocol final {
public:
    static constexpr std::size_t maxRequestBytes = 16u * 1024u * 1024u;
    // The installed MCP client SDK reports this version as its latest version.
    static constexpr std::string_view mcpProtocolVersion = "2025-11-25";

    explicit EditorRemoteProtocol(std::string_view bearerToken);

    network::http::Response handle(const network::http::Request& request, EditorShell& shell);

private:
    bool authorized(const network::http::Request& request) const noexcept;
    bool allowedOrigin(const network::http::Request& request) const;
    network::http::Response discovery(EditorShell& shell);
    network::http::Response restCommand(const network::http::Request& request, EditorShell& shell);
    network::http::Response mcpMessage(const network::http::Request& request, EditorShell& shell);

    core::pmr::String bearerToken_;
    std::uint64_t nextCorrelationId_ = 1;
    std::uint64_t nextSourceSequence_ = 1;
};
} // namespace arp::editor
