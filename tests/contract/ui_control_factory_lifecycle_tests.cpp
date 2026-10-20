#include "tests/fixtures/ui_control_event_fixture.h"
#include "ui/ui_document.h"

#include <cassert>
using namespace arp::ui;
using F = arp::tests::UiControlEventFixture;

namespace {
const char* document()
{
    return R"yaml(
type: ui.document
contractVersion: ui.document/v1
canvas: { id: factory, width: 320, height: 180, dpi: 96 }
root:
  id: root
  type: Panel
  layer: root
  rect: { x: 0, y: 0, width: 320, height: 180 }
  style: root
  state: enabled
  event: none
  children:
    - id: button
      type: Button
      layer: button
      rect: { x: 4, y: 4, width: 80, height: 24 }
      style: primary
      state: enabled
      event: activate
      text: Before
    - id: text
      type: TextInput
      layer: text
      rect: { x: 4, y: 40, width: 100, height: 24 }
      style: body
      state: enabled
      event: edit
)yaml";
}

class FactoryButton final : public Button {
public:
    int clicks = 0;

protected:
    void onClick(UiEventContext& ctx) override
    {
        ++clicks;
        ctx.runtime().canvas().findWidget(widgetId())->setText("After");
    }
};

class NullFactory final : public IUiControlFactory {
public:
    arp::core::UniquePtr<Control> create(const UiControlDesc&) const override { return {}; }
};

void factory_contract_and_yaml_smoke()
{
    UiControlFactoryRegistry factories(1);
    assert(factories
            .registerFactory("button", arp::core::makeUnique<UiControlFactory<FactoryButton>>())
            .ok());
    assert(!factories
            .registerFactory("button", arp::core::makeUnique<UiControlFactory<FactoryButton>>())
            .ok());
    auto full =
        factories.registerFactory("other", arp::core::makeUnique<UiControlFactory<Button>>());
    assert(!full.ok() && full.diagnostic().code == UiDiagnosticCode::EventCapacityExceeded);
    UiArpuiRuntimeLoadDesc desc;
    desc.sourceYaml = document();
    desc.factories = &factories;
    auto loaded = UiArpuiRuntimeLoader().load(desc);
    assert(loaded.ok());
    auto id = loaded.widgetIdForSourceNodeId("button");
    auto* object = dynamic_cast<FactoryButton*>(loaded.runtime().findControlObject(id));
    assert(object);
    auto second = UiArpuiRuntimeLoader().load(desc);
    assert(second.ok());
    auto* independent = dynamic_cast<FactoryButton*>(
        second.runtime().findControlObject(second.widgetIdForSourceNodeId("button")));
    assert(independent && independent != object && independent->clicks == 0);
    arp::input::InputFrame frame;
    frame.add(arp::input::InputEvent::pointerMoved(8, 8));
    frame.add(arp::input::InputEvent::mouseButtonPressed(arp::input::InputMouseButton::Left));
    frame.add(arp::input::InputEvent::mouseButtonReleased(arp::input::InputMouseButton::Left));
    assert(loaded.runtime().dispatchFrame(frame, {0, 8}).ok());
    assert(object->clicks == 1);
    assert(independent->clicks == 0);
    auto layout = loaded.runtime().canvas().resolveLayout();
    auto extracted = RenderExtractor().extract(loaded.runtime().canvas(), layout);
    assert(extracted.ok());
    bool updated = false;
    for (const auto& item : extracted.queue().items())
        if (item.widgetId == id && item.kind == UIRenderItemKind::Text) {
            assert(item.text == "After");
            updated = true;
        }
    assert(updated);
    auto text = loaded.widgetIdForSourceNodeId("text");
    assert(loaded.runtime().dispatch(UiEvent::pointerDown(8, 48)).ok());
    assert(loaded.runtime().unload().ok());
    assert(loaded.runtime().controlCount() == 0 && !loaded.runtime().focusedControl().valid()
        && !loaded.runtime().capturedControl().valid());
    assert(!loaded.runtime().findControl(text));
    UiControlFactoryRegistry absent(1);
    assert(
        absent.registerFactory("absent", arp::core::makeUnique<UiControlFactory<Button>>()).ok());
    desc.factories = &absent;
    auto rejected = UiArpuiRuntimeLoader().load(desc);
    assert(!rejected.ok() && rejected.diagnostic().code == UiDiagnosticCode::InvalidFactoryBinding);
    UiControlFactoryRegistry mismatch(1);
    assert(
        mismatch.registerFactory("button", arp::core::makeUnique<UiControlFactory<Slider>>()).ok());
    desc.factories = &mismatch;
    rejected = UiArpuiRuntimeLoader().load(desc);
    assert(!rejected.ok() && rejected.diagnostic().code == UiDiagnosticCode::InvalidFactoryBinding);
    UiControlFactoryRegistry null(1);
    assert(null.registerFactory("button", arp::core::makeUnique<NullFactory>()).ok());
    desc.factories = &null;
    assert(!UiArpuiRuntimeLoader().load(desc).ok());
    desc.factories = nullptr;
    auto standard = UiArpuiRuntimeLoader().load(desc);
    assert(standard.ok());
    assert(!dynamic_cast<FactoryButton*>(
        standard.runtime().findControlObject(standard.widgetIdForSourceNodeId("button"))));
    desc.maxWidgetCount = 2;
    assert(!UiArpuiRuntimeLoader().load(desc).ok());
    desc.maxWidgetCount = 8;
    auto expandable = UiArpuiRuntimeLoader().load(desc);
    assert(expandable.ok());
    assert(expandable.runtime()
            .createControl(F::control(UiControlType::Checkbox), expandable.runtime().rootId())
            .ok());
}

