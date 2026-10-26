#include "editor/validation/editor_inspector_resource_reference_smoke.h"

#include "2d/components.h"
#include "editor/validation/editor_control_input.h"
#include "tests/fixtures/scene_2d_fixture.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
bool EditorInspectorResourceReferenceSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-INSPECTOR-RESOURCE-REFERENCE-SMOKE";
    if (scenario != "res_unassigned" && scenario != "res_missing" && scenario != "res_type"
        && scenario != "res_not_ready" && scenario != "res_capacity" && scenario != "res_repaired"
        && scenario != "res_stale")
        return false;
    shell.activate("workspace_3D");
    if (!arp::tests::loadSpriteFixture(shell.projectResources())
        || shell.submitCommand(CreateSceneObjectCommand("Sprite reference")).status
            != EditorResponseStatus::Completed
        || shell.submitCommand(AddSceneComponentCommand("SpriteRendererComponent")).status
            != EditorResponseStatus::Completed)
        return false;
    auto* object = shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    auto* sprite = object->getComponent<scene2d::SpriteRendererComponent>();
    if (!sprite)
        return false;
    // Only these explicit fixtures bypass the authoring write validator to
    // simulate external corruption. Normal commands continue to reject bad IDs.
    if (scenario == "res_missing")
        sprite->setSpriteResource("00000000-0000-4000-8000-000000000099");
    else if (scenario == "res_type")
        sprite->setSpriteResource(arp::tests::atlasA);
    else if (scenario == "res_not_ready" || scenario == "res_capacity") {
        sprite->setSpriteResource(arp::tests::spriteA);
        if (scenario == "res_not_ready"
            && !shell.projectResources().unload(arp::tests::spriteA).success)
            return false;
    }
    if (scenario == "res_capacity") {
        // Two Ready sprites already exist; non-Ready records count as well.
        for (std::size_t i = 2; i <= EditorSceneResourceReferenceChoices::maxEntries; ++i) {
            char id[40];
            std::snprintf(id, sizeof(id), "00000000-0000-4000-8000-%012zu", i + 100);
            if (!shell.projectResources().declareResource(id, "SpriteResource").success)
                return false;
        }
    }
    if (!shell.retrySceneInspector())
        return false;
    if (scenario == "res_stale") {
        SetSceneComponentPropertyCommand set("spriteResourceId");
        set.objectId = object->id().value();
        set.componentId = sprite->id().value();
        set.valueKind = ScenePropertyValueKind::ResourceReference;
        set.stringValue = arp::tests::spriteA;
        if (shell.submitCommand(std::move(set)).status != EditorResponseStatus::Completed)
            return false;
    }
    const auto findProperty = [](const auto& properties) {
        return std::find_if(properties.begin(), properties.end(), [](const auto& p) {
            return p.name == "spriteResourceId"
                && p.kind == SceneInspectorPropertyKind::ResourceReference;
        });
    };
    auto snapshot = shell.submitCommand(GetSceneInspectorQuery{});
    auto property = findProperty(snapshot.sceneInspector.properties);
    if (!snapshot.hasSceneInspector || property == snapshot.sceneInspector.properties.end())
        return false;
    const auto source = scenePropertyControlSource(*property);
    if (scenario == "res_repaired" || scenario == "res_stale") {
        diagnostic = "EDITOR-INSPECTOR-RESOURCE-REPAIR-INPUT";
        const auto option = std::find_if(
            property->enumOptions.begin(), property->enumOptions.end(), [](const auto& candidate) {
                return candidate.commandValue == arp::tests::spriteB;
            });
        if (option == property->enumOptions.end() || !EditorControlInput::click(shell, source))
            return false;
        const auto version = shell.stateVersion();
        // B was Ready when the menu opened, but is retired before the click.
        // Rejecting that selection must restore the actual A control as well.
        if (scenario == "res_stale"
            && !shell.projectResources().unload(arp::tests::spriteB).success)
            return false;
        char suffix[32];
        std::snprintf(suffix,
            sizeof(suffix),
            "_option_%zu",
            std::size_t(option - property->enumOptions.begin()));
        if (!EditorControlInput::click(shell, source + suffix))
            return false;
        snapshot = shell.submitCommand(GetSceneInspectorQuery{});
        property = findProperty(snapshot.sceneInspector.properties);
        const auto expected = scenario == "res_stale" ? arp::tests::spriteA : arp::tests::spriteB;
        if (property == snapshot.sceneInspector.properties.end()
            || property->stringValue != expected || !property->resourceDiagnostic.empty()
            || (scenario == "res_stale" && shell.stateVersion() != version))
            return false;
    }
    const auto current = sprite->editorAuthoringProperties();
    const auto& descriptor = current.cast<const scene2d::SpriteRendererComponentDesc&>();
    const auto selected = std::find_if(
        property->enumOptions.begin(), property->enumOptions.end(), [&](const auto& option) {
            return option.value == property->integerValue;
        });
    diagnostic = "EDITOR-INSPECTOR-RESOURCE-CONTROL";
    if (selected == property->enumOptions.end()
        || selected->commandValue != descriptor.spriteResourceId
        || property->stringValue != descriptor.spriteResourceId
        || shell.inspectorState() != EditorShell::InspectorState::Ready)
        return false;
    const auto widgetId = EditorControlInput::find(shell, source);
    const auto* widget = shell.runtime().canvas().findWidget(widgetId);
    const auto* control = shell.runtime().findControlObject(widgetId);
    if (!widget || !control)
        return false;
    if (property->editable) {
        if (control->value().index != std::uint32_t(selected - property->enumOptions.begin()))
            return false;
    } else if (widget->desc().text.find(selected->name) == core::pmr::String::npos)
        return false;
    const auto draws = shell.drawCommands();
    diagnostic = "EDITOR-INSPECTOR-RESOURCE-DRAWS";
    const auto drawn = [&](ui::WidgetId id) {
        return std::any_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
            return draw.widgetId == id;
        });
    };
    if (!draws.ok() || !drawn(widgetId))
        return false;
    if (scenario == "res_stale") {
        const auto failureId = EditorControlInput::find(shell, "scene_reference_failure");
        const auto* failure = shell.runtime().canvas().findWidget(failureId);
        if (!failure || failure->desc().text != "Reference edit failed; value kept"
            || !drawn(failureId))
            return false;
    }
    if (!property->stringValue.empty()) {
        core::pmr::String reconstructed;
        for (const auto suffix : {"_reference_id", "_reference_id_suffix"}) {
            const auto idLabel = EditorControlInput::find(shell, source + suffix);
            const auto* label = shell.runtime().canvas().findWidget(idLabel);
            if (!label || !drawn(idLabel))
                return false;
            reconstructed += label->desc().text;
        }
        if (reconstructed != property->stringValue)
            return false;
    }
    const auto diagnosticId = EditorControlInput::find(shell, source + "_diagnostic");
    if (property->resourceDiagnostic.empty()) {
        if (diagnosticId.valid())
            return false;
    } else {
        const auto* label = shell.runtime().canvas().findWidget(diagnosticId);
        if (!label || label->desc().text != property->resourceDiagnostic || !drawn(diagnosticId))
            return false;
    }
    std::printf("PASS native resource reference fixture=%.*s ID=%s selected=%s editable=%u\n",
        int(scenario.size()),
        scenario.data(),
        property->stringValue.c_str(),
        selected->name.c_str(),
        unsigned(property->editable));
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
