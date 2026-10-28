// WindowSystem/1 — Emscripten / Web implementation.
//
// Lowering contract (docs/platform-window-contract.md):
//   initialize          -> no-op success (the browser owns the "display").
//   createWindow        -> a single sentinel window bound to the host canvas
//                          (PlatformSurfaceKind::WebCanvas). A second window
//                          fails closed: the browser exposes exactly one
//                          canvas surface per module.
//   pumpEvents          -> the browser event loop dispatches DOM callbacks
//                          into the internal queue between frames; pump
//                          drains the queue. waitForEvents sleeps via
//                          emscripten_sleep.
//   Input               -> key/mouse/wheel DOM callbacks are translated
//                          through the neutral WindowKey table. Status:
//                          Prototype (Editor WebGL pointer/ASCII path validated;
//                          full IME and platform input coverage remain open).
//   Vulkan              -> none: the web GPU path is WebGPU through the
//                          canvas; createVulkanSurface fails closed invalid
//                          and requiredVulkanInstanceExtensions is empty.
//   pixel               == logical; density always 1.0 (canvas CSS sizing is
//                          handled by the browser / module shell).
//   primaryDisplayBounds-> false: the browser owns sizing.

#include "core/memory/cpu_allocator.h"
#include "platform/window.h"
#include "platform/window_input_mapping.h"

#if defined(__EMSCRIPTEN__)

#include <cstdio>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <utility>

