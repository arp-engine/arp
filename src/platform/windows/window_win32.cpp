// WindowSystem/1 — Win32 implementation (window_win32.cpp).
//
// Implements the L2 window contract for desktop Windows through the raw
// Win32 API: RegisterClassExW/CreateWindowExW windows, a WndProc that
// translates OS messages into neutral WindowEvent/WindowInputEvent batches,
// and a direct vulkan-1.dll loader for VK_KHR_win32_surface creation. There
// is no third-party windowing layer between the OS and this TU.
//
// Layout notes:
// - logical extents are DIPs at the reference 96 DPI; pixel extents are
//   physical client-area pixels. logical <-> pixel uses MulDiv in both
//   directions with the window's current DPI.
// - the OS window is owned by WindowSystemState (destroyed in its dtor);
//   PlatformWindow is a movable {state pointer, window id} handle.

#include "core/memory/cpu_allocator.h"
#include "platform/window.h"
#include "platform/window_input_mapping.h"
#include "platform/windows/windows_application_icon.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <iterator>
#include <string>
#include <utility>
#include <vulkan/vulkan.h>

namespace arp::platform {
namespace {

short clientXFromLParam(LPARAM lParam)
{
    return static_cast<short>(static_cast<std::int16_t>(LOWORD(lParam)));
}

short clientYFromLParam(LPARAM lParam)
{
    return static_cast<short>(static_cast<std::int16_t>(HIWORD(lParam)));
}

using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
using GetDpiForSystemFn = UINT(WINAPI*)(void);
using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(void*);

// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 — the handle value is (-4); kept
// as a literal so this TU does not depend on a minimum Windows SDK header.
// A runtime helper because reinterpret_cast is not a constant expression.
void* perMonitorAwareV2Context()
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(-4));
}

constexpr UINT kWindowsReferenceDpi = 96;

std::pmr::string pmrText(const char* text)
{
    return std::pmr::string{text, &core::defaultCpuMemoryResource()};
}

} // namespace

// WindowRecord and WindowSystemState live at namespace scope so their
// definitions complete the opaque `arp::platform::WindowSystemState` forward
// declaration in window.h. The CMake source selection links exactly one
// platform window TU per build, so the per-platform definitions never meet.
struct WindowSystemState;

struct WindowRecord {
    std::uint64_t id = 0;
    HWND hwnd = nullptr;
    WindowSystemState* system = nullptr;
    bool gpuSurface = true;
    bool highPixelDensity = true;
    bool hidden = false;
    bool minimized = false;
    bool creating = false;
    bool hasLastPointer = false;
    float lastPointerX = 0.0f;
    float lastPointerY = 0.0f;
    std::uint8_t heldMouseButtons = 0;
    std::uint16_t pendingHighSurrogate = 0;
    UINT dpi = kWindowsReferenceDpi;
    float density = 1.0f;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidthValue = 0;
    int pixelHeightValue = 0;
    DWORD style = 0;
    DWORD exStyle = 0;
};

struct WindowSystemState {
    ~WindowSystemState()
    {
        for (auto& record : records) {
            if (record && record->hwnd != nullptr) {
                DestroyWindow(record->hwnd);
                record->hwnd = nullptr;
            }
        }
        records.clear();
        if (vulkanLibrary != nullptr) {
            FreeLibrary(vulkanLibrary);
            vulkanLibrary = nullptr;
        }
        vkGetInstanceProcAddr = nullptr;
    }

    WindowRecord* recordById(std::uint64_t id);
    WindowRecord* recordByHwnd(HWND hwnd);

    HINSTANCE hinstance = nullptr;
    bool windowClassRegistered = false;
    WindowEventBatch queued;
    std::uint64_t nextWindowId = 1;
    std::pmr::vector<core::UniquePtr<WindowRecord>> records{&core::defaultCpuMemoryResource()};
    HMODULE vulkanLibrary = nullptr;
    void* vkGetInstanceProcAddr = nullptr;
    GetDpiForWindowFn getDpiForWindow = nullptr;
    GetDpiForSystemFn getDpiForSystem = nullptr;
};

WindowRecord* WindowSystemState::recordById(std::uint64_t id)
{
    for (auto& record : records) {
        if (record && record->id == id)
            return record.get();
    }
    return nullptr;
}