class SelfRemovingButton final : public Button {
public:
    explicit SelfRemovingButton(int& destroyed) : destroyed_(destroyed) {}

    ~SelfRemovingButton() override { ++destroyed_; }

protected:
    void onClick(UiEventContext& context) override
    {
        assert(context.runtime().removeControl(widgetId()).ok());
        assert(context.runtime().findControlObject(widgetId()) == this);
        assert(!context.runtime().eventEligible(widgetId()));
        auto reentered = context.runtime().dispatch(UiEvent::pointerDown(8, 8));
        assert(!reentered.ok()
            && reentered.diagnostic().code == UiDiagnosticCode::ReentrantEventDispatch);
    }

private:
    int& destroyed_;
};

class CancellingPanel final : public Panel {
public:
    WidgetId remove;

protected:
    UiResult onPreviewEvent(const UiEvent& event, UiEventContext& ctx) override
    {
        if (event.kind == UiInputEventKind::PointerDown)
            assert(ctx.runtime().removeControl(remove).ok());
        return UiResult::success();
    }
};

class LifecycleButton final : public Button {
public:
    int gained = 0, lost = 0, cancelled = 0, captureLost = 0;

protected:
    void onFocusChanged(bool value) override { value ? ++gained : ++lost; }

    void onPointerCancelled() override { ++cancelled; }

    void onPointerCaptureLost() override { ++captureLost; }
};

void mutation_and_cancellation_contract()
{
    Runtime runtime("mutations", F::canvas(8));
    int destroyed = 0;
    auto object = arp::core::makeUnique<SelfRemovingButton>(destroyed);
    auto self =
        runtime
            .createControl(std::move(object), F::control(UiControlType::Button), runtime.rootId())
            .widgetId();
    F::click(runtime);
    assert(destroyed == 1 && !runtime.findControl(self) && !runtime.focusedControl().valid()
        && !runtime.capturedControl().valid());
    auto panel = arp::core::makeUnique<CancellingPanel>();
    auto* p = panel.get();
    auto parent = runtime
                      .createControl(std::move(panel),
                          F::control(UiControlType::Panel, 0, 0, 200, 120),
                          runtime.rootId())
                      .widgetId();
    p->remove = F::add(runtime, F::control(UiControlType::Button), parent);
    assert(runtime.dispatch(UiEvent::pointerDown(8, 8)).ok());
    assert(!runtime.findControl(p->remove) && !runtime.capturedControl().valid());
    assert(runtime.removeControl(parent).ok());
    auto life = arp::core::makeUnique<LifecycleButton>();
    auto* b = life.get();
    auto id =
        runtime.createControl(std::move(life), F::control(UiControlType::Button), runtime.rootId())
            .widgetId();
    assert(runtime.dispatch(UiEvent::pointerDown(8, 8)).ok());
    assert(b->gained == 1);
    assert(runtime.setVisible(id, false).ok());
    assert(b->lost == 1 && b->cancelled == 1 && b->captureLost == 1);
    assert(runtime.setVisible(id, true).ok());
    assert(runtime.dispatch(UiEvent::pointerDown(8, 8)).ok());
    assert(runtime.setEnabled(id, false).ok());
    assert(b->lost == 2 && b->cancelled == 2 && b->captureLost == 2);
    assert(runtime.setEnabled(id, true).ok());
    assert(runtime.dispatch(UiEvent::pointerDown(8, 8)).ok());
    assert(runtime.unload().ok());
    assert(runtime.controlCount() == 0 && !runtime.focusedControl().valid());
    Runtime bounded("bounded", F::canvas(2));
    F::add(bounded, F::control(UiControlType::Button));
    auto overflow = bounded.createControl(F::control(UiControlType::Button), bounded.rootId());
    assert(!overflow.ok() && overflow.diagnostic().code == UiDiagnosticCode::EventCapacityExceeded);
}

