#include "editor/validation/editor_transform_settings_smoke.h"

#include "editor/validation/editor_control_input.h"

#include <cstdio>

namespace arp::editor {
bool EditorTransformSettingsSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-TRANSFORM-SETTINGS-SMOKE";
    if (scenario != "settings_open" && scenario != "settings_draft"
        && scenario != "settings_invalid" && scenario != "settings_failed"
        && scenario != "settings_applied" && scenario != "settings_cancel")
        return false;
    shell.activate("workspace_3D");
    const auto initial = shell.sceneTransformSettings();
    const auto click = [&](std::string_view source) {
        return EditorControlInput::click(shell, source);
    };
    const auto text = [&](std::string_view source, std::string_view value) {
        return shell.submitCommand(SetTextValueCommand(source, value)).status
            == EditorResponseStatus::Completed;
    };
    if (!click("scene_transform_settings"))
        return false;
    if (scenario != "settings_open") {
        if (!click("transform_ui_tool") || !click("transform_ui_space")
            || !click("transform_ui_pivot") || !click("transform_ui_snap")
            || !text("transform_ui_move", "0.5") || !text("transform_ui_rotate", "0.25")
            || !text("transform_ui_scale", "0.125"))
            return false;
        if (scenario == "settings_invalid") {
            if (!text("transform_ui_rotate", "0")
                || shell.submitCommand(ActivateControlCommand("transform_ui_apply")).status
                    != EditorResponseStatus::Failed
                || !shell.runtime()
                    .canvas()
                    .findWidget(shell.id("transform_ui_invalid"))
                    ->visible())
                return false;
        } else if (scenario == "settings_failed") {
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            EditorCommandPayload apply = ActivateControlCommand("transform_ui_apply");
            {
                core::CpuMemoryQuotaScope scope(quota);
                if (shell.execute(apply).success)
                    return false;
            }
            if (!shell.runtime().canvas().findWidget(shell.id("transform_ui_failed"))->visible())
                return false;
        } else if (scenario == "settings_applied") {
            if (!click("transform_ui_apply"))
                return false;
            const auto value = shell.sceneTransformSettings();
            if (value.tool != EditorSceneTransformTool::Move
                || value.space != EditorSceneTransformSpace::Local
                || value.pivot != EditorSceneTransformPivot::Center || !value.snap.enabled
                || value.snap.moveStep != .5 || value.snap.rotateStepRadians != .25
                || value.snap.scaleStep != .125)
                return false;
        } else if (scenario == "settings_cancel" && !click("transform_ui_cancel"))
            return false;
    }
    const bool active =
        shell.runtime().canvas().findWidget(shell.id("transform_ui_modal"))->visible();
    const auto draws = shell.drawCommands();
    if (active
        && (!draws.ok() || draws.commands().empty()
            || draws.commands().back().widgetId != shell.id("transform_ui_cancel")))
        return false;
    if (active != (scenario != "settings_applied" && scenario != "settings_cancel")
        || (scenario != "settings_applied" && !shell.sceneTransformSettings().same(initial)))
        return false;
    std::printf("PASS transform settings native hidden scenario=%.*s draft=%s\n",
        int(scenario.size()),
        scenario.data(),
        active ? "open" : "closed");
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
