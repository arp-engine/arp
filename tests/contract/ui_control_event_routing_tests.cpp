#include "platform/input_bridge.h"
#include "tests/fixtures/ui_control_event_fixture.h"

#include <cassert>
using namespace arp::ui;
using F = arp::tests::UiControlEventFixture;

namespace {
class RoutedPanel final : public Panel {
public:
    explicit RoutedPanel(arp::core::pmr::Vector<int>& log) : log_(log) {}

    bool stop = false, prevent = false, mark = false, bubblePrevent = false;

protected:
    UiResult onPreviewEvent(const UiEvent& event, UiEventContext& ctx) override
    {
        if (event.kind == UiInputEventKind::PointerDown) {
            assert(ctx.phase() == UiEventPhase::Capture);
            log_.push_back(1);
            if (stop)
                ctx.stopPropagation();
            if (prevent)
                ctx.preventDefault();
            if (mark)
                ctx.markHandled();
        }
        return UiResult::success();
    }

    UiResult onBubbleEvent(const UiEvent& event, UiEventContext& ctx) override
    {
        if (event.kind == UiInputEventKind::PointerDown) {
            assert(ctx.phase() == UiEventPhase::Bubble);
            log_.push_back(3);
            if (bubblePrevent)
                ctx.preventDefault();
        }
        return UiResult::success();
    }

private:
    arp::core::pmr::Vector<int>& log_;
};

class RoutedButton final : public Button {
public:
    explicit RoutedButton(arp::core::pmr::Vector<int>& log) : log_(log) {}

    int clicks = 0;

protected:
    UiResult onEvent(const UiEvent& event, UiEventContext& ctx) override
    {
        if (event.kind == UiInputEventKind::PointerDown) {
            assert(ctx.phase() == UiEventPhase::Target);
            assert(ctx.localPosition().x == 8);
            log_.push_back(2);
        }
        return UiResult::success();
    }

