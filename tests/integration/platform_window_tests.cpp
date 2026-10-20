// WindowSystem/1 integration tests: window creation, extents, resize,
// display bounds, the Vulkan surface path and — on Windows — synthetic
// keyboard/mouse message delivery through the real WndProc. Runs against the
// real platform implementation; platforms without a display/video connection
// exit early with a diagnostic rather than failing.
#include "platform/window.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "2d/runtime_contract.h"

#include <cassert>
#include <cmath>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#if defined(_WIN32)
bool hasKeyDown(const arp::platform::WindowEventBatch& batch, arp::platform::WindowKey key)
{
    for (const auto& event : batch.inputEvents) {
        if (event.kind == arp::platform::WindowInputEvent::Kind::KeyDown && event.key == key) {
            return true;
        }
    }
    return false;
}

bool hasKeyUp(const arp::platform::WindowEventBatch& batch, arp::platform::WindowKey key)
{
    for (const auto& event : batch.inputEvents) {
        if (event.kind == arp::platform::WindowInputEvent::Kind::KeyUp && event.key == key) {
            return true;
        }
    }
    return false;
}

const arp::platform::WindowInputEvent* findButton(const arp::platform::WindowEventBatch& batch,
    arp::platform::WindowInputEvent::Kind kind,
    arp::platform::WindowMouseButton button)
{
    for (const auto& event : batch.inputEvents) {
        if (event.kind == kind && event.mouseButton == button) {
            return &event;
        }
    }
    return nullptr;
}

bool hasPointerAt(const arp::platform::WindowEventBatch& batch, float x, float y)
{
    for (const auto& event : batch.inputEvents) {
        if (event.kind == arp::platform::WindowInputEvent::Kind::PointerMoved && event.x == x
            && event.y == y) {
            return true;
        }
    }
    return false;
}

bool hasMouseDelta(const arp::platform::WindowEventBatch& batch, float dx, float dy)
{
    for (const auto& event : batch.inputEvents) {
        if (event.kind == arp::platform::WindowInputEvent::Kind::MouseMoved && event.dx == dx
            && event.dy == dy) {
            return true;
        }
    }
    return false;
}

void runWin32SyntheticInputTest(
    arp::platform::WindowSystem& platform, arp::platform::PlatformWindow& window)
{
    // The L2 contract exposes no native handle; the behavioral test reaches
    // the real WndProc through the window's stable class/title pair.
    const HWND hwnd = FindWindowW(L"ArpWindowSystemWindowClass", L"ARP Platform Window Test");
    assert(hwnd != nullptr);

    const float density = window.pixelDensity();

    arp::platform::WindowEventBatch scratch;
    platform.pumpEvents(scratch);
    scratch.clear();

    // Key coverage: E previously had no InputKey and was dropped.
    PostMessageW(hwnd, WM_KEYDOWN, 0x45 /* VK_E */, 0);
    PostMessageW(hwnd, WM_KEYUP, 0x45, 0);
    // Keypad Enter shares VK_RETURN with the main Enter; the KF_EXTENDED bit
    // (lParam bit 24) must select WindowKey::NumpadEnter.
    PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, static_cast<LPARAM>(1) << 24);
    PostMessageW(hwnd,
        WM_KEYUP,
        VK_RETURN,
        (static_cast<LPARAM>(1) << 24) | (static_cast<LPARAM>(1) << 30)
            | (static_cast<LPARAM>(1) << 31));
    // Auto-repeat remains in the ordered platform stream; action routing
    // decides whether repeats trigger a semantic action.
    PostMessageW(hwnd, WM_KEYDOWN, 0x57 /* VK_W */, static_cast<LPARAM>(1) << 30);

    // Drag: press, two moves, a move outside the client area (negative client
    // coordinates are valid while captured), then release outside.
    PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
    PostMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(20, 25));
    PostMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(50, 25));
    PostMessageW(
        hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(static_cast<WORD>(static_cast<short>(-5)), 30));
    PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(static_cast<WORD>(static_cast<short>(-5)), 30));

    arp::platform::WindowEventBatch batch;
    platform.pumpEvents(batch);

    assert(hasKeyDown(batch, arp::platform::WindowKey::E));
    assert(hasKeyUp(batch, arp::platform::WindowKey::E));
    assert(hasKeyDown(batch, arp::platform::WindowKey::NumpadEnter));
    assert(hasKeyUp(batch, arp::platform::WindowKey::NumpadEnter));
    assert(hasKeyDown(batch, arp::platform::WindowKey::W));

    const auto* press = findButton(batch,
        arp::platform::WindowInputEvent::Kind::MouseButtonDown,
        arp::platform::WindowMouseButton::Left);
    assert(press != nullptr);
    assert(press->x == 10.0f / density);
    assert(press->y == 10.0f / density);

    // Button events carry the message's own coordinates: the release arrives
    // outside the client area and must not report a stale position.
    const auto* release = findButton(batch,
        arp::platform::WindowInputEvent::Kind::MouseButtonUp,
        arp::platform::WindowMouseButton::Left);
    assert(release != nullptr);
    assert(release->x == -5.0f / density);
    assert(release->y == 30.0f / density);

    assert(hasPointerAt(batch, 20.0f / density, 25.0f / density));
    assert(hasPointerAt(batch, 50.0f / density, 25.0f / density));
    assert(hasPointerAt(batch, -5.0f / density, 30.0f / density));
    assert(hasMouseDelta(batch, 30.0f / density, 0.0f));

    batch.clear();
}

