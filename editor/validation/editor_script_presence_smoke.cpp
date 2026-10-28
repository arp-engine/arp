#include "editor/validation/editor_script_presence_smoke.h"

#include "editor/tests/fixtures/script_scene_shell_fixture.h"

#include <cstdio>

namespace arp::editor {
bool EditorScriptPresenceSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    using namespace testfixtures;
    if (scenario != "presence_before" && scenario != "presence_removed"
        && scenario != "presence_undo" && scenario != "presence_redo") {
        diagnostic = "EDITOR-SCRIPT-PRESENCE-SMOKE-SCENARIO";
        return false;
    }
    ScriptSceneShellFixture files;
    files.open(shell);
    shell.activate("workspace_3D");
    ScriptSceneShellFixture::command(shell, SelectSceneObjectCommand{files.child.value()});
    const auto find = [&]() -> scene::Component* {
        return shell.sceneDocument().scene().findObject(files.child)->findComponent(files.linkId);
    };
    const auto original = find()->scriptInstance();
    const auto version = shell.stateVersion();
    core::pmr::String name = "scene_component_remove_";
    name += std::to_string(files.child.value());
    name += '_';
    name += std::to_string(files.linkId.value());
    ui::UiControlInteraction click;
    click.kind = ui::UiControlInteractionKind::Click;
    for (const auto widgetId : shell.runtime().canvas().traversal()) {
        const auto* widget = shell.runtime().canvas().findWidget(widgetId);
        if (widget && widget->name() == name) {
            click.widgetId = widgetId;
            break;
        }
    }
    diagnostic = "EDITOR-SCRIPT-PRESENCE-SMOKE-CONTROL";
    if (!click.widgetId.valid())
        return false;
    if (scenario != "presence_before") {
        if (!shell.runtime().interact(click).ok() || find() || shell.stateVersion() != version + 1
            || original.ptr<reflection::instance>()->alive())
            return false;
        if (scenario != "presence_removed") {
            diagnostic = "EDITOR-SCRIPT-PRESENCE-SMOKE-UNDO";
            ScriptSceneShellFixture::command(shell, UndoSceneCommand{});
            if (!find() || find()->scriptInstance() == original
                || original.ptr<reflection::instance>()->alive())
                return false;
            if (scenario == "presence_redo") {
                ScriptSceneShellFixture::command(shell, RedoSceneCommand{});
                if (find())
                    return false;
            }
        }
    }
    const bool present = scenario == "presence_before" || scenario == "presence_undo";
    diagnostic = "EDITOR-SCRIPT-PRESENCE-SMOKE-STATE";
    if (bool(find()) != present || shell.sceneDirty() == present
        || shell.sceneDocument().scene().name() != "Shell script root")
        return false;
    diagnostic.clear();
    std::printf("PASS v4 Inspector script presence scenario=%.*s component=%llu present=%d\n",
        int(scenario.size()),
        scenario.data(),
        static_cast<unsigned long long>(files.linkId.value()),
        int(present));
    return true;
}
} // namespace arp::editor
