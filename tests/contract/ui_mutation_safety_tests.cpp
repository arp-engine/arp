#include "tests/fixtures/ui_control_event_fixture.h"

#include <cassert>
#include <cstdio>
#include <new>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::ui;
using F = tests::UiControlEventFixture;

WidgetDesc widgetDesc()
{
    WidgetDesc value;
    value.name = "new widget with an owned descriptor and explicit long text";
    value.text.assign(128, 'x');
    return value;
}

void canvasCreation()
{
    std::uint64_t peak = 0;
    auto measured = core::makeShared<core::CpuMemoryQuota>();
    {
        Canvas canvas("creation", F::canvas(16));
        auto desc = widgetDesc();
        core::CpuMemoryQuotaScope scope(measured);
        canvas.createWidget(std::move(desc));
        peak = measured->peakBytes();
    }
    assert(peak && !measured->liveBytes());
    std::size_t rejected = 0;
    for (std::uint64_t bytes = 1; bytes <= peak; ++bytes) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            Canvas canvas("creation", F::canvas(16));
            const auto* root = canvas.findWidget(canvas.rootId());
            auto desc = widgetDesc();
            bool failed = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                canvas.createWidget(std::move(desc));
            } catch (const std::bad_alloc&) {
                failed = true;
            }
            assert(canvas.findWidget(canvas.rootId()) == root && root->childIds().empty());
            if (failed) {
                ++rejected;
                assert(!canvas.findWidget(WidgetId::fromValue(2)));
                auto& retry = canvas.createWidget(widgetDesc());
                assert(retry.id().value() == 3 && canvas.findWidget(retry.id()) == &retry);
            }
        }
        assert(!quota->liveBytes());
    }
    assert(rejected);
    std::printf(
        "PASS Canvas create: %zu injected byte budgets, no dangling lookup, monotonic retry ID\n",
        rejected);
}

void reparentFailure()
{
    Runtime runtime("reparent", F::canvas(16));
    const auto oldParent = F::add(runtime, F::control(UiControlType::Panel));
    const auto newParent = F::add(runtime, F::control(UiControlType::Panel, 160, 0));
    const auto child = F::add(runtime, F::control(UiControlType::Button), oldParent);
    const auto* before = runtime.findControlObject(child);
    assert(runtime.beginFocusCapture(child).ok());
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    bool failed = false;
    try {
        core::CpuMemoryQuotaScope scope(quota);
        runtime.reparentControl(child, newParent);
    } catch (const std::bad_alloc&) {
        failed = true;
    }
    assert(failed && !quota->liveBytes() && runtime.findControlObject(child) == before
        && runtime.canvas().findWidget(child)->parentId() == oldParent
        && runtime.canvas().findWidget(oldParent)->childIds().size() == 1
        && runtime.canvas().findWidget(oldParent)->childIds().front() == child
        && runtime.canvas().findWidget(newParent)->childIds().empty()
        && runtime.focusedControl() == child && runtime.capturedControl() == child);
    assert(runtime.reparentControl(child, newParent).ok());
    assert(runtime.focusedControl() == child && runtime.capturedControl() == child);
    std::puts(
        "PASS direct reparent reserve failure: original tree, Control, focus/capture and retry");
}

ControlDesc textDesc()
{
    auto desc = F::control(UiControlType::TextInput, 180, 40);
    desc.widget = widgetDesc();
    desc.initialValue = UiControlValue::fromText(core::pmr::String(96, 'v'));
    desc.maxTextBytes = 128;
    return desc;
}

