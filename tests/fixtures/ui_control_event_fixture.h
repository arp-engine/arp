#pragma once
#include "ui/ui.h"

#include <cassert>

namespace arp::tests {
class UiControlEventFixture final {
public:
    static ui::CanvasDesc canvas(size_t capacity = 32)
    {
        ui::CanvasDesc desc;
        desc.viewport = ui::UiViewport::explicitPixels(320, 180);
        desc.safeArea = ui::UiSafeArea::none();
        desc.scalePolicy = ui::UiScalePolicy::fixed(1);
        desc.maxWidgetCount = capacity;
        return desc;
    }

    static ui::ControlDesc control(
        ui::UiControlType type, float x = 0, float y = 0, float width = 80, float height = 24)
    {
        ui::ControlDesc desc;
        desc.type = type;
        desc.widget.controlType = type;
        desc.widget.localRect = ui::LayoutRect::fromOriginSize(x, y, width, height);
        desc.widget.acceptsFocus = type != ui::UiControlType::Panel
            && type != ui::UiControlType::Label && type != ui::UiControlType::ScrollView;
        return desc;
    }

    static ui::WidgetId add(ui::Runtime& runtime, ui::ControlDesc desc, ui::WidgetId parent = {})
    {
        const auto created =
            runtime.createControl(std::move(desc), parent.valid() ? parent : runtime.rootId());
        assert(created.ok());
        return created.widgetId();
    }

    static void click(ui::Runtime& runtime, float x = 8, float y = 8)
    {
        assert(runtime.dispatch(ui::UiEvent::pointerDown(x, y)).ok());
        assert(runtime.dispatch(ui::UiEvent::pointerUp(x, y)).ok());
    }
};
} // namespace arp::tests
