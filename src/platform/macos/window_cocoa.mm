// WindowSystem/1 — Cocoa implementation (macOS desktop, manual reference
// counting to match the Metal backend TUs; no ARC).
//
// Lowering contract (docs/platform-window-contract.md):
//   initialize          -> [NSApplication sharedApplication] + activation
//                          policy Accessory (no Dock icon required for
//                          engine tools/samples).
//   createWindow        -> NSWindow + NSView; gpuSurface installs a
//                          CAMetalLayer on the content view and surfaceToken()
//                          hands out the retained layer pointer for
//                          SwapchainDesc::platformSurface. pixel = logical x
//                          backingScaleFactor when highPixelDensity, else the
//                          layer contentsScale is forced to 1.0 and pixel ==
//                          logical.
//   pumpEvents          -> [NSApp nextEventMatchingMask ... untilDate:
//                          distantPast] + sendEvent; keyboard events map
//                          through the kVK_ANSI table, mouse/scroll through
//                          the neutral events. Focus / resize / miniaturize
//                          are reconciled against cached state after the
//                          drain (deterministic polling, no observers).
//   waitForEvents       -> same pump with untilDate = now + timeout.
//   windowShouldClose   -> delegate queues CloseRequested and returns NO; the
//                          owner decides destruction (matches Win32 WM_CLOSE).
//   sync()              -> flushes the pending CATransaction and drains the
//                          run loop once.
//   Vulkan              -> none on Apple; createVulkanSurface fails closed
//                          invalid and requiredVulkanInstanceExtensions is
//                          empty.

#include "core/memory/cpu_allocator.h"
#include "platform/window.h"
#include "platform/window_input_mapping.h"

#if defined(__APPLE__) && defined(__MACH__) && !defined(__EMSCRIPTEN__) && !TARGET_OS_IOS

#import <Cocoa/Cocoa.h>
#import <IOKit/hidsystem/IOLLEvent.h>
#import <QuartzCore/QuartzCore.h>
#include <utility>

@class ArpWindowEventDelegate;

@interface ArpWindowEventDelegate : NSObject <NSWindowDelegate> {
    void* statePointer;
    std::uint64_t boundWindowId;
}
- (instancetype)initWithState:(void*)state windowId:(std::uint64_t)windowId;
@end

namespace arp::platform {

std::pmr::string pmrText(const char* text)
{
    return std::pmr::string{text, &core::defaultCpuMemoryResource()};
}

struct WindowRecord {
    std::uint64_t id = 0;
    NSWindow* window = nullptr;
    NSView* view = nullptr;
    CAMetalLayer* layer = nullptr;
    ArpWindowEventDelegate* delegate = nullptr; // forward-declared ObjC class
    bool gpuSurface = true;
    bool highPixelDensity = true;
    bool hidden = false;
    bool minimized = false;
    bool wasKey = false;
    bool hasLastPointer = false;
    float lastPointerX = 0.0f;
    float lastPointerY = 0.0f;
    float density = 1.0f;
    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidthValue = 0;
    int pixelHeightValue = 0;
};

struct WindowSystemState {
    ~WindowSystemState()
    {
        for (auto& record : records) {
            if (!record)
                continue;
            if (record->window != nullptr) {
                [record->window orderOut:nil];
                record->window.delegate = nil;
            }
            if (record->delegate != nullptr)
                [record->delegate release];
            if (record->window != nullptr)
                [record->window release];
            record->window = nullptr;
            record->view = nullptr;
            record->layer = nullptr;
            record->delegate = nullptr;
        }
        records.clear();
    }

    WindowRecord* recordById(std::uint64_t id)
    {
        for (auto& record : records) {
            if (record && record->id == id)
                return record.get();
        }
        return nullptr;
    }

