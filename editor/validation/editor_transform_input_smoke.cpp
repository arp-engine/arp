#include "editor/validation/editor_transform_input_smoke.h"

#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_key_input.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
bool EditorTransformInputSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-TRANSFORM-INPUT-SMOKE";
    if (scenario != "input_tool" && scenario != "input_text" && scenario != "input_modal"
        && scenario != "input_recovery")
        return false;
    using Key = platform::WindowKey;
    using Tool = EditorSceneTransformTool;
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Keyboard target")).status
        != EditorResponseStatus::Completed)
        return false;
    const auto selected = shell.sceneDocument().selectedObject();
    core::pmr::String before, after;
    if (!EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), before, diagnostic))
        return false;
    diagnostic = "EDITOR-TRANSFORM-INPUT-SMOKE-INITIAL-TOOL";
    EditorKeyInput::send(shell, Key::R);
    if (shell.sceneTransformSettings().tool != Tool::Scale)
        return false;
    const auto guard = [&] {
        const auto camera = shell.sceneViewportCamera();
        for (auto key : {Key::Q, Key::W, Key::E, Key::R, Key::F})
            EditorKeyInput::send(shell, key);
        const auto current = shell.sceneViewportCamera();
        return shell.sceneTransformSettings().tool == Tool::Scale
            && current.target().x == camera.target().x && current.target().y == camera.target().y
            && current.target().z == camera.target().z && current.distance() == camera.distance()
            && current.yawDegrees() == camera.yawDegrees()
            && current.pitchDegrees() == camera.pitchDegrees();
    };
    ui::WidgetId focused;
    core::pmr::String text;
    if (scenario == "input_text" || scenario == "input_modal") {
        diagnostic = "EDITOR-TRANSFORM-INPUT-SMOKE-OPEN-SETTINGS";
        if (scenario == "input_modal"
            && !EditorControlInput::click(shell, "scene_transform_settings"))
            return false;
        focused = shell.id(scenario == "input_text" ? "scene_name" : "transform_ui_move");
        diagnostic = "EDITOR-TRANSFORM-INPUT-SMOKE-FOCUS";
        const auto* control = shell.runtime().findControlObject(focused);
        if (!control || !shell.runtime().requestFocus(focused).ok())
            return false;
        text = control->value().text;
        diagnostic = "EDITOR-TRANSFORM-INPUT-SMOKE-GUARD";
        if (!guard() || shell.runtime().focusedControl() != focused
            || shell.runtime().findControlObject(focused)->value().text != text)
            return false;
    } else {
        if (scenario == "input_recovery") {
            SetGamePreviewStateCommand play("input-priority smoke");
            play.running = true;
            if (shell.submitCommand(play).status != EditorResponseStatus::Completed || !guard())
                return false;
            play.running = false;
            if (shell.submitCommand(play).status != EditorResponseStatus::Completed
                || shell.submitCommand(BeginSceneSelectionTransformEditCommand{}).status
                    != EditorResponseStatus::Completed
                || !guard())
                return false;
            EditorKeyInput::focusLost(shell);
            if (shell.activeSceneSelectionTransformEditId())
                return false;
        }
        EditorKeyInput::send(shell, Key::E);
        if (shell.sceneTransformSettings().tool != Tool::Rotate)
            return false;
    }
    diagnostic = "EDITOR-TRANSFORM-INPUT-SMOKE-DRAW";
    const auto draws = shell.drawCommands();
    // Modal background controls are disabled. Inspect the modal's own tool
    // caption rather than requiring an inactive toolbar command in its draw plan.
    const auto tool = shell.id(scenario == "input_modal" ? "transform_ui_tool"
            : focused.valid()                            ? "tool_3"
                                                         : "tool_2");
    if (!draws.ok() || draws.commands().empty()
        || std::none_of(draws.commands().begin(),
            draws.commands().end(),
            [&](const auto& draw) {
                return draw.widgetId == tool;
            })
        || (scenario == "input_modal"
            && draws.commands().back().widgetId != shell.id("transform_ui_cancel"))
        || selected != shell.sceneDocument().selectedObject()
        || !EditorSceneAuthoringCodec::contentHash(shell.sceneDocument(), after, diagnostic)
        || before != after)
        return false;
    std::printf(
        "PASS native key input scenario=%.*s tool=%s focused=%s Scene/selection unchanged\n",
        int(scenario.size()),
        scenario.data(),
        focused.valid() ? "Scale" : "Rotate",
        focused.valid() ? "TextInput" : "none");
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
