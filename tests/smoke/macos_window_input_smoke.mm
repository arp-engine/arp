// Exercise the production NSApplication queue -> WindowSystem path, including
// window routing. No direct calls to the private Cocoa translator.
#include "platform/input_bridge.h"

#import <Cocoa/Cocoa.h>
#import <IOKit/hidsystem/IOLLEvent.h>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp::platform;
using Kind = WindowInputEvent::Kind;

class CocoaInputFixture {
public:
    CocoaInputFixture()
    {
        assert(system.initialize().success);
        window = system.createWindow({"ARP Cocoa input smoke", 320, 180, true, false});
        assert(window.valid());
        for (NSWindow* candidate in NSApp.windows) {
            if ([candidate.title isEqualToString:@"ARP Cocoa input smoke"])
                native = candidate;
        }
        assert(native != nil);
        pump();
    }

    void key(NSEventType type,
        unsigned short code,
        NSString* text,
        NSEventModifierFlags flags = 0,
        bool repeat = false,
        NSWindow* target = nil)
    {
        NSEvent* event = [NSEvent keyEventWithType:type
                                          location:NSZeroPoint
                                     modifierFlags:flags
                                         timestamp:0
                                      windowNumber:(target ?: native).windowNumber
                                           context:nil
                                        characters:text
                       charactersIgnoringModifiers:text
                                         isARepeat:repeat
                                           keyCode:code];
        assert(event != nil);
        [NSApp postEvent:event atStart:NO];
    }

    void mouse(NSEventType type, CGFloat x, CGFloat y)
    {
        NSEvent* event = [NSEvent mouseEventWithType:type
                                            location:NSMakePoint(x, y)
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:native.windowNumber
                                             context:nil
                                         eventNumber:0
                                          clickCount:1
                                            pressure:0];
        assert(event != nil);
        if (type == NSEventTypeOtherMouseDown || type == NSEventTypeOtherMouseUp) {
            // The AppKit factory leaves buttonNumber at zero for OtherMouse;
            // a real middle-button event carries CoreGraphics button 2.
            CGEventRef cgEvent = CGEventCreateCopy(event.CGEvent);
            CGEventSetIntegerValueField(cgEvent, kCGMouseEventButtonNumber, kCGMouseButtonCenter);
            event = [NSEvent eventWithCGEvent:cgEvent];
            CFRelease(cgEvent);
            assert(event.buttonNumber == 2 && event.window == native);
        }
        [NSApp postEvent:event atStart:NO];
    }

    void pump(bool wait = false)
    {
        batch.clear();
        if (wait)
            assert(system.waitForEvents(1, batch).success);
        else
            system.pumpEvents(batch);
    }

    void wheel()
    {
        NSEvent* position = [NSEvent mouseEventWithType:NSEventTypeMouseMoved
                                               location:NSMakePoint(42, 135)
                                          modifierFlags:0
                                              timestamp:0
                                           windowNumber:native.windowNumber
                                                context:nil
                                            eventNumber:0
                                             clickCount:0
                                               pressure:0];
        CGEventRef cgEvent = CGEventCreateCopy(position.CGEvent);
        CGEventSetType(cgEvent, kCGEventScrollWheel);
        CGEventSetIntegerValueField(cgEvent, kCGScrollWheelEventDeltaAxis1, 2);
        NSEvent* event = [NSEvent eventWithCGEvent:cgEvent];
        CFRelease(cgEvent);
        assert(event.window == native && event.scrollingDeltaY == 2);
        [NSApp postEvent:event atStart:NO];
    }

    const WindowInputEvent& expect(std::size_t index, Kind kind) const
    {
        assert(index < batch.inputEvents.size());
        const auto& event = batch.inputEvents[index];
        assert(event.kind == kind && event.windowId == window.id());
        return event;
    }

    WindowSystem system;
    PlatformWindow window;
    NSWindow* native = nil;
    WindowEventBatch batch;
};
} // namespace