void controlCreation()
{
    auto measured = core::makeShared<core::CpuMemoryQuota>();
    std::uint64_t peak = 0;
    {
        Runtime runtime("control", F::canvas(16));
        F::add(runtime, F::control(UiControlType::Button));
        auto desc = textDesc();
        core::CpuMemoryQuotaScope scope(measured);
        assert(runtime.createControl(std::move(desc), runtime.rootId()).ok());
        peak = measured->peakBytes();
    }
    assert(peak && !measured->liveBytes());
    std::size_t failures = 0;
    for (std::uint64_t bytes = 1; bytes <= peak; ++bytes) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            Runtime runtime("control", F::canvas(16));
            const auto existing = F::add(runtime, F::control(UiControlType::Button));
            const auto* before = runtime.findControlObject(existing);
            assert(runtime.beginFocusCapture(existing).ok());
            const auto children = runtime.canvas().findWidget(runtime.rootId())->childIds();
            auto desc = textDesc();
            bool failed = false;
            try {
                core::CpuMemoryQuotaScope scope(quota);
                assert(runtime.createControl(std::move(desc), runtime.rootId()).ok());
            } catch (const std::bad_alloc&) {
                failed = true;
            }
            if (failed) {
                ++failures;
                assert(runtime.controlCount() == 1 && runtime.findControlObject(existing) == before
                    && runtime.focusedControl() == existing && runtime.capturedControl() == existing
                    && runtime.canvas().findWidget(runtime.rootId())->childIds() == children);
                auto retry = runtime.createControl(textDesc(), runtime.rootId());
                assert(retry.ok() && runtime.controlCount() == 2);
                for (auto id = existing.value() + 1; id < retry.widgetId().value(); ++id)
                    assert(!runtime.findControl(WidgetId::fromValue(id))
                        && !runtime.canvas().findWidget(WidgetId::fromValue(id)));
                const auto* widget = runtime.canvas().findWidget(retry.widgetId());
                assert(widget->desc().text == runtime.findControl(retry.widgetId())->value().text);
            }
        }
        assert(!quota->liveBytes());
    }
    assert(failures);
    std::printf(
        "PASS Control create: %zu injected byte budgets; no partial object, text sync or captured-owner change\n",
        failures);
}

// Quotas are installed inside the callback so routing/layout itself succeeds.
// This exercises unpublished creation and preserves independent accepted work.
class CreatingButton final : public Button {
public:
    WidgetId parent, moveChild, moveParent, accepted, later;
    bool propagate = false, enabled = true;
    bool createRejected = false, reparentRejected = false;

protected:
    void onClick(UiEventContext& context) override
    {
        if (!enabled)
            return;
        auto& runtime = context.runtime();
        accepted = runtime.createControl(F::control(UiControlType::Label), parent).widgetId();
        assert(accepted.valid() && !runtime.eventEligible(accepted));
        auto desc = textDesc();
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        if (propagate) {
            core::CpuMemoryQuotaScope scope(quota);
            runtime.createControl(std::move(desc), parent);
            assert(false);
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            runtime.createControl(std::move(desc), parent);
        } catch (const std::bad_alloc&) {
            createRejected = true;
        }
        try {
            core::CpuMemoryQuotaScope scope(quota);
            runtime.reparentControl(moveChild, moveParent);
        } catch (const std::bad_alloc&) {
            reparentRejected = true;
        }
        later = runtime.createControl(F::control(UiControlType::Label), parent).widgetId();
        assert(later.valid() && !runtime.eventEligible(later));
    }
};

