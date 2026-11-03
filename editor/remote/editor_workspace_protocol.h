#pragma once

#include "core/concurrency/callback.h"
#include "core/io/file_service.h"
#include "network/http/message.h"

namespace arp::editor {

struct EditorWorkspaceProtocolDesc {
    core::PoolHandle mount;
    core::pmr::String workspaceId, sessionId, readToken, writeToken, expectedHost, allowedOrigin;
    std::size_t maxFileBytes = 2 * 1024 * 1024;
    std::size_t maxRequestBytes = 3 * 1024 * 1024;
    std::size_t maxResponseBytes = 3 * 1024 * 1024;
    std::size_t preparationBytes = 32 * 1024 * 1024;
    std::size_t maxQueryBytes = 128 * 1024;
    std::uint32_t maxDirectoryEntries = 512;
    std::size_t maxDirectoryNameBytes = 64 * 1024;
    std::uint32_t maxReceipts = 64;
};

// Owner-thread RPC adapter over an explicitly admitted mounted root. It never
// accepts a host path, creates a backend, guesses credentials or parses assets.
// FileService/queue outlive the protocol and all accepted callbacks. One file
// operation is in flight; receipt queries remain available while it completes.
class EditorWorkspaceProtocol final {
public:
    using Completion = core::Callback<void(network::http::Response)>;
    EditorWorkspaceProtocol(core::io::FileService&, EditorWorkspaceProtocolDesc);
    ~EditorWorkspaceProtocol();
    EditorWorkspaceProtocol(const EditorWorkspaceProtocol&) = delete;
    EditorWorkspaceProtocol& operator=(const EditorWorkspaceProtocol&) = delete;
    bool valid() const noexcept;
    bool pending() const noexcept;
    const char* diagnostic() const noexcept;
    void handle(const network::http::Request&, Completion);
    void shutdown() noexcept;

private:
    struct State;
    core::SharedPtr<State> state_;
};
} // namespace arp::editor
