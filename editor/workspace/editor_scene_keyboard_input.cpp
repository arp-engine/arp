#include "editor/workspace/editor_shell.h"

#include <new>

namespace arp::editor {
EditorCommandOutcome EditorShell::applySceneKeyboardInput(const EditorInputCommand& event)
{
    if (event.kind != EditorInputKind::KeyDown)
        return {true, false, EditorDiagnosticCode::None};
    const auto focusedId = runtime().focusedControl();
    const auto* focused = focusedId.valid() ? runtime().canvas().findWidget(focusedId) : nullptr;
    if (focused
        && (focused->desc().controlType == ui::UiControlType::TextInput
            || focused->desc().controlType == ui::UiControlType::NumericInput))
        return {true, false, EditorDiagnosticCode::None};
    const auto dispatch = [&](EditorCommandPayload command) -> EditorCommandOutcome {
        auto response = submitCommand(std::move(command), EditorCommandOrigin::PlatformInput);
        // The nested dispatcher already owns every semantic version change.
        return std::move(response).intoNestedOutcome();
    };
    try {
        if (modifiers_.control() && event.key == EditorInputKey::S)
            return dispatch(
                ActivateControlCommand(modifiers_.shift() ? "save_scene_as" : "save_scene"));
        if (modifiers_.control() && event.key == EditorInputKey::Z && !modifiers_.shift())
            return dispatch(UndoSceneCommand{});
        if (modifiers_.control()
            && (event.key == EditorInputKey::Y
                || (event.key == EditorInputKey::Z && modifiers_.shift())))
            return dispatch(RedoSceneCommand{});
        if (sceneViewportRightButtonHeld_) {
            float horizontal = 0, forward = 0;
            if (event.key == EditorInputKey::A)
                horizontal = -1;
            else if (event.key == EditorInputKey::D)
                horizontal = 1;
            else if (event.key == EditorInputKey::W)
                forward = 1;
            else if (event.key == EditorInputKey::S)
                forward = -1;
            if (horizontal != 0 || forward != 0) {
                const auto layout = runtime().canvas().resolveLayout();
                const auto* viewport = layout.ok() ? layout.findWidget(id("viewport")) : nullptr;
                if (!viewport)
                    return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
                SceneViewportCameraCommand camera;
                camera.operation = SceneViewportCameraOperation::Move;
                camera.deltaX = horizontal;
                camera.deltaY = forward;
                camera.viewportWidth = viewport->rect.width;
                camera.viewportHeight = viewport->rect.height;
                return dispatch(camera);
            }
        }
        if (!modifiers_.control()) {
            if (event.key == EditorInputKey::F && !modifiers_.shift())
                return dispatch(FocusSceneSelectionCommand{});
            const char* tool = event.key == EditorInputKey::Q ? "tool_4"
                : event.key == EditorInputKey::W              ? "tool_1"
                : event.key == EditorInputKey::E              ? "tool_2"
                : event.key == EditorInputKey::R              ? "tool_3"
                                                              : nullptr;
            if (tool)
                return dispatch(ActivateControlCommand(tool));
        }
        return {true, false, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}
} // namespace arp::editor
