#pragma once

#include "core/cpu_allocator.h"

#include <filesystem>

namespace arp::ui {
struct LayoutRect;
}

namespace arp::editor {
class EditorShell;

// Owns the native child process and platform window embedding. Native types
// remain private; application/workspace code consumes semantic preview state.
class GamePreviewHost final {
public:
    enum class Update { Idle, Starting, Running, Exited };
    GamePreviewHost();
    ~GamePreviewHost();
    GamePreviewHost(const GamePreviewHost&) = delete;
    GamePreviewHost& operator=(const GamePreviewHost&) = delete;
    bool hasEditorWindow() const;
    bool start(const std::filesystem::path& root, core::pmr::String& diagnostic);
    bool requestStop();
    Update update(const ui::LayoutRect* viewport, unsigned width, unsigned height, bool visible);
    bool synchronize(EditorShell& shell, unsigned width, unsigned height, bool minimized);

private:
    struct Impl;
    core::UniquePtr<Impl> impl_;
};
} // namespace arp::editor
