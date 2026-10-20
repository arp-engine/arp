#include "editor/ingress/platform_input_adapter.h"

namespace arp::editor {
namespace {
EditorInputKey normalizeKey(platform::WindowKey key)
{
    using In = platform::WindowKey;
    using Out = EditorInputKey;
    switch (key) {
    case In::A:
        return Out::A;
    case In::C:
        return Out::C;
    case In::D:
        return Out::D;
    case In::E:
        return Out::E;
    case In::Q:
        return Out::Q;
    case In::R:
        return Out::R;
    case In::S:
        return Out::S;
    case In::V:
        return Out::V;
    case In::W:
        return Out::W;
    case In::X:
        return Out::X;
    case In::Y:
        return Out::Y;
    case In::Z:
        return Out::Z;
    case In::Backspace:
        return Out::Backspace;
    case In::Delete:
        return Out::Delete;
    case In::Enter:
        return Out::Enter;
    case In::NumpadEnter:
        return Out::NumpadEnter;
    case In::Tab:
        return Out::Tab;
    case In::ArrowLeft:
        return Out::ArrowLeft;
    case In::ArrowRight:
        return Out::ArrowRight;
    case In::ArrowUp:
        return Out::ArrowUp;
    case In::ArrowDown:
        return Out::ArrowDown;
    case In::Home:
        return Out::Home;
    case In::End:
        return Out::End;
    case In::PageUp:
        return Out::PageUp;
    case In::PageDown:
        return Out::PageDown;
    case In::LeftShift:
        return Out::LeftShift;
    case In::RightShift:
        return Out::RightShift;
    case In::LeftControl:
        return Out::LeftControl;
    case In::RightControl:
        return Out::RightControl;
    default:
        return Out::Other;
    }
}
} // namespace

EditorInputCommand normalizePlatformInput(const platform::WindowInputEvent& event)
{
    using In = platform::WindowInputEvent::Kind;
    EditorInputCommand command;
    command.x = event.x;
    command.y = event.y;
    command.deltaX = event.dx;
    command.deltaY = event.dy;
    command.wheelDelta = event.wheelDelta;
    switch (event.kind) {
    case In::KeyDown:
        command.kind = EditorInputKind::KeyDown;
        command.key = normalizeKey(event.key);
        break;
    case In::KeyUp:
        command.kind = EditorInputKind::KeyUp;
        command.key = normalizeKey(event.key);
        break;
    case In::MouseButtonDown:
        command.kind = EditorInputKind::PointerDown;
        command.button = event.mouseButton == platform::WindowMouseButton::Left
            ? EditorPointerButton::Left
            : event.mouseButton == platform::WindowMouseButton::Middle ? EditorPointerButton::Middle
            : event.mouseButton == platform::WindowMouseButton::Right
            ? EditorPointerButton::Right
            : EditorPointerButton::Auxiliary;
        command.primaryButton = event.mouseButton == platform::WindowMouseButton::Left;
        break;
    case In::MouseButtonUp:
        command.kind = EditorInputKind::PointerUp;
        command.button = event.mouseButton == platform::WindowMouseButton::Left
            ? EditorPointerButton::Left
            : event.mouseButton == platform::WindowMouseButton::Middle ? EditorPointerButton::Middle
            : event.mouseButton == platform::WindowMouseButton::Right
            ? EditorPointerButton::Right
            : EditorPointerButton::Auxiliary;
        command.primaryButton = event.mouseButton == platform::WindowMouseButton::Left;
        break;
    case In::MouseMoved:
        command.kind = EditorInputKind::PointerDelta;
        break;
    case In::PointerMoved:
        command.kind = EditorInputKind::PointerMoved;
        break;
    case In::MouseWheel:
        command.kind = EditorInputKind::Wheel;
        break;
    case In::TextInput:
        command.kind = EditorInputKind::Text;
        command.text.assign(event.text.data(), event.text.size());
        break;
    case In::FocusLost:
        command.kind = EditorInputKind::FocusLost;
        break;
    }
    return command;
}
} // namespace arp::editor
