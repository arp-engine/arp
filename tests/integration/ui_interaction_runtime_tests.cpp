#include "ui/ui.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::ui::CanvasDesc canvasDesc()
{
    arp::ui::CanvasDesc desc;
    desc.viewport = arp::ui::UiViewport::explicitPixels(320.0f, 180.0f);
    desc.safeArea = arp::ui::UiSafeArea::none();
    desc.scalePolicy = arp::ui::UiScalePolicy::fixed(1.0f);
    desc.maxWidgetCount = 32;
    return desc;
}

arp::ui::UiControlDesc control(
    const char* name, arp::ui::UiControlType type, arp::ui::UiControlValue value)
{
    arp::ui::UiControlDesc desc;
    desc.type = type;
    desc.widget.name = text(name);
    desc.widget.localRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 80.0f, 24.0f);
    desc.widget.acceptsFocus = true;
    desc.initialValue = value;
    return desc;
}

arp::ui::ControlDesc runtimeControl(const char* name,
    arp::ui::UiControlType type,
    arp::ui::UiControlValue value,
    arp::ui::LayoutRect rect)
{
    arp::ui::ControlDesc desc;
    desc.type = type;
    desc.widget.name = text(name);
    desc.widget.localRect = rect;
    desc.widget.acceptsFocus = true;
    desc.initialValue = std::move(value);
    return desc;
}

void applies_complete_retained_control_interactions()
{
    arp::ui::UiControlRuntime runtime(text("interactions"), canvasDesc());

    const auto button = runtime
                            .createControl(control("button",
                                               arp::ui::UiControlType::Button,
                                               arp::ui::UiControlValue::fromBool(false)),
                                runtime.rootId())
                            .widgetId();
    const auto checkbox = runtime
                              .createControl(control("checkbox",
                                                 arp::ui::UiControlType::Checkbox,
                                                 arp::ui::UiControlValue::fromBool(false)),
                                  runtime.rootId())
                              .widgetId();
    const auto slider = runtime
                            .createControl(control("slider",
                                               arp::ui::UiControlType::Slider,
                                               arp::ui::UiControlValue::fromNumber(0.0f)),
                                runtime.rootId())
                            .widgetId();
    const auto textInput = runtime
                               .createControl(control("text",
                                                  arp::ui::UiControlType::TextInput,
                                                  arp::ui::UiControlValue::fromText(text(""))),
                                   runtime.rootId())
                               .widgetId();
    const auto numeric = runtime
                             .createControl(control("number",
                                                arp::ui::UiControlType::NumericInput,
                                                arp::ui::UiControlValue::fromNumber(1.0f)),
                                 runtime.rootId())
                             .widgetId();
    const auto scroll = runtime
                            .createControl(control("scroll",
                                               arp::ui::UiControlType::ScrollView,
                                               arp::ui::UiControlValue::fromScroll(0.0f, 0.0f)),
                                runtime.rootId())
                            .widgetId();
    const auto dropdown = runtime
                              .createControl(control("dropdown",
                                                 arp::ui::UiControlType::Dropdown,
                                                 arp::ui::UiControlValue::fromIndex(0)),
                                  runtime.rootId())
                              .widgetId();
    const auto tabs = runtime
                          .createControl(control("tabs",
                                             arp::ui::UiControlType::Tabs,
                                             arp::ui::UiControlValue::fromIndex(0)),
                              runtime.rootId())
                          .widgetId();
    const auto tree = runtime
                          .createControl(control("tree",
                                             arp::ui::UiControlType::TreeView,
                                             arp::ui::UiControlValue::fromIndex(0)),
                              runtime.rootId())
                          .widgetId();
    const auto color =
        runtime
            .createControl(control("color",
                               arp::ui::UiControlType::ColorPicker,
                               arp::ui::UiControlValue::fromColor(0.0f, 0.0f, 0.0f, 1.0f)),
                runtime.rootId())
            .widgetId();
    const auto image =
        runtime
            .createControl(
                control("image", arp::ui::UiControlType::Image, arp::ui::UiControlValue::none()),
                runtime.rootId())
            .widgetId();

    assert(runtime.interact(arp::ui::UiControlInteraction::click(button)).ok());
    assert(runtime.findControl(button)->value().boolean);
    assert(runtime.interact(arp::ui::UiControlInteraction::toggle(checkbox)).ok());
    assert(runtime.findControl(checkbox)->value().boolean);
    assert(runtime.interact(arp::ui::UiControlInteraction::dragSlider(slider, 0.75f)).ok());
    assert(runtime.findControl(slider)->value().number == 0.75f);
    assert(
        runtime.interact(arp::ui::UiControlInteraction::editText(textInput, text("pilot"))).ok());
    assert(runtime.findControl(textInput)->value().text == text("pilot"));
    assert(runtime.interact(arp::ui::UiControlInteraction::stepNumber(numeric, 2.0f)).ok());
    assert(runtime.findControl(numeric)->value().number == 3.0f);
    assert(runtime.interact(arp::ui::UiControlInteraction::scrollBy(scroll, 4.0f, 8.0f)).ok());
    assert(runtime.findControl(scroll)->value().scroll.y == 8.0f);
    assert(runtime.interact(arp::ui::UiControlInteraction::select(dropdown, 2, 4)).ok());
    assert(runtime.findControl(dropdown)->value().index == 2u);
    assert(runtime.interact(arp::ui::UiControlInteraction::select(tabs, 1, 3)).ok());
    assert(runtime.findControl(tabs)->value().index == 1u);
    assert(runtime.interact(arp::ui::UiControlInteraction::select(tree, 3, 8)).ok());
    assert(runtime.findControl(tree)->value().index == 3u);
    assert(runtime.interact(arp::ui::UiControlInteraction::pickColor(color, 0.2f, 0.4f, 0.6f, 1.0f))
            .ok());
    assert(runtime.findControl(color)->value().color.b == 0.6f);
    assert(runtime.interact(arp::ui::UiControlInteraction::imageHitTest(image)).ok());

    auto nav = runtime.interact(arp::ui::UiControlInteraction::keyboardNavigateNext());
    assert(nav.ok());
    assert(nav.focusedWidget().valid());
    assert(runtime.interact(arp::ui::UiControlInteraction::releasePointerCapture()).ok());
    assert(!runtime.capturedControl().valid());
}