    WindowEventBatch queued;
    std::uint64_t nextWindowId = 1;
    std::pmr::vector<core::UniquePtr<WindowRecord>> records{&core::defaultCpuMemoryResource()};
};

namespace {

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

WindowMouseButton buttonNumberToWindowButton(NSInteger buttonNumber)
{
    switch (buttonNumber) {
    case 1:
        return WindowMouseButton::Right;
    case 2:
        return WindowMouseButton::Middle;
    case 3:
        return WindowMouseButton::X1;
    case 4:
        return WindowMouseButton::X2;
    default:
        return WindowMouseButton::Left;
    }
}

// Cocoa client coordinates are bottom-left origin; the contract is top-left.
NSPoint pointerInView(WindowRecord& record, NSEvent* event)
{
    const NSPoint point = [record.view convertPoint:event.locationInWindow fromView:nil];
    return NSMakePoint(point.x, record.view.bounds.size.height - point.y);
}

void queuePointerMoveAt(WindowSystemState& state, WindowRecord& record, float x, float y)
{
    const float dx = record.hasLastPointer ? x - record.lastPointerX : 0.0f;
    const float dy = record.hasLastPointer ? y - record.lastPointerY : 0.0f;
    record.lastPointerX = x;
    record.lastPointerY = y;
    record.hasLastPointer = true;

    WindowInputEvent pointer;
    pointer.kind = WindowInputEvent::Kind::PointerMoved;
    pointer.windowId = record.id;
    pointer.x = x;
    pointer.y = y;
    queueInputEvent(state, std::move(pointer));

    WindowInputEvent move;
    move.kind = WindowInputEvent::Kind::MouseMoved;
    move.windowId = record.id;
    move.dx = dx;
    move.dy = dy;
    queueInputEvent(state, std::move(move));
}

void queuePointerMove(WindowSystemState& state, WindowRecord& record, NSView* view, NSEvent* event)
{
    (void)view;
    const NSPoint point = pointerInView(record, event);
    queuePointerMoveAt(state, record, static_cast<float>(point.x), static_cast<float>(point.y));
}

// Single entry point for button transitions: refreshes the pointer position
// from the event, then queues the button with the event's own coordinates
// (never a stale pointer position). Drag delivery needs no explicit capture
// on macOS: the window server keeps sending dragged/released events to the
// mouse-down window wherever the cursor goes (AppKit implicit capture), and
// AppKit exposes no grab-loss event, so no synthetic-release repair exists
// on this platform.
void handleMouseButton(WindowSystemState& state,
    WindowRecord& record,
    NSEvent* event,
    WindowMouseButton button,
    bool down)
{
    const NSPoint point = pointerInView(record, event);
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    queuePointerMoveAt(state, record, x, y);
    queueMouseButton(state, record, button, down, x, y);
}

void translateEvent(WindowSystemState& state, WindowRecord& record, NSEvent* event)
{
    switch (event.type) {
    case NSEventTypeFlagsChanged: {
        // AppKit reports modifiers separately from KeyDown/KeyUp. Use the
        // device-specific bits so releasing one Shift while the other is held
        // still produces the correct release for that physical key.
        const auto key = mapMacVirtualKeyToWindowKey(event.keyCode);
        NSEventModifierFlags mask = 0;
        switch (key) {
        case WindowKey::LeftShift:
            mask = NX_DEVICELSHIFTKEYMASK;
            break;
        case WindowKey::RightShift:
            mask = NX_DEVICERSHIFTKEYMASK;
            break;
        case WindowKey::LeftControl:
            mask = NX_DEVICELCTLKEYMASK;
            break;
        case WindowKey::RightControl:
            mask = NX_DEVICERCTLKEYMASK;
            break;
        case WindowKey::LeftAlt:
            mask = NX_DEVICELALTKEYMASK;
            break;
        case WindowKey::RightAlt:
            mask = NX_DEVICERALTKEYMASK;
            break;
        case WindowKey::LeftSuper:
            mask = NX_DEVICELCMDKEYMASK;
            break;
        case WindowKey::RightSuper:
            mask = NX_DEVICERCMDKEYMASK;
            break;
        case WindowKey::CapsLock:
            mask = NSEventModifierFlagCapsLock;
            break;
        default:
            break;
        }
        if (mask != 0) {
            WindowInputEvent input;
            input.kind = (event.modifierFlags & mask) != 0 ? WindowInputEvent::Kind::KeyDown
                                                           : WindowInputEvent::Kind::KeyUp;
            input.windowId = record.id;
            input.key = key;
            queueInputEvent(state, std::move(input));
        }
        break;
    }
    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp: {
        const bool down = event.type == NSEventTypeKeyDown;
        {
            const auto key = mapMacVirtualKeyToWindowKey(event.keyCode);
            if (key != WindowKey::Unknown) {
                WindowInputEvent input;
                input.kind = down ? WindowInputEvent::Kind::KeyDown : WindowInputEvent::Kind::KeyUp;
                input.windowId = record.id;
                input.key = key;
                queueInputEvent(state, std::move(input));
            }
        }
        if (down) {
            const char* utf8 = event.characters.UTF8String;
            if (utf8 != nullptr && utf8[0] >= 0x20) {
                WindowInputEvent text;
                text.kind = WindowInputEvent::Kind::TextInput;
                text.windowId = record.id;
                text.text = pmrText(utf8);
                queueInputEvent(state, std::move(text));
            }
        }
        break;
    }
    case NSEventTypeLeftMouseDown:
        handleMouseButton(state, record, event, WindowMouseButton::Left, true);
        break;
    case NSEventTypeLeftMouseUp:
        handleMouseButton(state, record, event, WindowMouseButton::Left, false);
        break;
    case NSEventTypeRightMouseDown:
        handleMouseButton(state, record, event, WindowMouseButton::Right, true);
        break;
    case NSEventTypeRightMouseUp:
        handleMouseButton(state, record, event, WindowMouseButton::Right, false);
        break;
    case NSEventTypeOtherMouseDown:
        handleMouseButton(
            state, record, event, buttonNumberToWindowButton(event.buttonNumber), true);
        break;
    case NSEventTypeOtherMouseUp:
        handleMouseButton(
            state, record, event, buttonNumberToWindowButton(event.buttonNumber), false);
        break;
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
        queuePointerMove(state, record, record.view, event);
        break;
    case NSEventTypeScrollWheel: {
        // Positive = scroll up = wheel away from the user, in notch units
        // (precise trackpad deltas are normalized at 10 px per notch).
        const float deltaY = static_cast<float>(event.scrollingDeltaY);
        WindowInputEvent wheel;
        wheel.kind = WindowInputEvent::Kind::MouseWheel;
        wheel.windowId = record.id;
        wheel.wheelDelta = event.hasPreciseScrollingDeltas ? deltaY / 10.0f : deltaY;
        queueInputEvent(state, std::move(wheel));
        break;
    }
    default:
        break;
    }
}

// Reconcile focus / resize / miniaturize / density against the cached state;
// called after every drain so event batches are deterministic without
// notification observers.
void reconcileWindow(WindowSystemState& state, WindowRecord& record)
{
    NSWindow* window = record.window;
    if (window == nullptr)
        return;

    const bool isKey = window.isKeyWindow;
    if (isKey != record.wasKey) {
        record.wasKey = isKey;
        WindowEvent event;
        event.type = isKey ? WindowEventType::FocusGained : WindowEventType::FocusLost;
        event.windowId = record.id;
        queueWindowEvent(state, event);
    }

    const bool miniaturized = window.isMiniaturized;
    if (miniaturized && !record.minimized) {
        record.minimized = true;
        queueWindowEvent(state, WindowEvent{WindowEventType::Minimized, record.id});
    } else if (!miniaturized && record.minimized) {
        record.minimized = false;
        queueWindowEvent(state, WindowEvent{WindowEventType::Restored, record.id});
    }

    const NSSize contentSize = window.contentView.bounds.size;
    const CGFloat scale = window.backingScaleFactor;
    if (static_cast<int>(contentSize.width) != record.logicalWidth
        || static_cast<int>(contentSize.height) != record.logicalHeight
        || static_cast<float>(scale) != record.density) {
        if (record.highPixelDensity) {
            record.density = static_cast<float>(scale);
        }
        record.logicalWidth = static_cast<int>(contentSize.width);
        record.logicalHeight = static_cast<int>(contentSize.height);
        record.pixelWidthValue = static_cast<int>(contentSize.width * record.density);
        record.pixelHeightValue = static_cast<int>(contentSize.height * record.density);
        if (record.layer != nullptr) {
            record.layer.contentsScale = static_cast<CGFloat>(record.density);
        }
        queueResized(state, record);
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

void pumpOnce(WindowSystemState& state, NSDate* untilDate)
{
    for (;;) {
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:untilDate
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event == nil)
            break;
        // sendEvent only dispatches to AppKit responders; our neutral input
        // queue must be populated explicitly for the owning engine window.
        for (auto& record : state.records) {
            if (record && record->window == event.window) {
                translateEvent(state, *record, event);
                break;
            }
        }
        [NSApp sendEvent:event];
    }
    for (auto& record : state.records) {
        if (record)
            reconcileWindow(state, *record);
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
    // The NSWindow is owned by WindowSystemState, not by this handle.
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
    return PlatformSurfaceKind::DesktopWindow;
}

void PlatformWindow::resize(int width, int height)
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == nullptr || width < 1 || height < 1)
        return;
    [record->window setContentSize:NSMakeSize(width, height)];
    reconcileWindow(*static_cast<WindowSystemState*>(systemState_), *record);
}

void PlatformWindow::show()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == nullptr)
        return;
    [record->window makeKeyAndOrderFront:nil];
    record->hidden = false;
    reconcileWindow(*static_cast<WindowSystemState*>(systemState_), *record);
}

