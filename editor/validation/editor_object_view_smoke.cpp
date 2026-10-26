#include "editor/validation/editor_object_view_smoke.h"

#include "editor/validation/editor_object_view_input.h"
#include "editor/workspace/editor_shell.h"

#include <cstdio>

namespace arp::editor {
bool EditorObjectViewSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-OBJECT-VIEW-SMOKE";
    const bool controls = scenario == "view_locked" || scenario == "view_unlocked"
        || scenario == "view_ui_hidden" || scenario == "view_ui_shown";
    if (!controls && scenario != "view_visible" && scenario != "view_hidden"
        && scenario != "view_shown")
        return false;
    const auto hierarchy = shell.submitCommand(GetSceneHierarchyQuery{});
    if (!hierarchy.hasSceneHierarchy || hierarchy.status != EditorResponseStatus::Completed)
        return false;
    const auto selected = shell.sceneDocument().selectedObject();
    core::pmr::String before, after, issue;
    if (!EditorSceneAuthoringCodec::encode(shell.sceneDocument(), before, issue))
        return false;
    if (controls) {
        const auto root = shell.sceneDocument().scene().rootId();
        const bool lock = scenario == "view_locked" || scenario == "view_unlocked";
        const auto action =
            lock ? layout::SceneHierarchyAction::Lock : layout::SceneHierarchyAction::Visibility;
        if (!EditorObjectViewInput::click(shell, root, action))
            return false;
        const bool restore = scenario == "view_unlocked" || scenario == "view_ui_shown";
        if (restore && !EditorObjectViewInput::click(shell, root, action))
            return false;
        const auto observation = shell.sceneObjectView(root);
        if (!observation.valid || observation.local.locked != (lock && !restore)
            || observation.local.hidden != (!lock && !restore)
            || shell.sceneDocument().selectedObject() != selected
            || !EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, issue)
            || after != before || shell.inspectorState() != EditorShell::InspectorState::Ready
            || !shell.drawCommands().ok())
            return false;
        const auto inspector = shell.submitCommand(GetSceneInspectorQuery{});
        if (!inspector.hasSceneInspector)
            return false;
        if (lock && !restore)
            for (const auto& property : inspector.sceneInspector.properties)
                if (property.editable)
                    return false;
        std::printf(
            "PASS native view UI scenario=%.*s actual pointer root toggle, Inspector/YAML/selection checked\n",
            int(scenario.size()),
            scenario.data());
        diagnostic.clear();
        return true;
    }
    SetSceneObjectViewCommand command{shell.sceneDocument().scene().rootId().value(),
        hierarchy.sceneHierarchy.documentEpoch,
        scenario != "view_visible",
        false};
    if (shell.submitCommand(command).status != EditorResponseStatus::Completed)
        return false;
    if (scenario == "view_shown") {
        command.hidden = false;
        if (shell.submitCommand(command).status != EditorResponseStatus::Completed)
            return false;
    }
    const auto source =
        shell.sceneDocument().scene().extractTraversalSnapshot({EditorSceneViewState::maxEntries});
    const char* viewDiagnostic = nullptr;
    const auto visible = shell.visibleSceneSnapshot(source, viewDiagnostic);
    if (!visible || visible->nodes().empty() != (scenario == "view_hidden")
        || shell.sceneDocument().selectedObject() != selected
        || !EditorSceneAuthoringCodec::encode(shell.sceneDocument(), after, issue)
        || after != before)
        return false;
    std::printf("PASS native view scenario=%.*s source=%zu visible=%zu selection/YAML unchanged\n",
        int(scenario.size()),
        scenario.data(),
        source.nodes().size(),
        visible->nodes().size());
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
