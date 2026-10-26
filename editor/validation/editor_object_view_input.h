#pragma once

#include "editor/validation/editor_reference_drag_input.h"

namespace arp::editor {

// Controlled input locates a painted action inside the real Scene panel. It
// uses the same ordered ingress as native input; it does not issue view commands.
class EditorObjectViewInput final {
public:
    static ui::UiPoint action(
        EditorShell& shell, scene::GameObjectId object, layout::SceneHierarchyAction action)
    {
        const auto resolved = shell.runtime().canvas().resolveLayout();
        const auto clip = layout::sceneHierarchyClip(resolved, shell.id("left_scene"));
        const auto hierarchy = shell.sceneDocument().hierarchyEntries();
        const auto draws = shell.drawCommands();
        assert(draws.ok());
        for (std::size_t row = 0; row < hierarchy.size(); ++row) {
            if (hierarchy[row].objectId != object)
                continue;
            assert(row < layout::kSceneHierarchyVisibleRows);
            const auto rect = layout::sceneHierarchyActionRect(clip, row, action);
            const ui::UiPoint point{rect.x + rect.width / 2, rect.y + rect.height / 2};
            assert(clip.contains(point));
            bool painted = false;
            for (const auto& draw : draws.commands())
                if (draw.kind == renderer::ui::UiDrawCommandKind::Image && draw.rect.contains(point)
                    && draw.textureResourceId
                        == (action == layout::SceneHierarchyAction::Visibility ? "icon-eye"
                                                                               : "icon-tool-lock"))
                    painted = true;
            assert(painted
                && shell.runtime().canvas().isInSubtree(
                    shell.runtime().canvas().hitTest(resolved, point).widgetId(),
                    shell.id("left_scene")));
            return point;
        }
        assert(false);
        return {};
    }

    static bool click(
        EditorShell& shell, scene::GameObjectId object, layout::SceneHierarchyAction action)
    {
        const auto point = EditorObjectViewInput::action(shell, object, action);
        EditorReferenceDragInput pointer(shell);
        return pointer.down(point) && pointer.up(point);
    }
};

} // namespace arp::editor
