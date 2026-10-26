#pragma once

#include "editor/workspace/editor_shell.h"

namespace arp::editor {

// Controlled Canvas input shared by CPU contracts and native hidden captures.
// Coordinates come from the loaded layout, never from screenshot guesses.
class EditorControlInput final {
public:
    // Authored source IDs use the document binding; dynamic controls use their
    // exact, unique WidgetDesc name. Never guess a numeric WidgetId.
    static ui::WidgetId find(EditorShell& shell, std::string_view source)
    {
        if (const auto authored = shell.id(core::pmr::String(source)); authored.valid())
            return authored;
        ui::WidgetId result;
        for (const auto id : shell.runtime().canvas().traversal()) {
            const auto* widget = shell.runtime().canvas().findWidget(id);
            if (widget && widget->desc().name == source) {
                if (result.valid())
                    return {};
                result = id;
            }
        }
        return result;
    }

    static bool click(EditorShell& shell, std::string_view source)
    {
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto id = find(shell, source);
        const auto* widget = layout.findWidget(id);
        if (!layout.ok() || !widget || !widget->visibleInHierarchy || !widget->enabledInHierarchy)
            return false;
        const float x = widget->rect.x + widget->rect.width * 0.5f;
        const float y = widget->rect.y + widget->rect.height * 0.5f;
        return shell.runtime().canvas().hitTest(layout, {x, y}).widgetId() == id
            && shell.runtime().dispatch(ui::UiInputEvent::pointerDown(x, y)).ok()
            && shell.runtime().dispatch(ui::UiInputEvent::pointerUp(x, y)).ok();
    }
};
} // namespace arp::editor
