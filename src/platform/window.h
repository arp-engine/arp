#pragma once

// WindowSystem/1 — the L2 platform window, display, event and GPU-surface
// contract. This header replaces the former third-party windowing facade; it
// carries no Win32, Cocoa or X11 types so upper layers never observe a native
// window handle.
//
// Per-platform implementations live in exactly one translation unit per
// build: window_win32.cpp (Windows), window_cocoa.mm (Apple desktop),
// window_xcb.cpp (Linux X11), window_web.cpp (Emscripten). Each TU defines
// every out-of-line member below plus WindowSystemState.
//
// Lifetime rule: PlatformWindow / PlatformVulkanSurface instances must not
// outlive their creating WindowSystem (the previous facade had the same
// global coupling, made explicit here). A default-constructed or moved-from
// PlatformWindow reports invalid() / zero extents.
//
// Thread rule: WindowSystem is main-thread-only, matching AppKit/Win32
// windowing. Callers pump events explicitly on the thread that owns the
// window; no hidden event thread is spawned.

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "platform/platform_lifecycle_state.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct VkInstance_T;
struct VkSurfaceKHR_T;
using VkInstance = VkInstance_T*;
using VkSurfaceKHR = VkSurfaceKHR_T*;

namespace arp::platform {

/// Per-platform WindowSystem state. Forward-declared here, defined in the
/// single platform implementation TU of each build (window_win32.cpp,
/// window_cocoa.mm, window_xcb.cpp or window_web.cpp). The type is opaque to
/// every consumer; only the platform TU may interpret it.
struct WindowSystemState;

struct PlatformResult {
    bool success = false;
    std::pmr::string diagnostics;
};

struct WindowDesc {
    std::pmr::string title;
    int width = 1280;
    int height = 720;
    bool hidden = false;
    /// Request a GPU-presentable native surface: a Vulkan surface on Windows /
    /// Linux, a CAMetalLayer-backed view on Apple, the browser canvas on
    /// Emscripten.
    bool gpuSurface = true;
    /// Create the window without decorations (no title bar / borders).
    /// Callers pair this with primaryDisplayBounds() for borderless-fullscreen
    /// desktop coverage.
    bool borderless = false;
    /// Request a high-density backing store where the platform supports it:
    /// pixel extents = logical extents x pixelDensity(). Platforms without a
    /// per-window density report 1.0 and pixel == logical.
    bool highPixelDensity = true;
};

enum class WindowEventType : std::uint8_t {
    FocusGained,
    FocusLost,
    CloseRequested,
    Resized,
    Minimized,
    Restored,
};

struct WindowEvent {
    WindowEventType type = WindowEventType::FocusGained;
    std::uint64_t windowId = 0;
    /// Resized only: the new logical extent.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Neutral keyboard key set. Each platform implementation maps its native
/// key representation through window_input_mapping.h; keys outside this set
/// are dropped deterministically. The enumerators form a closed vocabulary:
/// every non-Unknown WindowKey has an InputKey equivalent (input/input.h) so
/// the input bridge forwards every mapped key. Platform tables map only
/// keys whose native representation is unambiguous — layout-dependent OEM
/// keys (VK_OEM_8 and similar) stay unmapped instead of guessing.
enum class WindowKey : std::uint16_t {
    Unknown = 0,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Space,
    Enter,
    Tab,
    Backspace,
    Escape,
    Delete,
    ArrowRight,
    ArrowLeft,
    ArrowDown,
    ArrowUp,
    PageUp,
    PageDown,
    Home,
    End,
    Insert,
    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    LeftSuper,
    RightSuper,
    CapsLock,
    NumLock,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,
    /// Keypad block: digits and operators as distinct keys, independent of
    /// the top-row digits and of NumLock state.
    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,
    NumpadDecimal,
    NumpadEnter,
    /// Locked / system keys.
    PrintScreen,
    ScrollLock,
    Pause,
    /// Context-menu key and the ISO 102nd key (extra backslash next to the
    /// left shift on ISO layouts).
    Menu,
    NonUsBackslash,
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    /// Sentinel: one past the last key. Never a mapped value.
    Count,
};

enum class WindowMouseButton : std::uint8_t {
    Left,
    Right,
    Middle,
    X1,
    X2,
};

struct WindowInputEvent {
    enum class Kind : std::uint8_t {
        KeyDown,
        KeyUp,
        MouseButtonDown,
        MouseButtonUp,
        MouseMoved,
        PointerMoved,
        MouseWheel,
        TextInput,
        FocusLost,
    };

    Kind kind = Kind::KeyDown;
    std::uint64_t windowId = 0;
    WindowKey key = WindowKey::Unknown;
    WindowMouseButton mouseButton = WindowMouseButton::Left;
    /// PointerMoved: absolute client position in logical coordinates,
    /// origin top-left.
    float x = 0.0f;
    float y = 0.0f;
    bool hasPosition = false;
    /// MouseMoved: delta since the previous pointer position.
    float dx = 0.0f;
    float dy = 0.0f;
    /// MouseWheel: positive away from the user, in wheel-notch units.
    float wheelDelta = 0.0f;
    /// TextInput: UTF-8 text produced by the platform key translation.
    std::pmr::string text;
};

struct WindowEventBatch {
    std::pmr::vector<WindowEvent> windowEvents{&core::defaultCpuMemoryResource()};
    std::pmr::vector<WindowInputEvent> inputEvents{&core::defaultCpuMemoryResource()};