WindowRecord* WindowSystemState::recordByHwnd(HWND hwnd)
{
    for (auto& record : records) {
        if (record && record->hwnd == hwnd)
            return record.get();
    }
    return nullptr;
}

namespace {

UINT querySystemDpi(WindowSystemState& state)
{
    if (state.getDpiForSystem != nullptr) {
        const UINT dpi = state.getDpiForSystem();
        return dpi != 0 ? dpi : kWindowsReferenceDpi;
    }
    const HDC dc = GetDC(nullptr);
    const UINT dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
    return dpi != 0 ? dpi : kWindowsReferenceDpi;
}

// pixel -> logical uses the same MulDiv rounding rule in every direction.
int logicalFromPixel(int pixel, UINT dpi)
{
    return MulDiv(pixel, kWindowsReferenceDpi, static_cast<int>(dpi));
}

int pixelFromLogical(int logical, UINT dpi)
{
    return MulDiv(logical, static_cast<int>(dpi), kWindowsReferenceDpi);
}

void refreshExtentCache(WindowSystemState& state, WindowRecord& record)
{
    RECT client{};
    GetClientRect(record.hwnd, &client);
    record.pixelWidthValue = client.right - client.left;
    record.pixelHeightValue = client.bottom - client.top;
    if (record.highPixelDensity) {
        record.density = static_cast<float>(record.dpi) / static_cast<float>(kWindowsReferenceDpi);
        record.logicalWidth = logicalFromPixel(record.pixelWidthValue, record.dpi);
        record.logicalHeight = logicalFromPixel(record.pixelHeightValue, record.dpi);
    } else {
        record.density = 1.0f;
        record.logicalWidth = record.pixelWidthValue;
        record.logicalHeight = record.pixelHeightValue;
    }
    (void)state;
}

void queueWindowEvent(WindowSystemState& state, WindowEvent event)
{
    state.queued.addWindowEvent(event);
}

void queueInputEvent(WindowSystemState& state, WindowInputEvent event)
{
    state.queued.inputEvents.push_back(std::move(event));
}

void queueResized(WindowSystemState& state, WindowRecord& record)
{
    WindowEvent event;
    event.type = WindowEventType::Resized;
    event.windowId = record.id;
    event.width = static_cast<std::uint32_t>(record.logicalWidth);
    event.height = static_cast<std::uint32_t>(record.logicalHeight);
    queueWindowEvent(state, event);
}

void queueFocus(WindowSystemState& state, WindowRecord& record, bool gained)
{
    WindowEvent event;
    event.type = gained ? WindowEventType::FocusGained : WindowEventType::FocusLost;
    event.windowId = record.id;
    queueWindowEvent(state, event);
}

void queueKey(
    WindowSystemState& state, WindowRecord& record, WPARAM wParam, LPARAM lParam, bool down)
{
    const auto scanCode =
        static_cast<std::uint32_t>((static_cast<ULONG_PTR>(lParam) >> 16u) & 0xffu);
    const bool extended = (lParam & (static_cast<LPARAM>(KF_EXTENDED) << 16u)) != 0;
    WindowKey key = mapWin32KeyMessageToWindowKey(static_cast<UINT>(wParam), scanCode, extended);
    // The keypad Enter shares VK_RETURN with the main Enter; the KF_EXTENDED
    // bit (HIWORD flag 0x100, i.e. lParam bit 24) is the deterministic
    // discriminator between them.
    if (wParam == VK_RETURN && (lParam & (KF_EXTENDED << 16)) != 0) {
        key = WindowKey::NumpadEnter;
    }
    if (key == WindowKey::Unknown)
        return;
    WindowInputEvent event;
    event.kind = down ? WindowInputEvent::Kind::KeyDown : WindowInputEvent::Kind::KeyUp;
    event.windowId = record.id;
    event.key = key;
    queueInputEvent(state, std::move(event));
}

// WM_CHAR delivers one UTF-16 code unit per message (MinGW wchar_t is 16-bit).
// High surrogates are held until their low surrogate arrives, control
// characters are dropped, and the assembled code point is encoded to UTF-8.
void queueText(WindowSystemState& state, WindowRecord& record, WPARAM wParam)
{
    const wchar_t codeUnit = static_cast<wchar_t>(wParam);
    if (codeUnit < 0x20 || codeUnit == 0x7F) {
        record.pendingHighSurrogate = 0;
        return;
    }

    wchar_t pair[2] = {codeUnit, L'\0'};
    std::size_t pairLength = 1;
    if (codeUnit >= 0xD800 && codeUnit <= 0xDBFF) {
        record.pendingHighSurrogate = static_cast<std::uint16_t>(codeUnit);
        return;
    }
    if (codeUnit >= 0xDC00 && codeUnit <= 0xDFFF) {
        if (record.pendingHighSurrogate == 0)
            return;
        pair[0] = static_cast<wchar_t>(record.pendingHighSurrogate);
        record.pendingHighSurrogate = 0;
        pairLength = 2;
    } else {
        record.pendingHighSurrogate = 0;
    }

    std::uint32_t codePoint = static_cast<std::uint32_t>(pair[0]);
    if (pairLength == 2) {
        codePoint = 0x10000u + ((static_cast<std::uint32_t>(pair[0]) - 0xD800u) << 10)
            + (static_cast<std::uint32_t>(pair[1]) - 0xDC00u);
    }

    char utf8[5] = {};
    std::size_t utf8Length = 0;
    if (codePoint < 0x80) {
        utf8[utf8Length++] = static_cast<char>(codePoint);
    } else if (codePoint < 0x800) {
        utf8[utf8Length++] = static_cast<char>(0xC0u | (codePoint >> 6));
        utf8[utf8Length++] = static_cast<char>(0x80u | (codePoint & 0x3Fu));
    } else if (codePoint < 0x10000) {
        utf8[utf8Length++] = static_cast<char>(0xE0u | (codePoint >> 12));
        utf8[utf8Length++] = static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu));
        utf8[utf8Length++] = static_cast<char>(0x80u | (codePoint & 0x3Fu));
    } else {
        utf8[utf8Length++] = static_cast<char>(0xF0u | (codePoint >> 18));
        utf8[utf8Length++] = static_cast<char>(0x80u | ((codePoint >> 12) & 0x3Fu));
        utf8[utf8Length++] = static_cast<char>(0x80u | ((codePoint >> 6) & 0x3Fu));
        utf8[utf8Length++] = static_cast<char>(0x80u | (codePoint & 0x3Fu));
    }

    WindowInputEvent event;
    event.kind = WindowInputEvent::Kind::TextInput;
    event.windowId = record.id;
    event.text = std::pmr::string(utf8, utf8Length, &core::defaultCpuMemoryResource());
    queueInputEvent(state, std::move(event));
}

