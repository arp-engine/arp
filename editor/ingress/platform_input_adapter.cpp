#include "editor/ingress/platform_input_adapter.h"

#include <cmath>
#include <limits>

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
    case In::F:
        return Out::F;
    case In::Escape:
        return Out::Escape;
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

EditorCanvasInputMapping::EditorCanvasInputMapping(
    float windowWidth, float windowHeight, float canvasWidth, float canvasHeight)
    : windowWidth_(windowWidth), windowHeight_(windowHeight), canvasWidth_(canvasWidth),
      canvasHeight_(canvasHeight)
{}

const char* EditorCanvasInputMapping::apply(platform::WindowInputEvent& event) const
{
    using Kind = platform::WindowInputEvent::Kind;
    const bool delta = event.kind == Kind::MouseMoved;
    const bool position = event.hasPosition || event.kind == Kind::PointerMoved || delta;
    if (!position)
        return nullptr;
    const auto positive = [](float value) {
        return std::isfinite(value) && value > 0;
    };
    if (!positive(windowWidth_) || !positive(windowHeight_) || !positive(canvasWidth_)
        || !positive(canvasHeight_))
        return "EDITOR-INPUT-CANVAS-EXTENT";
    const double sx = double(canvasWidth_) / windowWidth_;
    const double sy = double(canvasHeight_) / windowHeight_;
    const double x = double(event.x) * sx;
    const double y = double(event.y) * sy;
    const double dx = delta ? double(event.dx) * sx : 0;
    const double dy = delta ? double(event.dy) * sy : 0;
    const auto representable = [](double value) {
        return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
    };
    if (!representable(x) || !representable(y) || !representable(dx) || !representable(dy))
        return "EDITOR-INPUT-CANVAS-COORDINATE";
    event.x = static_cast<float>(x);
    event.y = static_cast<float>(y);
    if (delta) {
        event.dx = static_cast<float>(dx);
        event.dy = static_cast<float>(dy);
    }
    return nullptr;
}

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
