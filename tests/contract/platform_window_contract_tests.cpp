// WindowSystem/1 contract tests: neutral key tables, input bridge mapping,
// descriptor invariants and the SDL-free architecture boundary.
#include "input/input.h"
#include "platform/input_bridge.h"
#include "platform/window.h"
#include "platform/window_input_mapping.h"

#include <cassert>
#include <cstdint>

namespace {

void assertNeutralKeyContract()
{
    using arp::platform::WindowKey;
    assert(WindowKey::Unknown == static_cast<WindowKey>(0));
    // Count is the one-past-the-last sentinel; the exhaustive bridge loop in
    // assertInputKeyBridge iterates [1, Count) against it, so any WindowKey
    // added without its InputKey mirror or bridge case fails the suite.
    assert(WindowKey::Count > WindowKey::Unknown);
    // Movement set is order-stable and begins at the first enumerator after
    // Unknown; the input bridge relies on this when appending InputKey.
    assert(WindowKey::W != WindowKey::A);
    assert(WindowKey::A != WindowKey::S);
    assert(WindowKey::S != WindowKey::D);
}

void assertWin32VirtualKeyTable()
{
    using arp::platform::mapWin32KeyMessageToWindowKey;
    using arp::platform::mapWin32VirtualKeyToWindowKey;
    using arp::platform::WindowKey;
    assert(mapWin32VirtualKeyToWindowKey(0x41) == WindowKey::A);
    assert(mapWin32VirtualKeyToWindowKey(0x57) == WindowKey::W);
    assert(mapWin32VirtualKeyToWindowKey(0x5A) == WindowKey::Z);
    assert(mapWin32VirtualKeyToWindowKey(0x30) == WindowKey::Num0);
    assert(mapWin32VirtualKeyToWindowKey(0x39) == WindowKey::Num9);
    assert(mapWin32VirtualKeyToWindowKey(0x20) == WindowKey::Space);
    assert(mapWin32VirtualKeyToWindowKey(0x0D) == WindowKey::Enter);
    assert(mapWin32VirtualKeyToWindowKey(0x1B) == WindowKey::Escape);
    assert(mapWin32VirtualKeyToWindowKey(0x25) == WindowKey::ArrowLeft);
    assert(mapWin32VirtualKeyToWindowKey(0x26) == WindowKey::ArrowUp);
    assert(mapWin32VirtualKeyToWindowKey(0x27) == WindowKey::ArrowRight);
    assert(mapWin32VirtualKeyToWindowKey(0x28) == WindowKey::ArrowDown);
    assert(mapWin32VirtualKeyToWindowKey(0xA0) == WindowKey::LeftShift);
    assert(mapWin32VirtualKeyToWindowKey(0xA2) == WindowKey::LeftControl);
    assert(mapWin32VirtualKeyToWindowKey(0xA4) == WindowKey::LeftAlt);
    assert(mapWin32VirtualKeyToWindowKey(0x70) == WindowKey::F1);
    assert(mapWin32VirtualKeyToWindowKey(0x7B) == WindowKey::F12);
    assert(mapWin32VirtualKeyToWindowKey(0x11)
        == WindowKey::Unknown); // raw virtual-key table has no side information
    assert(mapWin32KeyMessageToWindowKey(0x11, 0x1D, false) == WindowKey::LeftControl);
    assert(mapWin32KeyMessageToWindowKey(0x11, 0x1D, true) == WindowKey::RightControl);
    assert(mapWin32KeyMessageToWindowKey(0x10, 0x2A, false) == WindowKey::LeftShift);
    assert(mapWin32KeyMessageToWindowKey(0x10, 0x36, false) == WindowKey::RightShift);
    assert(mapWin32KeyMessageToWindowKey(0x12, 0x38, false) == WindowKey::LeftAlt);
    assert(mapWin32KeyMessageToWindowKey(0x12, 0x38, true) == WindowKey::RightAlt);
    assert(mapWin32VirtualKeyToWindowKey(0x03) == WindowKey::Unknown); // Cancel: unmapped
    assert(mapWin32VirtualKeyToWindowKey(0xFF) == WindowKey::Unknown);
    assert(mapWin32VirtualKeyToWindowKey(0x60) == WindowKey::Numpad0); // VK_NUMPAD0
    assert(mapWin32VirtualKeyToWindowKey(0x69) == WindowKey::Numpad9);
    assert(mapWin32VirtualKeyToWindowKey(0x6A) == WindowKey::NumpadMultiply);
    assert(mapWin32VirtualKeyToWindowKey(0x6B) == WindowKey::NumpadAdd);
    assert(mapWin32VirtualKeyToWindowKey(0x6D) == WindowKey::NumpadSubtract);
    assert(mapWin32VirtualKeyToWindowKey(0x6E) == WindowKey::NumpadDecimal);
    assert(mapWin32VirtualKeyToWindowKey(0x6F) == WindowKey::NumpadDivide);
    assert(mapWin32VirtualKeyToWindowKey(0x90) == WindowKey::NumLock);     // VK_NUMLOCK
    assert(mapWin32VirtualKeyToWindowKey(0x2C) == WindowKey::PrintScreen); // VK_SNAPSHOT
    assert(mapWin32VirtualKeyToWindowKey(0x91) == WindowKey::ScrollLock);
    assert(mapWin32VirtualKeyToWindowKey(0x13) == WindowKey::Pause);
    assert(mapWin32VirtualKeyToWindowKey(0x5D) == WindowKey::Menu);           // VK_APPS
    assert(mapWin32VirtualKeyToWindowKey(0xE2) == WindowKey::NonUsBackslash); // VK_OEM_102
    assert(mapWin32VirtualKeyToWindowKey(0x7C) == WindowKey::F13);            // VK_F13
    assert(mapWin32VirtualKeyToWindowKey(0x87) == WindowKey::F24);            // VK_F24
}

void assertMacVirtualKeyTable()
{
    using arp::platform::mapMacVirtualKeyToWindowKey;
    using arp::platform::WindowKey;
    assert(mapMacVirtualKeyToWindowKey(0x00) == WindowKey::A);
    assert(mapMacVirtualKeyToWindowKey(0x0D) == WindowKey::W);
    assert(mapMacVirtualKeyToWindowKey(0x12) == WindowKey::Num1);
    assert(mapMacVirtualKeyToWindowKey(0x1D) == WindowKey::Num0);
    assert(mapMacVirtualKeyToWindowKey(0x31) == WindowKey::Space);
    assert(mapMacVirtualKeyToWindowKey(0x38) == WindowKey::LeftShift);
    assert(mapMacVirtualKeyToWindowKey(0x37) == WindowKey::LeftSuper);
    assert(mapMacVirtualKeyToWindowKey(0x7B) == WindowKey::ArrowLeft);
    assert(mapMacVirtualKeyToWindowKey(0x7D) == WindowKey::ArrowDown);
    assert(mapMacVirtualKeyToWindowKey(0x7E) == WindowKey::ArrowUp);
    assert(mapMacVirtualKeyToWindowKey(0x7A) == WindowKey::F1);
    assert(mapMacVirtualKeyToWindowKey(0x5A) == WindowKey::F20);
    assert(mapMacVirtualKeyToWindowKey(0x5B) == WindowKey::Numpad8);
    assert(mapMacVirtualKeyToWindowKey(0x5C) == WindowKey::Numpad9);
    assert(mapMacVirtualKeyToWindowKey(0x24) == WindowKey::Enter);
    assert(mapMacVirtualKeyToWindowKey(0xFF) == WindowKey::Unknown);
    assert(mapMacVirtualKeyToWindowKey(0x52) == WindowKey::Numpad0); // kVK_ANSI_Keypad0
    assert(mapMacVirtualKeyToWindowKey(0x59) == WindowKey::Numpad7);
    assert(mapMacVirtualKeyToWindowKey(0x5B) == WindowKey::Numpad8); // kVK_ANSI_Keypad8
    assert(mapMacVirtualKeyToWindowKey(0x5C) == WindowKey::Numpad9); // kVK_ANSI_Keypad9
    assert(mapMacVirtualKeyToWindowKey(0x41) == WindowKey::NumpadDecimal);
    assert(mapMacVirtualKeyToWindowKey(0x43) == WindowKey::NumpadMultiply);
    assert(mapMacVirtualKeyToWindowKey(0x45) == WindowKey::NumpadAdd);
    assert(mapMacVirtualKeyToWindowKey(0x4B) == WindowKey::NumpadDivide);
    assert(mapMacVirtualKeyToWindowKey(0x4C) == WindowKey::NumpadEnter);
    assert(mapMacVirtualKeyToWindowKey(0x4E) == WindowKey::NumpadSubtract);
    assert(mapMacVirtualKeyToWindowKey(0x69) == WindowKey::F13);
    assert(mapMacVirtualKeyToWindowKey(0x5A) == WindowKey::F20);
    // Mac AppKit keycodes have no PrintScreen/ScrollLock/Pause/NumLock
    // equivalents; kVK_ANSI_KeypadClear stays unmapped rather than guessing.
    assert(mapMacVirtualKeyToWindowKey(0x47) == WindowKey::Unknown);
}

void assertX11KeysymTable()
{
    using arp::platform::mapX11KeysymToWindowKey;
    using arp::platform::WindowKey;
    assert(mapX11KeysymToWindowKey(0x61) == WindowKey::A);
    assert(mapX11KeysymToWindowKey(0x77) == WindowKey::W);
    assert(mapX11KeysymToWindowKey(0x30) == WindowKey::Num0);
    assert(mapX11KeysymToWindowKey(0x20) == WindowKey::Space);
    assert(mapX11KeysymToWindowKey(0xFF0D) == WindowKey::Enter);
    assert(mapX11KeysymToWindowKey(0xFF1B) == WindowKey::Escape);
    assert(mapX11KeysymToWindowKey(0xFF51) == WindowKey::ArrowLeft);
    assert(mapX11KeysymToWindowKey(0xFF54) == WindowKey::ArrowDown);
    assert(mapX11KeysymToWindowKey(0xFFBE) == WindowKey::F1);
    assert(mapX11KeysymToWindowKey(0x0000) == WindowKey::Unknown);
    assert(mapX11KeysymToWindowKey(0x1234) == WindowKey::Unknown);
    assert(mapX11KeysymToWindowKey(0xFFB0) == WindowKey::Numpad0); // XK_KP_0
    assert(mapX11KeysymToWindowKey(0xFFB9) == WindowKey::Numpad9);
    assert(mapX11KeysymToWindowKey(0xFF8D) == WindowKey::NumpadEnter);
    assert(mapX11KeysymToWindowKey(0xFFAA) == WindowKey::NumpadMultiply);
    assert(mapX11KeysymToWindowKey(0xFFAB) == WindowKey::NumpadAdd);
    assert(mapX11KeysymToWindowKey(0xFFAD) == WindowKey::NumpadSubtract);
    assert(mapX11KeysymToWindowKey(0xFFAE) == WindowKey::NumpadDecimal);
    assert(mapX11KeysymToWindowKey(0xFFAF) == WindowKey::NumpadDivide);
    assert(mapX11KeysymToWindowKey(0xFF61) == WindowKey::PrintScreen);
    assert(mapX11KeysymToWindowKey(0xFF14) == WindowKey::ScrollLock);
    assert(mapX11KeysymToWindowKey(0xFF13) == WindowKey::Pause);
    assert(mapX11KeysymToWindowKey(0xFF67) == WindowKey::Menu);
    assert(mapX11KeysymToWindowKey(0xFF7F) == WindowKey::NumLock);
    assert(mapX11KeysymToWindowKey(0xFFCA) == WindowKey::F13);
    assert(mapX11KeysymToWindowKey(0xFFD5) == WindowKey::F24);
    // XK_KP_Separator is layout-dependent; it stays unmapped.
    assert(mapX11KeysymToWindowKey(0xFFAC) == WindowKey::Unknown);
}

void assertWebCodeTable()
{
    using arp::platform::mapWebCodeToWindowKey;
    using arp::platform::WindowKey;
    assert(mapWebCodeToWindowKey("KeyW") == WindowKey::W);
    assert(mapWebCodeToWindowKey("KeyA") == WindowKey::A);
    assert(mapWebCodeToWindowKey("Digit0") == WindowKey::Num0);
    assert(mapWebCodeToWindowKey("Digit9") == WindowKey::Num9);
    assert(mapWebCodeToWindowKey("Space") == WindowKey::Space);
    assert(mapWebCodeToWindowKey("Enter") == WindowKey::Enter);
    assert(mapWebCodeToWindowKey("Escape") == WindowKey::Escape);
    assert(mapWebCodeToWindowKey("ArrowUp") == WindowKey::ArrowUp);
    assert(mapWebCodeToWindowKey("ShiftLeft") == WindowKey::LeftShift);
    assert(mapWebCodeToWindowKey("ControlRight") == WindowKey::RightControl);
    assert(mapWebCodeToWindowKey("F12") == WindowKey::F12);
    assert(mapWebCodeToWindowKey("Numpad5") == WindowKey::Numpad5);
    assert(mapWebCodeToWindowKey("Numpad0") == WindowKey::Numpad0);
    assert(mapWebCodeToWindowKey("NumpadAdd") == WindowKey::NumpadAdd);
    assert(mapWebCodeToWindowKey("NumpadSubtract") == WindowKey::NumpadSubtract);
    assert(mapWebCodeToWindowKey("NumpadMultiply") == WindowKey::NumpadMultiply);
    assert(mapWebCodeToWindowKey("NumpadDivide") == WindowKey::NumpadDivide);
    assert(mapWebCodeToWindowKey("NumpadDecimal") == WindowKey::NumpadDecimal);
    assert(mapWebCodeToWindowKey("NumpadEnter") == WindowKey::NumpadEnter);
    assert(mapWebCodeToWindowKey("NumLock") == WindowKey::NumLock);
    assert(mapWebCodeToWindowKey("PrintScreen") == WindowKey::PrintScreen);
    assert(mapWebCodeToWindowKey("ScrollLock") == WindowKey::ScrollLock);
    assert(mapWebCodeToWindowKey("Pause") == WindowKey::Pause);
    assert(mapWebCodeToWindowKey("ContextMenu") == WindowKey::Menu);
    assert(mapWebCodeToWindowKey("IntlBackslash") == WindowKey::NonUsBackslash);
    assert(mapWebCodeToWindowKey("NonUsBackslash") == WindowKey::NonUsBackslash);
    assert(mapWebCodeToWindowKey("F13") == WindowKey::F13);
    assert(mapWebCodeToWindowKey("F24") == WindowKey::F24);
    assert(mapWebCodeToWindowKey("") == WindowKey::Unknown);
    assert(mapWebCodeToWindowKey("Unidentified") == WindowKey::Unknown);
    assert(mapWebCodeToWindowKey("KeyUnknownKey") == WindowKey::Unknown);
}

void assertInputKeyBridge()
{
    using arp::input::InputKey;
    using arp::platform::WindowKey;
    using arp::platform::windowKeyToInputKey;

    InputKey mapped = InputKey::W;
    assert(windowKeyToInputKey(WindowKey::W, mapped) && mapped == InputKey::W);
    assert(windowKeyToInputKey(WindowKey::A, mapped) && mapped == InputKey::A);
    assert(windowKeyToInputKey(WindowKey::S, mapped) && mapped == InputKey::S);
    assert(windowKeyToInputKey(WindowKey::D, mapped) && mapped == InputKey::D);
    assert(windowKeyToInputKey(WindowKey::Space, mapped) && mapped == InputKey::Space);
    assert(windowKeyToInputKey(WindowKey::Escape, mapped) && mapped == InputKey::Escape);
    assert(windowKeyToInputKey(WindowKey::Enter, mapped) && mapped == InputKey::Enter);
    assert(windowKeyToInputKey(WindowKey::ArrowUp, mapped) && mapped == InputKey::ArrowUp);
    assert(windowKeyToInputKey(WindowKey::LeftShift, mapped) && mapped == InputKey::LeftShift);
    assert(windowKeyToInputKey(WindowKey::E, mapped) && mapped == InputKey::E);
    assert(windowKeyToInputKey(WindowKey::F12, mapped) && mapped == InputKey::F12);
    assert(windowKeyToInputKey(WindowKey::NumpadEnter, mapped) && mapped == InputKey::NumpadEnter);
    assert(windowKeyToInputKey(WindowKey::F24, mapped) && mapped == InputKey::F24);

    // Closed vocabulary: every non-sentinel WindowKey maps 1:1 into InputKey;
    // Unknown and Count do not. A WindowKey added without its InputKey mirror
    // or bridge case fails here.
    for (std::uint16_t raw = 1; raw < static_cast<std::uint16_t>(WindowKey::Count); ++raw) {
        const auto key = static_cast<WindowKey>(raw);
        assert(windowKeyToInputKey(key, mapped));
    }
    assert(!windowKeyToInputKey(WindowKey::Unknown, mapped));
    assert(!windowKeyToInputKey(WindowKey::Count, mapped));

    arp::input::InputFrame frame;
    arp::platform::WindowEventBatch batch;
    arp::platform::WindowInputEvent keyDown;
    keyDown.kind = arp::platform::WindowInputEvent::Kind::KeyDown;
    keyDown.windowId = 1;
    keyDown.key = WindowKey::W;
    batch.inputEvents.push_back(keyDown);
    // Unknown is the only key the bridge still drops, and it is counted.
    arp::platform::WindowInputEvent unmapped;
    unmapped.kind = arp::platform::WindowInputEvent::Kind::KeyDown;
    unmapped.windowId = 1;
    unmapped.key = WindowKey::Unknown;
    batch.inputEvents.push_back(unmapped);
    arp::platform::WindowInputEvent mouse;
    mouse.kind = arp::platform::WindowInputEvent::Kind::MouseButtonDown;
    mouse.windowId = 1;
    mouse.mouseButton = arp::platform::WindowMouseButton::Left;
    batch.inputEvents.push_back(mouse);

    const auto stats = arp::platform::applyWindowInputToInputFrame(batch, frame);
    assert(stats.forwardedEvents == 2u);
    assert(stats.droppedKeyEvents == 1u);
    assert(stats.droppedMouseButtonEvents == 0u);
    assert(stats.windowEventsIgnored == 0u);
}

void assertWindowDescriptorContract()
{
    arp::platform::WindowDesc desc;
    assert(desc.width == 1280);
    assert(desc.height == 720);
    assert(!desc.hidden);
    assert(desc.gpuSurface);
    assert(!desc.borderless);
    assert(desc.highPixelDensity);

    // Event batches bind to the shared CPU memory resource and clear to empty.
    arp::platform::WindowEventBatch batch;
    assert(batch.empty());
    batch.windowEvents.push_back({});
    batch.inputEvents.push_back({});
    assert(!batch.empty());
    batch.clear();
    assert(batch.empty());
}

} // namespace

int main()
{
    assertNeutralKeyContract();
    assertWin32VirtualKeyTable();
    assertMacVirtualKeyTable();
    assertX11KeysymTable();
    assertWebCodeTable();
    assertInputKeyBridge();
    assertWindowDescriptorContract();
    return 0;
}
