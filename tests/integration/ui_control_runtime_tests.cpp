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
    desc.viewport = arp::ui::UiViewport::explicitPixels(640.0f, 360.0f);
    desc.safeArea = arp::ui::UiSafeArea::none();
    desc.scalePolicy = arp::ui::UiScalePolicy::fixed(1.0f);
    desc.maxWidgetCount = 64;
    return desc;
}

arp::ui::UiControlDesc controlDesc(
    const char* name, arp::ui::UiControlType type, arp::ui::UiControlValue initialValue)
{
    arp::ui::UiControlDesc desc;
    desc.type = type;
    desc.widget.name = text(name);
    desc.widget.localRect = arp::ui::LayoutRect::fromOriginSize(4.0f, 4.0f, 80.0f, 24.0f);
    desc.widget.order = 1;
    desc.widget.acceptsFocus =
        type != arp::ui::UiControlType::Panel && type != arp::ui::UiControlType::Label;
    desc.initialValue = initialValue;
    return desc;
}

void constructs_and_updates_all_retained_control_types()
{
    arp::ui::UiControlRuntime runtime(text("hud"), canvasDesc());

    const arp::ui::UiControlType types[] = {
        arp::ui::UiControlType::Panel,
        arp::ui::UiControlType::Label,
        arp::ui::UiControlType::Button,
        arp::ui::UiControlType::Slider,
        arp::ui::UiControlType::Checkbox,
        arp::ui::UiControlType::TextInput,
        arp::ui::UiControlType::NumericInput,
        arp::ui::UiControlType::ScrollView,
        arp::ui::UiControlType::Dropdown,
        arp::ui::UiControlType::Tabs,
        arp::ui::UiControlType::TreeView,
        arp::ui::UiControlType::ColorPicker,
        arp::ui::UiControlType::Image,
    };

    arp::core::pmr::Vector<arp::ui::WidgetId> ids;
    for (const auto type : types) {
        const auto created = runtime.createControl(
            controlDesc(arp::ui::controlTypeName(type), type, arp::ui::UiControlValue::none()),
            runtime.rootId());
        assert(created.ok());
        assert(created.widgetId().valid());
        ids.push_back(created.widgetId());
    }

    assert(runtime.controlCount() == 13u);
    assert(runtime.canvas().traversal().size() == 14u);
    assert(runtime.findControl(ids[0])->type() == arp::ui::UiControlType::Panel);
    assert(runtime.findControl(ids[12])->type() == arp::ui::UiControlType::Image);

    assert(runtime.setValue(ids[1], arp::ui::UiControlValue::fromText(text("HP"))).ok());
    assert(runtime.setValue(ids[2], arp::ui::UiControlValue::fromBool(true)).ok());
    assert(runtime.setValue(ids[3], arp::ui::UiControlValue::fromNumber(0.75f)).ok());
    assert(runtime.setValue(ids[4], arp::ui::UiControlValue::fromBool(true)).ok());
    assert(runtime.setValue(ids[5], arp::ui::UiControlValue::fromText(text("name"))).ok());
    assert(runtime.setValue(ids[6], arp::ui::UiControlValue::fromNumber(42.0f)).ok());
    assert(runtime.setValue(ids[7], arp::ui::UiControlValue::fromScroll(12.0f, 24.0f)).ok());
    assert(runtime.setValue(ids[8], arp::ui::UiControlValue::fromIndex(2)).ok());
    assert(runtime.setValue(ids[9], arp::ui::UiControlValue::fromIndex(1)).ok());
    assert(runtime.setValue(ids[10], arp::ui::UiControlValue::fromIndex(3)).ok());
    assert(runtime.setValue(ids[11], arp::ui::UiControlValue::fromColor(0.25f, 0.5f, 0.75f, 1.0f))
            .ok());

    assert(runtime.findControl(ids[5])->value().text == text("name"));
    assert(runtime.findControl(ids[6])->value().number == 42.0f);
    assert(runtime.findControl(ids[11])->value().color.r == 0.25f);

    const auto layout = runtime.canvas().resolveLayout();
    assert(layout.ok());
    assert(layout.resolvedWidgets().size() == 14u);
}

void diagnostics_are_deterministic_for_invalid_control_runtime_contracts()
{
    arp::ui::UiControlRuntime runtime(text("diagnostics"), canvasDesc());

    const auto invalidParent = runtime.createControl(
        controlDesc("bad_parent", arp::ui::UiControlType::Button, arp::ui::UiControlValue::none()),
        arp::ui::WidgetId::fromValue(999));
    assert(!invalidParent.ok());
    assert(invalidParent.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidParentId);

    const auto unsupported = runtime.createControl(
        controlDesc(
            "unsupported", arp::ui::UiControlType::Unknown, arp::ui::UiControlValue::none()),
        runtime.rootId());
    assert(!unsupported.ok());
    assert(unsupported.diagnostic().code == arp::ui::UiDiagnosticCode::UnsupportedControlBehavior);

    const auto created = runtime.createControl(
        controlDesc(
            "slider", arp::ui::UiControlType::Slider, arp::ui::UiControlValue::fromNumber(0.5f)),
        runtime.rootId());
    assert(created.ok());

    const auto invalidWidget = runtime.setValue(
        arp::ui::WidgetId::fromValue(777), arp::ui::UiControlValue::fromNumber(1.0f));
    assert(!invalidWidget.ok());
    assert(invalidWidget.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidWidgetId);

    const auto invalidState = runtime.setValue(
        created.widgetId(), arp::ui::UiControlValue::fromText(text("not a slider value")));
    assert(!invalidState.ok());
    assert(invalidState.diagnostic().code == arp::ui::UiDiagnosticCode::InvalidStateTransition);

    assert(runtime.beginFocusCapture(created.widgetId()).ok());
    const auto other = runtime.createControl(
        controlDesc(
            "text", arp::ui::UiControlType::TextInput, arp::ui::UiControlValue::fromText(text(""))),
        runtime.rootId());
    assert(other.ok());
    const auto duplicateCapture = runtime.beginFocusCapture(other.widgetId());
    assert(!duplicateCapture.ok());
    assert(duplicateCapture.diagnostic().code == arp::ui::UiDiagnosticCode::DuplicateFocusCapture);
}

} // namespace

int main()
{
    constructs_and_updates_all_retained_control_types();
    diagnostics_are_deterministic_for_invalid_control_runtime_contracts();
    return 0;
}
