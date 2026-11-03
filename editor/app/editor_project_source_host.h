#pragma once

#include "core/io/workspace_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "editor/workspace/editor_project_io_task.h"

#include <filesystem>

namespace arp::editor {
class EditorShell;

// Application-owned IO lifetime. Declare before Shell so accepted callbacks
// remain valid while Shell cancels and destroys its private candidates.
class EditorProjectSourceHost final {
public:
    enum class ConnectionStatus { Unconfigured, Connecting, Ready, Bound, Failed };
    EditorProjectSourceHost();
    ~EditorProjectSourceHost();
    EditorProjectSourceHost(const EditorProjectSourceHost&) = delete;
    EditorProjectSourceHost& operator=(const EditorProjectSourceHost&) = delete;
    bool bind(EditorShell&, const std::filesystem::path& root, bool allowWrites = false);
    core::io::FileIoStatus connectRemote(
        core::io::WorkspaceFileBackendDesc, EditorProjectIoBudget) noexcept;
    bool bindRemote(EditorShell&, std::string_view alias = "workspace");
    ConnectionStatus connectionStatus() const noexcept;
    const char* diagnostic() const noexcept;
    bool remoteWritable() const noexcept;
    void dispatch() noexcept;

private:
    struct Connection;
    // runtime is destroyed first: its callbacks only borrow Connection, never Shell.
    core::UniquePtr<Connection> connection_;
    core::SharedPtr<core::io::WorkspaceFileBackend> remote_;
    core::PoolHandle remoteMount_;
    EditorProjectIoBudget remoteBudget_;
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
};
} // namespace arp::editor