void runWin32DpiTransitionTest(
    arp::platform::WindowSystem& platform, arp::platform::PlatformWindow& window)
{
    const auto hwnd = FindWindowW(L"ArpWindowSystemWindowClass", L"ARP Platform Window Test");
    assert(hwnd);
    RECT original;
    assert(GetWindowRect(hwnd, &original));
    const auto originalDpi = UINT(std::lround(window.pixelDensity() * 96));
    for (const UINT dpi : {144u, 192u}) {
        // Synthetic OS message exercises the real WndProc without changing
        // the user's display settings or pretending to move physical monitors.
        RECT suggested{original.left, original.top, original.left + 1920, original.top + 1080};
        SendMessageW(
            hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        arp::platform::WindowEventBatch batch;
        platform.pumpEvents(batch);
        assert(std::abs(window.pixelDensity() - float(dpi) / 96) < .0001f);
        assert(window.pixelWidth() > 0 && window.pixelHeight() > 0);
        assert(std::abs(window.width() * window.pixelDensity() - window.pixelWidth()) <= 1);
        bool resized = false;
        for (const auto& event : batch.windowEvents)
            resized |= event.type == arp::platform::WindowEventType::Resized;
        assert(resized);
        arp::scene2d::Viewport2DMapping mapping;
        assert(mapping.configure(window.pixelWidth(), window.pixelHeight(), {1280, 720}, false));
        const auto screen = mapping.worldToScreen({3, 4}, {1, 2}, 64);
        arp::core::Vec2 world;
        assert(mapping.screenToWorld(screen, {1, 2}, 64, world) && std::abs(world.x - 3) < .0001f
            && std::abs(world.y - 4) < .0001f);
    }
    SendMessageW(hwnd,
        WM_DPICHANGED,
        MAKEWPARAM(originalDpi, originalDpi),
        reinterpret_cast<LPARAM>(&original));
    arp::platform::WindowEventBatch batch;
    platform.pumpEvents(batch);
}

#endif // defined(_WIN32)

} // namespace

int main()
{
    arp::platform::WindowSystem platform;
    const auto initResult = platform.initialize();
    assert(initResult.success);
    assert(initResult.diagnostics.empty());

    auto window = platform.createWindow(arp::platform::WindowDesc{
        "ARP Platform Window Test",
        320,
        180,
        true,
        true,
    });

    assert(window.valid());
    assert(window.id() != 0);
    assert(window.width() == 320);
    assert(window.height() == 180);
    assert(window.surfaceKind() == arp::platform::PlatformSurfaceKind::DesktopWindow
        || window.surfaceKind() == arp::platform::PlatformSurfaceKind::WebCanvas);
    window.resize(400, 240);
    assert(window.width() == 400);
    assert(window.height() == 240);

    // Logical <-> pixel contract: pixel = logical x density, rounded by the
    // platform's deterministic rule (identity at density 1.0).
    const float density = window.pixelDensity();
    assert(density > 0.0f);
    if (density == 1.0f) {
        assert(window.pixelWidth() == window.width());
        assert(window.pixelHeight() == window.height());
    }

    // Sync is always a valid commit point.
    const auto synced = window.sync();
    assert(synced.success);

    // Display bounds: supported on desktop; browsers report false.
    int x = 0;
    int y = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    const bool hasDisplayBounds = platform.primaryDisplayBounds(x, y, displayWidth, displayHeight);
    assert(!hasDisplayBounds || (displayWidth > 0 && displayHeight > 0));

    // The resize above queued exactly one Resized event; drain and validate it.
    arp::platform::WindowEventBatch batch;
    const auto drained = platform.pumpEvents(batch);
    assert(drained == 1);
    assert(batch.windowEvents.size() == 1);
    assert(batch.windowEvents.front().type == arp::platform::WindowEventType::Resized);
    assert(batch.windowEvents.front().windowId == window.id());
    batch.clear();

    // Event pump: an idle hidden window drains to an empty batch.
    platform.pumpEvents(batch);
    assert(batch.empty());
    (void)drained;

    // Vulkan surface path: extensions must be reported on Vulkan platforms,
    // and surface creation against a null instance fails closed invalid.
    const auto extensions = platform.requiredVulkanInstanceExtensions();
    const auto surface = platform.createVulkanSurface(window, nullptr);
    if (!extensions.empty()) {
        bool hasSurfaceExtension = false;
        bool hasPlatformExtension = false;
        for (const auto& extension : extensions) {
            if (extension == "VK_KHR_surface")
                hasSurfaceExtension = true;
            if (extension == "VK_KHR_win32_surface" || extension == "VK_KHR_xcb_surface") {
                hasPlatformExtension = true;
            }
        }
        assert(hasSurfaceExtension);
        assert(hasPlatformExtension);
        assert(!surface.valid());
        (void)surface;
    } else {
        assert(!surface.valid());
    }

    // Second window receives a distinct id.
    auto second = platform.createWindow({"ARP Platform Window Test 2", 64, 64, true, true});
    if (second.valid()) {
        assert(second.id() != window.id());
    }

#if defined(_WIN32)
    runWin32SyntheticInputTest(platform, window);
    runWin32DpiTransitionTest(platform, window);
#endif

    return 0;
}
