#include "editor/validation/editor_scene_keyboard_smoke.h"

#include "editor/validation/editor_key_input.h"

#include <cstdio>

namespace arp::editor {
bool EditorSceneKeyboardSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-SCENE-KEYBOARD-SMOKE-HISTORY";
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Keyboard target")).status
            != EditorResponseStatus::Completed
        || shell.submitCommand(SetSceneObjectTranslationCommand{3, 1, 0}).status
            != EditorResponseStatus::Completed)
        return false;
    using Key = platform::WindowKey;
    const auto object = shell.sceneDocument().selectedObject();
    EditorKeyInput::send(shell, Key::RightControl);
    auto version = shell.stateVersion();
    EditorKeyInput::send(shell, Key::Z);
    if (shell.stateVersion() != version + 1
        || shell.sceneDocument().scene().findObject(object)->local_transform().translation.x != 0)
        return false;
    EditorKeyInput::send(shell, Key::RightShift);
    version = shell.stateVersion();
    EditorKeyInput::send(shell, Key::Z);
    if (shell.stateVersion() != version + 1
        || shell.sceneDocument().scene().findObject(object)->local_transform().translation.x != 3)
        return false;
    EditorKeyInput::focusLost(shell);
    diagnostic = "EDITOR-SCENE-KEYBOARD-SMOKE-FOCUS";
    version = shell.stateVersion();
    EditorKeyInput::send(shell, Key::F);
    EditorKeyInput::send(shell, Key::F);
    const auto target = shell.sceneViewportCamera().target();
    if (shell.stateVersion() != version + 1 || target.x != 3 || target.y != 1 || target.z != 0)
        return false;
    diagnostic = "EDITOR-SCENE-KEYBOARD-SMOKE-TOOL";
    version = shell.stateVersion();
    EditorKeyInput::send(shell, Key::R);
    EditorKeyInput::send(shell, Key::R);
    if (shell.stateVersion() != version + 1
        || shell.sceneTransformTool() != EditorSceneTransformTool::Scale)
        return false;
    if (scenario == "scene_keys_text") {
        diagnostic = "EDITOR-SCENE-KEYBOARD-SMOKE-TEXT";
        const auto name = shell.id("scene_name");
        if (!shell.runtime().requestFocus(name).ok())
            return false;
        EditorKeyInput::send(shell, Key::LeftControl);
        version = shell.stateVersion();
        for (const auto key : {Key::Z, Key::Y, Key::S})
            EditorKeyInput::send(shell, key);
        if (shell.stateVersion() != version || shell.runtime().focusedControl() != name)
            return false;
        EditorKeyInput::send(shell, Key::LeftControl, false);
        version = shell.stateVersion();
        for (const auto key : {Key::Q, Key::W, Key::E, Key::R, Key::F})
            EditorKeyInput::send(shell, key);
        if (shell.stateVersion() != version
            || shell.sceneTransformTool() != EditorSceneTransformTool::Scale
            || shell.runtime().findControlObject(name)->value().text != "Keyboard target"
            || shell.sceneDocument().scene().findObject(object)->local_transform().translation.x
                != 3)
            return false;
    } else if (scenario == "scene_keys_save") {
        diagnostic = "EDITOR-SCENE-KEYBOARD-SMOKE-SAVE-AS";
        EditorKeyInput::send(shell, Key::LeftControl);
        EditorKeyInput::send(shell, Key::LeftShift);
        version = shell.stateVersion();
        EditorKeyInput::send(shell, Key::S);
        if (shell.stateVersion() != version + 1
            || !shell.runtime().canvas().findWidget(shell.id("scene_ui_modal"))->visible()
            || shell.runtime().canvas().findWidget(shell.id("scene_ui_title"))->desc().text
                != "Save Scene As")
            return false;
        version = shell.stateVersion();
        EditorKeyInput::send(shell, Key::R);
        if (shell.stateVersion() != version
            || shell.sceneTransformTool() != EditorSceneTransformTool::Scale)
            return false;
    } else if (scenario == "scene_keys_tool") {
        version = shell.stateVersion();
        EditorKeyInput::send(shell, Key::E);
        EditorKeyInput::send(shell, Key::E);
        if (shell.stateVersion() != version + 1
            || shell.sceneTransformTool() != EditorSceneTransformTool::Rotate)
            return false;
    } else
        return false;
    diagnostic.clear();
    std::printf(
        "PASS controlled native scene keyboard scenario=%.*s: history/focus/tool/text/save state and exact receipts verified\n",
        static_cast<int>(scenario.size()),
        scenario.data());
    return true;
}
} // namespace arp::editor
