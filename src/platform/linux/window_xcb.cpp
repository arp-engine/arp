// WindowSystem/1 — X11 implementation (Linux, xcb + xkbcommon).
//
// Lowering contract (docs/platform-window-contract.md):
//   initialize          -> xcb_connect + XKB X11 extension setup. When the
//                          XKB extension or keymap is unavailable, key /
//                          text events are dropped deterministically (no
//                          silent fallback mapping) while window management
//                          stays fully functional.
//   createWindow        -> xcb_create_window (copy_from_parent, top-left
//                          origin client geometry); WM_PROTOCOLS /
//                          WM_DELETE_WINDOW registered so the close button
//                          produces CloseRequested instead of killing the
//                          window. density is always 1.0: pixel == logical.
//   pumpEvents          -> xcb_poll_for_event loop; CONFIGURE_NOTIFY ->
//                          Resized, FOCUS_IN/OUT -> FocusGained/Lost,
//                          CLIENT_MESSAGE(WM_DELETE_WINDOW) ->
//                          CloseRequested, key/button/motion -> input
//                          events. Drag contract: the first button held
//                          takes an explicit pointer grab (owner_events = 0)
//                          so motion/release keep arriving outside the
//                          window; LeaveNotify(mode Ungrab) with buttons
//                          still held synthesizes deterministic releases.
//   waitForEvents       -> select() on the xcb file descriptor with the
//                          requested timeout, then the pump drain.
//   minimize/restore    -> ICCCM WM_CHANGE_STATE client messages
//                          (IconicState / NormalState) to the root window.
//   Vulkan              -> dlopen("libvulkan.so.1") for
//                          vkGetInstanceProcAddr; extensions VK_KHR_surface
//                          + VK_KHR_xcb_surface; vkCreateXcbSurfaceKHR
//                          resolved per instance.
//   surfaceToken()      -> empty; Linux produces GPU surfaces through
//                          createVulkanSurface instead.

#include "core/memory/cpu_allocator.h"
#include "platform/window.h"
#include "platform/window_input_mapping.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iterator>
#include <poll.h>
#include <sys/select.h>
#include <utility>
#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

namespace arp::platform {
namespace {

std::pmr::string pmrText(const char* text)
{
    return std::pmr::string{text, &core::defaultCpuMemoryResource()};
}

struct WindowRecord {
    std::uint64_t id = 0;
    xcb_window_t window = 0;
    bool gpuSurface = true;
    bool highPixelDensity = true;
    bool hidden = false;
    bool minimized = false;
    bool mappedOnce = false;
    bool hasLastPointer = false;
    float lastPointerX = 0.0f;
    float lastPointerY = 0.0f;
    std::uint8_t heldMouseButtons = 0;
    bool pointerGrabbed = false;
    int logicalWidth = 0;
    int logicalHeight = 0;
};

struct WindowSystemState {
    ~WindowSystemState()
    {
        if (connection != nullptr) {
            for (auto& record : records) {
                if (record && record->pointerGrabbed) {
                    xcb_ungrab_pointer(connection, XCB_CURRENT_TIME);
                    record->pointerGrabbed = false;
                }
            }
            for (auto& record : records) {
                if (record && record->window != 0) {
                    xcb_destroy_window(connection, record->window);
                }
            }
            records.clear();
            xcb_flush(connection);
        }
        if (keyState != nullptr) {
            xkb_state_unref(keyState);
            keyState = nullptr;
        }
        if (keymap != nullptr) {
            xkb_keymap_unref(keymap);
            keymap = nullptr;
        }
        if (keyContext != nullptr) {
            xkb_context_unref(keyContext);
            keyContext = nullptr;
        }
        if (vulkanLibrary != nullptr) {
            dlclose(vulkanLibrary);
            vulkanLibrary = nullptr;
        }
        vkGetInstanceProcAddr = nullptr;
        if (connection != nullptr) {
            xcb_disconnect(connection);
            connection = nullptr;
        }
    }

    WindowRecord* recordById(std::uint64_t id)
    {
        for (auto& record : records) {
            if (record && record->id == id)
                return record.get();
        }
        return nullptr;
    }

    WindowRecord* recordByXcbWindow(xcb_window_t window)
    {
        for (auto& record : records) {
            if (record && record->window == window)
                return record.get();
        }
        return nullptr;
    }

