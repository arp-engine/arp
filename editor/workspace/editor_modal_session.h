#pragma once

#include "core/cpu_allocator.h"
#include "ui/ui.h"

namespace arp::editor {
class EditorShell;

// Editor presentation lifetime only; commands retain ownership of all document IO.
class EditorModalSession final {
public:
    bool open(EditorShell&, const char* root);
    void close(EditorShell&);
    void armConfirmation();
    bool confirmationValid(std::uint64_t stateVersion) const;
    void commandCompleted(std::uint64_t previous, std::uint64_t current, bool blockedInput);

private:
    friend class EditorSceneTransformPanel;
    friend class EditorScriptFilePanel;
    friend class EditorScenePanel;
    // Only after input has been released successfully. Publication cannot fail.
    void closePresentation(EditorShell&) noexcept;
    ui::WidgetId root_;
    bool confirmationPending_ = false;
    std::uint64_t confirmationVersion_ = 0;
    core::pmr::Vector<std::pair<ui::WidgetId, bool>> background_;
};
} // namespace arp::editor
