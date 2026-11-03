#pragma once

#include "editor/remote/editor_workspace_protocol.h"

namespace arp::editor {

// Native localhost host only. Root/credentials originate in explicit process
// configuration; requests cannot register or redirect roots. No EditorShell or
// GPU is required. Ordinary shutdown drains accepted file outcomes first.
class EditorWorkspaceServer final {
public:
    EditorWorkspaceServer();
    ~EditorWorkspaceServer();
    EditorWorkspaceServer(const EditorWorkspaceServer&) = delete;
    EditorWorkspaceServer& operator=(const EditorWorkspaceServer&) = delete;
    bool start(std::string_view absoluteRoot, EditorWorkspaceProtocolDesc, std::uint16_t port = 0);
    void pump();
    void stop();
    bool running() const noexcept;
    bool stopped() const noexcept;
    std::uint16_t port() const noexcept;
    std::string_view diagnostic() const noexcept;

private:
    struct State;
    core::SharedPtr<State> state_;
};
} // namespace arp::editor