    xcb_connection_t* connection = nullptr;
    xcb_screen_t* screen = nullptr;
    xcb_window_t rootWindow = 0;
    xcb_atom_t wmProtocols = 0;
    xcb_atom_t wmDeleteWindow = 0;
    xcb_atom_t wmChangeState = 0;
    std::uint8_t xkbFirstEvent = 0;
    bool xkbReady = false;
    xkb_context* keyContext = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* keyState = nullptr;
    std::int32_t keyDeviceId = 0;
    WindowEventBatch queued;
    std::uint64_t nextWindowId = 1;
    std::pmr::vector<core::UniquePtr<WindowRecord>> records{&core::defaultCpuMemoryResource()};
    void* vulkanLibrary = nullptr;
    void* vkGetInstanceProcAddr = nullptr;
};

void queueWindowEvent(WindowSystemState& state, WindowEvent event)
{
    state.queued.addWindowEvent(event);
}

void queueInputEvent(WindowSystemState& state, WindowInputEvent event)
{
    state.queued.inputEvents.push_back(std::move(event));
}

void queueResized(
    WindowSystemState& state, WindowRecord& record, std::uint16_t width, std::uint16_t height)
{
    record.logicalWidth = static_cast<int>(width);
    record.logicalHeight = static_cast<int>(height);
    WindowEvent event;
    event.type = WindowEventType::Resized;
    event.windowId = record.id;
    event.width = width;
    event.height = height;
    queueWindowEvent(state, event);
}

void queuePointerMove(
    WindowSystemState& state, WindowRecord& record, std::int16_t x, std::int16_t y)
{
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);
    const float dx = record.hasLastPointer ? px - record.lastPointerX : 0.0f;
    const float dy = record.hasLastPointer ? py - record.lastPointerY : 0.0f;
    record.lastPointerX = px;
    record.lastPointerY = py;
    record.hasLastPointer = true;

    WindowInputEvent pointer;
    pointer.kind = WindowInputEvent::Kind::PointerMoved;
    pointer.windowId = record.id;
    pointer.x = px;
    pointer.y = py;
    queueInputEvent(state, std::move(pointer));

