#include "editor/validation/editor_inspector_reference_smoke.h"

#include "editor/validation/editor_control_input.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
bool EditorInspectorReferenceSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-INSPECTOR-REFERENCE-SMOKE";
    if (scenario != "ref_unassigned" && scenario != "ref_missing" && scenario != "ref_type"
        && scenario != "ref_repaired")
        return false;
    shell.activate("workspace_3D");
    // Explicit small fixture budget; an empty project has no Physics2D service.
    const physics2d::WorldConfig budget{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f};
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(budget);
    if (!world->valid())
        return false;
    shell.sceneDocument().setPhysicsWorld2DProvider([world] {
        return world;
    });
    const auto create = [&](std::string_view name, std::string_view component) {
        const auto created = shell.submitCommand(CreateSceneObjectCommand(name));
        const auto added = shell.submitCommand(AddSceneComponentCommand(component));
        if (created.status != EditorResponseStatus::Completed
            || added.status != EditorResponseStatus::Completed) {
            std::fprintf(stderr,
                "reference fixture create %.*s: object status=%u, component status=%u diagnostic=%u\n",
                int(component.size()),
                component.data(),
                unsigned(created.status),
                unsigned(added.status),
                unsigned(added.diagnostic));
            return scene::GameObjectId::invalid();
        }
        return shell.sceneDocument().selectedObject();
    };
    const auto target = create("Body target", "RigidBody2DComponent");
    const auto replacement = create("Replacement body", "RigidBody2DComponent");
    const auto owner = create("Reference diagnostic", "Joint2DComponent");
    if (!target.valid() || !replacement.valid() || !owner.valid())
        return false;
    auto snapshot = shell.submitCommand(GetSceneInspectorQuery{});
    diagnostic = "EDITOR-INSPECTOR-REFERENCE-QUERY";
    if (!snapshot.hasSceneInspector)
        return false;
    const auto findProperty = [](const auto& properties, std::string_view name) {
        return std::find_if(properties.begin(), properties.end(), [&](const auto& p) {
            return p.kind == SceneInspectorPropertyKind::SceneObjectReference && p.name == name;
        });
    };
    auto property = findProperty(snapshot.sceneInspector.properties, "bodyA");
    if (property == snapshot.sceneInspector.properties.end())
        return false;
    const auto source = scenePropertyControlSource(*property);
    if (scenario == "ref_missing" || scenario == "ref_type") {
        diagnostic = "EDITOR-INSPECTOR-REFERENCE-CORRUPTION-FIXTURE";
        SetSceneComponentPropertyCommand set("bodyA");
        set.objectId = owner.value();
        set.componentId = property->componentId;
        set.valueKind = ScenePropertyValueKind::SceneObjectReference;
        set.sceneObjectReferenceValue = target.value();
        if (shell.submitCommand(std::move(set)).status != EditorResponseStatus::Completed)
            return false;
        // Explicit corruption fixture: simulate an externally removed target or
        // component. This is not evidence that a normal delete accepts bad links.
        if (scenario == "ref_missing") {
            if (!shell.sceneDocument().scene().destroyObject(target).ok())
                return false;
        } else {
            auto* object = shell.sceneDocument().scene().findObject(target);
            const auto body = std::find_if(
                object->components().begin(), object->components().end(), [](const auto& c) {
                    return std::string_view(c->scriptTypeName()) == "RigidBody2DComponent";
                });
            if (body == object->components().end() || !object->removeComponent((*body)->id()))
                return false;
        }
        if (!shell.retrySceneInspector())
            return false;
    }
    snapshot = shell.submitCommand(GetSceneInspectorQuery{});
    property = findProperty(snapshot.sceneInspector.properties, "bodyA");
    if (!snapshot.hasSceneInspector || property == snapshot.sceneInspector.properties.end())
        return false;
    if (scenario == "ref_repaired") {
        diagnostic = "EDITOR-INSPECTOR-REFERENCE-REPAIR-INPUT";
        char replacementId[32];
        std::snprintf(replacementId,
            sizeof(replacementId),
            "%llu",
            static_cast<unsigned long long>(replacement.value()));
        const auto option = std::find_if(
            property->enumOptions.begin(), property->enumOptions.end(), [&](const auto& candidate) {
                return candidate.commandValue == replacementId;
            });
        if (option == property->enumOptions.end() || !EditorControlInput::click(shell, source)) {
            std::fprintf(stderr,
                "repair click failed: source=%s option=%u editable=%u\n",
                source.c_str(),
                unsigned(option != property->enumOptions.end()),
                unsigned(property->editable));
            return false;
        }
        char suffix[32];
        std::snprintf(suffix,
            sizeof(suffix),
            "_option_%zu",
            std::size_t(option - property->enumOptions.begin()));
        if (!EditorControlInput::click(shell, source + suffix))
            return false;
        snapshot = shell.submitCommand(GetSceneInspectorQuery{});
        property = findProperty(snapshot.sceneInspector.properties, "bodyA");
        if (!snapshot.hasSceneInspector || property == snapshot.sceneInspector.properties.end()
            || property->sceneObjectReferenceValue != replacement.value()
            || !property->resourceDiagnostic.empty())
            return false;
    }
    const auto controlId = EditorControlInput::find(shell, source);
    const auto* control = shell.runtime().findControlObject(controlId);
    diagnostic = "EDITOR-INSPECTOR-REFERENCE-CONTROL";
    const auto selected = std::find_if(
        property->enumOptions.begin(), property->enumOptions.end(), [&](const auto& option) {
            return option.value == property->integerValue;
        });
    if (!control || selected == property->enumOptions.end()
        || control->value().index
            != static_cast<std::uint32_t>(selected - property->enumOptions.begin())
        || shell.inspectorState() != EditorShell::InspectorState::Ready) {
        std::fprintf(stderr,
            "reference control failed: source=%s control=%u editable=%u options=%zu selection=%d actual=%u state=%u\n",
            source.c_str(),
            unsigned(control != nullptr),
            unsigned(property->editable),
            property->enumOptions.size(),
            property->integerValue,
            control ? control->value().index : 999u,
            unsigned(shell.inspectorState()));
        return false;
    }
    const auto draws = shell.drawCommands();
    diagnostic = "EDITOR-INSPECTOR-REFERENCE-DRAWS";
    if (!draws.ok()
        || std::none_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
               return draw.widgetId == controlId;
           }))
        return false;
    if ((scenario == "ref_repaired" || scenario == "ref_unassigned")
        && EditorControlInput::find(shell, source + "_diagnostic").valid())
        return false;
    if (scenario == "ref_unassigned"
        && (!property->resourceOptional || property->sceneObjectReferenceValue != 0
            || !property->resourceDiagnostic.empty()))
        return false;
    if (scenario == "ref_missing" || scenario == "ref_type") {
        diagnostic = "EDITOR-INSPECTOR-REFERENCE-DIAGNOSTIC-DRAW";
        const auto expected = scenario == "ref_missing" ? "Missing object" : "Wrong type: ";
        const auto* label = shell.runtime().canvas().findWidget(
            EditorControlInput::find(shell, source + "_diagnostic"));
        if (!label || !label->visible()
            || label->desc().text.find(expected) == core::pmr::String::npos
            || std::none_of(
                draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
                    return draw.widgetId == label->id();
                }))
            return false;
    }
    std::printf(
        "PASS Inspector reference native fixture=%.*s ID=%llu selected=%s; actual control/draw verified\n",
        int(scenario.size()),
        scenario.data(),
        static_cast<unsigned long long>(property->sceneObjectReferenceValue),
        selected->name.c_str());
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
