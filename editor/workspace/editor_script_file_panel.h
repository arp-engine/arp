#pragma once

#include "editor/workspace/editor_modal_session.h"

#include <array>

namespace arp::editor {
class EditorShell;
struct EditorCommandOutcome;

// Presentation and confirmation only; file work stays in ScriptDocumentSession.
class EditorScriptFilePanel final {
public:
    void initialize(EditorShell&);
    void synchronize(EditorShell&);
    EditorCommandOutcome requestClose(EditorShell&, std::uint64_t documentId);
    static bool handles(std::string_view source) noexcept;
    EditorCommandOutcome activate(EditorShell&, std::string_view source);
    void commandCompleted(std::uint64_t previous, std::uint64_t current, bool blockedInput);

    bool active() const noexcept { return documentId_ != 0; }

private:
    enum class Mode { Reload, Close };

    enum Index { Root, Title, Description, Detail, Status, Confirm, Cancel, Failed, Stale, Count };

    EditorCommandOutcome open(EditorShell&, Mode, std::uint64_t documentId);
    EditorCommandOutcome close(EditorShell&);
    void closePresentation(EditorShell&) noexcept;
    void failureNotice(EditorShell&, bool stale) noexcept;
    bool validBindings(EditorShell&) const noexcept;
    std::array<ui::WidgetId, Count> ids_{};
    EditorModalSession modal_;
    std::uint64_t documentId_ = 0;
    Mode mode_ = Mode::Reload;
};
} // namespace arp::editor