    WindowInputEvent move;
    move.kind = WindowInputEvent::Kind::MouseMoved;
    move.windowId = record.id;
    move.dx = dx;
    move.dy = dy;
    queueInputEvent(state, std::move(move));
}

WindowMouseButton buttonDetailToWindowButton(std::uint8_t detail)
{
    switch (detail) {
    case 2:
        return WindowMouseButton::Middle;
    case 3:
        return WindowMouseButton::Right;
    case 8:
        return WindowMouseButton::X1;
    case 9:
        return WindowMouseButton::X2;
    default:
        return WindowMouseButton::Left;
    }
}

// One bit per button family in the held-button mask; bit order matches the
// release loops below.
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

void queueMouseButton(WindowSystemState& state,
    WindowRecord& record,
    WindowMouseButton button,
    bool down,
    float x,
    float y)
{
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

// Single entry point for button transitions: refreshes the pointer position
// from the message coordinates, maintains the held-button mask and the X11
// pointer grab, then queues the neutral event. X11 lowering of the drag
// contract: the first button held takes an explicit pointer grab with
// owner_events = 0, so Motion and Release keep arriving anywhere on screen
// with the grab event mask while event coordinates stay relative to this
// window (negative outside it). X11's automatic passive grab remains as the
// delivery baseline while the grab reply is in flight, and for additional
// simultaneous windows (the server allows one grab per connection).
void handleMouseButton(WindowSystemState& state,
    WindowRecord& record,
    WindowMouseButton button,
    bool down,
    std::int16_t x,
    std::int16_t y)
{
    if (down) {
        record.heldMouseButtons |= mouseButtonBit(button);
        if (record.heldMouseButtons == mouseButtonBit(button) && !record.pointerGrabbed) {
            constexpr std::uint16_t grabMask = XCB_EVENT_MASK_BUTTON_PRESS
                | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_BUTTON_MOTION
                | XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_LEAVE_WINDOW;
            const xcb_grab_pointer_cookie_t cookie = xcb_grab_pointer(state.connection,
                0, // owner_events: deliver anywhere using the grab mask
                record.window,
                grabMask,
                XCB_GRAB_MODE_ASYNC,
                XCB_GRAB_MODE_ASYNC,
                XCB_NONE,
                XCB_NONE,
                XCB_CURRENT_TIME);
            xcb_grab_pointer_reply_t* reply =
                xcb_grab_pointer_reply(state.connection, cookie, nullptr);
            record.pointerGrabbed = reply != nullptr && reply->status == XCB_GRAB_STATUS_SUCCESS;
            free(reply);
        }
    } else {
        record.heldMouseButtons &= static_cast<std::uint8_t>(~mouseButtonBit(button));
        if (record.heldMouseButtons == 0 && record.pointerGrabbed) {
            xcb_ungrab_pointer(state.connection, XCB_CURRENT_TIME);
            record.pointerGrabbed = false;
        }
    }

    queueMouseButton(state, record, button, down, static_cast<float>(x), static_cast<float>(y));
}

// Deterministic repair when pointer delivery can no longer produce the
// natural button-ups (grab terminated without our release, e.g. window
// unmap): synthesize releases for every button still marked held.
void releaseHeldButtons(WindowSystemState& state, WindowRecord& record)
{
    const WindowMouseButton heldOrder[] = {
        WindowMouseButton::Left,
        WindowMouseButton::Right,
        WindowMouseButton::Middle,
        WindowMouseButton::X1,
        WindowMouseButton::X2,
    };
    for (const WindowMouseButton button : heldOrder) {
        const std::uint8_t bit = mouseButtonBit(button);
        if ((record.heldMouseButtons & bit) != 0) {
            record.heldMouseButtons &= static_cast<std::uint8_t>(~bit);
            queueMouseButton(
                state, record, button, false, record.lastPointerX, record.lastPointerY);
        }
    }
    if (record.pointerGrabbed) {
        xcb_ungrab_pointer(state.connection, XCB_CURRENT_TIME);
        record.pointerGrabbed = false;
    }
}

void handleKeyEvent(
    WindowSystemState& state, WindowRecord& record, xcb_key_press_event_t* event, bool down)
{
    if (!state.xkbReady || state.keyState == nullptr)
        return;
    // XKB keycodes are xcb keycodes + 8.
    const xkb_keycode_t keyCode = static_cast<xkb_keycode_t>(event->detail) + 8;
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(state.keyState, keyCode);
    if (down) {
        char utf8Buffer[64];
        const int utf8Length =
            xkb_state_key_get_utf8(state.keyState, keyCode, utf8Buffer, sizeof(utf8Buffer));
        if (utf8Length > 0) {
            WindowInputEvent text;
            text.kind = WindowInputEvent::Kind::TextInput;
            text.windowId = record.id;
            text.text = pmrText(utf8Buffer);
            queueInputEvent(state, std::move(text));
        }
    }
    const auto key = mapX11KeysymToWindowKey(static_cast<std::uint32_t>(keysym));
    if (key == WindowKey::Unknown)
        return;
    WindowInputEvent input;
    input.kind = down ? WindowInputEvent::Kind::KeyDown : WindowInputEvent::Kind::KeyUp;
    input.windowId = record.id;
    input.key = key;
    queueInputEvent(state, std::move(input));
}

void handleXkbEvent(WindowSystemState& state, xcb_generic_event_t* event)
{
    if (!state.xkbReady || state.keymap == nullptr)
        return;
    // XKB event opcodes are offset from the extension's first event id; the
    // high bit of response_type is the "sent event" flag.
    const std::uint8_t responseType = event->response_type & 0x7F;
    const std::uint8_t xkbOpcode = static_cast<std::uint8_t>(responseType - state.xkbFirstEvent);
    if (xkbOpcode == XCB_XKB_NEW_KEYBOARD_NOTIFY || xkbOpcode == XCB_XKB_MAP_NOTIFY) {
        xkb_state_unref(state.keyState);
        state.keyState = xkb_state_new(state.keymap);
    }
}

bool internAtom(WindowSystemState& state, const char* name, xcb_atom_t& outAtom)
{
    const xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(state.connection, 0, static_cast<std::uint16_t>(std::strlen(name)), name);
    xcb_generic_error_t* error = nullptr;
    auto* reply = xcb_intern_atom_reply(state.connection, cookie, &error);
    if (error != nullptr) {
        free(error);
        return false;
    }
    if (reply == nullptr)
        return false;
    outAtom = reply->atom;
    free(reply);
    return true;
}

void dispatchEvent(WindowSystemState& state, xcb_generic_event_t* event)
{
    const std::uint8_t responseType = event->response_type & 0x7F;
    switch (responseType) {
    case XCB_CONFIGURE_NOTIFY: {
        auto* configure = reinterpret_cast<xcb_configure_notify_event_t*>(event);
        auto* record = state.recordByXcbWindow(configure->window);
        if (record != nullptr
            && (static_cast<int>(configure->width) != record->logicalWidth
                || static_cast<int>(configure->height) != record->logicalHeight)) {
            queueResized(state, *record, configure->width, configure->height);
        }
        break;
    }
    case XCB_FOCUS_IN: {
        auto* focus = reinterpret_cast<xcb_focus_in_event_t*>(event);
        auto* record = state.recordByXcbWindow(focus->event);
        if (record != nullptr) {
            queueWindowEvent(state, WindowEvent{WindowEventType::FocusGained, record->id});
        }
        break;
    }
    case XCB_FOCUS_OUT: {
        auto* focus = reinterpret_cast<xcb_focus_out_event_t*>(event);
        auto* record = state.recordByXcbWindow(focus->event);
        if (record != nullptr) {
            queueWindowEvent(state, WindowEvent{WindowEventType::FocusLost, record->id});
        }
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        auto* client = reinterpret_cast<xcb_client_message_event_t*>(event);
        auto* record = state.recordByXcbWindow(client->window);
        if (record != nullptr && client->type == state.wmProtocols
            && client->data.data32[0] == state.wmDeleteWindow) {
            queueWindowEvent(state, WindowEvent{WindowEventType::CloseRequested, record->id});
        }
        break;
    }
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE: {
        auto* key = reinterpret_cast<xcb_key_press_event_t*>(event);
        auto* record = state.recordByXcbWindow(key->event);
        if (record != nullptr) {
            handleKeyEvent(state, *record, key, responseType == XCB_KEY_PRESS);
        }
        break;
    }
    case XCB_BUTTON_PRESS:
    case XCB_BUTTON_RELEASE: {
        auto* button = reinterpret_cast<xcb_button_press_event_t*>(event);
        auto* record = state.recordByXcbWindow(button->event);
        if (record != nullptr) {
            queuePointerMove(state, *record, button->event_x, button->event_y);
            if (button->detail >= 4 && button->detail <= 5) {
                // Wheel: button 4 = up (away from user), 5 = down.
                WindowInputEvent wheel;
                wheel.kind = WindowInputEvent::Kind::MouseWheel;
                wheel.windowId = record->id;
                wheel.x = static_cast<float>(button->event_x);
                wheel.y = static_cast<float>(button->event_y);
                wheel.hasPosition = true;
                wheel.wheelDelta = button->detail == 4 ? 1.0f : -1.0f;
                queueInputEvent(state, std::move(wheel));
            } else {
                handleMouseButton(state,
                    *record,
                    buttonDetailToWindowButton(button->detail),
                    responseType == XCB_BUTTON_PRESS,
                    button->event_x,
                    button->event_y);
            }
        }
        break;
    }
    case XCB_LEAVE_NOTIFY: {
        auto* leave = reinterpret_cast<xcb_leave_notify_event_t*>(event);
        auto* record = state.recordByXcbWindow(leave->event);
        // LeaveNotify with mode Ungrab is the server's signal that a pointer
        // grab ended: while buttons are still held, their natural
        // button-ups will not all arrive — the X11 analog of Win32
        // WM_CAPTURECHANGED.
        if (record != nullptr && leave->mode == XCB_NOTIFY_MODE_UNGRAB
            && record->heldMouseButtons != 0) {
            releaseHeldButtons(state, *record);
        }
        break;
    }
    case XCB_MOTION_NOTIFY: {
        auto* motion = reinterpret_cast<xcb_motion_notify_event_t*>(event);
        auto* record = state.recordByXcbWindow(motion->event);
        if (record != nullptr) {
            queuePointerMove(state, *record, motion->event_x, motion->event_y);
        }
        break;
    }
    default:
        if (state.xkbReady && responseType == state.xkbFirstEvent) {
            handleXkbEvent(state, event);
        }
        break;
    }
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

void pumpOnce(WindowSystemState& state)
{
    for (;;) {
        xcb_generic_event_t* event = xcb_poll_for_event(state.connection);
        if (event == nullptr)
            break;
        dispatchEvent(state, event);
        free(event);
    }
}

} // namespace

// ----------------------------------------------------------------------------
// PlatformWindow

PlatformWindow::PlatformWindow(WindowSystemState* state, std::uint64_t windowId) noexcept
    : systemState_{state}, windowId_{windowId}
{}

PlatformWindow::PlatformWindow(PlatformWindow&& other) noexcept
    : systemState_{other.systemState_}, windowId_{other.windowId_}
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
    reset();
}

void PlatformWindow::reset()
{
    // The xcb window is owned by WindowSystemState, not by this handle.
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
    return width();
}

int PlatformWindow::pixelHeight() const
{
    return height();
}

float PlatformWindow::pixelDensity() const
{
    return 1.0f;
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
    return PlatformSurfaceKind::DesktopWindow;
}

void PlatformWindow::resize(int width, int height)
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == 0 || width < 1 || height < 1)
        return;
    auto* state = static_cast<WindowSystemState*>(systemState_);
    const std::uint16_t valueMask = XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    const std::uint32_t values[] = {
        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    xcb_configure_window(state->connection, record->window, valueMask, values);
    xcb_flush(state->connection);
    record->logicalWidth = width;
    record->logicalHeight = height;
}

