#include "platform/input_bridge.h"
#include "platform/window.h"
#include "tests/fixtures/ui_control_event_fixture.h"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <windows.h>
using namespace arp::ui;
using F = arp::tests::UiControlEventFixture;

class NativeButton final : public Button {
public:
    int clicks = 0;

protected:
    void onClick(UiEventContext&) override { ++clicks; }
};

int main()
{
    arp::platform::WindowSystem windows;
    assert(windows.initialize().success);
    auto window =
        windows.createWindow({"ARP UI Control Events Hidden Smoke", 320, 180, true, false});
    assert(window.valid() && window.hidden());
    HWND native = FindWindowW(nullptr, L"ARP UI Control Events Hidden Smoke");
    assert(native);
    arp::platform::WindowEventBatch batch;
    windows.pumpEvents(batch);
    batch.clear();
    Runtime runtime("native-input", F::canvas());
    auto object = arp::core::makeUnique<NativeButton>();
    auto* ptr = object.get();
    auto created = runtime.createControl(
        std::move(object), F::control(UiControlType::Button), runtime.rootId());
    assert(created.ok());
    auto slider = F::add(runtime, F::control(UiControlType::Slider, 0, 40, 100, 24));
    auto text = F::add(runtime, F::control(UiControlType::TextInput, 0, 80, 100, 24));
    auto position = [&](int x, int y) {
        return MAKELPARAM(static_cast<int>(std::lround(x * window.pixelDensity())),
            static_cast<int>(std::lround(y * window.pixelDensity())));
    };
    assert(PostMessageW(native, WM_MOUSEMOVE, 0, position(8, 8)));
    assert(PostMessageW(native, WM_LBUTTONDOWN, MK_LBUTTON, position(8, 8)));
    assert(PostMessageW(native, WM_LBUTTONUP, 0, position(8, 8)));
    assert(PostMessageW(native, WM_MOUSEMOVE, 0, position(4, 48)));
    assert(PostMessageW(native, WM_LBUTTONDOWN, MK_LBUTTON, position(4, 48)));
    assert(PostMessageW(native, WM_MOUSEMOVE, MK_LBUTTON, position(75, 48)));
    assert(PostMessageW(native, WM_LBUTTONUP, 0, position(75, 48)));
    assert(PostMessageW(native, WM_LBUTTONDOWN, MK_LBUTTON, position(8, 88)));
    assert(PostMessageW(native, WM_LBUTTONUP, 0, position(8, 88)));
    assert(PostMessageW(native, WM_CHAR, L'X', 0));
    windows.pumpEvents(batch);
    arp::input::InputFrame frame;
    auto stats = arp::platform::applyWindowInputToInputFrame(batch, frame);
    assert(stats.forwardedEvents >= 10);
    auto routed = runtime.dispatchFrame(frame, {window.id(), 64});
    if (!routed.ok())
        std::fprintf(stderr, "%s\n", routed.diagnostic.message.c_str());
    assert(routed.ok());
    arp::input::InputSystem input({1, window.id()});
    assert(input.beginFrame(frame, routed.eventConsumption).ok());
    assert(ptr->clicks == 1);
    assert(std::fabs(runtime.findControl(slider)->value().number - 0.75f) < 0.01f);
    assert(runtime.findControl(text)->value().text == "X");
    assert(!input.hasTextInput());
    assert(PostMessageW(native, WM_CHAR, L'Y', 0));
    assert(PostMessageW(native, WM_CHAR, L'Z', 0));
    assert(PostMessageW(native, WM_KEYDOWN, VK_BACK, 0));
    assert(PostMessageW(native, WM_KEYDOWN, VK_BACK, static_cast<LPARAM>(1) << 30));
    assert(PostMessageW(native, WM_KEYUP, VK_BACK, 0));
    batch.clear();
    windows.pumpEvents(batch);
    arp::input::InputFrame repeated;
    arp::platform::applyWindowInputToInputFrame(batch, repeated);
    routed = runtime.dispatchFrame(repeated, {window.id(), 64});
    assert(routed.ok());
    assert(runtime.findControl(text)->value().text == "X");
    assert(PostMessageW(native, WM_KILLFOCUS, 0, 0));
    batch.clear();
    windows.pumpEvents(batch);
    arp::input::InputFrame lost;
    stats = arp::platform::applyWindowInputToInputFrame(batch, lost);
    assert(stats.forwardedLifecycleEvents == 1);
    routed = runtime.dispatchFrame(lost, {window.id(), 64});
    assert(routed.ok() && !runtime.focusedControl().valid() && !runtime.capturedControl().valid());
    // A press and focus loss in one pump must cancel in that order.
    assert(PostMessageW(native, WM_LBUTTONDOWN, MK_LBUTTON, position(8, 8)));
    assert(PostMessageW(native, WM_KILLFOCUS, 0, 0));
    batch.clear();
    windows.pumpEvents(batch);
    arp::input::InputFrame samePump;
    stats = arp::platform::applyWindowInputToInputFrame(batch, samePump);
    assert(stats.forwardedLifecycleEvents == 1);
    routed = runtime.dispatchFrame(samePump, {window.id(), 64});
    assert(routed.ok() && !runtime.focusedControl().valid() && !runtime.capturedControl().valid());
    std::puts(
        "PASS: native hidden Win32 messages -> InputFrame -> derived controls -> input consumption -> focus-loss "
        "cleanup");
}