int main()
{
    @autoreleasepool {
        CocoaInputFixture fixture;
        fixture.key(NSEventTypeKeyDown, 0x0D, @"w");
        fixture.key(NSEventTypeKeyDown, 0x0D, @"w", 0, true);
        fixture.key(NSEventTypeKeyUp, 0x0D, @"w");
        fixture.pump();
        assert(fixture.batch.inputEvents.size() == 5);
        assert(fixture.expect(0, Kind::KeyDown).key == WindowKey::W);
        assert(fixture.expect(1, Kind::TextInput).text == "w");
        assert(fixture.expect(2, Kind::KeyDown).key == WindowKey::W);
        assert(fixture.expect(3, Kind::TextInput).text == "w");
        assert(fixture.expect(4, Kind::KeyUp).key == WindowKey::W);
        arp::input::InputFrame frame;
        assert(applyWindowInputToInputFrame(fixture.batch, frame).forwardedEvents == 5);
        assert(frame.events().front().kind == arp::input::InputEventKind::KeyPressed);
        assert(frame.events().back().kind == arp::input::InputEventKind::KeyReleased);

        // A second owned window keeps its own id; unrelated AppKit windows
        // must not leak input into this WindowSystem.
        auto second = fixture.system.createWindow({"ARP input second", 64, 64, true, false});
        NSWindow* secondNative = nil;
        for (NSWindow* candidate in NSApp.windows) {
            if ([candidate.title isEqualToString:@"ARP input second"])
                secondNative = candidate;
        }
        assert(secondNative != nil);
        NSWindow* unrelated = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 32, 32)
                                                          styleMask:NSWindowStyleMaskTitled
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO];
        fixture.key(NSEventTypeKeyUp, 0x00, @"a", 0, false, secondNative);
        fixture.key(NSEventTypeKeyUp, 0x02, @"d", 0, false, unrelated);
        fixture.pump(true);
        assert(fixture.batch.inputEvents.size() == 1);
        assert(fixture.batch.inputEvents[0].windowId == second.id());
        assert(fixture.batch.inputEvents[0].key == WindowKey::A);
        [unrelated release];

        // Modifier releases must distinguish the two sides even when the
        // opposite Shift remains held (the aggregate Shift bit stays set).
        fixture.key(
            NSEventTypeFlagsChanged, 0x38, @"", NSEventModifierFlagShift | NX_DEVICELSHIFTKEYMASK);
        fixture.key(NSEventTypeFlagsChanged,
            0x3C,
            @"",
            NSEventModifierFlagShift | NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK);
        fixture.key(
            NSEventTypeFlagsChanged, 0x38, @"", NSEventModifierFlagShift | NX_DEVICERSHIFTKEYMASK);
        fixture.key(NSEventTypeFlagsChanged, 0x3C, @"", 0);
        fixture.pump();
        assert(fixture.batch.inputEvents.size() == 4);
        assert(fixture.expect(0, Kind::KeyDown).key == WindowKey::LeftShift);
        assert(fixture.expect(1, Kind::KeyDown).key == WindowKey::RightShift);
        assert(fixture.expect(2, Kind::KeyUp).key == WindowKey::LeftShift);
        assert(fixture.expect(3, Kind::KeyUp).key == WindowKey::RightShift);

        const struct {
            unsigned short code;
            WindowKey key;
            NSEventModifierFlags flags;
        } modifiers[] = {
            {0x3B, WindowKey::LeftControl, NSEventModifierFlagControl | NX_DEVICELCTLKEYMASK},
            {0x3E, WindowKey::RightControl, NSEventModifierFlagControl | NX_DEVICERCTLKEYMASK},
            {0x3A, WindowKey::LeftAlt, NSEventModifierFlagOption | NX_DEVICELALTKEYMASK},
            {0x3D, WindowKey::RightAlt, NSEventModifierFlagOption | NX_DEVICERALTKEYMASK},
            {0x37, WindowKey::LeftSuper, NSEventModifierFlagCommand | NX_DEVICELCMDKEYMASK},
            {0x36, WindowKey::RightSuper, NSEventModifierFlagCommand | NX_DEVICERCMDKEYMASK},
            {0x39, WindowKey::CapsLock, NSEventModifierFlagCapsLock},
        };

        for (const auto& modifier : modifiers) {
            fixture.key(NSEventTypeFlagsChanged, modifier.code, @"", modifier.flags);
            fixture.key(NSEventTypeFlagsChanged, modifier.code, @"", 0);
            fixture.pump();
            assert(fixture.batch.inputEvents.size() == 2);
            assert(fixture.expect(0, Kind::KeyDown).key == modifier.key);
            assert(fixture.expect(1, Kind::KeyUp).key == modifier.key);
        }

        // Click without a preceding move must update the pick location first.
        fixture.mouse(NSEventTypeLeftMouseDown, 30, 140);
        fixture.mouse(NSEventTypeLeftMouseUp, 30, 140);
        fixture.pump();
        assert(fixture.expect(0, Kind::PointerMoved).x == 30);
        assert(fixture.expect(0, Kind::PointerMoved).y == 40);
        assert(fixture.expect(2, Kind::MouseButtonDown).mouseButton == WindowMouseButton::Left);
        assert(fixture.expect(2, Kind::MouseButtonDown).x == 30);
        assert(fixture.expect(5, Kind::MouseButtonUp).mouseButton == WindowMouseButton::Left);

        fixture.mouse(NSEventTypeRightMouseDown, 30, 140);
        fixture.mouse(NSEventTypeRightMouseDragged, 42, 135);
        fixture.mouse(NSEventTypeRightMouseUp, 42, 135);
        fixture.pump();
        assert(fixture.expect(2, Kind::MouseButtonDown).mouseButton == WindowMouseButton::Right);
        assert(fixture.expect(3, Kind::PointerMoved).x == 42);
        assert(fixture.expect(4, Kind::MouseMoved).dx == 12);
        assert(fixture.expect(4, Kind::MouseMoved).dy == 5);
        assert(fixture.expect(7, Kind::MouseButtonUp).mouseButton == WindowMouseButton::Right);

        fixture.mouse(NSEventTypeOtherMouseDown, 42, 135);
        fixture.mouse(NSEventTypeOtherMouseUp, 42, 135);
        fixture.pump();
        assert(fixture.expect(2, Kind::MouseButtonDown).mouseButton == WindowMouseButton::Middle);
        assert(fixture.expect(5, Kind::MouseButtonUp).mouseButton == WindowMouseButton::Middle);
        fixture.wheel();
        fixture.pump();
        assert(fixture.batch.inputEvents.size() == 1);
        assert(fixture.expect(0, Kind::MouseWheel).wheelDelta == 2);
        std::puts(
            "Cocoa input smoke: key/text/repeat, window routing, wait, modifiers, click, drag, middle button, wheel PASS");
    }
}