void dropdown_options_validate_selection_and_refresh_visible_label()
{
    arp::ui::UiControlRuntime runtime(text("dropdown-options"), canvasDesc());
    auto desc =
        control("enum", arp::ui::UiControlType::Dropdown, arp::ui::UiControlValue::fromIndex(1));
    desc.optionLabels.emplace_back(text("Directional"));
    desc.optionLabels.emplace_back(text("Point"));
    desc.optionLabels.emplace_back(text("Spot"));
    const auto created = runtime.createControl(std::move(desc), runtime.rootId());
    assert(created.ok());
    assert(runtime.canvas().findWidget(created.widgetId())->desc().text == text("Point"));

    auto result = runtime.interact(arp::ui::UiControlInteraction::select(created.widgetId(), 2, 3));
    assert(result.ok());
    assert(runtime.findControl(created.widgetId())->value().index == 2u);
    assert(runtime.canvas().findWidget(created.widgetId())->desc().text == text("Spot"));

    result = runtime.interact(arp::ui::UiControlInteraction::select(created.widgetId(), 0, 2));
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidSelectionIndex);
    assert(runtime.findControl(created.widgetId())->value().index == 2u);
}

void rejects_invalid_or_disabled_interactions_deterministically()
{
    arp::ui::UiControlRuntime runtime(text("diagnostics"), canvasDesc());
    const auto button = runtime
                            .createControl(control("button",
                                               arp::ui::UiControlType::Button,
                                               arp::ui::UiControlValue::fromBool(false)),
                                runtime.rootId())
                            .widgetId();
    const auto dropdown = runtime
                              .createControl(control("dropdown",
                                                 arp::ui::UiControlType::Dropdown,
                                                 arp::ui::UiControlValue::fromIndex(0)),
                                  runtime.rootId())
                              .widgetId();
    const auto textInput = runtime
                               .createControl(control("text",
                                                  arp::ui::UiControlType::TextInput,
                                                  arp::ui::UiControlValue::fromText(text(""))),
                                   runtime.rootId())
                               .widgetId();

    runtime.canvas().findWidget(button)->setEnabled(false);
    auto result = runtime.interact(arp::ui::UiControlInteraction::click(button));
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::DisabledControlInput);

    result = runtime.interact(arp::ui::UiControlInteraction::select(dropdown, 9, 2));
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidSelectionIndex);

    result = runtime.interact(arp::ui::UiControlInteraction::editText(
        textInput, text("12345678901234567890123456789012345678901234567890123456789012345")));
    assert(!result.ok());
    assert(result.diagnostic().code == arp::ui::UiDiagnosticCode::TextEditOverflow);
}