// PointerMoved carries the absolute logical position; MouseMoved carries the
// delta since the previous pointer position (zero for the first observation).
void notePointerPosition(WindowRecord& record, float x, float y)
{
    record.hasLastPointer = true;
    record.lastPointerX = x;
    record.lastPointerY = y;
}

void queuePointerMove(WindowSystemState& state, WindowRecord& record, float x, float y)
{
    const float dx = record.hasLastPointer ? x - record.lastPointerX : 0.0f;
    const float dy = record.hasLastPointer ? y - record.lastPointerY : 0.0f;
    notePointerPosition(record, x, y);

    WindowInputEvent pointer;
    pointer.kind = WindowInputEvent::Kind::PointerMoved;
    pointer.windowId = record.id;
    pointer.x = x;
    pointer.y = y;
    queueInputEvent(state, std::move(pointer));

    WindowInputEvent moved;
    moved.kind = WindowInputEvent::Kind::MouseMoved;
    moved.windowId = record.id;
    moved.dx = dx;
    moved.dy = dy;
    moved.x = x;
    moved.y = y;
    queueInputEvent(state, std::move(moved));
}

// Button events carry the cursor position delivered with the message so a
// press or release never reports a stale pointer coordinate.
void queueMouseButton(WindowSystemState& state,
    WindowRecord& record,
    WindowMouseButton button,
    bool down,
    float x,
    float y)
{
    notePointerPosition(record, x, y);
    WindowInputEvent event;
    event.kind =
        down ? WindowInputEvent::Kind::MouseButtonDown : WindowInputEvent::Kind::MouseButtonUp;
    event.windowId = record.id;
    event.mouseButton = button;
    event.x = x;
    event.y = y;
    event.hasPosition = true;
    queueInputEvent(state, std::move(event));
}

