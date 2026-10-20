#include "ui/ui.h"

#include <cassert>
#include <type_traits>

using namespace arp::ui;

namespace {
class DerivedButton final : public Button {
public:
    explicit DerivedButton(int& destroyed) : destroyed_(destroyed) {}

    ~DerivedButton() override { ++destroyed_; }

    int notifications = 0;

protected:
    void onValueChanged(const UiControlValue&, const UiControlValue&) override { ++notifications; }

private:
    int& destroyed_;
};

CanvasDesc canvas()
{
    CanvasDesc desc;
    desc.viewport = UiViewport::explicitPixels(320, 180);
    desc.safeArea = UiSafeArea::none();
    desc.scalePolicy = UiScalePolicy::fixed(1);
    desc.maxWidgetCount = 16;
    return desc;
}

void objects_keep_their_identity_and_embedded_state()
{
    static_assert(std::is_abstract_v<IUiEventTarget>);
    static_assert(std::has_virtual_destructor_v<Control>);
    int destroyed = 0;
    {
        Runtime runtime("objects", canvas());
        ControlDesc desc;
        desc.type = UiControlType::Button;
        desc.widget.acceptsFocus = true;
        desc.widget.localRect = LayoutRect::fromOriginSize(0, 0, 40, 20);
        desc.initialValue = UiControlValue::fromBool(false);
        auto object = arp::core::makeUnique<DerivedButton>(destroyed);
        auto* address = object.get();
        const auto created = runtime.createControl(std::move(object), desc, runtime.rootId());
        assert(created.ok());
        assert(runtime.findControlObject(created.widgetId()) == address);
        assert(runtime.findControl(created.widgetId()) == &address->state());
        assert(runtime.setValue(created.widgetId(), UiControlValue::fromBool(true)).ok());
        assert(address->value().boolean && address->notifications == 0);
        assert(runtime
                .setValue(
                    created.widgetId(), UiControlValue::fromBool(false), UiNotifyPolicy::Notify)
                .ok());
        assert(address->notifications == 1);
        assert(runtime
                .setValue(
                    created.widgetId(), UiControlValue::fromBool(false), UiNotifyPolicy::Notify)
                .ok());
        assert(address->notifications == 1);
        for (int i = 0; i < 6; ++i)
            assert(runtime.createControl(desc, runtime.rootId()).ok());
        assert(runtime.findControlObject(created.widgetId()) == address);
        assert(destroyed == 0);
        ControlDesc mismatch = desc;
        mismatch.type = UiControlType::Slider;
        const auto rejected = runtime.createControl(
            arp::core::makeUnique<DerivedButton>(destroyed), mismatch, runtime.rootId());
        assert(!rejected.ok()
            && rejected.diagnostic().code == UiDiagnosticCode::InvalidFactoryBinding);
        assert(destroyed == 1);
        assert(runtime.removeControl(created.widgetId()).ok());
        assert(!runtime.findControl(created.widgetId()));
        assert(destroyed == 2);
    }
    assert(destroyed == 2);
}
} // namespace

int main()
{
    objects_keep_their_identity_and_embedded_state();
}