void PlatformWindow::show()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == 0)
        return;
    auto* state = static_cast<WindowSystemState*>(systemState_);
    xcb_map_window(state->connection, record->window);
    xcb_flush(state->connection);
    record->hidden = false;
    record->mappedOnce = true;
}

void PlatformWindow::hide()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == 0)
        return;
    auto* state = static_cast<WindowSystemState*>(systemState_);
    xcb_unmap_window(state->connection, record->window);
    xcb_flush(state->connection);
    record->hidden = true;
}

void PlatformWindow::raise()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == 0)
        return;
    auto* state = static_cast<WindowSystemState*>(systemState_);
    const std::uint16_t valueMask = XCB_CONFIG_WINDOW_STACK_MODE;
    const std::uint32_t values[] = {XCB_STACK_MODE_ABOVE};
    xcb_configure_window(state->connection, record->window, valueMask, values);
    xcb_set_input_focus(
        state->connection, XCB_INPUT_FOCUS_POINTER_ROOT, record->window, XCB_CURRENT_TIME);
    xcb_flush(state->connection);
}

void PlatformWindow::minimize()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == 0)
        return;
    auto* state = static_cast<WindowSystemState*>(systemState_);
    xcb_client_message_event_t message{};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.window = record->window;
    message.type = state->wmChangeState;
    message.format = 32;
    message.data.data32[0] = 3; // IconicState
    xcb_send_event(state->connection,
        0,
        state->rootWindow,
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
        reinterpret_cast<const char*>(&message));
    xcb_flush(state->connection);
    record->minimized = true;
}

