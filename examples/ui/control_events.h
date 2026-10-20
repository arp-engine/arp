#pragma once
#include "ui/ui_document.h"

#include <filesystem>

namespace arp::examples {
// Each business control overrides only its semantic response, not raw input.
class ActionButton final : public ui::Button {
public:
    ui::WidgetId status;
    ui::UiDiagnostic diagnostic;
    int clicks = 0;

protected:
    void onClick(ui::UiEventContext& ctx) override
    {
        ++clicks;
        auto result =
            ctx.runtime().setValue(status, ui::UiControlValue::fromText("Button clicked"));
        diagnostic = result.diagnostic();
    }
};

class VolumeSlider final : public ui::Slider {
public:
    ui::WidgetId status;
    ui::UiDiagnostic diagnostic;
    float lastValue = 0;

protected:
    void onValueChanged(const ui::UiControlValue&, const ui::UiControlValue& after) override
    {
        lastValue = after.number;
        auto result = runtime()->setValue(status, ui::UiControlValue::fromText("Slider changed"));
        diagnostic = result.diagnostic();
    }
};

class EntryTextInput final : public ui::TextInput {
public:
    ui::WidgetId status;
    ui::UiDiagnostic diagnostic;

protected:
    void onTextChanged(const core::pmr::String&, const core::pmr::String& after) override
    {
        auto result = runtime()->setValue(status, ui::UiControlValue::fromText(after));
        diagnostic = result.diagnostic();
    }
};

class ControlEventsExample final {
public:
    ui::UiResult load(const std::filesystem::path& path)
    {
        ui::UiControlFactoryRegistry factories(3);
        auto registered = factories.registerFactory(
            "action", core::makeUnique<ui::UiControlFactory<ActionButton>>());
        if (!registered.ok())
            return registered;
        registered = factories.registerFactory(
            "volume", core::makeUnique<ui::UiControlFactory<VolumeSlider>>());
        if (!registered.ok())
            return registered;
        registered = factories.registerFactory(
            "entry", core::makeUnique<ui::UiControlFactory<EntryTextInput>>());
        if (!registered.ok())
            return registered;
        ui::UiArpuiRuntimeLoadDesc desc;
        desc.path = path;
        desc.factories = &factories;
        loaded_ = ui::UiArpuiRuntimeLoader().load(desc);
        if (!loaded_.ok())
            return ui::UiResult::failure(loaded_.diagnostic().code, loaded_.diagnostic().message);
        button()->status = loaded_.widgetIdForSourceNodeId("action_status");
        slider()->status = loaded_.widgetIdForSourceNodeId("volume_status");
        input()->status = loaded_.widgetIdForSourceNodeId("entry_status");
        return ui::UiResult::success();
    }

    ui::UiControlRuntime& runtime() { return loaded_.runtime(); }

    ActionButton* button()
    {
        return static_cast<ActionButton*>(
            runtime().findControlObject(loaded_.widgetIdForSourceNodeId("action")));
    }

    VolumeSlider* slider()
    {
        return static_cast<VolumeSlider*>(
            runtime().findControlObject(loaded_.widgetIdForSourceNodeId("volume")));
    }

    EntryTextInput* input()
    {
        return static_cast<EntryTextInput*>(
            runtime().findControlObject(loaded_.widgetIdForSourceNodeId("entry")));
    }

    ui::UiInputFrameDispatchResult runScenario()
    {
        input::InputFrame frame;
        frame.add(input::InputEvent::pointerMoved(28, 28));
        frame.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
        frame.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
        frame.add(input::InputEvent::pointerMoved(28, 93));
        frame.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
        frame.add(input::InputEvent::pointerMoved(140, 93));
        frame.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
        frame.add(input::InputEvent::pointerMoved(28, 158));
        frame.add(input::InputEvent::mouseButtonPressed(input::InputMouseButton::Left));
        frame.add(input::InputEvent::mouseButtonReleased(input::InputMouseButton::Left));
        frame.add(input::InputEvent::textInput("Typed through InputFrame"));
        return runtime().dispatchFrame(frame, {0, 16});
    }

private:
    ui::UiArpuiRuntimeLoadResult loaded_;
};
} // namespace arp::examples
