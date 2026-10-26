#include "editor/ingress/editor_platform_input_router.h"

#include "editor/workspace/editor_shell.h"
#include "platform/input_bridge.h"

namespace arp::editor {
ui::UiInputFrameDispatchResult EditorPlatformInputRouter::dispatch(EditorShell& shell,
    const platform::WindowInputEvent& event,
    input::InputSystem& system,
    std::uint64_t windowId)
{
    const bool consumed = shell.handlePlatformInput(event);
    const bool keyboard = event.kind == platform::WindowInputEvent::Kind::KeyDown
        || event.kind == platform::WindowInputEvent::Kind::KeyUp
        || event.kind == platform::WindowInputEvent::Kind::TextInput;
    if (consumed || (keyboard && shell.scriptEditorCapturesKeyboard()))
        return {};
    platform::WindowEventBatch batch;
    batch.inputEvents.push_back(event);
    input::InputFrame frame;
    platform::applyWindowInputToInputFrame(batch, frame);
    if (frame.events().empty())
        return {};
    auto routed = shell.runtime().dispatchFrame(frame, {windowId, 512});
    if (!routed.ok())
        return routed;
    const auto begun = system.beginFrame(frame, routed.eventConsumption);
    if (!begun.ok())
        routed.diagnostic = {
            ui::UiDiagnosticCode::InvalidStateTransition, begun.diagnostic().message};
    return routed;
}
} // namespace arp::editor