void dispatchCreation()
{
    for (bool propagate : {false, true}) {
        Runtime runtime("dispatch", F::canvas(16));
        const auto parent = F::add(runtime, F::control(UiControlType::Panel));
        const auto destination = F::add(runtime, F::control(UiControlType::Panel, 160, 0));
        auto object = core::makeUnique<CreatingButton>();
        auto* button = object.get();
        button->parent = parent;
        button->moveParent = destination;
        button->propagate = propagate;
        const auto id =
            runtime.createControl(std::move(object), F::control(UiControlType::Button), parent)
                .widgetId();
        button->moveChild = id;
        assert(runtime.beginFocusCapture(id).ok());
        UiControlInteraction click;
        click.kind = UiControlInteractionKind::Click;
        click.widgetId = id;
        bool threw = false;
        try {
            assert(runtime.interact(click).ok());
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        assert(threw == propagate && runtime.focusedControl() == id
            && runtime.capturedControl() == id);
        if (!propagate) {
            assert(button->createRejected && button->reparentRejected
                && runtime.canvas().findWidget(id)->parentId() == parent
                && runtime.legacyRuntimeForMigration().eventEligible(button->later));
        } else {
            button->enabled = false;
            // The next dispatch drains the accepted create; no reentrant lock
            // or failed create remains, and already accepted work is not lost.
            assert(runtime.interact(click).ok());
        }
        assert(runtime.legacyRuntimeForMigration().eventEligible(button->accepted)
            && runtime.canvas().findWidget(button->accepted)->parentId() == parent
            && runtime.canvas().findWidget(destination)->childIds().empty());
    }
    std::puts(
        "PASS callback caught/propagated create failure, deferred reparent rejection, accepted mutation retention and dispatch retry");
}

class RemovingParentButton final : public Button {
public:
    WidgetId parent, created;
    core::UniquePtr<core::CpuMemoryQuotaScope> scope;

protected:
    void onClick(UiEventContext& context) override
    {
        auto& runtime = context.runtime();
        created = runtime.createControl(F::control(UiControlType::Label), parent).widgetId();
        assert(created.valid() && runtime.removeControl(parent).ok());
        // This remains active during mutation draining. The unpublished leaf
        // must disappear without scratch before the independent Remove fails.
        scope =
            core::makeUnique<core::CpuMemoryQuotaScope>(core::makeShared<core::CpuMemoryQuota>(1));
    }
};

class NotifyingText final : public TextInput {
public:
    bool fail = false;
    unsigned notifications = 0;

protected:
    void onTextChanged(const core::pmr::String&, const core::pmr::String&) override
    {
        ++notifications;
        if (fail)
            throw std::bad_alloc{};
    }
};

class PublishingButton final : public Button {
public:
    WidgetId created;
    NotifyingText* receiver = nullptr;

protected:
    void onClick(UiEventContext& context) override
    {
        auto& runtime = context.runtime();
        auto object = core::makeUnique<NotifyingText>();
        receiver = object.get();
        created = runtime.createControl(std::move(object), textDesc(), runtime.rootId()).widgetId();
        const auto rejected =
            runtime.setValue(created, UiControlValue::fromText("rejected"), UiNotifyPolicy::Notify);
        assert(!rejected.ok()
            && rejected.diagnostic().code == UiDiagnosticCode::InvalidStateTransition
            && receiver->notifications == 0
            && runtime.findControl(created)->value().text != "rejected");
        assert(
            runtime.setValue(created, UiControlValue::fromText("prepared"), UiNotifyPolicy::Silent)
                .ok());
        assert(receiver->notifications == 0);
    }
};

void parentRemovalAndNotifications()
{
    {
        Runtime runtime("pending-parent", F::canvas(16));
        const auto parent = F::add(runtime, F::control(UiControlType::Panel, 160, 0));
        auto object = core::makeUnique<RemovingParentButton>();
        auto* button = object.get();
        button->parent = parent;
        const auto id =
            runtime
                .createControl(
                    std::move(object), F::control(UiControlType::Button), runtime.rootId())
                .widgetId();
        assert(runtime.beginFocusCapture(id).ok());
        UiControlInteraction click;
        click.kind = UiControlInteractionKind::Click;
        click.widgetId = id;
        bool failed = false;
        try {
            runtime.interact(click);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        button->scope.reset();
        assert(failed && !runtime.findControl(button->created)
            && !runtime.canvas().findWidget(button->created) && runtime.findControl(parent)
            && runtime.focusedControl() == id && runtime.capturedControl() == id);
        assert(runtime.dispatch(UiEvent::pointerMove(250, 150)).ok());
        assert(!runtime.findControl(parent) && runtime.controlCount() == 1);
    }
    {
        Runtime runtime("unpublished-value", F::canvas(16));
        auto object = core::makeUnique<PublishingButton>();
        auto* button = object.get();
        const auto id =
            runtime
                .createControl(
                    std::move(object), F::control(UiControlType::Button), runtime.rootId())
                .widgetId();
        F::click(runtime);
        assert(runtime.findControl(id) && runtime.findControl(button->created));
        button->receiver->fail = true;
        bool failed = false;
        try {
            runtime.setValue(button->created,
                UiControlValue::fromText("committed before callback"),
                UiNotifyPolicy::Notify);
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        assert(failed && button->receiver->notifications == 1);
        button->receiver->fail = false;
        assert(runtime
                .setValue(
                    button->created, UiControlValue::fromText("retry"), UiNotifyPolicy::Notify)
                .ok());
        assert(button->receiver->notifications == 2);
    }
    std::puts(
        "PASS allocation-free unpublished leaf cancellation, interrupted drain retry, no unpublished value callback and notification flag recovery; arbitrary callback effects are not rolled back");
}

void lifecycle()
{
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            Runtime runtime("lifetime", F::canvas(16));
            const auto first = F::add(runtime, F::control(UiControlType::Panel));
            const auto second = F::add(runtime, F::control(UiControlType::Panel));
            const auto child = F::add(runtime, F::control(UiControlType::Button), first);
            assert(runtime.reparentControl(child, second).ok());
            assert(runtime.canvas().findWidget(child)->parentId() == second);
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS 20 warmed ordinary UI creation/reparent/destruction cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        lifecycle();
    else {
        canvasCreation();
        reparentFailure();
        controlCreation();
        dispatchCreation();
        parentRemovalAndNotifications();
    }
}