void PlatformWindow::restore()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == 0)
        return;
    auto* state = static_cast<WindowSystemState*>(systemState_);
    xcb_client_message_event_t message{};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.window = record->window;
    message.type = state->wmChangeState;
    message.format = 32;
    message.data.data32[0] = 1; // NormalState
    xcb_send_event(state->connection,
        0,
        state->rootWindow,
        XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
        reinterpret_cast<const char*>(&message));
    xcb_map_window(state->connection, record->window);
    xcb_flush(state->connection);
    record->minimized = false;
}

PlatformResult PlatformWindow::sync()
{
    if (systemState_ == nullptr)
        return PlatformResult{false, pmrText("Window closed")};
    xcb_flush(static_cast<WindowSystemState*>(systemState_)->connection);
    return PlatformResult{true, {}};
}

std::shared_ptr<void> PlatformWindow::surfaceToken() const
{
    // Linux produces GPU surfaces through WindowSystem::createVulkanSurface.
    return {};
}

// ----------------------------------------------------------------------------
// PlatformVulkanSurface

PlatformVulkanSurface::PlatformVulkanSurface(PlatformVulkanSurface&& other) noexcept
    : instance_{other.instance_}, surface_{other.surface_},
      destroySurfaceProc_{other.destroySurfaceProc_}
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
        const auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(destroySurfaceProc_);
        destroy(instance_, surface_, nullptr);
    }
    instance_ = nullptr;
    surface_ = nullptr;
    destroySurfaceProc_ = nullptr;
}