// One bit per button family in the held-button mask; bit order matches the
// capture-loss release loop below.
constexpr std::uint8_t mouseButtonBit(WindowMouseButton button)
{
    switch (button) {
    case WindowMouseButton::Left:
        return 1u << 0;
    case WindowMouseButton::Right:
        return 1u << 1;
    case WindowMouseButton::Middle:
        return 1u << 2;
    case WindowMouseButton::X1:
        return 1u << 3;
    case WindowMouseButton::X2:
        return 1u << 4;
    }
    return 0;
}

// Single entry point for button transitions: refreshes the pointer position
// from the message's client coordinates, maintains the held-button mask and
// the Win32 mouse capture (so a drag keeps delivering WM_MOUSEMOVE and its
// closing button-up outside the client area), then queues the neutral event.
void handleMouseButton(WindowSystemState& state,
    WindowRecord& record,
    WindowMouseButton button,
    bool down,
    LPARAM lParam)
{
    const float x = static_cast<float>(clientXFromLParam(lParam)) / record.density;
    const float y = static_cast<float>(clientYFromLParam(lParam)) / record.density;

    if (down) {
        record.heldMouseButtons |= mouseButtonBit(button);
        SetCapture(record.hwnd);
    } else {
        record.heldMouseButtons &= static_cast<std::uint8_t>(~mouseButtonBit(button));
        if (record.heldMouseButtons == 0 && GetCapture() == record.hwnd) {
            ReleaseCapture();
        }
    }

    queueMouseButton(state, record, button, down, x, y);
}

WindowMouseButton xButtonFromWParam(WPARAM wParam)
{
    return HIWORD(wParam) == 2 ? WindowMouseButton::X2 : WindowMouseButton::X1;
}

LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // WM_NCCREATE carries the record through lpCreateParams and must be bound
    // before the guard below: GWLP_USERDATA is still unset when this first
    // message arrives, so reading it here would skip the binding forever.
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        auto* created = static_cast<WindowRecord*>(create->lpCreateParams);
        if (created != nullptr)
            created->hwnd = hwnd;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    auto* record = reinterpret_cast<WindowRecord*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (record == nullptr || record->system == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    WindowSystemState& state = *record->system;

    switch (message) {
    case WM_CLOSE:
        queueWindowEvent(state, WindowEvent{WindowEventType::CloseRequested, record->id});
        return 0;
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) {
            if (!record->minimized) {
                record->minimized = true;
                queueWindowEvent(state, WindowEvent{WindowEventType::Minimized, record->id});
            }
        } else if (LOWORD(lParam) != 0 || HIWORD(lParam) != 0) {
            const int previousPixelWidth = record->pixelWidthValue;
            const int previousPixelHeight = record->pixelHeightValue;
            refreshExtentCache(state, *record);
            if (record->minimized) {
                record->minimized = false;
                queueWindowEvent(state, WindowEvent{WindowEventType::Restored, record->id});
            }
            // Resized reports post-creation geometry changes only: the
            // creation-time WM_SIZE folds into the create result, and
            // redundant same-size WM_SIZEs are suppressed deterministically.
            if (!record->creating
                && (record->pixelWidthValue != previousPixelWidth
                    || record->pixelHeightValue != previousPixelHeight)) {
                queueResized(state, *record);
            }
        }
        break;
    case WM_SETFOCUS:
        queueFocus(state, *record, true);
        break;
    case WM_KILLFOCUS:
        queueFocus(state, *record, false);
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        // Preserve repeats; InputSystem owns physical edges and UI owns repeat behavior.
        queueKey(state, *record, wParam, lParam, true);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        queueKey(state, *record, wParam, lParam, false);
        break;
    case WM_CHAR:
        queueText(state, *record, wParam);
        break;
    case WM_MOUSEMOVE: {
        const float x = static_cast<float>(clientXFromLParam(lParam)) / record->density;
        const float y = static_cast<float>(clientYFromLParam(lParam)) / record->density;
        queuePointerMove(state, *record, x, y);
        break;
    }
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        handleMouseButton(state, *record, WindowMouseButton::Left, true, lParam);
        break;
    case WM_LBUTTONUP:
        handleMouseButton(state, *record, WindowMouseButton::Left, false, lParam);
        break;
    case WM_RBUTTONDOWN:
        SetFocus(hwnd);
        handleMouseButton(state, *record, WindowMouseButton::Right, true, lParam);
        break;
    case WM_RBUTTONUP:
        handleMouseButton(state, *record, WindowMouseButton::Right, false, lParam);
        break;
    case WM_MBUTTONDOWN:
        SetFocus(hwnd);
        handleMouseButton(state, *record, WindowMouseButton::Middle, true, lParam);
        break;
    case WM_MBUTTONUP:
        handleMouseButton(state, *record, WindowMouseButton::Middle, false, lParam);
        break;
    case WM_XBUTTONDOWN:
        SetFocus(hwnd);
        handleMouseButton(state, *record, xButtonFromWParam(wParam), true, lParam);
        return TRUE;
    case WM_XBUTTONUP:
        handleMouseButton(state, *record, xButtonFromWParam(wParam), false, lParam);
        return TRUE;
    case WM_CAPTURECHANGED: {
        // Capture was taken by another window while buttons were still held:
        // their natural WM_*BUTTONUP will never arrive, so synthesize
        // deterministic releases for everything still marked held (using the
        // last observed pointer position) and clear the mask.
        const WindowMouseButton heldOrder[] = {
            WindowMouseButton::Left,
            WindowMouseButton::Right,
            WindowMouseButton::Middle,
            WindowMouseButton::X1,
            WindowMouseButton::X2,
        };
        for (const WindowMouseButton button : heldOrder) {
            const std::uint8_t bit = mouseButtonBit(button);
            if ((record->heldMouseButtons & bit) != 0) {
                record->heldMouseButtons &= static_cast<std::uint8_t>(~bit);
                queueMouseButton(
                    state, *record, button, false, record->lastPointerX, record->lastPointerY);
            }
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT point{clientXFromLParam(lParam), clientYFromLParam(lParam)};
        ScreenToClient(record->hwnd, &point);
        queuePointerMove(state,
            *record,
            static_cast<float>(point.x) / record->density,
            static_cast<float>(point.y) / record->density);
        WindowInputEvent wheel;
        wheel.kind = WindowInputEvent::Kind::MouseWheel;
        wheel.windowId = record->id;
        wheel.x = static_cast<float>(point.x) / record->density;
        wheel.y = static_cast<float>(point.y) / record->density;
        wheel.hasPosition = true;
        wheel.wheelDelta = static_cast<float>(static_cast<short>(HIWORD(wParam))) / 120.0f;
        queueInputEvent(state, std::move(wheel));
        break;
    }
    case WM_DPICHANGED: {
        record->dpi = static_cast<UINT>(LOWORD(wParam));
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
            SetWindowPos(record->hwnd,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        refreshExtentCache(state, *record);
        queueResized(state, *record);
        break;
    }
    case WM_DESTROY:
        record->hwnd = nullptr;
        break;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::size_t drainQueued(WindowSystemState& state, WindowEventBatch& outBatch)
{
    const std::size_t count = state.queued.windowEvents.size() + state.queued.inputEvents.size();
    if (count == 0)
        return 0;
    outBatch.windowEvents.insert(outBatch.windowEvents.end(),
        std::make_move_iterator(state.queued.windowEvents.begin()),
        std::make_move_iterator(state.queued.windowEvents.end()));
    outBatch.inputEvents.insert(outBatch.inputEvents.end(),
        std::make_move_iterator(state.queued.inputEvents.begin()),
        std::make_move_iterator(state.queued.inputEvents.end()));
    state.queued.clear();
    return count;
}

bool registerWindowClass(WindowSystemState& state)
{
    if (state.windowClassRegistered)
        return true;
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &windowProcedure;
    windowClass.hInstance = state.hinstance;
    const auto iconResource = MAKEINTRESOURCEW(ARP_APPLICATION_ICON_RESOURCE_ID);
    // RT_GROUP_ICON is resource type 14; spell the wide form explicitly.
    if (FindResourceW(state.hinstance, iconResource, MAKEINTRESOURCEW(14)) != nullptr) {
        windowClass.hIcon = static_cast<HICON>(LoadImageW(state.hinstance,
            iconResource,
            IMAGE_ICON,
            GetSystemMetrics(SM_CXICON),
            GetSystemMetrics(SM_CYICON),
            LR_SHARED));
        if (windowClass.hIcon == nullptr)
            return false;
        windowClass.hIconSm = static_cast<HICON>(LoadImageW(state.hinstance,
            iconResource,
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_SHARED));
        if (windowClass.hIconSm == nullptr)
            return false;
    }
    // IDC_ARROW switches on the UNICODE macro and would expand to an ANSI
    // LPSTR here; use the explicit wide resource id (32512 = OCR_NORMAL).
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"ArpWindowSystemWindowClass";
    if (RegisterClassExW(&windowClass) == 0)
        return false;
    state.windowClassRegistered = true;
    return true;
}

std::wstring utf8ToWide(const std::pmr::string& text)
{
    if (text.empty())
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 1), L'\0');
    if (length > 0) {
        MultiByteToWideChar(
            CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), length);
        wide.resize(static_cast<std::size_t>(length));
    } else {
        wide.clear();
    }
    return wide;
}

} // namespace