namespace arp::platform {

constexpr std::uint64_t kCanvasWindowId = 1;

// Defined at arp::platform scope so it IS the type forward-declared in
// window.h; only one platform window TU is linked per build.
struct WindowSystemState {
    bool canvasWindowCreated = false;
    bool callbacksInstalled = false;
    bool hidden = false;
    int logicalWidth = 0;
    int logicalHeight = 0;
    float lastPointerX = 0.0f;
    float lastPointerY = 0.0f;
    bool hasLastPointer = false;
    bool pointerMappingFailed = false;
    std::uint64_t nextWindowId = kCanvasWindowId;
    WindowEventBatch queued;
};

namespace {

// DOM WheelEvent.deltaMode spec values; the Emscripten header exposes the raw
// field without named constants.
constexpr unsigned kDomDeltaModeLine = 1;

std::pmr::string pmrText(const char* text)
{
    return std::pmr::string{text, &core::defaultCpuMemoryResource()};
}

void queueWindowEvent(WindowSystemState& state, WindowEvent event)
{
    state.queued.addWindowEvent(event);
}

void queueInputEvent(WindowSystemState& state, WindowInputEvent event)
{
    state.queued.inputEvents.push_back(std::move(event));
}

WindowSystemState& callbackState(void* userData)
{
    return *static_cast<WindowSystemState*>(userData);
}

EM_BOOL handleKey(int eventType, const EmscriptenKeyboardEvent* event, void* userData)
{
    auto& state = callbackState(userData);
    // Preserve DOM repeats; physical press edges are separated in InputSystem.
    const auto key = mapWebCodeToWindowKey(event->code);
    if (key != WindowKey::Unknown) {
        WindowInputEvent input;
        input.kind = eventType == EMSCRIPTEN_EVENT_KEYDOWN ? WindowInputEvent::Kind::KeyDown
                                                           : WindowInputEvent::Kind::KeyUp;
        input.windowId = kCanvasWindowId;
        input.key = key;
        queueInputEvent(state, std::move(input));
    }
    // Prototype text input: single-character DOM keys become TextInput.
    if (eventType == EMSCRIPTEN_EVENT_KEYDOWN && event->key[0] != '\0' && event->key[1] == '\0'
        && static_cast<unsigned char>(event->key[0]) >= 0x20u) {
        WindowInputEvent text;
        text.kind = WindowInputEvent::Kind::TextInput;
        text.windowId = kCanvasWindowId;
        text.text = pmrText(event->key);
        queueInputEvent(state, std::move(text));
    }
    return EM_FALSE;
}

WindowMouseButton mouseButtonFromDom(unsigned short button)
{
    switch (button) {
    case 1:
        return WindowMouseButton::Middle;
    case 2:
        return WindowMouseButton::Right;
    case 3:
        return WindowMouseButton::X1;
    case 4:
        return WindowMouseButton::X2;
    default:
        return WindowMouseButton::Left;
    }
}

// clang-format off
EM_JS(int, windowCanvasClientPosition, (int clientX, int clientY, float* output), {
    const canvas = Module.canvas;
    if (!canvas || !canvas.isConnected) return 0;
    const rect = canvas.getBoundingClientRect();
    if (![rect.left, rect.top, rect.width, rect.height].every(Number.isFinite)
        || rect.width <= 0 || rect.height <= 0) return 0;
    HEAPF32[output >> 2] = clientX - rect.left;
    HEAPF32[(output >> 2) + 1] = clientY - rect.top;
    return 1;
});
// clang-format on

bool queueDomPointerMove(WindowSystemState& state, const EmscriptenMouseEvent& event)
{
    // SDK 4.x no longer populates canvasX/Y. Window callbacks retain releases
    // outside the canvas; translate client coordinates using its actual rect.
    float position[2];
    if (!windowCanvasClientPosition(event.clientX, event.clientY, position)) {
        if (!state.pointerMappingFailed) {
            std::fputs("PLATFORM-WEB-INPUT-CANVAS-UNAVAILABLE\n", stderr);
            WindowInputEvent cancel;
            cancel.kind = WindowInputEvent::Kind::FocusLost;
            cancel.windowId = kCanvasWindowId;
            queueInputEvent(state, std::move(cancel));
        }
        state.pointerMappingFailed = true;
        state.hasLastPointer = false;
        return false;
    }
    state.pointerMappingFailed = false;
    const float x = position[0], y = position[1];
    const float dx = state.hasLastPointer ? x - state.lastPointerX : 0.0f;
    const float dy = state.hasLastPointer ? y - state.lastPointerY : 0.0f;
    state.lastPointerX = x;
    state.lastPointerY = y;
    state.hasLastPointer = true;

    WindowInputEvent pointer;
    pointer.kind = WindowInputEvent::Kind::PointerMoved;
    pointer.windowId = kCanvasWindowId;
    pointer.x = x;
    pointer.y = y;
    queueInputEvent(state, std::move(pointer));

    WindowInputEvent move;
    move.kind = WindowInputEvent::Kind::MouseMoved;
    move.windowId = kCanvasWindowId;
    move.dx = dx;
    move.dy = dy;
    queueInputEvent(state, std::move(move));
    return true;
}

EM_BOOL handleMouseButton(int eventType, const EmscriptenMouseEvent* event, void* userData)
{
    auto& state = callbackState(userData);
    if (!queueDomPointerMove(state, *event))
        return EM_FALSE;
    WindowInputEvent input;
    input.kind = eventType == EMSCRIPTEN_EVENT_MOUSEDOWN ? WindowInputEvent::Kind::MouseButtonDown
                                                         : WindowInputEvent::Kind::MouseButtonUp;
    input.windowId = kCanvasWindowId;
    input.mouseButton = mouseButtonFromDom(event->button);
    input.x = state.lastPointerX;
    input.y = state.lastPointerY;
    input.hasPosition = true;
    queueInputEvent(state, std::move(input));
    return EM_FALSE;
}

EM_BOOL handleMouseMove(int eventType, const EmscriptenMouseEvent* event, void* userData)
{
    (void)eventType;
    auto& state = callbackState(userData);
    (void)queueDomPointerMove(state, *event);
    return EM_FALSE;
}

EM_BOOL handleWheel(int eventType, const EmscriptenWheelEvent* event, void* userData)
{
    (void)eventType;
    auto& state = callbackState(userData);
    if (!queueDomPointerMove(state, event->mouse))
        return EM_FALSE;
    WindowInputEvent wheel;
    wheel.kind = WindowInputEvent::Kind::MouseWheel;
    wheel.x = state.lastPointerX;
    wheel.y = state.lastPointerY;
    wheel.hasPosition = true;
    wheel.windowId = kCanvasWindowId;
    // Deterministic Prototype rule: line mode counts deltas directly, pixel
    // mode normalizes at 40 px per notch.
    wheel.wheelDelta = event->deltaMode == kDomDeltaModeLine
        ? static_cast<float>(-event->deltaY)
        : static_cast<float>(-event->deltaY) / 40.0f;
    queueInputEvent(state, std::move(wheel));
    return EM_FALSE;
}

EM_BOOL handleFocus(int eventType, const EmscriptenFocusEvent*, void* userData)
{
    auto& state = callbackState(userData);
    queueWindowEvent(state,
        WindowEvent{eventType == EMSCRIPTEN_EVENT_BLUR ? WindowEventType::FocusLost
                                                       : WindowEventType::FocusGained,
            kCanvasWindowId});
    return EM_FALSE;
}

void installCallbacks(WindowSystemState& state)
{
    if (state.callbacksInstalled)
        return;
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleKey);
    emscripten_set_mousedown_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleMouseButton);
    emscripten_set_mouseup_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleMouseButton);
    emscripten_set_mousemove_callback(
        EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleMouseMove);
    emscripten_set_wheel_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleWheel);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleFocus);
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &state, EM_TRUE, handleFocus);
    state.callbacksInstalled = true;
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
    systemState_ = nullptr;
    windowId_ = 0;
}

