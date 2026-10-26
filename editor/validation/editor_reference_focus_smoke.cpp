#include "editor/validation/editor_reference_focus_smoke.h"

#include "2d/physics_components.h"
#include "editor/validation/editor_control_input.h"

#include <algorithm>
#include <cstdio>

namespace arp::editor {
bool EditorReferenceFocusSmoke::run(
    EditorShell& shell, std::string_view scenario, core::pmr::String& diagnostic)
{
    diagnostic = "EDITOR-REFERENCE-FOCUS-SMOKE";
    if (scenario != "focus_valid" && scenario != "focus_current" && scenario != "focus_missing"
        && scenario != "focus_failed" && scenario != "focus_retry")
        return false;
    shell.activate("workspace_3D");
    auto world = core::makeShared<physics2d::PhysicsWorld2D>(
        physics2d::WorldConfig{{0, 0}, 8, 8, 4, 8, 16, 8, 8, 4, 1.f / 60.f});
    if (!world->valid())
        return false;
    shell.sceneDocument().setPhysicsWorld2DProvider([world] {
        return world;
    });
    const auto create = [&](std::string_view name, std::string_view component) {
        if (shell.submitCommand(CreateSceneObjectCommand(name)).status
                != EditorResponseStatus::Completed
            || shell.submitCommand(AddSceneComponentCommand(component)).status
                != EditorResponseStatus::Completed)
            return scene::GameObjectId{};
        return shell.sceneDocument().selectedObject();
    };
    const auto first = create("Same name body", "RigidBody2DComponent");
    const auto second = create("Same name body", "RigidBody2DComponent");
    const auto owner = create("Reference source", "Joint2DComponent");
    if (!first.valid() || !second.valid() || !owner.valid())
        return false;
    auto& document = shell.sceneDocument();
    document.scene().findObject(first)->set_local_transform(
        scene::Transform::fromTranslation({8, 1, 0}));
    document.scene().findObject(second)->set_local_transform(
        scene::Transform::fromTranslation({-4, 2, 0}));
    auto properties = document.inspectorProperties(owner);
    const auto field = std::find_if(properties.begin(), properties.end(), [](const auto& value) {
        return value.name == "bodyA"
            && value.kind == SceneInspectorPropertyKind::SceneObjectReference;
    });
    if (field == properties.end())
        return false;
    const auto source =
        core::pmr::String("scene_reference_focus_") + scenePropertyControlSource(*field);
    SetSceneComponentPropertyCommand set("bodyA");
    set.objectId = owner.value();
    set.componentId = field->componentId;
    set.valueKind = ScenePropertyValueKind::SceneObjectReference;
    set.sceneObjectReferenceValue = first.value();
    if (shell.submitCommand(std::move(set)).status != EditorResponseStatus::Completed)
        return false;
    // Present a valid target before mutating the native fixture externally.
    if (!shell.retrySceneInspector())
        return false;
    const auto oldCamera = shell.sceneViewportCamera();
    const auto version = shell.stateVersion();
    const auto dirty = shell.sceneDirty();
    auto* joint =
        static_cast<physics2d::Joint2DComponent*>(document.scene().findObject(owner)->findComponent(
            scene::ComponentId::fromValue(field->componentId)));
    if (scenario == "focus_current") {
        auto value = joint->editorAuthoringProperties();
        value.cast<physics2d::Joint2DEditorProperties&>().bodyA = second.value();
        if (!joint->applyEditorAuthoringProperties(value))
            return false;
    } else if (scenario == "focus_missing") {
        if (!document.scene().destroyObject(first).ok())
            return false;
    }
    if (scenario == "focus_failed" || scenario == "focus_retry") {
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            if (shell.retrySceneInspector())
                return false;
        }
        if (shell.inspectorState() != EditorShell::InspectorState::Failed
            || quota->liveBytes() != 0)
            return false;
        if (scenario == "focus_retry" && !EditorControlInput::click(shell, "scene_inspector_retry"))
            return false;
    }
    if (scenario != "focus_failed" && !EditorControlInput::click(shell, source)) {
        diagnostic = "EDITOR-REFERENCE-FOCUS-SMOKE-CLICK";
        return false;
    }
    const auto camera = shell.sceneViewportCamera();
    const bool moved = scenario != "focus_missing" && scenario != "focus_failed";
    const auto expected = !moved      ? oldCamera.target()
        : scenario == "focus_current" ? core::Vec3{-4, 2, 0}
                                      : core::Vec3{8, 1, 0};
    if (camera.target().x != expected.x || camera.target().y != expected.y
        || camera.target().z != expected.z || camera.yawDegrees() != oldCamera.yawDegrees()
        || camera.pitchDegrees() != oldCamera.pitchDegrees()
        || camera.distance() != oldCamera.distance() || document.selectedObject() != owner
        || shell.sceneDirty() != dirty || shell.stateVersion() != version + unsigned(moved)) {
        diagnostic = "EDITOR-REFERENCE-FOCUS-SMOKE-STATE";
        return false;
    }
    const auto* action =
        shell.runtime().canvas().findWidget(EditorControlInput::find(shell, source));
    const auto draws = shell.drawCommands();
    if (!draws.ok())
        return false;
    if (scenario == "focus_failed") {
        if (shell.inspectorState() != EditorShell::InspectorState::Failed)
            return false;
    } else if (!action || action->desc().text != (moved ? "Locate object" : "No target to locate")
        || !action->visible()
        || (!moved && (action->desc().acceptsFocus || action->desc().acceptsPointerInput))
        || std::none_of(draws.commands().begin(), draws.commands().end(), [&](const auto& draw) {
               return draw.widgetId == action->id();
           })) {
        diagnostic = "EDITOR-REFERENCE-FOCUS-SMOKE-DRAW";
        return false;
    }
    std::printf(
        "PASS reference focus %.*s target=(%.1f,%.1f,%.1f) Inspector owner=%llu preserved\n",
        int(scenario.size()),
        scenario.data(),
        expected.x,
        expected.y,
        expected.z,
        static_cast<unsigned long long>(owner.value()));
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
