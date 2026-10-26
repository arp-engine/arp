#pragma once

#include "editor/workspace/editor_modal_session.h"

namespace arp::editor {
class EditorShell;

// Only presents confirmation. The typed exit command owns the acceptance gate.
class EditorExitPanel final {
public:
    void initialize(EditorShell&);
    bool activate(EditorShell&, std::string_view source);
    void commandCompleted(std::uint64_t previous, std::uint64_t current, bool blockedInput);

    bool active() const noexcept { return active_; }

private:
    void close(EditorShell&);
    EditorModalSession modal_;
    bool active_ = false;
};
} // namespace arp::editor