    // Preserve lifecycle cancellation at its actual place in the input stream.
    void addWindowEvent(WindowEvent event)
    {
        windowEvents.push_back(event);
        if (event.type == WindowEventType::FocusLost || event.type == WindowEventType::Minimized
            || event.type == WindowEventType::CloseRequested) {
            WindowInputEvent barrier;
            barrier.kind = WindowInputEvent::Kind::FocusLost;
            barrier.windowId = event.windowId;
            inputEvents.push_back(std::move(barrier));
        }
    }

    void clear()
    {
        windowEvents.clear();
        inputEvents.clear();
    }

    bool empty() const { return windowEvents.empty() && inputEvents.empty(); }
};

class ARP_API PlatformWindow {
public:
    PlatformWindow() = default;
    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;
    PlatformWindow(PlatformWindow&& other) noexcept;
    PlatformWindow& operator=(PlatformWindow&& other) noexcept;
    ~PlatformWindow();

    bool valid() const;
    /// Stable per-WindowSystem identifier, assigned at creation. Event
    /// batches reference windows through this id.
    std::uint64_t id() const;
    /// Logical extent in platform points / DIPs.
    int width() const;
    int height() const;
    /// Physical backing-store extent (logical x pixelDensity, rounded by the
    /// platform's deterministic rule).
    int pixelWidth() const;
    int pixelHeight() const;
    /// Logical-to-pixel scale factor; 1.0 when the platform reports none.
    float pixelDensity() const;
    bool hidden() const;
    PlatformSurfaceKind surfaceKind() const;
    /// Resize the client area to the given logical extent. The cached extents
    /// update synchronously; use sync() before relying on OS-visible state.
    void resize(int width, int height);
    void show();
    void hide();
    void raise();
    void minimize();
    void restore();
    /// Commit pending window geometry / state changes with the platform
    /// compositor. On Windows and X11 the preceding calls are already
    /// synchronous and this returns success; on Cocoa it flushes the pending
    /// AppKit transaction and drains the run loop once.
    PlatformResult sync();
    /// Opaque GPU-surface token (CAMetalLayer on Apple). Empty on platforms
    /// without a token-producing surface (Windows / Linux create Vulkan
    /// surfaces through WindowSystem instead) or when gpuSurface was false.
    std::shared_ptr<void> surfaceToken() const;

private:
    friend class WindowSystem;

    PlatformWindow(WindowSystemState* state, std::uint64_t windowId) noexcept;

    void reset();

    /// WindowSystemState back reference; never null for a window created by
    /// WindowSystem::createWindow.
    WindowSystemState* systemState_ = nullptr;
    std::uint64_t windowId_ = 0;
};

class ARP_API PlatformVulkanSurface {
public:
    PlatformVulkanSurface() = default;
    PlatformVulkanSurface(const PlatformVulkanSurface&) = delete;
    PlatformVulkanSurface& operator=(const PlatformVulkanSurface&) = delete;
    PlatformVulkanSurface(PlatformVulkanSurface&& other) noexcept;
    PlatformVulkanSurface& operator=(PlatformVulkanSurface&& other) noexcept;
    ~PlatformVulkanSurface();

    bool valid() const;
    VkSurfaceKHR handle() const;

private:
    friend class WindowSystem;

    void reset();

    VkInstance instance_ = nullptr;
    VkSurfaceKHR surface_ = nullptr;
    /// PFN_vkDestroySurfaceKHR, type-erased so this header stays free of
    /// Vulkan headers. Resolved at creation from the same loader instance.
    void* destroySurfaceProc_ = nullptr;
};

class ARP_API WindowSystem {
public:
    WindowSystem() = default;
    WindowSystem(const WindowSystem&) = delete;
    WindowSystem& operator=(const WindowSystem&) = delete;
    ~WindowSystem();

    /// Bring up the platform video / window-server connection. Calling this
    /// twice is a no-op returning success.
    PlatformResult initialize();
    PlatformWindow createWindow(const WindowDesc& desc);
    /// Primary display desktop bounds in top-left-origin logical screen
    /// coordinates. Returns false when the platform cannot query displays
    /// (e.g. Emscripten, where the browser owns sizing).
    bool primaryDisplayBounds(int& outX, int& outY, int& outWidth, int& outHeight);
    /// Drain pending OS events without blocking. Appends to the batch and
    /// returns the total number of collected events.
    std::size_t pumpEvents(WindowEventBatch& outBatch);
    /// Block up to timeoutMs waiting for OS events, then drain. success=false
    /// is reserved for platform-level failure; a timeout is a successful
    /// empty drain.
    PlatformResult waitForEvents(std::uint32_t timeoutMs, WindowEventBatch& outBatch);
    /// Vulkan instance extensions required to create surfaces for this
    /// platform's windows. Empty on platforms without a Vulkan path (Apple,
    /// Emscripten). Valid after initialize().
    std::pmr::vector<std::pmr::string> requiredVulkanInstanceExtensions() const;
    /// Create a Vulkan surface for the window. Fails closed (invalid surface)
    /// when the loader, window or surface entry points are unavailable.
    PlatformVulkanSurface createVulkanSurface(
        const PlatformWindow& window, VkInstance instance) const;
    /// Platform Vulkan loader entry point (vkGetInstanceProcAddr) as a
    /// type-erased void* so this header stays free of Vulkan headers; null
    /// when the loader is unavailable. Null on Apple / Emscripten.
    void* vulkanGetInstanceProcAddr() const;

private:
    friend class PlatformWindow;

    arp::core::UniquePtr<WindowSystemState> state_;
    bool initialized_ = false;
};

} // namespace arp::platform