bool PlatformVulkanSurface::valid() const
{
    return surface_ != nullptr && destroySurfaceProc_ != nullptr;
}

VkSurfaceKHR PlatformVulkanSurface::handle() const
{
    return surface_;
}

// ----------------------------------------------------------------------------
// WindowSystem

WindowSystem::~WindowSystem() = default;

PlatformResult WindowSystem::initialize()
{
    if (initialized_)
        return PlatformResult{true, {}};
    if (!state_)
        state_ = core::makeUnique<WindowSystemState>();
    auto& state = *state_;

    int preferredScreen = 0;
    state.connection = xcb_connect(nullptr, &preferredScreen);
    if (xcb_connection_has_error(state.connection) != 0) {
        xcb_disconnect(state.connection);
        state.connection = nullptr;
        return PlatformResult{false, pmrText("xcb_connect failed")};
    }
    const xcb_setup_t* setup = xcb_get_setup(state.connection);
    xcb_screen_iterator_t iterator = xcb_setup_roots_iterator(setup);
    for (int index = 0; index < preferredScreen && iterator.rem > 0; ++index) {
        xcb_screen_next(&iterator);
    }
    state.screen = iterator.data;
    if (state.screen == nullptr) {
        return PlatformResult{false, pmrText("xcb: no screen available")};
    }
    state.rootWindow = state.screen->root;

    if (!internAtom(state, "WM_PROTOCOLS", state.wmProtocols)
        || !internAtom(state, "WM_DELETE_WINDOW", state.wmDeleteWindow)
        || !internAtom(state, "WM_CHANGE_STATE", state.wmChangeState)) {
        return PlatformResult{false, pmrText("xcb: WM atom intern failed")};
    }

    // XKB: explicit capability, deterministic degradation. Without the XKB
    // extension the window path works but key events are dropped (no silent
    // fallback mapping).
    state.keyContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (state.keyContext != nullptr
        && xkb_x11_setup_xkb_extension(state.connection,
               XKB_X11_MIN_MAJOR_XKB_VERSION,
               XKB_X11_MIN_MINOR_XKB_VERSION,
               XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS,
               nullptr,
               nullptr,
               &state.xkbFirstEvent,
               nullptr)
            == 1) {
        state.keyDeviceId = xkb_x11_get_core_keyboard_device_id(state.connection);
        if (state.keyDeviceId >= 0) {
            state.keymap = xkb_x11_keymap_new_from_device(
                state.keyContext, state.connection, state.keyDeviceId, XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (state.keymap != nullptr) {
                state.keyState = xkb_state_new(state.keymap);
                state.xkbReady = state.keyState != nullptr;
            }
        }
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

    auto record = core::makeUnique<WindowRecord>();
    record->id = state.nextWindowId++;
    record->gpuSurface = desc.gpuSurface;
    record->highPixelDensity = desc.highPixelDensity;
    record->hidden = desc.hidden;
    record->logicalWidth = desc.width;
    record->logicalHeight = desc.height;

    const xcb_window_t window = xcb_generate_id(state.connection);
    const std::uint32_t eventMask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY
        | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE
        | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE
        | XCB_EVENT_MASK_POINTER_MOTION
        | XCB_EVENT_MASK_BUTTON_MOTION
        // LeaveWindowMask guarantees delivery of the mode-Ungrab LeaveNotify
        // generated when a pointer grab terminates (the grab is already over,
        // so delivery follows the window's own event mask).
        | XCB_EVENT_MASK_LEAVE_WINDOW;
    const std::uint32_t valueMask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    const std::uint32_t values[] = {state.screen->black_pixel, eventMask};
    xcb_create_window(state.connection,
        XCB_COPY_FROM_PARENT,
        window,
        state.rootWindow,
        0,
        0,
        static_cast<std::uint16_t>(desc.width),
        static_cast<std::uint16_t>(desc.height),
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        state.screen->root_visual,
        valueMask,
        values);

    xcb_change_property(state.connection,
        XCB_PROP_MODE_REPLACE,
        window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,
        static_cast<std::uint32_t>(desc.title.size()),
        desc.title.c_str());
    xcb_change_property(state.connection,
        XCB_PROP_MODE_REPLACE,
        window,
        state.wmProtocols,
        XCB_ATOM_ATOM,
        32,
        1,
        &state.wmDeleteWindow);
    xcb_flush(state.connection);

    record->window = window;
    if (!desc.hidden) {
        xcb_map_window(state.connection, window);
        xcb_flush(state.connection);
        record->mappedOnce = true;
    }

    const std::uint64_t windowId = record->id;
    state.records.push_back(std::move(record));
    return PlatformWindow{&state, windowId};
}

bool WindowSystem::primaryDisplayBounds(int& outX, int& outY, int& outWidth, int& outHeight)
{
    if (!initialized_ || !state_ || state_->screen == nullptr)
        return false;
    outX = 0;
    outY = 0;
    outWidth = static_cast<int>(state_->screen->width_in_pixels);
    outHeight = static_cast<int>(state_->screen->height_in_pixels);
    return true;
}

std::size_t WindowSystem::pumpEvents(WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_)
        return 0;
    auto& state = *state_;
    pumpOnce(state);
    return drainQueued(state, outBatch);
}

PlatformResult WindowSystem::waitForEvents(std::uint32_t timeoutMs, WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_) {
        return PlatformResult{false, pmrText("WindowSystem not initialized")};
    }
    auto& state = *state_;
    const int fd = xcb_get_file_descriptor(state.connection);
    pollfd fds{};
    fds.fd = fd;
    fds.events = POLLIN;
    const int ready = poll(&fds, 1, static_cast<int>(timeoutMs));
    if (ready < 0) {
        return PlatformResult{false, pmrText("xcb poll failed")};
    }
    pumpOnce(state);
    drainQueued(state, outBatch);
    return PlatformResult{true, {}};
}