PlatformWindow::PlatformWindow(WindowSystemState* state, std::uint64_t windowId) noexcept
    : systemState_(state), windowId_(windowId)
{}

PlatformWindow::PlatformWindow(PlatformWindow&& other) noexcept
    : systemState_(other.systemState_), windowId_(other.windowId_)
{
    other.systemState_ = nullptr;
    other.windowId_ = 0;
}

PlatformWindow& PlatformWindow::operator=(PlatformWindow&& other) noexcept
{
    if (this != &other) {
        systemState_ = other.systemState_;
        windowId_ = other.windowId_;
        other.systemState_ = nullptr;
        other.windowId_ = 0;
    }
    return *this;
}

PlatformWindow::~PlatformWindow()
{
    // The OS window is owned by WindowSystemState, not by this handle.
    reset();
}

void PlatformWindow::reset()
{
    systemState_ = nullptr;
    windowId_ = 0;
}

bool PlatformWindow::valid() const
{
    if (systemState_ == nullptr || windowId_ == 0)
        return false;
    return static_cast<WindowSystemState*>(systemState_)->recordById(windowId_) != nullptr;
}

std::uint64_t PlatformWindow::id() const
{
    return windowId_;
}

int PlatformWindow::width() const
{
    if (systemState_ == nullptr)
        return 0;
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    return record != nullptr ? record->logicalWidth : 0;
}

int PlatformWindow::height() const
{
    if (systemState_ == nullptr)
        return 0;
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    return record != nullptr ? record->logicalHeight : 0;
}

int PlatformWindow::pixelWidth() const
{
    if (systemState_ == nullptr)
        return 0;
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    return record != nullptr ? record->pixelWidthValue : 0;
}

int PlatformWindow::pixelHeight() const
{
    if (systemState_ == nullptr)
        return 0;
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    return record != nullptr ? record->pixelHeightValue : 0;
}

float PlatformWindow::pixelDensity() const
{
    if (systemState_ == nullptr)
        return 1.0f;
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    return record != nullptr ? record->density : 1.0f;
}

bool PlatformWindow::hidden() const
{
    if (systemState_ == nullptr)
        return true;
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    return record != nullptr ? record->hidden : true;
}

PlatformSurfaceKind PlatformWindow::surfaceKind() const
{
    if (systemState_ == nullptr)
        return PlatformSurfaceKind::Headless;
    return PlatformSurfaceKind::DesktopWindow;
}

void PlatformWindow::resize(int width, int height)
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->hwnd == nullptr || width < 1 || height < 1)
        return;

    const int pixelWidth = record->highPixelDensity ? pixelFromLogical(width, record->dpi) : width;
    const int pixelHeight =
        record->highPixelDensity ? pixelFromLogical(height, record->dpi) : height;
    RECT rect{0, 0, pixelWidth, pixelHeight};
    AdjustWindowRectEx(&rect, record->style, FALSE, record->exStyle);
    SetWindowPos(record->hwnd,
        nullptr,
        0,
        0,
        rect.right - rect.left,
        rect.bottom - rect.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    refreshExtentCache(*static_cast<WindowSystemState*>(systemState_), *record);
}

void PlatformWindow::show()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->hwnd == nullptr)
        return;
    ShowWindow(record->hwnd, SW_SHOW);
    record->hidden = false;
}

void PlatformWindow::hide()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->hwnd == nullptr)
        return;
    ShowWindow(record->hwnd, SW_HIDE);
    record->hidden = true;
}

void PlatformWindow::raise()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->hwnd == nullptr)
        return;
    BringWindowToTop(record->hwnd);
    SetForegroundWindow(record->hwnd);
}

void PlatformWindow::minimize()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->hwnd == nullptr)
        return;
    ShowWindow(record->hwnd, SW_MINIMIZE);
}

void PlatformWindow::restore()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->hwnd == nullptr)
        return;
    ShowWindow(record->hwnd, SW_RESTORE);
}