    void onClick(UiEventContext&) override { ++clicks; }

private:
    arp::core::pmr::Vector<int>& log_;
};

void route_and_independent_flags()
{
    Runtime runtime("routing", F::canvas());
    arp::core::pmr::Vector<int> log;
    auto panel = arp::core::makeUnique<RoutedPanel>(log);
    auto* p = panel.get();
    auto parent = runtime
                      .createControl(std::move(panel),
                          F::control(UiControlType::Panel, 0, 0, 200, 120),
                          runtime.rootId())
                      .widgetId();
    auto button = arp::core::makeUnique<RoutedButton>(log);
    auto* b = button.get();
    auto id = runtime.createControl(std::move(button), F::control(UiControlType::Button), parent)
                  .widgetId();
    auto down = runtime.dispatch(UiEvent::pointerDown(8, 8));
    assert(down.ok() && down.capturedWidget() == id);
    assert(log.size() == 3 && log[0] == 1 && log[1] == 2 && log[2] == 3);
    assert(runtime.dispatch(UiEvent::pointerUp(8, 8)).clicked());
    assert(b->clicks == 1);
    log.clear();
    p->prevent = true;
    down = runtime.dispatch(UiEvent::pointerDown(8, 8));
    assert(down.ok() && !down.capturedWidget().valid());
    assert(log.size() == 3);
    assert(!runtime.dispatch(UiEvent::pointerUp(8, 8)).clicked());
    assert(b->clicks == 1);
    log.clear();
    p->stop = true;
    p->prevent = false;
    p->mark = true;
    down = runtime.dispatch(UiEvent::pointerDown(8, 8));
    assert(down.ok() && down.capturedWidget() == id && down.consumption().pointerConsumed());
    assert(log.size() == 1);
    assert(runtime.dispatch(UiEvent::pointerUp(8, 8)).clicked());
    assert(b->clicks == 2);
    assert(runtime.dispatch(UiEvent::pointerDown(8, 8)).ok());
    const auto outside = runtime.dispatch(UiEvent::pointerUp(310, 170));
    assert(outside.ok() && !outside.clicked() && !outside.capturedWidget().valid());
    assert(b->clicks == 2);
    p->stop = false;
    p->mark = false;
    p->bubblePrevent = true;
    down = runtime.dispatch(UiEvent::pointerDown(8, 8));
    assert(down.ok() && !down.capturedWidget().valid());
    p->bubblePrevent = false;
    assert(runtime.setEnabled(parent, false).ok());
    down = runtime.dispatch(UiEvent::pointerDown(8, 8));
    assert(down.ok() && !down.consumption().anyConsumed());
}

class MarkingLabel final : public Text {
protected:
    UiResult onEvent(const UiEvent& event, UiEventContext& context) override
    {
        if (event.kind == UiInputEventKind::ScrollWheel)
            context.markHandled();
        return UiResult::success();
    }
};

class CommandBlocker final : public Panel {
protected:
    UiResult onPreviewEvent(const UiEvent& event, UiEventContext& context) override
    {
        if (event.kind == UiInputEventKind::ControlInteraction)
            context.preventDefault();
        return UiResult::success();
    }
};

void compatibility_and_clipping()
{
    Runtime runtime("compatibility", F::canvas());
    auto scroll = F::add(runtime, F::control(UiControlType::ScrollView, 0, 0, 80, 80));
    runtime.createControl(arp::core::makeUnique<MarkingLabel>(),
        F::control(UiControlType::Label, 0, 0, 24, 24),
        scroll);
    assert(runtime.dispatch(UiEvent::scrollWheel(8, 8, -2)).ok());
    assert(runtime.findControl(scroll)->value().scroll.y == 2);
    auto parent = runtime
                      .createControl(arp::core::makeUnique<CommandBlocker>(),
                          F::control(UiControlType::Panel, 100, 0, 20, 60),
                          runtime.rootId())
                      .widgetId();
    auto slider = F::add(runtime, F::control(UiControlType::Slider, 0, 0, 100, 24), parent);
    auto command = UiControlInteraction::dragSlider(slider, 0.8f);
    assert(runtime.interact(command).ok() && runtime.findControl(slider)->value().number == 0);
    const auto layout = runtime.canvas().resolveLayout();
    assert(layout.clipRect(slider).width == 20);
    assert(layout.findWidget(slider)->rect.width == 100);
    auto hit = runtime.canvas().hitTest(layout, {140, 8});
    assert(hit.ok() && !hit.widgetId().valid());
    auto extracted = RenderExtractor().extract(runtime.canvas(), layout);
    assert(extracted.ok());
    bool clipped = false;
    for (const auto& item : extracted.queue().items())
        if (item.widgetId == slider) {
            assert(item.clipRect.width == 20);
            clipped = true;
        }
    assert(clipped);
}

void nested_scroll_and_text()
{
    Runtime runtime("scroll-text", F::canvas());
    auto outer = F::add(runtime, F::control(UiControlType::ScrollView, 0, 0, 200, 150));
    auto inner = F::add(runtime, F::control(UiControlType::ScrollView, 4, 4, 120, 100), outer);
    F::add(runtime, F::control(UiControlType::Label, 4, 4, 50, 20), inner);
    auto result = runtime.dispatch(UiEvent::scrollWheel(12, 12, -3));
    assert(result.ok() && result.consumption().pointerConsumed());
    assert(runtime.findControl(inner)->value().scroll.y == 3);
    assert(runtime.findControl(outer)->value().scroll.y == 0);
    auto text = F::add(runtime, F::control(UiControlType::TextInput, 210, 4, 100, 24));
    F::click(runtime, 214, 8);
    assert(runtime.dispatch(UiEvent::textInput(u8"中文")).ok());
    assert(runtime.dispatch(UiEvent::backspace()).ok());
    assert(runtime.findControl(text)->value().text == u8"中");
    auto layout = runtime.canvas().resolveLayout();
    auto extracted = RenderExtractor().extract(runtime.canvas(), layout);
    assert(extracted.ok());
    bool rendered = false;
    for (const auto& item : extracted.queue().items())
        if (item.widgetId == text) {
            assert(item.text == u8"中");
            rendered = true;
        }
    assert(rendered);
    assert(!runtime.dispatch(UiEvent::textInput(arp::core::pmr::String("\x80"))).ok());
    assert(runtime.findControl(text)->value().text == u8"中");
    assert(runtime.cancelInput().ok());
    result = runtime.dispatch(UiEvent::textInput("unfocused"));
    assert(result.ok() && !result.consumption().anyConsumed());
}

void frame_order_and_ownership()
{
    Runtime runtime("frame", F::canvas());
    auto slider = F::add(runtime, F::control(UiControlType::Slider, 0, 0, 100, 24));
    arp::input::InputFrame frame;
    frame.add(arp::input::InputEvent::pointerMoved(4, 8));
    frame.add(arp::input::InputEvent::mouseButtonPressed(arp::input::InputMouseButton::Left));
    frame.add(arp::input::InputEvent::pointerMoved(75, 8));
    frame.add(arp::input::InputEvent::mouseButtonReleased(arp::input::InputMouseButton::Left));
    auto routed = runtime.dispatchFrame(frame, {0, 16});
    assert(routed.ok() && routed.eventConsumption.size() == 4);
    assert(
        runtime.findControl(slider)->value().number == 0.75f && !runtime.capturedControl().valid());
    assert(routed.eventConsumption[1].pointerConsumed()
        && routed.eventConsumption[2].pointerConsumed()
        && routed.eventConsumption[3].pointerConsumed());
    assert(!runtime.dispatchFrame(frame, {0, 3}).ok());
    Runtime missing("missing", F::canvas());
    arp::input::InputFrame noPosition;
    noPosition.add(arp::input::InputEvent::mouseButtonPressed(arp::input::InputMouseButton::Left));
    const auto rejected = missing.dispatchFrame(noPosition, {0, 4});
    assert(!rejected.ok() && rejected.diagnostic.code == UiDiagnosticCode::MissingPointerPosition);
    auto button = F::add(runtime, F::control(UiControlType::Button, 0, 40, 80, 24));
    F::click(runtime, 8, 48);
    arp::input::InputSystem system({1});
    arp::input::InputActionMap actions("game");
    assert(
        actions.bindButton("activate", arp::input::InputBinding::key(arp::input::InputKey::Enter))
            .ok());
    assert(system.registerActionMap(std::move(actions)).ok());
    assert(system.bindPlayerActionMap(0, "game").ok());
    arp::input::InputFrame press;
    press.add(arp::input::InputEvent::keyPressed(arp::input::InputKey::Enter));
    routed = runtime.dispatchFrame(press, {0, 16});
    assert(routed.ok());
    assert(system.beginFrame(press, routed.eventConsumption).ok());
    assert(!system.actionState(0).held("activate"));
    assert(runtime.setEnabled(button, false).ok());
    routed = runtime.dispatchFrame(press, {0, 16});
    assert(routed.ok() && routed.eventConsumption[0].keyboardConsumed());
    assert(system.beginFrame(press, routed.eventConsumption).ok());
    assert(!system.actionState(0).held("activate"));
    arp::input::InputFrame release;
    release.add(arp::input::InputEvent::keyReleased(arp::input::InputKey::Enter));
    routed = runtime.dispatchFrame(release, {0, 16});
    assert(routed.ok());
    assert(system.beginFrame(release, routed.eventConsumption).ok());
    assert(runtime.cancelInput().ok());
    routed = runtime.dispatchFrame(press, {0, 16});
    assert(routed.ok() && !routed.eventConsumption[0].keyboardConsumed());
    assert(system.beginFrame(press, routed.eventConsumption).ok());
    assert(system.actionState(0).held("activate"));
    assert(runtime.setEnabled(button, true).ok());
    F::click(runtime, 8, 48);
    routed = runtime.dispatchFrame(release, {0, 16});
    assert(routed.ok() && !routed.eventConsumption[0].keyboardConsumed());
    assert(system.beginFrame(release, routed.eventConsumption).ok());
    assert(!system.actionState(0).held("activate") && system.actionState(0).released("activate"));
    arp::core::pmr::Vector<arp::input::InputConsumption> wrong;
    assert(!system.beginFrame(press, wrong).ok());
}

void platform_metadata_and_router_lifetime()
{
    arp::platform::WindowEventBatch batch;
    arp::platform::WindowInputEvent pointer;
    pointer.kind = arp::platform::WindowInputEvent::Kind::PointerMoved;
    pointer.windowId = 7;
    pointer.x = 8;
    pointer.y = 8;
    batch.inputEvents.push_back(pointer);
    arp::platform::WindowInputEvent down;
    down.kind = arp::platform::WindowInputEvent::Kind::MouseButtonDown;
    down.windowId = 7;
    batch.inputEvents.push_back(down);
    auto up = down;
    up.kind = arp::platform::WindowInputEvent::Kind::MouseButtonUp;
    batch.inputEvents.push_back(up);
    arp::input::InputFrame frame;
    auto stats = arp::platform::applyWindowInputToInputFrame(batch, frame);
    assert(stats.forwardedEvents == 3 && frame.events()[1].windowId == 7);
    InputRouter router;
    {
        Runtime runtime("platform", F::canvas());
        auto button = F::add(runtime, F::control(UiControlType::Button));
        assert(router.dispatchFrame(runtime, frame, {7, 16}).ok());
        assert(runtime.findControl(button)->value().boolean);
        assert(router.focusedWidget() == button);
    }
    assert(!router.focusedWidget().valid() && !router.capturedWidget().valid());
}
} // namespace

int main()
{
    route_and_independent_flags();
    nested_scroll_and_text();
    frame_order_and_ownership();
    platform_metadata_and_router_lifetime();
    compatibility_and_clipping();
}
