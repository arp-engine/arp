#include "editor/validation/editor_inspector_label_smoke.h"

#include "editor/validation/editor_control_input.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
bool EditorInspectorLabelSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-INSPECTOR-LABEL-SMOKE";
    const bool locked = scenario == "label_locked";
    if (!locked && scenario != "label_nested")
        return false;
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Property path layout")).status
            != EditorResponseStatus::Completed
        || shell.submitCommand(AddSceneComponentCommand("SpriteRendererComponent")).status
            != EditorResponseStatus::Completed)
        return false;
    const auto object = shell.sceneDocument().selectedObject();
    if (locked) {
        const auto hierarchy = shell.submitCommand(GetSceneHierarchyQuery{});
        if (!hierarchy.hasSceneHierarchy
            || shell.submitCommand(
                        SetSceneObjectViewCommand{
                            object.value(), hierarchy.sceneHierarchy.documentEpoch, false, true})
                    .status
                != EditorResponseStatus::Completed)
            return false;
    }
    const auto query = shell.submitCommand(GetSceneInspectorQuery{});
    if (!query.hasSceneInspector || shell.inspectorState() != EditorShell::InspectorState::Ready)
        return false;
    const auto field = std::find_if(query.sceneInspector.properties.begin(),
        query.sceneInspector.properties.end(),
        [](const auto& property) {
            return property.name == "localBounds.extents";
        });
    if (field == query.sceneInspector.properties.end() || field->editable == locked)
        return false;
    const auto source = scenePropertyControlSource(*field);
    const auto titleId = EditorControlInput::find(shell, source + "_label");
    const auto valueId = EditorControlInput::find(shell, source + (locked ? "" : "_x"));
    const auto* title = shell.runtime().canvas().findWidget(titleId);
    const auto* value = shell.runtime().canvas().findWidget(valueId);
    if (!title || !value || title->desc().text != field->name || title->desc().acceptsPointerInput
        || title->desc().acceptsFocus || title->desc().localRect.width != 288.f
        || value->desc().localRect.y < title->desc().localRect.y + title->desc().localRect.height)
        return false;
    // Use the production scroll command, with coordinates from the real field.
    shell.inspectorScrollChanged(title->desc().localRect.y - 30.f);
    const auto layout = shell.runtime().canvas().resolveLayout();
    const auto* resolved = layout.findWidget(titleId);
    const auto clip = layout.clipRect(titleId);
    if (!layout.ok() || !resolved || resolved->rect.y < clip.y
        || resolved->rect.y + resolved->rect.height > clip.y + clip.height)
        return false;
    const auto draws = shell.drawCommands();
    if (!draws.ok()
        || std::none_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
               return draw.widgetId == titleId;
           }))
        return false;
    std::printf(
        "PASS Inspector label scenario=%.*s full path=%s independent 288px title/value rows\n",
        int(scenario.size()),
        scenario.data(),
        field->name.c_str());
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
