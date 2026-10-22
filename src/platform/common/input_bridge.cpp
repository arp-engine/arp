#include "platform/input_bridge.h"

namespace arp::platform {

bool windowKeyToInputKey(WindowKey key, arp::input::InputKey& outKey)
{
    // Closed vocabulary contract: every non-Unknown WindowKey has an InputKey
    // equivalent, so this switch is exhaustive and the bridge never drops a
    // mapped platform key. Verified by the exhaustive loop in the platform
    // window contract tests.
    using arp::input::InputKey;
    switch (key) {
    case WindowKey::A:
        outKey = InputKey::A;
        return true;
    case WindowKey::B:
        outKey = InputKey::B;
        return true;
    case WindowKey::C:
        outKey = InputKey::C;
        return true;
    case WindowKey::D:
        outKey = InputKey::D;
        return true;
    case WindowKey::E:
        outKey = InputKey::E;
        return true;
    case WindowKey::F:
        outKey = InputKey::F;
        return true;
    case WindowKey::G:
        outKey = InputKey::G;
        return true;
    case WindowKey::H:
        outKey = InputKey::H;
        return true;
    case WindowKey::I:
        outKey = InputKey::I;
        return true;
    case WindowKey::J:
        outKey = InputKey::J;
        return true;
    case WindowKey::K:
        outKey = InputKey::K;
        return true;
    case WindowKey::L:
        outKey = InputKey::L;
        return true;
    case WindowKey::M:
        outKey = InputKey::M;
        return true;
    case WindowKey::N:
        outKey = InputKey::N;
        return true;
    case WindowKey::O:
        outKey = InputKey::O;
        return true;
    case WindowKey::P:
        outKey = InputKey::P;
        return true;
    case WindowKey::Q:
        outKey = InputKey::Q;
        return true;
    case WindowKey::R:
        outKey = InputKey::R;
        return true;
    case WindowKey::S:
        outKey = InputKey::S;
        return true;
    case WindowKey::T:
        outKey = InputKey::T;
        return true;
    case WindowKey::U:
        outKey = InputKey::U;
        return true;
    case WindowKey::V:
        outKey = InputKey::V;
        return true;
    case WindowKey::W:
        outKey = InputKey::W;
        return true;
    case WindowKey::X:
        outKey = InputKey::X;
        return true;
    case WindowKey::Y:
        outKey = InputKey::Y;
        return true;
    case WindowKey::Z:
        outKey = InputKey::Z;
        return true;
    case WindowKey::Num0:
        outKey = InputKey::Num0;
        return true;
    case WindowKey::Num1:
        outKey = InputKey::Num1;
        return true;
    case WindowKey::Num2:
        outKey = InputKey::Num2;
        return true;
    case WindowKey::Num3:
        outKey = InputKey::Num3;
        return true;
    case WindowKey::Num4:
        outKey = InputKey::Num4;
        return true;
    case WindowKey::Num5:
        outKey = InputKey::Num5;
        return true;
    case WindowKey::Num6:
        outKey = InputKey::Num6;
        return true;
    case WindowKey::Num7:
        outKey = InputKey::Num7;
        return true;
    case WindowKey::Num8:
        outKey = InputKey::Num8;
        return true;
    case WindowKey::Num9:
        outKey = InputKey::Num9;
        return true;
    case WindowKey::Space:
        outKey = InputKey::Space;
        return true;
    case WindowKey::Enter:
        outKey = InputKey::Enter;
        return true;
    case WindowKey::Tab:
        outKey = InputKey::Tab;
        return true;
    case WindowKey::Backspace:
        outKey = InputKey::Backspace;
        return true;
    case WindowKey::Escape:
        outKey = InputKey::Escape;
        return true;
    case WindowKey::Delete:
        outKey = InputKey::Delete;
        return true;
    case WindowKey::ArrowRight:
        outKey = InputKey::ArrowRight;
        return true;
    case WindowKey::ArrowLeft:
        outKey = InputKey::ArrowLeft;
        return true;
    case WindowKey::ArrowDown:
        outKey = InputKey::ArrowDown;
        return true;
    case WindowKey::ArrowUp:
        outKey = InputKey::ArrowUp;
        return true;
    case WindowKey::PageUp:
        outKey = InputKey::PageUp;
        return true;
    case WindowKey::PageDown:
        outKey = InputKey::PageDown;
        return true;
    case WindowKey::Home:
        outKey = InputKey::Home;
        return true;
    case WindowKey::End:
        outKey = InputKey::End;
        return true;
    case WindowKey::Insert:
        outKey = InputKey::Insert;
        return true;
    case WindowKey::LeftShift:
        outKey = InputKey::LeftShift;
        return true;
    case WindowKey::RightShift:
        outKey = InputKey::RightShift;
        return true;
    case WindowKey::LeftControl:
        outKey = InputKey::LeftControl;
        return true;
    case WindowKey::RightControl:
        outKey = InputKey::RightControl;
        return true;
    case WindowKey::LeftAlt:
        outKey = InputKey::LeftAlt;
        return true;
    case WindowKey::RightAlt:
        outKey = InputKey::RightAlt;
        return true;
    case WindowKey::LeftSuper:
        outKey = InputKey::LeftSuper;
        return true;
    case WindowKey::RightSuper:
        outKey = InputKey::RightSuper;
        return true;
    case WindowKey::CapsLock:
        outKey = InputKey::CapsLock;
        return true;
    case WindowKey::NumLock:
        outKey = InputKey::NumLock;
        return true;
    case WindowKey::F1:
        outKey = InputKey::F1;
        return true;
    case WindowKey::F2:
        outKey = InputKey::F2;
        return true;
    case WindowKey::F3:
        outKey = InputKey::F3;
        return true;
    case WindowKey::F4:
        outKey = InputKey::F4;
        return true;
    case WindowKey::F5:
        outKey = InputKey::F5;
        return true;
    case WindowKey::F6:
        outKey = InputKey::F6;
        return true;
    case WindowKey::F7:
        outKey = InputKey::F7;
        return true;
    case WindowKey::F8:
        outKey = InputKey::F8;
        return true;
    case WindowKey::F9:
        outKey = InputKey::F9;
        return true;
    case WindowKey::F10:
        outKey = InputKey::F10;
        return true;
    case WindowKey::F11:
        outKey = InputKey::F11;
        return true;
    case WindowKey::F12:
        outKey = InputKey::F12;
        return true;
    case WindowKey::Minus:
        outKey = InputKey::Minus;
        return true;
    case WindowKey::Equals:
        outKey = InputKey::Equals;
        return true;
    case WindowKey::LeftBracket:
        outKey = InputKey::LeftBracket;
        return true;
    case WindowKey::RightBracket:
        outKey = InputKey::RightBracket;
        return true;
    case WindowKey::Backslash:
        outKey = InputKey::Backslash;
        return true;
    case WindowKey::Semicolon:
        outKey = InputKey::Semicolon;
        return true;
    case WindowKey::Apostrophe:
        outKey = InputKey::Apostrophe;
        return true;
    case WindowKey::Grave:
        outKey = InputKey::Grave;
        return true;
    case WindowKey::Comma:
        outKey = InputKey::Comma;
        return true;
    case WindowKey::Period:
        outKey = InputKey::Period;
        return true;
    case WindowKey::Slash:
        outKey = InputKey::Slash;
        return true;
    case WindowKey::Numpad0:
        outKey = InputKey::Numpad0;
        return true;
    case WindowKey::Numpad1:
        outKey = InputKey::Numpad1;
        return true;
    case WindowKey::Numpad2:
        outKey = InputKey::Numpad2;
        return true;
    case WindowKey::Numpad3:
        outKey = InputKey::Numpad3;
        return true;
    case WindowKey::Numpad4:
        outKey = InputKey::Numpad4;
        return true;
    case WindowKey::Numpad5:
        outKey = InputKey::Numpad5;
        return true;
    case WindowKey::Numpad6:
        outKey = InputKey::Numpad6;
        return true;
    case WindowKey::Numpad7:
        outKey = InputKey::Numpad7;
        return true;
    case WindowKey::Numpad8:
        outKey = InputKey::Numpad8;
        return true;
    case WindowKey::Numpad9:
        outKey = InputKey::Numpad9;
        return true;
    case WindowKey::NumpadAdd:
        outKey = InputKey::NumpadAdd;
        return true;
    case WindowKey::NumpadSubtract:
        outKey = InputKey::NumpadSubtract;
        return true;
    case WindowKey::NumpadMultiply:
        outKey = InputKey::NumpadMultiply;
        return true;
    case WindowKey::NumpadDivide:
        outKey = InputKey::NumpadDivide;
        return true;
    case WindowKey::NumpadDecimal:
        outKey = InputKey::NumpadDecimal;
        return true;
    case WindowKey::NumpadEnter:
        outKey = InputKey::NumpadEnter;
        return true;
    case WindowKey::PrintScreen:
        outKey = InputKey::PrintScreen;
        return true;
    case WindowKey::ScrollLock:
        outKey = InputKey::ScrollLock;
        return true;
    case WindowKey::Pause:
        outKey = InputKey::Pause;
        return true;
    case WindowKey::Menu:
        outKey = InputKey::Menu;
        return true;
    case WindowKey::NonUsBackslash:
        outKey = InputKey::NonUsBackslash;
        return true;
    case WindowKey::F13:
        outKey = InputKey::F13;
        return true;
    case WindowKey::F14:
        outKey = InputKey::F14;
        return true;
    case WindowKey::F15:
        outKey = InputKey::F15;
        return true;
    case WindowKey::F16:
        outKey = InputKey::F16;
        return true;
    case WindowKey::F17:
        outKey = InputKey::F17;
        return true;
    case WindowKey::F18:
        outKey = InputKey::F18;
        return true;
    case WindowKey::F19:
        outKey = InputKey::F19;
        return true;
    case WindowKey::F20:
        outKey = InputKey::F20;
        return true;
    case WindowKey::F21:
        outKey = InputKey::F21;
        return true;
    case WindowKey::F22:
        outKey = InputKey::F22;
        return true;
    case WindowKey::F23:
        outKey = InputKey::F23;
        return true;
    case WindowKey::F24:
        outKey = InputKey::F24;
        return true;
    case WindowKey::Unknown:
    case WindowKey::Count:
        return false;
    }
    return false;
}

bool windowMouseButtonToInputMouseButton(
    WindowMouseButton button, arp::input::InputMouseButton& outButton)
{
    using arp::input::InputMouseButton;
    switch (button) {
    case WindowMouseButton::Left:
        outButton = InputMouseButton::Left;
        return true;
    case WindowMouseButton::Right:
        outButton = InputMouseButton::Right;
        return true;
    case WindowMouseButton::Middle:
        outButton = InputMouseButton::Middle;
        return true;
    default:
        return false;
    }
}

WindowInputBridgeStats applyWindowInputToInputFrame(
    const WindowEventBatch& batch, arp::input::InputFrame& outFrame)
{
    WindowInputBridgeStats stats;

    // Cancellation barriers are mirrored by addWindowEvent into ordered input.
    for (const auto& event : batch.windowEvents) {
        if (event.type == WindowEventType::FocusLost
            || event.type == WindowEventType::CloseRequested
            || event.type == WindowEventType::Minimized) {
            continue;
        } else
            ++stats.windowEventsIgnored;
    }
    for (const auto& event : batch.inputEvents) {
        arp::input::InputEvent input;
        bool forwarded = true;
        switch (event.kind) {
        case WindowInputEvent::Kind::KeyDown:
        case WindowInputEvent::Kind::KeyUp: {
            arp::input::InputKey key;
            if (!windowKeyToInputKey(event.key, key)) {
                ++stats.droppedKeyEvents;
                forwarded = false;
                break;
            }
            input = event.kind == WindowInputEvent::Kind::KeyDown
                ? arp::input::InputEvent::keyPressed(key)
                : arp::input::InputEvent::keyReleased(key);
            break;
        }
        case WindowInputEvent::Kind::MouseButtonDown:
        case WindowInputEvent::Kind::MouseButtonUp: {
            arp::input::InputMouseButton button;
            if (!windowMouseButtonToInputMouseButton(event.mouseButton, button)) {
                ++stats.droppedMouseButtonEvents;
                forwarded = false;
                break;
            }
            input = event.kind == WindowInputEvent::Kind::MouseButtonDown
                ? arp::input::InputEvent::mouseButtonPressed(button)
                : arp::input::InputEvent::mouseButtonReleased(button);
            break;
        }
        case WindowInputEvent::Kind::MouseMoved:
            input = arp::input::InputEvent::mouseMoved(event.dx, event.dy);
            break;
        case WindowInputEvent::Kind::PointerMoved:
            input = arp::input::InputEvent::pointerMoved(event.x, event.y);
            break;
        case WindowInputEvent::Kind::MouseWheel:
            input = arp::input::InputEvent::mouseWheel(event.wheelDelta);
            break;
        case WindowInputEvent::Kind::TextInput:
            input = arp::input::InputEvent::textInput(arp::core::pmr::String(
                event.text.data(), event.text.size(), &core::defaultCpuMemoryResource()));
            break;
        case WindowInputEvent::Kind::FocusLost:
            input.kind = arp::input::InputEventKind::FocusLost;
            ++stats.forwardedLifecycleEvents;
            break;
        }
        if (!forwarded)
            continue;
        input.windowId = event.windowId;
        input.hasPosition = event.hasPosition || event.kind == WindowInputEvent::Kind::PointerMoved;
        input.pointerX = event.x;
        input.pointerY = event.y;
        outFrame.add(std::move(input));
        if (event.kind != WindowInputEvent::Kind::FocusLost)
            ++stats.forwardedEvents;
    }

    return stats;
}

} // namespace arp::platform
