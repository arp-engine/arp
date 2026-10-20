#include "ui/ui.h"

#include <cassert>
using namespace arp::ui;

class SmokeButton final : public Button {
public:
    int calls = 0;

protected:
    UiResult onEvent(const UiEvent&, UiEventContext& context) override
    {
        ++calls;
        context.markHandled();
        return UiResult::success();
    }
};

int main()
{
    CanvasDesc desc;
    desc.viewport = UiViewport::explicitPixels(128, 64);
    desc.safeArea = UiSafeArea::none();
    desc.scalePolicy = UiScalePolicy::fixed(1);
    desc.maxWidgetCount = 4;
    UiControlRuntime runtime("smoke", desc);
    UiControlDesc control;
    control.type = UiControlType::Button;
    control.widget.acceptsFocus = true;
    auto object = arp::core::makeUnique<SmokeButton>();
    auto* ptr = object.get();
    const auto created = runtime.createControl(std::move(object), control, runtime.rootId());
    assert(created.ok());
    UiEventContext context(runtime, created.widgetId());
    assert(ptr->handleEvent(UiEvent::pointerMove(1, 1), context).ok());
    assert(ptr->calls == 1 && context.handled());
    assert(runtime.beginFocusCapture(created.widgetId()).ok());
    assert(runtime.focusedControl() == created.widgetId()
        && runtime.capturedControl() == created.widgetId());
    assert(runtime.cancelInput().ok());
    assert(!runtime.focusedControl().valid() && !runtime.capturedControl().valid());
    assert(runtime.removeControl(created.widgetId()).ok());
    assert(runtime.controlCount() == 0);
}