std::pmr::vector<std::pmr::string> WindowSystem::requiredVulkanInstanceExtensions() const
{
    std::pmr::vector<std::pmr::string> extensions{&core::defaultCpuMemoryResource()};
    extensions.push_back(pmrText("VK_KHR_surface"));
    extensions.push_back(pmrText("VK_KHR_xcb_surface"));
    return extensions;
}

PlatformVulkanSurface WindowSystem::createVulkanSurface(
    const PlatformWindow& window, VkInstance instance) const
{
    PlatformVulkanSurface surface;
    if (!initialized_ || !state_ || !window.valid())
        return surface;
    auto* record = state_->recordById(window.id());
    if (record == nullptr || record->window == 0 || !record->gpuSurface)
        return surface;

    const auto getInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(vulkanGetInstanceProcAddr());
    if (getInstanceProcAddr == nullptr)
        return surface;
    const auto createSurface = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
        getInstanceProcAddr(instance, "vkCreateXcbSurfaceKHR"));
    const auto destroySurface = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        getInstanceProcAddr(instance, "vkDestroySurfaceKHR"));
    if (createSurface == nullptr || destroySurface == nullptr)
        return surface;

    VkXcbSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    info.connection = state_->connection;
    info.window = record->window;
    VkSurfaceKHR nativeSurface = VK_NULL_HANDLE;
    if (createSurface(instance, &info, nullptr, &nativeSurface) != VK_SUCCESS
        || nativeSurface == VK_NULL_HANDLE) {
        return surface;
    }
    surface.instance_ = instance;
    surface.surface_ = nativeSurface;
    surface.destroySurfaceProc_ = reinterpret_cast<void*>(destroySurface);
    return surface;
}

void* WindowSystem::vulkanGetInstanceProcAddr() const
{
    if (!state_)
        return nullptr;
    if (state_->vkGetInstanceProcAddr == nullptr) {
        if (state_->vulkanLibrary == nullptr) {
            state_->vulkanLibrary = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        }
        if (state_->vulkanLibrary == nullptr)
            return nullptr;
        state_->vkGetInstanceProcAddr = dlsym(state_->vulkanLibrary, "vkGetInstanceProcAddr");
    }
    return state_->vkGetInstanceProcAddr;
}

} // namespace arp::platform

#endif
