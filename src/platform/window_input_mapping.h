#pragma once

// Neutral, platform-independent key translation tables for WindowSystem/1.
// Every platform TU converts its native key representation through these pure
// functions, so the mapping contract is testable on any host platform without
// native SDK headers. Unmapped inputs deterministically produce
// WindowKey::Unknown — callers must not guess a substitute.

#include "platform/window.h"

#include <cstdint>

namespace arp::platform {

/// Win32 virtual-key code (the WPARAM of WM_KEYDOWN / WM_KEYUP).
WindowKey mapWin32VirtualKeyToWindowKey(std::uint32_t virtualKey);
/// Win32 key message translation with its lParam scan code and extended bit.
/// Resolves generic VK_SHIFT / VK_CONTROL / VK_MENU to left/right keys.
WindowKey mapWin32KeyMessageToWindowKey(
    std::uint32_t virtualKey, std::uint32_t scanCode, bool extended);

/// macOS AppKit virtual keycode (NSEvent.keyCode, hardware-independent
/// ANSI-layout code).
WindowKey mapMacVirtualKeyToWindowKey(std::uint16_t keyCode);

/// X11 keysym (as returned by xkbcommon's xkb_state_key_get_one_sym).
/// Both lowercase and uppercase letter keysyms map to the same neutral key.
WindowKey mapX11KeysymToWindowKey(std::uint32_t keysym);

/// Web KeyboardEvent.code ("KeyW", "ArrowLeft", "Digit1", ...). A null code
/// or an unmapped code yields WindowKey::Unknown.
WindowKey mapWebCodeToWindowKey(const char* code);

} // namespace arp::platform