bool PlatformWindow::valid() const
{
    return systemState_ != nullptr && windowId_ != 0;
}

std::uint64_t PlatformWindow::id() const
{
    return windowId_;
}

int PlatformWindow::width() const
{
    const auto* state = static_cast<const WindowSystemState*>(systemState_);
    return state != nullptr ? state->logicalWidth : 0;
}

int PlatformWindow::height() const
{
    const auto* state = static_cast<const WindowSystemState*>(systemState_);
    return state != nullptr ? state->logicalHeight : 0;
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
    const auto* state = static_cast<const WindowSystemState*>(systemState_);
    return state != nullptr ? state->hidden : true;
}

PlatformSurfaceKind PlatformWindow::surfaceKind() const
{
    return PlatformSurfaceKind::WebCanvas;
}

void PlatformWindow::resize(int width, int height)
{
    auto* state = static_cast<WindowSystemState*>(systemState_);
    if (state == nullptr || width < 1 || height < 1)
        return;
    state->logicalWidth = width;
    state->logicalHeight = height;
    if (windowId_ == kCanvasWindowId) {
        WindowEvent event;
        event.type = WindowEventType::Resized;
        event.windowId = windowId_;
        event.width = static_cast<std::uint32_t>(width);
        event.height = static_cast<std::uint32_t>(height);
        queueWindowEvent(*state, event);
    }
}

void PlatformWindow::show()
{
    auto* state = static_cast<WindowSystemState*>(systemState_);
    if (state != nullptr)
        state->hidden = false;
}

void PlatformWindow::hide()
{
    auto* state = static_cast<WindowSystemState*>(systemState_);
    if (state != nullptr)
        state->hidden = true;
}

void PlatformWindow::raise()
{}

void PlatformWindow::minimize()
{}

void PlatformWindow::restore()
{}

PlatformResult PlatformWindow::sync()
{
    // Canvas sizing is committed by the browser layout; nothing to flush.
    return PlatformResult{true, {}};
}

std::shared_ptr<void> PlatformWindow::surfaceToken() const
{
    return {};
}

// ----------------------------------------------------------------------------
// PlatformVulkanSurface

PlatformVulkanSurface::PlatformVulkanSurface(PlatformVulkanSurface&&) noexcept = default;
PlatformVulkanSurface& PlatformVulkanSurface::operator=(PlatformVulkanSurface&&) noexcept = default;
PlatformVulkanSurface::~PlatformVulkanSurface() = default;

void PlatformVulkanSurface::reset()
{}

bool PlatformVulkanSurface::valid() const
{
    return false;
}

VkSurfaceKHR PlatformVulkanSurface::handle() const
{
    return nullptr;
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
    if (state.canvasWindowCreated)
        return {}; // exactly one canvas surface
    state.canvasWindowCreated = true;
    state.hidden = desc.hidden;
    state.logicalWidth = desc.width;
    state.logicalHeight = desc.height;
    const std::uint64_t windowId = state.nextWindowId++;
    installCallbacks(state);
    return PlatformWindow{&state, windowId};
}

bool WindowSystem::primaryDisplayBounds(int&, int&, int&, int&)
{
    return false;
}

std::size_t WindowSystem::pumpEvents(WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_)
        return 0;
    return drainQueued(*state_, outBatch);
}

PlatformResult WindowSystem::waitForEvents(std::uint32_t timeoutMs, WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_) {
        return PlatformResult{false, pmrText("WindowSystem not initialized")};
    }
    emscripten_sleep(timeoutMs);
    drainQueued(*state_, outBatch);
    return PlatformResult{true, {}};
}

std::pmr::vector<std::pmr::string> WindowSystem::requiredVulkanInstanceExtensions() const
{
    return std::pmr::vector<std::pmr::string>{&core::defaultCpuMemoryResource()};
}

PlatformVulkanSurface WindowSystem::createVulkanSurface(const PlatformWindow&, VkInstance) const
{
    return PlatformVulkanSurface{};
}

void* WindowSystem::vulkanGetInstanceProcAddr() const
{
    return nullptr;
}

} // namespace arp::platform

#endif