PlatformResult PlatformWindow::sync()
{
    // Win32 window state changes are synchronous with the call; there is
    // never a pending compositor transaction to commit.
    if (systemState_ == nullptr) {
        return PlatformResult{false, pmrText("Invalid window")};
    }
    return PlatformResult{true, {}};
}

std::shared_ptr<void> PlatformWindow::surfaceToken() const
{
    // Windows has no token-producing native surface object: Vulkan surfaces
    // are created through WindowSystem::createVulkanSurface instead.
    return {};
}

PlatformVulkanSurface::PlatformVulkanSurface(PlatformVulkanSurface&& other) noexcept
    : instance_(other.instance_), surface_(other.surface_),
      destroySurfaceProc_(other.destroySurfaceProc_)
{
    other.instance_ = nullptr;
    other.surface_ = nullptr;
    other.destroySurfaceProc_ = nullptr;
}

PlatformVulkanSurface& PlatformVulkanSurface::operator=(PlatformVulkanSurface&& other) noexcept
{
    if (this != &other) {
        reset();
        instance_ = other.instance_;
        surface_ = other.surface_;
        destroySurfaceProc_ = other.destroySurfaceProc_;
        other.instance_ = nullptr;
        other.surface_ = nullptr;
        other.destroySurfaceProc_ = nullptr;
    }
    return *this;
}

PlatformVulkanSurface::~PlatformVulkanSurface()
{
    reset();
}

void PlatformVulkanSurface::reset()
{
    if (surface_ != nullptr && destroySurfaceProc_ != nullptr) {
        const auto destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(destroySurfaceProc_);
        destroySurface(instance_, surface_, nullptr);
    }
    instance_ = nullptr;
    surface_ = nullptr;
    destroySurfaceProc_ = nullptr;
}

bool PlatformVulkanSurface::valid() const
{
    return surface_ != nullptr;
}

VkSurfaceKHR PlatformVulkanSurface::handle() const
{
    return surface_;
}

WindowSystem::~WindowSystem() = default;

PlatformResult WindowSystem::initialize()
{
    if (initialized_)
        return PlatformResult{true, {}};
    state_ = core::makeUnique<WindowSystemState>();
    auto& state = *state_;
    state.hinstance = GetModuleHandleW(nullptr);

    // Per-Monitor-V2 DPI awareness, resolved dynamically so this TU runs on
    // Windows versions without the entry point; SetProcessDPIAware is the
    // declared fallback.
    auto* setDpiAwarenessContext =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")));
    if (setDpiAwarenessContext != nullptr) {
        setDpiAwarenessContext(perMonitorAwareV2Context());
    } else {
        SetProcessDPIAware();
    }
    state.getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    state.getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem")));

    if (!registerWindowClass(state)) {
        const DWORD error = GetLastError();
        state_.reset();
        return PlatformResult{false,
            pmrText(("Win32 RegisterClassExW failed (GetLastError=" + std::to_string(error) + ")")
                    .c_str())};
    }
    initialized_ = true;
    return PlatformResult{true, {}};
}

PlatformWindow WindowSystem::createWindow(const WindowDesc& desc)
{
    if (!initialized_ || !state_)
        return {};
    if (desc.width < 1 || desc.height < 1)
        return {};
    auto& state = *state_;
    if (!registerWindowClass(state))
        return {};

    auto record = core::makeUnique<WindowRecord>();
    record->id = state.nextWindowId++;
    record->system = &state;
    record->gpuSurface = desc.gpuSurface;
    record->highPixelDensity = desc.highPixelDensity;
    record->hidden = desc.hidden;
    record->style = desc.borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    record->exStyle = 0;
    record->creating = true;
    record->dpi = querySystemDpi(state);
    if (desc.highPixelDensity) {
        record->density =
            static_cast<float>(record->dpi) / static_cast<float>(kWindowsReferenceDpi);
    }

    const int clientWidth =
        desc.highPixelDensity ? pixelFromLogical(desc.width, record->dpi) : desc.width;
    const int clientHeight =
        desc.highPixelDensity ? pixelFromLogical(desc.height, record->dpi) : desc.height;
    RECT rect{0, 0, clientWidth, clientHeight};
    AdjustWindowRectEx(&rect, record->style, FALSE, record->exStyle);

    const std::wstring title = utf8ToWide(desc.title);
    const HWND hwnd = CreateWindowExW(record->exStyle,
        L"ArpWindowSystemWindowClass",
        title.c_str(),
        record->style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        state.hinstance,
        record.get());
    if (hwnd == nullptr)
        return {};

    // WM_NCCREATE already bound the record; re-query in case the window's
    // per-monitor DPI differs from the system value.
    if (record->highPixelDensity && state.getDpiForWindow != nullptr) {
        const UINT windowDpi = state.getDpiForWindow(hwnd);
        if (windowDpi != 0)
            record->dpi = windowDpi;
    }
    refreshExtentCache(state, *record);
    record->creating = false;
    if (!desc.hidden)
        ShowWindow(hwnd, SW_SHOW);

    const std::uint64_t windowId = record->id;
    state.records.push_back(std::move(record));
    return PlatformWindow{&state, windowId};
}

