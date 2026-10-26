#pragma once

#include "editor/command/editor_command.h"
#include "editor/workspace/editor_modal_session.h"

#include <array>

namespace arp::editor {
class EditorShell;

// Editor preferences draft. No Scene, resource, IO or history ownership.
class EditorSceneTransformPanel final {
public:
    bool initialize(EditorShell&);

    bool active() const noexcept { return active_; }

    static bool handles(std::string_view source) noexcept;
    static bool textSource(std::string_view source) noexcept;
    EditorCommandOutcome activate(EditorShell&, std::string_view source);
    EditorCommandOutcome edit(
        EditorShell&, std::string_view source, const core::pmr::String& value);

private:
    enum Index : std::size_t {
        Root,
        Tool,
        Space,
        Pivot,
        Snap,
        Move,
        Rotate,
        Scale,
        Apply,
        Cancel,
        Invalid,
        Failed,
        Launcher,
        Count
    };

    bool validBindings(EditorShell&) const noexcept;
    bool interactable(EditorShell&, Index) const;
    bool readDraft(EditorShell&, EditorSceneTransformSettings&) const noexcept;
    void captions(EditorShell&, const EditorSceneTransformSettings&);
    void errors(EditorShell&, bool invalid, bool failed) noexcept;
    EditorCommandOutcome open(EditorShell&);
    EditorCommandOutcome apply(EditorShell&);
    void closePresentation(EditorShell&) noexcept;

    std::array<ui::WidgetId, Count> ids_{};
    EditorSceneTransformSettings draft_;
    EditorModalSession modal_;
    bool active_ = false;
};
} // namespace arp::editor
