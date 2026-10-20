#include "ui/ui.h"

#include <cassert>
#include <type_traits>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

void typed_controls_expose_dedicated_behavior_entry_points()
{
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Button>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Checkbox>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Slider>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::TextInput>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::NumericInput>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::ScrollView>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Dropdown>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Tabs>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::TreeView>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::ColorPicker>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Image>);
    static_assert(std::is_base_of_v<arp::ui::Control, arp::ui::Text>);

    arp::ui::Button button;
    arp::ui::Checkbox checkbox;
    arp::ui::Slider slider;
    arp::ui::TextInput textInput;
    arp::ui::NumericInput numericInput;
    arp::ui::ScrollView scrollView;
    arp::ui::Dropdown dropdown;
    arp::ui::Tabs tabs;
    arp::ui::TreeView treeView;
    arp::ui::ColorPicker colorPicker;
    arp::ui::Image image;
    arp::ui::Text label;

    assert(button.controlType() == arp::ui::UiControlType::Button);
    assert(checkbox.controlType() == arp::ui::UiControlType::Checkbox);
    assert(slider.controlType() == arp::ui::UiControlType::Slider);
    assert(textInput.controlType() == arp::ui::UiControlType::TextInput);
    assert(numericInput.controlType() == arp::ui::UiControlType::NumericInput);
    assert(scrollView.controlType() == arp::ui::UiControlType::ScrollView);
    assert(dropdown.controlType() == arp::ui::UiControlType::Dropdown);
    assert(tabs.controlType() == arp::ui::UiControlType::Tabs);
    assert(treeView.controlType() == arp::ui::UiControlType::TreeView);
    assert(colorPicker.controlType() == arp::ui::UiControlType::ColorPicker);
    assert(image.controlType() == arp::ui::UiControlType::Image);
    assert(label.controlType() == arp::ui::UiControlType::Label);

    auto buttonValue = arp::ui::UiControlValue::fromBool(false);
    assert(button.click(buttonValue).ok());
    assert(buttonValue.boolean);

    auto checkboxValue = arp::ui::UiControlValue::fromBool(false);
    assert(checkbox.toggle(checkboxValue).ok());
    assert(checkboxValue.boolean);

    auto sliderValue = arp::ui::UiControlValue::fromNumber(0.0f);
    assert(slider.drag(sliderValue, 0.75f).ok());
    assert(sliderValue.number == 0.75f);

    auto textValue = arp::ui::UiControlValue::fromText(text(""));
    assert(textInput.editText(textValue, text("pilot")).ok());
    assert(textValue.text == text("pilot"));

    auto numberValue = arp::ui::UiControlValue::fromNumber(2.0f);
    assert(numericInput.step(numberValue, 3.0f).ok());
    assert(numberValue.number == 5.0f);

    auto scrollValue = arp::ui::UiControlValue::fromScroll(0.0f, 0.0f);
    assert(scrollView.scrollBy(scrollValue, 4.0f, 8.0f).ok());
    assert(scrollValue.scroll.y == 8.0f);

    auto selectionValue = arp::ui::UiControlValue::fromIndex(0);
    assert(dropdown.select(selectionValue, 2u, 4u).ok());
    assert(tabs.select(selectionValue, 1u, 4u).ok());
    assert(treeView.select(selectionValue, 3u, 4u).ok());
    assert(selectionValue.index == 3u);

    auto colorValue = arp::ui::UiControlValue::fromColor(0.0f, 0.0f, 0.0f, 1.0f);
    assert(colorPicker.pickColor(colorValue, 0.2f, 0.4f, 0.6f, 1.0f).ok());
    assert(colorValue.color.b == 0.6f);

    assert(image.validateTextureBinding(text("ui/texture")).ok());
    assert(label.setText(text("Ready")).ok());
}

} // namespace

int main()
{
    typed_controls_expose_dedicated_behavior_entry_points();
    return 0;
}
