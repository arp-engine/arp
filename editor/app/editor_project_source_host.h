#pragma once

#include "core/runtime/async_runtime.h"

#include <filesystem>

namespace arp::editor {
class EditorShell;

// Application-owned IO lifetime. Declare before Shell so accepted callbacks
// remain valid while Shell cancels and destroys its private candidates.
class EditorProjectSourceHost final {
public:
    bool bind(EditorShell&, const std::filesystem::path& root);
    void dispatch() noexcept;

private:
    core::UniquePtr<core::runtime::AsyncRuntime> runtime_;
};
} // namespace arp::editor
