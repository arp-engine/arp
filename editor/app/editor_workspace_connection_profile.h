#pragma once

#include "core/io/workspace_file_backend.h"
#include "editor/workspace/editor_project_io_task.h"

namespace arp::editor {

// Application connection settings, not Project authoring or engine resources.
// YAML never contains credentials. The caller supplies one ephemeral token.
class EditorWorkspaceConnectionProfile final {
public:
    static core::UniquePtr<EditorWorkspaceConnectionProfile> decode(std::string_view yaml,
        std::string_view token,
        std::size_t maxBytes,
        const char*& diagnostic) noexcept;
    static bool validBudget(
        const core::io::WorkspaceFileBackendDesc&, const EditorProjectIoBudget&) noexcept;

    core::io::WorkspaceFileBackendDesc io;
    EditorProjectIoBudget editor;
    core::pmr::String projectRoot;
};
} // namespace arp::editor