bool WindowSystem::primaryDisplayBounds(int& outX, int& outY, int& outWidth, int& outHeight)
{
    if (!initialized_ || !state_)
        return false;
    const POINT origin{0, 0};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    if (monitor == nullptr)
        return false;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return false;

    const UINT dpi = querySystemDpi(*state_);
    outX = logicalFromPixel(info.rcMonitor.left, dpi);
    outY = logicalFromPixel(info.rcMonitor.top, dpi);
    outWidth = logicalFromPixel(info.rcMonitor.right - info.rcMonitor.left, dpi);
    outHeight = logicalFromPixel(info.rcMonitor.bottom - info.rcMonitor.top, dpi);
    return true;
}

std::size_t WindowSystem::pumpEvents(WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_)
        return 0;
    auto& state = *state_;
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return drainQueued(state, outBatch);
}

PlatformResult WindowSystem::waitForEvents(std::uint32_t timeoutMs, WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_) {
        return PlatformResult{false, pmrText("WindowSystem not initialized")};
    }
    const DWORD wait =
        MsgWaitForMultipleObjectsEx(0, nullptr, timeoutMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (wait == WAIT_FAILED) {
        return PlatformResult{false,
            pmrText(("Win32 MsgWaitForMultipleObjectsEx failed (GetLastError="
                + std::to_string(GetLastError()) + ")")
                    .c_str())};
    }
    drainQueued(*state_, outBatch);
    return PlatformResult{true, {}};
}

std::pmr::vector<std::pmr::string> WindowSystem::requiredVulkanInstanceExtensions() const
{
    std::pmr::vector<std::pmr::string> extensions{&core::defaultCpuMemoryResource()};
    extensions.push_back(pmrText("VK_KHR_surface"));
    extensions.push_back(pmrText("VK_KHR_win32_surface"));
    return extensions;
}

PlatformVulkanSurface WindowSystem::createVulkanSurface(
    const PlatformWindow& window, VkInstance instance) const
{
    PlatformVulkanSurface result;
    if (!initialized_ || !state_ || !window.valid() || instance == nullptr)
        return result;
    auto* record = state_->recordById(window.id());
    if (record == nullptr || record->hwnd == nullptr || !record->gpuSurface)
        return result;

    auto* getInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(vulkanGetInstanceProcAddr());
    if (getInstanceProcAddr == nullptr)
        return result;
    const auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        getInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR"));
    const auto destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        getInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    if (createSurface == nullptr || destroySurface == nullptr)
        return result;

    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = state_->hinstance;
    surfaceInfo.hwnd = record->hwnd;
    VkSurfaceKHR surface = nullptr;
    if (createSurface(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS
        || surface == nullptr) {
        return result;
    }
    result.instance_ = instance;
    result.surface_ = surface;
    result.destroySurfaceProc_ = reinterpret_cast<void*>(destroySurface);
    return result;
}

void* WindowSystem::vulkanGetInstanceProcAddr() const
{
    if (!initialized_ || !state_)
        return nullptr;
    if (state_->vkGetInstanceProcAddr == nullptr) {
        state_->vulkanLibrary = LoadLibraryW(L"vulkan-1.dll");
        if (state_->vulkanLibrary == nullptr)
            return nullptr;
        state_->vkGetInstanceProcAddr =
            reinterpret_cast<void*>(GetProcAddress(state_->vulkanLibrary, "vkGetInstanceProcAddr"));
    }
    return state_->vkGetInstanceProcAddr;
}

} // namespace arp::platform