void PlatformWindow::hide()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == nullptr)
        return;
    [record->window orderOut:nil];
    record->hidden = true;
}

void PlatformWindow::raise()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == nullptr)
        return;
    [record->window orderFrontRegardless];
    [record->window makeKeyWindow];
}

void PlatformWindow::minimize()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == nullptr)
        return;
    [record->window miniaturize:nil];
    reconcileWindow(*static_cast<WindowSystemState*>(systemState_), *record);
}

void PlatformWindow::restore()
{
    if (systemState_ == nullptr)
        return;
    auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->window == nullptr)
        return;
    [record->window deminiaturize:nil];
    reconcileWindow(*static_cast<WindowSystemState*>(systemState_), *record);
}

PlatformResult PlatformWindow::sync()
{
    if (systemState_ == nullptr)
        return PlatformResult{false, pmrText("Window closed")};
    [CATransaction flush];
    pumpOnce(*static_cast<WindowSystemState*>(systemState_), [NSDate distantPast]);
    return PlatformResult{true, {}};
}

std::shared_ptr<void> PlatformWindow::surfaceToken() const
{
    if (systemState_ == nullptr)
        return {};
    const auto* record = static_cast<WindowSystemState*>(systemState_)->recordById(windowId_);
    if (record == nullptr || record->layer == nullptr)
        return {};
    // One retain owned by the token; Metal consumers additionally retain the
    // layer for their own lifetime (see metal_native_swapchain.mm).
    CAMetalLayer* layer = record->layer;
    [layer retain];
    return std::shared_ptr<void>(reinterpret_cast<void*>(layer), [](void* pointer) {
        [static_cast<CAMetalLayer*>(pointer) release];
    });
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
    NSApplication* application = [NSApplication sharedApplication];
    if (application == nil) {
        return PlatformResult{false, pmrText("Cocoa NSApplication unavailable")};
    }
    application.activationPolicy = NSApplicationActivationPolicyAccessory;
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

    const NSWindowStyleMask styleMask = desc.borderless
        ? NSWindowStyleMaskBorderless
        : NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
            | NSWindowStyleMaskResizable;
    NSRect contentRect = NSMakeRect(0, 0, desc.width, desc.height);
    NSWindow* window = [[NSWindow alloc] initWithContentRect:contentRect
                                                   styleMask:styleMask
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    if (window == nil)
        return {};
    window.title = [NSString stringWithUTF8String:desc.title.c_str()];
    window.releasedWhenClosed = NO;
    window.acceptsMouseMovedEvents = YES;
    window.tabbingMode = NSWindowTabbingModeDisallowed;
    window.animationBehavior = NSWindowAnimationBehaviorNone;

    auto record = core::makeUnique<WindowRecord>();
    record->id = state.nextWindowId++;
    record->window = window;
    record->view = window.contentView;
    record->gpuSurface = desc.gpuSurface;
    record->highPixelDensity = desc.highPixelDensity;
    record->hidden = desc.hidden;
    record->density = desc.highPixelDensity ? static_cast<float>(window.backingScaleFactor) : 1.0f;
    record->logicalWidth = desc.width;
    record->logicalHeight = desc.height;
    record->pixelWidthValue = static_cast<int>(desc.width * record->density);
    record->pixelHeightValue = static_cast<int>(desc.height * record->density);

    if (desc.gpuSurface && record->view != nullptr) {
        CAMetalLayer* layer = [CAMetalLayer layer];
        if (layer != nil) {
            layer.contentsScale = static_cast<CGFloat>(record->density);
            [record->view setLayer:layer];
            [record->view setWantsLayer:YES];
            record->layer = layer;
        }
    }
    record->delegate = [[ArpWindowEventDelegate alloc] initWithState:&state windowId:record->id];
    window.delegate = record->delegate;

    if (!desc.hidden) {
        [window makeKeyAndOrderFront:nil];
        record->hidden = false;
        record->wasKey = window.isKeyWindow;
    }

    const std::uint64_t windowId = record->id;
    state.records.push_back(std::move(record));
    return PlatformWindow{&state, windowId};
}

bool WindowSystem::primaryDisplayBounds(int& outX, int& outY, int& outWidth, int& outHeight)
{
    NSArray<NSScreen*>* screens = [NSScreen screens];
    if (screens == nil || screens.count == 0)
        return false;
    const NSRect frame = screens[0].frame;
    // Cocoa global space is bottom-left origin; the primary screen's top-left
    // corner is therefore y = frame.height in the converted space.
    outX = static_cast<int>(frame.origin.x);
    outY = static_cast<int>(frame.size.height - frame.origin.y - frame.size.height);
    outWidth = static_cast<int>(frame.size.width);
    outHeight = static_cast<int>(frame.size.height);
    return true;
}

std::size_t WindowSystem::pumpEvents(WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_)
        return 0;
    auto& state = *state_;
    pumpOnce(state, [NSDate distantPast]);
    return drainQueued(state, outBatch);
}

PlatformResult WindowSystem::waitForEvents(std::uint32_t timeoutMs, WindowEventBatch& outBatch)
{
    if (!initialized_ || !state_) {
        return PlatformResult{false, pmrText("WindowSystem not initialized")};
    }
    auto& state = *state_;
    pumpOnce(state, [NSDate dateWithTimeIntervalSinceNow:timeoutMs / 1000.0]);
    drainQueued(state, outBatch);
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

@implementation ArpWindowEventDelegate

- (instancetype)initWithState:(void*)state windowId:(std::uint64_t)windowId
{
    self = [super init];
    if (self != nil) {
        statePointer = state;
        boundWindowId = windowId;
    }
    return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender
{
    (void)sender;
    auto* state = static_cast<arp::platform::WindowSystemState*>(statePointer);
    if (state != nullptr) {
        arp::platform::WindowEvent event;
        event.type = arp::platform::WindowEventType::CloseRequested;
        event.windowId = boundWindowId;
        state->queued.windowEvents.push_back(event);
    }
    return NO; // the window stays alive; the owner decides destruction
}

@end

#endif
