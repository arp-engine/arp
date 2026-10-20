#include "platform/window_input_mapping.h"

#include <cstring>

namespace arp::platform {
namespace {

/// Compares the code after its first character with the given suffix and
/// requires the code to end there ("Space" -> matchRest(code, "pace")).
bool matchRest(const char* code, const char* suffix)
{
    return std::strcmp(code + 1, suffix) == 0;
}

} // namespace

WindowKey mapWin32VirtualKeyToWindowKey(std::uint32_t virtualKey)
{
    // Letters and digits are contiguous ASCII ranges in the Win32 VK table.
    if (virtualKey >= 0x41u && virtualKey <= 0x5Au) { // VK_A .. VK_Z
        return static_cast<WindowKey>(
            static_cast<std::uint32_t>(WindowKey::A) + (virtualKey - 0x41u));
    }
    if (virtualKey >= 0x30u && virtualKey <= 0x39u) { // VK_0 .. VK_9
        return static_cast<WindowKey>(
            static_cast<std::uint32_t>(WindowKey::Num0) + (virtualKey - 0x30u));
    }
    if (virtualKey >= 0x7Cu && virtualKey <= 0x87u) { // VK_F13 .. VK_F24
        return static_cast<WindowKey>(
            static_cast<std::uint32_t>(WindowKey::F13) + (virtualKey - 0x7Cu));
    }

    switch (virtualKey) {
    case 0x20u:
        return WindowKey::Space; // VK_SPACE
    case 0x0Du:
        return WindowKey::Enter; // VK_RETURN
    case 0x09u:
        return WindowKey::Tab; // VK_TAB
    case 0x08u:
        return WindowKey::Backspace; // VK_BACK
    case 0x1Bu:
        return WindowKey::Escape; // VK_ESCAPE
    case 0x2Eu:
        return WindowKey::Delete; // VK_DELETE
    case 0x27u:
        return WindowKey::ArrowRight; // VK_RIGHT
    case 0x25u:
        return WindowKey::ArrowLeft; // VK_LEFT
    case 0x28u:
        return WindowKey::ArrowDown; // VK_DOWN
    case 0x26u:
        return WindowKey::ArrowUp; // VK_UP
    case 0x21u:
        return WindowKey::PageUp; // VK_PRIOR
    case 0x22u:
        return WindowKey::PageDown; // VK_NEXT
    case 0x24u:
        return WindowKey::Home; // VK_HOME
    case 0x23u:
        return WindowKey::End; // VK_END
    case 0x2Du:
        return WindowKey::Insert; // VK_INSERT
    case 0xA0u:
        return WindowKey::LeftShift; // VK_LSHIFT
    case 0xA1u:
        return WindowKey::RightShift; // VK_RSHIFT
    case 0xA2u:
        return WindowKey::LeftControl; // VK_LCONTROL
    case 0xA3u:
        return WindowKey::RightControl; // VK_RCONTROL
    case 0xA4u:
        return WindowKey::LeftAlt; // VK_LMENU
    case 0xA5u:
        return WindowKey::RightAlt; // VK_RMENU
    case 0x5Bu:
        return WindowKey::LeftSuper; // VK_LWIN
    case 0x5Cu:
        return WindowKey::RightSuper; // VK_RWIN
    case 0x14u:
        return WindowKey::CapsLock; // VK_CAPITAL
    case 0x70u:
        return WindowKey::F1; // VK_F1
    case 0x71u:
        return WindowKey::F2; // VK_F2
    case 0x72u:
        return WindowKey::F3; // VK_F3
    case 0x73u:
        return WindowKey::F4; // VK_F4
    case 0x74u:
        return WindowKey::F5; // VK_F5
    case 0x75u:
        return WindowKey::F6; // VK_F6
    case 0x76u:
        return WindowKey::F7; // VK_F7
    case 0x77u:
        return WindowKey::F8; // VK_F8
    case 0x78u:
        return WindowKey::F9; // VK_F9
    case 0x79u:
        return WindowKey::F10; // VK_F10
    case 0x7Au:
        return WindowKey::F11; // VK_F11
    case 0x7Bu:
        return WindowKey::F12; // VK_F12
    case 0xBDu:
        return WindowKey::Minus; // VK_OEM_MINUS
    case 0xBBu:
        return WindowKey::Equals; // VK_OEM_PLUS
    case 0xDBu:
        return WindowKey::LeftBracket; // VK_OEM_4
    case 0xDDu:
        return WindowKey::RightBracket; // VK_OEM_6
    case 0xDCu:
        return WindowKey::Backslash; // VK_OEM_5
    case 0xBAu:
        return WindowKey::Semicolon; // VK_OEM_1
    case 0xDEu:
        return WindowKey::Apostrophe; // VK_OEM_7
    case 0xC0u:
        return WindowKey::Grave; // VK_OEM_3
    case 0xBCu:
        return WindowKey::Comma; // VK_OEM_COMMA
    case 0xBEu:
        return WindowKey::Period; // VK_OEM_PERIOD
    case 0xBFu:
        return WindowKey::Slash; // VK_OEM_2
    case 0x60u:
        return WindowKey::Numpad0; // VK_NUMPAD0
    case 0x61u:
        return WindowKey::Numpad1;
    case 0x62u:
        return WindowKey::Numpad2;
    case 0x63u:
        return WindowKey::Numpad3;
    case 0x64u:
        return WindowKey::Numpad4;
    case 0x65u:
        return WindowKey::Numpad5;
    case 0x66u:
        return WindowKey::Numpad6;
    case 0x67u:
        return WindowKey::Numpad7;
    case 0x68u:
        return WindowKey::Numpad8;
    case 0x69u:
        return WindowKey::Numpad9;
    case 0x6Au:
        return WindowKey::NumpadMultiply; // VK_MULTIPLY
    case 0x6Bu:
        return WindowKey::NumpadAdd; // VK_ADD
    case 0x6Du:
        return WindowKey::NumpadSubtract; // VK_SUBTRACT
    case 0x6Eu:
        return WindowKey::NumpadDecimal; // VK_DECIMAL
    case 0x6Fu:
        return WindowKey::NumpadDivide; // VK_DIVIDE
    case 0x90u:
        return WindowKey::NumLock; // VK_NUMLOCK
    case 0x2Cu:
        return WindowKey::PrintScreen; // VK_SNAPSHOT
    case 0x91u:
        return WindowKey::ScrollLock; // VK_SCROLL
    case 0x13u:
        return WindowKey::Pause; // VK_PAUSE
    case 0x5Du:
        return WindowKey::Menu; // VK_APPS
    case 0xE2u:
        return WindowKey::NonUsBackslash; // VK_OEM_102
    default:
        return WindowKey::Unknown;
    }
}

WindowKey mapWin32KeyMessageToWindowKey(
    std::uint32_t virtualKey, std::uint32_t scanCode, bool extended)
{
    // WM_KEY* messages use generic VK_* values for modifier keys. The
    // extended flag distinguishes right Ctrl/Alt; the scan code distinguishes
    // right Shift (0x36) from left Shift (0x2A).
    if (virtualKey == 0x10u)
        virtualKey = scanCode == 0x36u ? 0xA1u : 0xA0u; // VK_SHIFT
    else if (virtualKey == 0x11u)
        virtualKey = extended ? 0xA3u : 0xA2u; // VK_CONTROL
    else if (virtualKey == 0x12u)
        virtualKey = extended ? 0xA5u : 0xA4u; // VK_MENU
    return mapWin32VirtualKeyToWindowKey(virtualKey);
}

WindowKey mapMacVirtualKeyToWindowKey(std::uint16_t keyCode)
{
    // Hardware-independent ANSI-layout AppKit keycodes (kVK_ANSI_*).
    switch (keyCode) {
    case 0x00u:
        return WindowKey::A;
    case 0x0Bu:
        return WindowKey::B;
    case 0x08u:
        return WindowKey::C;
    case 0x02u:
        return WindowKey::D;
    case 0x0Eu:
        return WindowKey::E;
    case 0x03u:
        return WindowKey::F;
    case 0x05u:
        return WindowKey::G;
    case 0x04u:
        return WindowKey::H;
    case 0x22u:
        return WindowKey::I;
    case 0x26u:
        return WindowKey::J;
    case 0x28u:
        return WindowKey::K;
    case 0x25u:
        return WindowKey::L;
    case 0x2Eu:
        return WindowKey::M;
    case 0x2Du:
        return WindowKey::N;
    case 0x1Fu:
        return WindowKey::O;
    case 0x23u:
        return WindowKey::P;
    case 0x0Cu:
        return WindowKey::Q;
    case 0x0Fu:
        return WindowKey::R;
    case 0x01u:
        return WindowKey::S;
    case 0x11u:
        return WindowKey::T;
    case 0x20u:
        return WindowKey::U;
    case 0x09u:
        return WindowKey::V;
    case 0x0Du:
        return WindowKey::W;
    case 0x07u:
        return WindowKey::X;
    case 0x10u:
        return WindowKey::Y;
    case 0x06u:
        return WindowKey::Z;
    case 0x1Du:
        return WindowKey::Num0;
    case 0x12u:
        return WindowKey::Num1;
    case 0x13u:
        return WindowKey::Num2;
    case 0x14u:
        return WindowKey::Num3;
    case 0x15u:
        return WindowKey::Num4;
    case 0x17u:
        return WindowKey::Num5;
    case 0x16u:
        return WindowKey::Num6;
    case 0x1Au:
        return WindowKey::Num7;
    case 0x1Cu:
        return WindowKey::Num8;
    case 0x19u:
        return WindowKey::Num9;
    case 0x31u:
        return WindowKey::Space;
    case 0x24u:
        return WindowKey::Enter;
    case 0x30u:
        return WindowKey::Tab;
    case 0x33u:
        return WindowKey::Backspace;
    case 0x35u:
        return WindowKey::Escape;
    case 0x75u:
        return WindowKey::Delete;
    case 0x7Cu:
        return WindowKey::ArrowRight;
    case 0x7Bu:
        return WindowKey::ArrowLeft;
    case 0x7Du:
        return WindowKey::ArrowDown;
    case 0x7Eu:
        return WindowKey::ArrowUp;
    case 0x74u:
        return WindowKey::PageUp;
    case 0x79u:
        return WindowKey::PageDown;
    case 0x73u:
        return WindowKey::Home;
    case 0x77u:
        return WindowKey::End;
    case 0x38u:
        return WindowKey::LeftShift;
    case 0x3Cu:
        return WindowKey::RightShift;
    case 0x3Bu:
        return WindowKey::LeftControl;
    case 0x3Eu:
        return WindowKey::RightControl;
    case 0x3Au:
        return WindowKey::LeftAlt;
    case 0x3Du:
        return WindowKey::RightAlt;
    case 0x37u:
        return WindowKey::LeftSuper; // Left command
    case 0x36u:
        return WindowKey::RightSuper; // Right command
    case 0x39u:
        return WindowKey::CapsLock;
    case 0x7Au:
        return WindowKey::F1;
    case 0x78u:
        return WindowKey::F2;
    case 0x63u:
        return WindowKey::F3;
    case 0x76u:
        return WindowKey::F4;
    case 0x60u:
        return WindowKey::F5;
    case 0x61u:
        return WindowKey::F6;
    case 0x62u:
        return WindowKey::F7;
    case 0x64u:
        return WindowKey::F8;
    case 0x65u:
        return WindowKey::F9;
    case 0x6Du:
        return WindowKey::F10;
    case 0x67u:
        return WindowKey::F11;
    case 0x6Fu:
        return WindowKey::F12;
    case 0x1Bu:
        return WindowKey::Minus;
    case 0x18u:
        return WindowKey::Equals;
    case 0x21u:
        return WindowKey::LeftBracket;
    case 0x1Eu:
        return WindowKey::RightBracket;
    case 0x2Au:
        return WindowKey::Backslash;
    case 0x29u:
        return WindowKey::Semicolon;
    case 0x27u:
        return WindowKey::Apostrophe;
    case 0x32u:
        return WindowKey::Grave;
    case 0x2Bu:
        return WindowKey::Comma;
    case 0x2Fu:
        return WindowKey::Period;
    case 0x2Cu:
        return WindowKey::Slash;
    case 0x52u:
        return WindowKey::Numpad0; // kVK_ANSI_Keypad0
    case 0x53u:
        return WindowKey::Numpad1;
    case 0x54u:
        return WindowKey::Numpad2;
    case 0x55u:
        return WindowKey::Numpad3;
    case 0x56u:
        return WindowKey::Numpad4;
    case 0x57u:
        return WindowKey::Numpad5;
    case 0x58u:
        return WindowKey::Numpad6;
    case 0x59u:
        return WindowKey::Numpad7;
    case 0x5Bu:
        return WindowKey::Numpad8; // kVK_ANSI_Keypad8
    case 0x5Cu:
        return WindowKey::Numpad9; // kVK_ANSI_Keypad9
    case 0x41u:
        return WindowKey::NumpadDecimal; // kVK_ANSI_KeypadDecimal
    case 0x43u:
        return WindowKey::NumpadMultiply; // kVK_ANSI_KeypadMultiply
    case 0x45u:
        return WindowKey::NumpadAdd; // kVK_ANSI_KeypadPlus
    case 0x4Bu:
        return WindowKey::NumpadDivide; // kVK_ANSI_KeypadDivide
    case 0x4Cu:
        return WindowKey::NumpadEnter; // kVK_ANSI_KeypadEnter
    case 0x4Eu:
        return WindowKey::NumpadSubtract; // kVK_ANSI_KeypadMinus
    case 0x69u:
        return WindowKey::F13; // kVK_F13
    case 0x6Bu:
        return WindowKey::F14; // kVK_F14
    case 0x71u:
        return WindowKey::F15; // kVK_F15
    case 0x6Au:
        return WindowKey::F16; // kVK_F16
    case 0x40u:
        return WindowKey::F17; // kVK_F17
    case 0x4Fu:
        return WindowKey::F18; // kVK_F18
    case 0x50u:
        return WindowKey::F19; // kVK_F19
    case 0x5Au:
        return WindowKey::F20; // kVK_F20
    default:
        return WindowKey::Unknown;
    }
}

WindowKey mapX11KeysymToWindowKey(std::uint32_t keysym)
{
    // Lowercase and uppercase Latin-1 letter keysyms are ASCII-valued.
    if (keysym >= 0x61u && keysym <= 0x7Au) {
        return static_cast<WindowKey>(static_cast<std::uint32_t>(WindowKey::A) + (keysym - 0x61u));
    }
    if (keysym >= 0x41u && keysym <= 0x5Au) {
        return static_cast<WindowKey>(static_cast<std::uint32_t>(WindowKey::A) + (keysym - 0x41u));
    }
    if (keysym >= 0x30u && keysym <= 0x39u) { // digits share ASCII values
        return static_cast<WindowKey>(
            static_cast<std::uint32_t>(WindowKey::Num0) + (keysym - 0x30u));
    }
    if (keysym >= 0xFFB0u && keysym <= 0xFFB9u) { // XK_KP_0 .. XK_KP_9
        return static_cast<WindowKey>(
            static_cast<std::uint32_t>(WindowKey::Numpad0) + (keysym - 0xFFB0u));
    }
    if (keysym >= 0xFFCAu && keysym <= 0xFFD5u) { // XK_F13 .. XK_F24
        return static_cast<WindowKey>(
            static_cast<std::uint32_t>(WindowKey::F13) + (keysym - 0xFFCAu));
    }

    switch (keysym) {
    case 0x0020u:
        return WindowKey::Space; // XK_space
    case 0xFF0Du:
        return WindowKey::Enter; // XK_Return
    case 0xFF09u:
        return WindowKey::Tab; // XK_Tab
    case 0xFF08u:
        return WindowKey::Backspace; // XK_BackSpace
    case 0xFF1Bu:
        return WindowKey::Escape; // XK_Escape
    case 0xFFFFu:
        return WindowKey::Delete; // XK_Delete
    case 0xFF53u:
        return WindowKey::ArrowRight; // XK_Right
    case 0xFF51u:
        return WindowKey::ArrowLeft; // XK_Left
    case 0xFF54u:
        return WindowKey::ArrowDown; // XK_Down
    case 0xFF52u:
        return WindowKey::ArrowUp; // XK_Up
    case 0xFF55u:
        return WindowKey::PageUp; // XK_Page_Up
    case 0xFF56u:
        return WindowKey::PageDown; // XK_Page_Down
    case 0xFF50u:
        return WindowKey::Home; // XK_Home
    case 0xFF57u:
        return WindowKey::End; // XK_End
    case 0xFF63u:
        return WindowKey::Insert; // XK_Insert
    case 0xFFE1u:
        return WindowKey::LeftShift; // XK_Shift_L
    case 0xFFE2u:
        return WindowKey::RightShift; // XK_Shift_R
    case 0xFFE3u:
        return WindowKey::LeftControl; // XK_Control_L
    case 0xFFE4u:
        return WindowKey::RightControl; // XK_Control_R
    case 0xFFE9u:
        return WindowKey::LeftAlt; // XK_Alt_L
    case 0xFFEAu:
        return WindowKey::RightAlt; // XK_Alt_R
    case 0xFFE7u:
        return WindowKey::LeftSuper; // XK_Meta_L
    case 0xFFE8u:
        return WindowKey::RightSuper; // XK_Meta_R
    case 0xFFE5u:
        return WindowKey::CapsLock; // XK_Caps_Lock
    case 0xFFBEu:
        return WindowKey::F1; // XK_F1
    case 0xFFBFu:
        return WindowKey::F2;
    case 0xFFC0u:
        return WindowKey::F3;
    case 0xFFC1u:
        return WindowKey::F4;
    case 0xFFC2u:
        return WindowKey::F5;
    case 0xFFC3u:
        return WindowKey::F6;
    case 0xFFC4u:
        return WindowKey::F7;
    case 0xFFC5u:
        return WindowKey::F8;
    case 0xFFC6u:
        return WindowKey::F9;
    case 0xFFC7u:
        return WindowKey::F10;
    case 0xFFC8u:
        return WindowKey::F11;
    case 0xFFC9u:
        return WindowKey::F12;
    case 0x002Du:
        return WindowKey::Minus; // XK_minus
    case 0x003Du:
        return WindowKey::Equals; // XK_equal
    case 0x005Bu:
        return WindowKey::LeftBracket; // XK_bracketleft
    case 0x005Du:
        return WindowKey::RightBracket; // XK_bracketright
    case 0x005Cu:
        return WindowKey::Backslash; // XK_backslash
    case 0x003Bu:
        return WindowKey::Semicolon; // XK_semicolon
    case 0x0027u:
        return WindowKey::Apostrophe; // XK_apostrophe
    case 0x0060u:
        return WindowKey::Grave; // XK_grave
    case 0x002Cu:
        return WindowKey::Comma; // XK_comma
    case 0x002Eu:
        return WindowKey::Period; // XK_period
    case 0x002Fu:
        return WindowKey::Slash; // XK_slash
    case 0xFF8Du:
        return WindowKey::NumpadEnter; // XK_KP_Enter
    case 0xFFAAu:
        return WindowKey::NumpadMultiply; // XK_KP_Multiply
    case 0xFFABu:
        return WindowKey::NumpadAdd; // XK_KP_Add
    case 0xFFADu:
        return WindowKey::NumpadSubtract; // XK_KP_Subtract
    case 0xFFAEu:
        return WindowKey::NumpadDecimal; // XK_KP_Decimal
    case 0xFFAFu:
        return WindowKey::NumpadDivide; // XK_KP_Divide
    case 0xFF61u:
        return WindowKey::PrintScreen; // XK_Print
    case 0xFF14u:
        return WindowKey::ScrollLock; // XK_Scroll_Lock
    case 0xFF13u:
        return WindowKey::Pause; // XK_Pause
    case 0xFF67u:
        return WindowKey::Menu; // XK_Menu
    case 0xFF7Fu:
        return WindowKey::NumLock; // XK_Num_Lock
    default:
        return WindowKey::Unknown;
    }
}

WindowKey mapWebCodeToWindowKey(const char* code)
{
    if (code == nullptr || code[0] == '\0') {
        return WindowKey::Unknown;
    }
    // KeyboardEvent.code layout identifiers ("KeyW", "Digit1", "ArrowLeft").
    if (code[0] == 'K' && code[1] == 'e' && code[2] == 'y' && code[3] != '\0' && code[4] == '\0') {
        const char letter = code[3];
        if (letter >= 'A' && letter <= 'Z') {
            return static_cast<WindowKey>(static_cast<std::uint32_t>(WindowKey::A)
                + static_cast<std::uint32_t>(letter - 'A'));
        }
        return WindowKey::Unknown;
    }
    if (code[0] == 'D' && code[1] == 'i' && code[2] == 'g' && code[3] == 'i' && code[4] == 't'
        && code[5] != '\0' && code[6] == '\0') {
        const char digit = code[5];
        if (digit >= '0' && digit <= '9') {
            return static_cast<WindowKey>(static_cast<std::uint32_t>(WindowKey::Num0)
                + static_cast<std::uint32_t>(digit - '0'));
        }
        return WindowKey::Unknown;
    }

    switch (code[0]) {
    case 'S':
        if (matchRest(code, "pace"))
            return WindowKey::Space;
        if (matchRest(code, "hiftLeft"))
            return WindowKey::LeftShift;
        if (matchRest(code, "hiftRight"))
            return WindowKey::RightShift;
        if (matchRest(code, "emicolon"))
            return WindowKey::Semicolon;
        if (matchRest(code, "lash"))
            return WindowKey::Slash;
        if (matchRest(code, "crollLock"))
            return WindowKey::ScrollLock;
        break;
    case 'E':
        if (matchRest(code, "nter"))
            return WindowKey::Enter;
        if (matchRest(code, "scape"))
            return WindowKey::Escape;
        if (matchRest(code, "quals"))
            return WindowKey::Equals;
        if (matchRest(code, "nd"))
            return WindowKey::End;
        break;
    case 'A':
        if (matchRest(code, "rrowRight"))
            return WindowKey::ArrowRight;
        if (matchRest(code, "rrowLeft"))
            return WindowKey::ArrowLeft;
        if (matchRest(code, "rrowDown"))
            return WindowKey::ArrowDown;
        if (matchRest(code, "rrowUp"))
            return WindowKey::ArrowUp;
        if (matchRest(code, "ltLeft"))
            return WindowKey::LeftAlt;
        if (matchRest(code, "ltRight"))
            return WindowKey::RightAlt;
        if (matchRest(code, "postrophe"))
            return WindowKey::Apostrophe;
        break;
    case 'T':
        if (matchRest(code, "ab"))
            return WindowKey::Tab;
        break;
    case 'B':
        if (matchRest(code, "ackspace"))
            return WindowKey::Backspace;
        if (matchRest(code, "racketLeft"))
            return WindowKey::LeftBracket;
        if (matchRest(code, "racketRight"))
            return WindowKey::RightBracket;
        if (matchRest(code, "ackslash"))
            return WindowKey::Backslash;
        break;
    case 'C':
        if (matchRest(code, "ontrolLeft"))
            return WindowKey::LeftControl;
        if (matchRest(code, "ontrolRight"))
            return WindowKey::RightControl;
        if (matchRest(code, "apsLock"))
            return WindowKey::CapsLock;
        if (matchRest(code, "omma"))
            return WindowKey::Comma;
        if (matchRest(code, "ontextMenu"))
            return WindowKey::Menu;
        break;
    case 'D':
        if (matchRest(code, "elete"))
            return WindowKey::Delete;
        break;
    case 'P':
        if (matchRest(code, "ageUp"))
            return WindowKey::PageUp;
        if (matchRest(code, "ageDown"))
            return WindowKey::PageDown;
        if (matchRest(code, "eriod"))
            return WindowKey::Period;
        if (matchRest(code, "rintScreen"))
            return WindowKey::PrintScreen;
        if (matchRest(code, "ause"))
            return WindowKey::Pause;
        break;
    case 'H':
        if (matchRest(code, "ome"))
            return WindowKey::Home;
        break;
    case 'I':
        if (matchRest(code, "nsert"))
            return WindowKey::Insert;
        if (matchRest(code, "ntlBackslash"))
            return WindowKey::NonUsBackslash;
        break;
    case 'M':
        if (matchRest(code, "inus"))
            return WindowKey::Minus;
        if (matchRest(code, "etaLeft"))
            return WindowKey::LeftSuper;
        if (matchRest(code, "etaRight"))
            return WindowKey::RightSuper;
        break;
    case 'F':
        if (matchRest(code, "1"))
            return WindowKey::F1;
        if (matchRest(code, "2"))
            return WindowKey::F2;
        if (matchRest(code, "3"))
            return WindowKey::F3;
        if (matchRest(code, "4"))
            return WindowKey::F4;
        if (matchRest(code, "5"))
            return WindowKey::F5;
        if (matchRest(code, "6"))
            return WindowKey::F6;
        if (matchRest(code, "7"))
            return WindowKey::F7;
        if (matchRest(code, "8"))
            return WindowKey::F8;
        if (matchRest(code, "9"))
            return WindowKey::F9;
        if (matchRest(code, "10"))
            return WindowKey::F10;
        if (matchRest(code, "11"))
            return WindowKey::F11;
        if (matchRest(code, "12"))
            return WindowKey::F12;
        if (matchRest(code, "13"))
            return WindowKey::F13;
        if (matchRest(code, "14"))
            return WindowKey::F14;
        if (matchRest(code, "15"))
            return WindowKey::F15;
        if (matchRest(code, "16"))
            return WindowKey::F16;
        if (matchRest(code, "17"))
            return WindowKey::F17;
        if (matchRest(code, "18"))
            return WindowKey::F18;
        if (matchRest(code, "19"))
            return WindowKey::F19;
        if (matchRest(code, "20"))
            return WindowKey::F20;
        if (matchRest(code, "21"))
            return WindowKey::F21;
        if (matchRest(code, "22"))
            return WindowKey::F22;
        if (matchRest(code, "23"))
            return WindowKey::F23;
        if (matchRest(code, "24"))
            return WindowKey::F24;
        break;
    case 'G':
        if (matchRest(code, "raveAccent"))
            return WindowKey::Grave;
        break;
    case 'N':
        if (matchRest(code, "umpad0"))
            return WindowKey::Numpad0;
        if (matchRest(code, "umpad1"))
            return WindowKey::Numpad1;
        if (matchRest(code, "umpad2"))
            return WindowKey::Numpad2;
        if (matchRest(code, "umpad3"))
            return WindowKey::Numpad3;
        if (matchRest(code, "umpad4"))
            return WindowKey::Numpad4;
        if (matchRest(code, "umpad5"))
            return WindowKey::Numpad5;
        if (matchRest(code, "umpad6"))
            return WindowKey::Numpad6;
        if (matchRest(code, "umpad7"))
            return WindowKey::Numpad7;
        if (matchRest(code, "umpad8"))
            return WindowKey::Numpad8;
        if (matchRest(code, "umpad9"))
            return WindowKey::Numpad9;
        if (matchRest(code, "umpadAdd"))
            return WindowKey::NumpadAdd;
        if (matchRest(code, "umpadSubtract"))
            return WindowKey::NumpadSubtract;
        if (matchRest(code, "umpadMultiply"))
            return WindowKey::NumpadMultiply;
        if (matchRest(code, "umpadDivide"))
            return WindowKey::NumpadDivide;
        if (matchRest(code, "umpadDecimal"))
            return WindowKey::NumpadDecimal;
        if (matchRest(code, "umpadEnter"))
            return WindowKey::NumpadEnter;
        if (matchRest(code, "umLock"))
            return WindowKey::NumLock;
        if (matchRest(code, "onUsBackslash"))
            return WindowKey::NonUsBackslash;
        break;
    default:
        break;
    }
    return WindowKey::Unknown;
}

} // namespace arp::platform
