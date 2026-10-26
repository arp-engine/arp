#include "editor/validation/editor_inspector_safety_smoke.h"

#include "editor/validation/editor_inspector_label_smoke.h"
#include "editor/validation/editor_inspector_picker_smoke.h"
#include "editor/validation/editor_inspector_reference_smoke.h"
#include "editor/validation/editor_inspector_resource_reference_smoke.h"
#include "editor/validation/editor_inspector_selection_smoke.h"
#include "editor/validation/editor_modifier_smoke.h"
#include "editor/validation/editor_reference_drag_smoke.h"
#include "editor/validation/editor_reference_focus_smoke.h"
#include "editor/validation/editor_resource_navigation_smoke.h"
#include "editor/validation/editor_scene_keyboard_smoke.h"
#include "editor/validation/editor_scene_modal_smoke.h"
#include "editor/validation/editor_script_command_smoke.h"
#include "editor/validation/editor_script_input_smoke.h"
#include "editor/validation/editor_script_modal_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <cstdio>

namespace arp::editor {
bool EditorInspectorSafetySmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    if (scenario.substr(0, 12) == "scene_modal_")
        return EditorSceneModalSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 11) == "scene_keys_")
        return EditorSceneKeyboardSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 9) == "modifier_")
        return EditorModifierSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 13) == "script_input_")
        return EditorScriptInputSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 13) == "script_modal_")
        return EditorScriptModalSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 7) == "script_")
        return EditorScriptCommandSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 6) == "label_")
        return EditorInspectorLabelSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 5) == "drag_")
        return EditorReferenceDragSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 4) == "nav_")
        return EditorResourceNavigationSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 6) == "focus_")
        return EditorReferenceFocusSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 10) == "selection_")
        return EditorInspectorSelectionSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 7) == "picker_")
        return EditorInspectorPickerSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 4) == "ref_")
        return EditorInspectorReferenceSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 4) == "res_")
        return EditorInspectorResourceReferenceSmoke::run(shell, scenario, diagnostic);
    diagnostic = "EDITOR-INSPECTOR-SAFETY-SMOKE";
    if (scenario != "normal" && scenario != "failed" && scenario != "retry")
        return false;
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Inspector target")).status
        != EditorResponseStatus::Completed)
        return false;
    const auto object = shell.sceneDocument().selectedObject();
    if (shell.inspectorState() != EditorShell::InspectorState::Ready)
        return false;
    if (scenario != "normal") {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            if (shell.retrySceneInspector())
                return false;
        }
        if (shell.inspectorState() != EditorShell::InspectorState::Failed
            || shell.acceptsInspectorInput("scene_name"))
            return false;
        shell.entryChanged("scene_name", "retired input");
        if (shell.sceneDocument().scene().findObject(object)->name() != "Inspector target")
            return false;
        // The public runtime drains accepted cleanup before the retry button.
        if (!shell.runtime().cancelInput().ok())
            return false;
        if (scenario == "retry") {
            ui::UiControlInteraction click;
            click.kind = ui::UiControlInteractionKind::Click;
            click.widgetId = shell.id("scene_inspector_retry");
            if (!shell.runtime().interact(click).ok()
                || shell.inspectorState() != EditorShell::InspectorState::Ready)
                return false;
        }
    }
    std::printf("PASS Inspector native hidden scenario=%.*s, committed object=%llu retained\n",
        int(scenario.size()),
        scenario.data(),
        static_cast<unsigned long long>(object.value()));
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