void routes_pointer_keyboard_text_and_scroll_events_through_ui_input_router()
{
    arp::ui::Runtime runtime(text("input-router"), canvasDesc());
    const auto button =
        runtime
            .createControl(runtimeControl("button",
                               arp::ui::UiControlType::Button,
                               arp::ui::UiControlValue::fromBool(false),
                               arp::ui::LayoutRect::fromOriginSize(4.0f, 4.0f, 80.0f, 24.0f)),
                runtime.rootId())
            .widgetId();
    const auto checkbox =
        runtime
            .createControl(runtimeControl("checkbox",
                               arp::ui::UiControlType::Checkbox,
                               arp::ui::UiControlValue::fromBool(false),
                               arp::ui::LayoutRect::fromOriginSize(4.0f, 36.0f, 80.0f, 24.0f)),
                runtime.rootId())
            .widgetId();
    const auto slider =
        runtime
            .createControl(runtimeControl("slider",
                               arp::ui::UiControlType::Slider,
                               arp::ui::UiControlValue::fromNumber(0.0f),
                               arp::ui::LayoutRect::fromOriginSize(4.0f, 68.0f, 100.0f, 24.0f)),
                runtime.rootId())
            .widgetId();
    const auto textInput =
        runtime
            .createControl(runtimeControl("text",
                               arp::ui::UiControlType::TextInput,
                               arp::ui::UiControlValue::fromText(text("ab")),
                               arp::ui::LayoutRect::fromOriginSize(4.0f, 100.0f, 120.0f, 24.0f)),
                runtime.rootId())
            .widgetId();
    const auto scroll =
        runtime
            .createControl(runtimeControl("scroll",
                               arp::ui::UiControlType::ScrollView,
                               arp::ui::UiControlValue::fromScroll(0.0f, 0.0f),
                               arp::ui::LayoutRect::fromOriginSize(140.0f, 4.0f, 80.0f, 80.0f)),
                runtime.rootId())
            .widgetId();

    arp::ui::InputRouter router;

    auto routed = router.dispatch(runtime, arp::ui::UiInputEvent::pointerMove(8.0f, 8.0f));
    assert(routed.ok());
    assert(routed.hoveredWidget() == button);
    assert(!routed.activeWidget().valid());

    routed = router.dispatch(runtime, arp::ui::UiInputEvent::pointerDown(8.0f, 8.0f));
    assert(routed.ok());
    assert(routed.focusedWidget() == button);
    assert(routed.capturedWidget() == button);
    assert(routed.activeWidget() == button);

    routed = router.dispatch(runtime, arp::ui::UiInputEvent::pointerUp(8.0f, 8.0f));
    assert(routed.ok());
    assert(routed.clicked());
    assert(runtime.findControl(button)->value().boolean);
    assert(!routed.capturedWidget().valid());

    assert(router.dispatch(runtime, arp::ui::UiInputEvent::keyboardNext()).focusedWidget()
        == checkbox);
    assert(router.dispatch(runtime, arp::ui::UiInputEvent::keyboardPrevious()).focusedWidget()
        == button);
    assert(router.dispatch(runtime, arp::ui::UiInputEvent::keyboardNext()).focusedWidget()
        == checkbox);
    routed = router.dispatch(runtime, arp::ui::UiInputEvent::keyboardActivate());
    assert(routed.ok());
    assert(runtime.findControl(checkbox)->value().boolean);

    assert(
        router.dispatch(runtime, arp::ui::UiInputEvent::keyboardNext()).focusedWidget() == slider);
    routed = router.dispatch(runtime, arp::ui::UiInputEvent::pointerDown(4.0f, 80.0f));
    assert(routed.capturedWidget() == slider);
    routed = router.dispatch(runtime, arp::ui::UiInputEvent::pointerMove(79.0f, 80.0f));
    assert(routed.ok());
    assert(runtime.findControl(slider)->value().number == 0.75f);
    assert(router.dispatch(runtime, arp::ui::UiInputEvent::pointerUp(79.0f, 80.0f)).ok());

    assert(router.dispatch(runtime, arp::ui::UiInputEvent::keyboardNext()).focusedWidget()
        == textInput);
    routed = router.dispatch(runtime, arp::ui::UiInputEvent::textInput(text("c")));
    assert(routed.ok());
    assert(runtime.findControl(textInput)->value().text == text("abc"));
    routed = router.dispatch(runtime, arp::ui::UiInputEvent::backspace());
    assert(routed.ok());
    assert(runtime.findControl(textInput)->value().text == text("ab"));

    routed = router.dispatch(runtime, arp::ui::UiInputEvent::scrollWheel(160.0f, 20.0f, -12.0f));
    assert(routed.ok());
    assert(routed.hoveredWidget() == scroll);
    assert(runtime.findControl(scroll)->value().scroll.y == 12.0f);
    routed = router.dispatch(runtime, arp::ui::UiInputEvent::scrollThumbDrag(scroll, 24.0f));
    assert(routed.ok());
    assert(runtime.findControl(scroll)->value().scroll.y == 36.0f);
}

} // namespace

int main()
{
    applies_complete_retained_control_interactions();
    dropdown_options_validate_selection_and_refresh_visible_label();
    rejects_invalid_or_disabled_interactions_deterministically();
    routes_pointer_keyboard_text_and_scroll_events_through_ui_input_router();
    return 0;
}