class CreatingButton final : public Button {
public:
    WidgetId parent, created;

protected:
    void onClick(UiEventContext& ctx) override
    {
        auto desc = F::control(UiControlType::Checkbox, 100, 0);
        auto result = ctx.runtime().createControl(desc, parent);
        assert(result.ok());
        created = result.widgetId();
        assert(!ctx.runtime().eventEligible(created));
        assert(ctx.runtime().reparentControl(created, ctx.runtime().rootId()).ok());
    }
};

class BudgetButton final : public Button {
protected:
    void onClick(UiEventContext& ctx) override
    {
        for (size_t i = 0; i < ctx.runtime().canvas().desc().maxWidgetCount; ++i)
            assert(ctx.runtime().reparentControl(widgetId(), ctx.runtime().rootId()).ok());
        const auto exhausted = ctx.runtime().reparentControl(widgetId(), ctx.runtime().rootId());
        assert(!exhausted.ok()
            && exhausted.diagnostic().code == UiDiagnosticCode::EventCapacityExceeded);
        const auto reentrant = setValue(UiControlValue::fromBool(false), UiNotifyPolicy::Notify);
        assert(reentrant.ok());
    }

    void onValueChanged(const UiControlValue&, const UiControlValue& after) override
    {
        const auto result = setValue(after, UiNotifyPolicy::Notify);
        assert(
            !result.ok() && result.diagnostic().code == UiDiagnosticCode::ReentrantEventDispatch);
    }
};

void deferred_create_reparent_and_cycles()
{
    Runtime runtime("create", F::canvas());
    auto panel = F::add(runtime, F::control(UiControlType::Panel, 0, 0, 200, 120));
    auto button = arp::core::makeUnique<CreatingButton>();
    auto* b = button.get();
    b->parent = panel;
    auto id = runtime.createControl(std::move(button), F::control(UiControlType::Button), panel)
                  .widgetId();
    F::click(runtime);
    assert(runtime.findControl(b->created));
    assert(runtime.canvas().findWidget(b->created)->parentId() == runtime.rootId());
    auto cycle = runtime.reparentControl(panel, id);
    assert(!cycle.ok() && cycle.diagnostic().code == UiDiagnosticCode::LayoutCycle);
    assert(runtime.canvas().findWidget(panel)->parentId() == runtime.rootId());
    Runtime budget("queue-budget", F::canvas(4));
    auto queued = budget.createControl(
        arp::core::makeUnique<BudgetButton>(), F::control(UiControlType::Button), budget.rootId());
    assert(queued.ok());
    F::click(budget);
    auto hidden = F::add(budget, F::control(UiControlType::Panel, 100, 0));
    assert(budget.setEnabled(hidden, false).ok());
    assert(budget.requestFocus(queued.widgetId()).ok());
    assert(budget.capturePointer(queued.widgetId()).ok());
    assert(budget.reparentControl(queued.widgetId(), hidden).ok());
    assert(!budget.focusedControl().valid() && !budget.capturedControl().valid());
}
} // namespace

int main()
{
    factory_contract_and_yaml_smoke();
    mutation_and_cancellation_contract();
    deferred_create_reparent_and_cycles();
}
