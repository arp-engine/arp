#include "editor/validation/editor_inspector_picker_smoke.h"

#include "core/text/string_utils.h"
#include "editor/validation/editor_control_input.h"
#include "editor/validation/editor_inspector_reference_smoke.h"
#include "editor/validation/editor_inspector_resource_reference_smoke.h"
#include "tests/fixtures/scene_2d_fixture.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
bool EditorInspectorPickerSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-INSPECTOR-PICKER-SCENARIO";
    const bool objects = scenario == "picker_objects" || scenario == "picker_object_repaired";
    if (scenario != "picker_first" && scenario != "picker_last" && scenario != "picker_repaired"
        && scenario != "picker_invalid" && !objects)
        return false;
    if (objects) {
        if (!EditorInspectorReferenceSmoke::run(shell, "ref_unassigned", diagnostic))
            return false;
        const auto owner = shell.sceneDocument().selectedObject();
        for (unsigned i = 0; i < 6; ++i)
            if (shell.submitCommand(CreateSceneObjectCommand("Same body name")).status
                    != EditorResponseStatus::Completed
                || shell.submitCommand(AddSceneComponentCommand("RigidBody2DComponent")).status
                    != EditorResponseStatus::Completed)
                return false;
        if (shell.submitCommand(SelectSceneObjectCommand{owner.value()}).status
            != EditorResponseStatus::Completed)
            return false;
    } else {
        if (!EditorInspectorResourceReferenceSmoke::run(
                shell, scenario == "picker_invalid" ? "res_missing" : "res_unassigned", diagnostic))
            return false;
        for (std::size_t i = 2; i < EditorSceneResourceReferenceChoices::maxEntries; ++i) {
            char id[40];
            std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012zu", i + 100);
            if (!shell.projectResources()
                    .load<scene2d::SpriteResource>({},
                        core::pmr::String(id),
                        core::pmr::String(arp::tests::atlasA),
                        core::pmr::String("left"))
                    .success)
                return false;
        }
    }
    if (!shell.retrySceneInspector())
        return false;
    auto snapshot = shell.submitCommand(GetSceneInspectorQuery{});
    const auto property = std::find_if(snapshot.sceneInspector.properties.begin(),
        snapshot.sceneInspector.properties.end(),
        [&](const auto& value) {
            return value.name == (objects ? "bodyA" : "spriteResourceId");
        });
    diagnostic = "EDITOR-INSPECTOR-PICKER-PROPERTY";
    if (!snapshot.hasSceneInspector || property == snapshot.sceneInspector.properties.end()
        || property->enumOptions.size()
            != (objects ? 9 : EditorSceneResourceReferenceChoices::maxEntries + 1))
        return false;
    const auto source = scenePropertyControlSource(*property);
    const auto version = shell.stateVersion();
    if (!EditorControlInput::click(shell, source))
        return false;
    auto layout = shell.runtime().canvas().resolveLayout();
    const auto lastSource = source + "_option_" + (objects ? "8" : "32");
    const auto last = EditorControlInput::find(shell, lastSource);
    const auto* row = layout.findWidget(last);
    const auto* root = layout.findWidget(shell.runtime().canvas().rootId());
    diagnostic = "EDITOR-INSPECTOR-PICKER-WINDOW-BOUNDS";
    if (!layout.ok() || !row || !root)
        return false;
    std::printf("picker last row y=%g height=%g window bottom=%g visible=%u\n",
        row->rect.y,
        row->rect.height,
        root->rect.y + root->rect.height,
        unsigned(row->visibleInHierarchy));
    std::fflush(stdout);
    // A hidden last row is expected only when a real bounded scrolling window
    // exists; an off-window visible button is not an accessible choice.
    const auto popup = EditorControlInput::find(shell, source + "_popup");
    if (!popup.valid() || row->visibleInHierarchy || shell.stateVersion() != version)
        return false;
    const auto* window = layout.findWidget(popup);
    const auto bounds = root->rect;
    const auto rect = window->rect;
    std::printf("popup=(%g,%g,%g,%g) root=(%g,%g,%g,%g)\n",
        rect.x,
        rect.y,
        rect.width,
        rect.height,
        bounds.x,
        bounds.y,
        bounds.width,
        bounds.height);
    std::fflush(stdout);
    if (rect.x < bounds.x || rect.y < bounds.y || rect.x + rect.width > bounds.x + bounds.width
        || rect.y + rect.height > bounds.y + bounds.height)
        return false;
    diagnostic = "EDITOR-INSPECTOR-PICKER-HIDDEN-EVENT";
    const auto hidden = shell.runtime().interact(ui::UiControlInteraction::click(last));
    std::printf("hidden click ok=%u before=%llu after=%llu popup=%u\n",
        unsigned(hidden.ok()),
        static_cast<unsigned long long>(version),
        static_cast<unsigned long long>(shell.stateVersion()),
        unsigned(EditorControlInput::find(shell, source + "_popup").valid()));
    std::fflush(stdout);
    if (shell.stateVersion() != version
        || !EditorControlInput::find(shell, source + "_popup").valid())
        return false;
    const auto validateRows = [&]() {
        layout = shell.runtime().canvas().resolveLayout();
        unsigned visible = 0;
        for (const auto child : shell.runtime().canvas().findWidget(popup)->childIds()) {
            const auto* value = layout.findWidget(child);
            if (!value || !value->visibleInHierarchy)
                continue;
            ++visible;
            const auto clip = layout.clipRect(child);
            if (clip.x != value->rect.x || clip.y != value->rect.y
                || clip.width != value->rect.width || clip.height != value->rect.height)
                return false;
            const auto* widget = shell.runtime().canvas().findWidget(child);
            core::pmr::String identity;
            for (const auto labelId : widget->childIds()) {
                const auto* label = shell.runtime().canvas().findWidget(labelId);
                if (label->desc().acceptsPointerInput || label->desc().acceptsFocus)
                    return false;
                if (core::endsWith(label->desc().name, "_identity_1")
                    || core::endsWith(label->desc().name, "_identity_2"))
                    identity += label->desc().text;
            }
            // Every visible row has a complete explicit identity (the empty
            // sentinel intentionally has none); names need not be unique.
            const auto candidate = std::find_if(property->enumOptions.begin(),
                property->enumOptions.end(),
                [&](const auto& option) {
                    return objects ? identity == "ID: " + option.commandValue
                                   : identity == option.commandValue;
                });
            if (candidate == property->enumOptions.end())
                return false;
        }
        return visible > 0 && visible <= 6 && shell.drawCommands().ok();
    };
    diagnostic = "EDITOR-INSPECTOR-PICKER-WHOLE-ROWS";
    if (!validateRows())
        return false;
    if (scenario != "picker_first") {
        const auto scroll = [&](float delta) {
            return shell.runtime()
                .dispatch(ui::UiInputEvent::scrollWheel(
                    rect.x + rect.width / 2, rect.y + rect.height / 2, delta))
                .ok();
        };
        if (!scroll(-10000) || !validateRows() || !layout.findWidget(last)->visibleInHierarchy
            || shell.stateVersion() != version)
            return false;
        const auto end = shell.runtime().findControl(popup)->value().scroll.y;
        if (!scroll(10000) || !validateRows() || layout.findWidget(last)->visibleInHierarchy
            || shell.runtime().findControl(popup)->value().scroll.y != 0 || !scroll(-10000)
            || !validateRows() || shell.runtime().findControl(popup)->value().scroll.y != end)
            return false;
    }
    if (scenario == "picker_repaired" || scenario == "picker_object_repaired") {
        diagnostic = "EDITOR-INSPECTOR-PICKER-LAST-ASSIGNMENT";
        const auto expected = property->enumOptions.back().commandValue;
        if (!EditorControlInput::click(shell, lastSource)
            || EditorControlInput::find(shell, source + "_popup").valid())
            return false;
        const auto assigned = shell.submitCommand(GetSceneInspectorQuery{});
        const auto current = std::find_if(assigned.sceneInspector.properties.begin(),
            assigned.sceneInspector.properties.end(),
            [&](const auto& value) {
                return value.name == (objects ? "bodyA" : "spriteResourceId");
            });
        if (current == assigned.sceneInspector.properties.end()
            || !current->resourceDiagnostic.empty())
            return false;
        const auto option = std::find_if(
            current->enumOptions.begin(), current->enumOptions.end(), [&](const auto& value) {
                return value.value == current->integerValue;
            });
        if (option == current->enumOptions.end() || option->commandValue != expected)
            return false;
    }
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
