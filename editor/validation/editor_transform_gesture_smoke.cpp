#include "editor/validation/editor_transform_gesture_smoke.h"

#include "editor/validation/editor_transform_gesture_input.h"
#include "editor/validation/editor_transform_input_smoke.h"
#include "editor/validation/editor_transform_settings_smoke.h"
#include "editor/workspace/editor_shell.h"

#include <cmath>
#include <cstdio>

namespace arp::editor {
bool EditorTransformGestureSmoke::run(EditorShell& shell,
    std::string_view scenario,
    core::pmr::String& diagnostic,
    float windowWidth,
    float windowHeight)
{
    if (scenario.substr(0, 6) == "input_")
        return EditorTransformInputSmoke::run(shell, scenario, diagnostic);
    if (scenario.substr(0, 9) == "settings_")
        return EditorTransformSettingsSmoke::run(shell, scenario, diagnostic);
    diagnostic = "EDITOR-TRANSFORM-GESTURE-SMOKE";
    const bool group = scenario == "group_move" || scenario == "group_rotate"
        || scenario == "group_scale" || scenario == "group_individual" || scenario == "group_undo"
        || scenario == "group_redo";
    if (group || scenario == "batch_move" || scenario == "batch_undo" || scenario == "batch_redo") {
        shell.activate("workspace_3D");
        const auto create = [&](const char* name, float x) {
            if (shell.submitCommand(CreateSceneObjectCommand(name)).status
                != EditorResponseStatus::Completed)
                return scene::GameObjectId::invalid();
            const auto id = shell.sceneDocument().selectedObject();
            SetSceneObjectTransformCommand pose;
            pose.objectId = id.value();
            pose.translation = {x, 0, 0};
            return shell.submitCommand(pose).status == EditorResponseStatus::Completed
                ? id
                : scene::GameObjectId::invalid();
        };
        const auto first = create("Batch first", -0.75f), second = create("Batch second", 0.75f);
        if (!first.valid() || !second.valid())
            return false;
        SelectSceneObjectsCommand selected;
        selected.objectIds = {first.value(), second.value()};
        selected.primaryObjectId = first.value();
        if (shell.submitCommand(std::move(selected)).status != EditorResponseStatus::Completed)
            return false;
        if (group) {
            const bool individual = scenario == "group_individual";
            if (shell.submitCommand(SetSceneTransformPivotCommand{individual
                                            ? EditorSceneTransformPivot::Individual
                                            : EditorSceneTransformPivot::Center})
                        .status
                    != EditorResponseStatus::Completed
                || shell.submitCommand(FocusSceneSelectionCommand{}).status
                    != EditorResponseStatus::Completed)
                return false;
            EditorTransformGestureInput input(shell, windowWidth, windowHeight);
            const bool rotation = scenario == "group_rotate" || individual;
            const bool scale = scenario == "group_scale";
            if (!(rotation ? input.rotate() : input.linear(scale))
                || shell.activeSceneSelectionTransformEditId())
                return false;
            const auto pose = [&](scene::GameObjectId id) {
                return shell.sceneDocument().scene().findObject(id)->local_transform();
            };
            const auto a = pose(first), b = pose(second);
            if (rotation) {
                if (std::abs(std::abs(core::dot(a.rotation, core::Quat{})) - std::cos(0.15f))
                        > 0.0001f
                    || std::abs(std::abs(core::dot(a.rotation, b.rotation)) - 1) > 0.0001f
                    || (individual && (a.translation.x != -0.75f || b.translation.x != 0.75f))
                    || (!individual && std::abs(a.translation.y) < 0.1f))
                    return false;
            } else if (scale) {
                if (std::abs(a.scale.x - 1.5f) > 0.0001f || std::abs(b.scale.x - 1.5f) > 0.0001f
                    || std::abs(a.translation.x + 1.125f) > 0.0001f
                    || std::abs(b.translation.x - 1.125f) > 0.0001f)
                    return false;
            } else if (std::abs(a.translation.x + 0.25f) > 0.0001f
                || std::abs(b.translation.x - 1.25f) > 0.0001f)
                return false;
            if (scenario == "group_undo" || scenario == "group_redo") {
                if (shell.submitCommand(UndoSceneCommand{}).status
                        != EditorResponseStatus::Completed
                    || pose(first).translation.x != -0.75f || pose(second).translation.x != 0.75f)
                    return false;
                if (scenario == "group_redo"
                    && (shell.submitCommand(RedoSceneCommand{}).status
                            != EditorResponseStatus::Completed
                        || !EditorSceneTransformFrame::same(pose(first), a)
                        || !EditorSceneTransformFrame::same(pose(second), b)))
                    return false;
            }
            if (!shell.sceneViewportObjectSelected(first)
                || !shell.sceneViewportObjectSelected(second))
                return false;
            std::printf(
                "PASS: native pointer %.*s pivot=%s first=(%f,%f) second=(%f,%f) selection=2 one group history\n",
                int(scenario.size()),
                scenario.data(),
                individual ? "Individual" : "Center",
                pose(first).translation.x,
                pose(first).translation.y,
                pose(second).translation.x,
                pose(second).translation.y);
            diagnostic.clear();
            return true;
        }
        TransformSceneSelectionCommand operation;
        operation.pivot = EditorSceneTransformPivot::Center;
        operation.axis = 1;
        operation.amount = 0.5;
        if (shell.submitCommand(operation).status != EditorResponseStatus::Completed)
            return false;
        if (scenario != "batch_move"
            && shell.submitCommand(UndoSceneCommand{}).status != EditorResponseStatus::Completed)
            return false;
        if (scenario == "batch_redo"
            && shell.submitCommand(RedoSceneCommand{}).status != EditorResponseStatus::Completed)
            return false;
        const auto a = shell.sceneDocument().scene().findObject(first)->local_transform();
        const auto b = shell.sceneDocument().scene().findObject(second)->local_transform();
        const float expectedY = scenario == "batch_undo" ? 0.f : 0.5f;
        if (a.translation.x != -0.75f || b.translation.x != 0.75f || a.translation.y != expectedY
            || b.translation.y != expectedY || !shell.sceneViewportObjectSelected(first)
            || !shell.sceneViewportObjectSelected(second)
            || shell.submitCommand(FocusSceneSelectionCommand{}).status
                != EditorResponseStatus::Completed)
            return false;
        std::printf(
            "PASS: native batch %.*s first=(%f,%f) second=(%f,%f) selection=2 one batch history\n",
            int(scenario.size()),
            scenario.data(),
            a.translation.x,
            a.translation.y,
            b.translation.x,
            b.translation.y);
        diagnostic.clear();
        return true;
    }
    const bool spaceScenario = scenario == "world_axes" || scenario == "local_axes"
        || scenario == "local_move" || scenario == "local_scale"
        || scenario == "world_scale_rejected" || scenario == "local_rotate"
        || scenario == "world_rotate" || scenario == "local_undo" || scenario == "local_redo";
    const bool snapScenario = scenario == "snap_before" || scenario == "snap_move"
        || scenario == "snap_negative" || scenario == "snap_scale" || scenario == "snap_rotate"
        || scenario == "snap_undo" || scenario == "snap_redo";
    if (scenario != "before" && scenario != "move" && scenario != "scale" && scenario != "rotate"
        && scenario != "undo" && scenario != "redo" && !spaceScenario && !snapScenario)
        return false;
    shell.activate("workspace_3D");
    if (shell.submitCommand(CreateSceneObjectCommand("Gesture target")).status
            != EditorResponseStatus::Completed
        || shell.submitCommand(FocusSceneSelectionCommand{}).status
            != EditorResponseStatus::Completed)
        return false;
    const auto objectId = shell.sceneDocument().selectedObject();
    const auto state = [&]() {
        return shell.sceneDocument().scene().findObject(objectId)->local_transform();
    };
    shell.activate("tool_1");
    EditorTransformGestureInput input(shell, windowWidth, windowHeight);
    if (snapScenario) {
        EditorSceneTransformSnap snap;
        snap.enabled = true;
        snap.moveStep = 0.75;
        snap.scaleStep = 0.75;
        snap.rotateStepRadians = 0.4;
        if (shell.submitCommand(SetSceneTransformSnapCommand{snap}).status
            != EditorResponseStatus::Completed)
            return false;
        const auto start = state();
        if (scenario == "snap_move" || scenario == "snap_negative") {
            const float sign = scenario == "snap_negative" ? -1.f : 1.f;
            if (!input.linear(false, sign)
                || core::distance(state().translation, {sign * 0.75f, 0, 0}) > 0.0001f)
                return false;
        } else if (scenario == "snap_scale") {
            if (!input.linear(true) || core::distance(state().scale, {1.75f, 1, 1}) > 0.0001f)
                return false;
        } else if (scenario != "snap_before") {
            if (!input.rotate())
                return false;
            const auto rotated = state();
            if (std::abs(std::abs(core::dot(rotated.rotation, start.rotation)) - std::cos(0.2f))
                > 0.00001f)
                return false;
            if (scenario == "snap_undo" || scenario == "snap_redo") {
                if (shell.submitCommand(UndoSceneCommand{}).status
                        != EditorResponseStatus::Completed
                    || !EditorSceneTransformFrame::same(state(), start))
                    return false;
                if (scenario == "snap_redo"
                    && (shell.submitCommand(RedoSceneCommand{}).status
                            != EditorResponseStatus::Completed
                        || !EditorSceneTransformFrame::same(state(), rotated)))
                    return false;
            }
        }
        std::puts(
            "PASS: native relative snapping, explicit Move/Turn/Scale steps, accepted raw sub-step accumulation");
    } else if (spaceScenario) {
        const bool localSpace = scenario != "world_axes" && scenario != "world_rotate"
            && scenario != "world_scale_rejected";
        const bool rotateScenario = scenario == "local_rotate" || scenario == "world_rotate"
            || scenario == "local_undo" || scenario == "local_redo";
        SetSceneObjectTransformCommand setup;
        setup.objectId = objectId.value();
        setup.rotation = rotateScenario ? core::Quat::fromAxisAngle({0, 1, 0}, 0.45f)
                                        : core::Quat::fromAxisAngle({0, 0, 1}, 0.7853981633974483f);
        if (shell.submitCommand(setup).status != EditorResponseStatus::Completed
            || shell.submitCommand(SetSceneTransformSpaceCommand{localSpace
                                           ? EditorSceneTransformSpace::Local
                                           : EditorSceneTransformSpace::World})
                    .status
                != EditorResponseStatus::Completed)
            return false;
        const auto start = state();
        if (scenario == "local_move") {
            if (!input.linear(false)
                || core::distance(state().translation, {0.35355339f, 0.35355339f, 0}) > 0.0001f)
                return false;
        } else if (scenario == "local_scale") {
            if (!input.linear(true) || core::distance(state().scale, {1.5f, 1, 1}) > 0.0001f)
                return false;
        } else if (scenario == "world_scale_rejected") {
            if (!input.linear(true) || !EditorSceneTransformFrame::same(start, state())
                || shell.runtime().canvas().findWidget(shell.id("output_status"))->desc().text
                    != "EDITOR-TRANSFORM-WORLD-SCALE-SHEAR")
                return false;
            std::puts(
                "PASS: EDITOR-TRANSFORM-WORLD-SCALE-SHEAR visible diagnostic; object unchanged");
        } else if (rotateScenario) {
            if (!input.rotate())
                return false;
            const auto rotated = state();
            const auto turn = core::Quat::fromAxisAngle({0, 0, 1}, 0.3f);
            const auto sameRotation = [](core::Quat a, core::Quat b) {
                return std::abs(std::abs(core::dot(a, b)) - 1) < 0.00001f;
            };
            if (!sameRotation(
                    rotated.rotation, localSpace ? start.rotation * turn : turn * start.rotation)
                && !sameRotation(rotated.rotation,
                    localSpace ? start.rotation * core::conjugate(turn)
                               : core::conjugate(turn) * start.rotation))
                return false;
            if (scenario == "local_undo" || scenario == "local_redo") {
                if (shell.submitCommand(UndoSceneCommand{}).status
                        != EditorResponseStatus::Completed
                    || !sameRotation(state().rotation, start.rotation))
                    return false;
                if (scenario == "local_redo"
                    && (shell.submitCommand(RedoSceneCommand{}).status
                            != EditorResponseStatus::Completed
                        || !sameRotation(state().rotation, rotated.rotation)))
                    return false;
            }
        }
        std::printf("PASS: transform space=%s scenario=%.*s\n",
            localSpace ? "Local" : "World",
            int(scenario.size()),
            scenario.data());
    } else if (scenario != "before") {
        if (!input.linear(false) || core::distance(state().translation, {0.5f, 0, 0}) > 0.0001f)
            return false;
        if (scenario != "move") {
            if (!input.linear(true) || core::distance(state().scale, {1.5f, 1, 1}) > 0.0001f)
                return false;
            if (scenario != "scale") {
                if (!input.rotate())
                    return false;
                const auto rotated = state().rotation;
                const auto rotationDot = [](core::Quat a, core::Quat b) {
                    return std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                };
                if (std::abs(rotationDot(rotated, core::Quat{}) - std::cos(0.15f)) > 0.0001f)
                    return false;
                if (scenario == "undo" || scenario == "redo") {
                    if (shell.submitCommand(UndoSceneCommand{}).status
                            != EditorResponseStatus::Completed
                        || std::abs(rotationDot(state().rotation, core::Quat{}) - 1) > 0.00001f)
                        return false;
                    if (scenario == "redo"
                        && (shell.submitCommand(RedoSceneCommand{}).status
                                != EditorResponseStatus::Completed
                            || std::abs(rotationDot(state().rotation, rotated) - 1) > 0.00001f))
                        return false;
                }
            }
        }
    }
    const auto final = state();
    std::printf(
        "PASS: native transform gesture %.*s position=(%f,%f,%f) scale=(%f,%f,%f) rotation=(%f,%f,%f,%f)\n",
        int(scenario.size()),
        scenario.data(),
        final.translation.x,
        final.translation.y,
        final.translation.z,
        final.scale.x,
        final.scale.y,
        final.scale.z,
        final.rotation.x,
        final.rotation.y,
        final.rotation.z,
        final.rotation.w);
    diagnostic.clear();
    return true;
}
} // namespace arp::editor
