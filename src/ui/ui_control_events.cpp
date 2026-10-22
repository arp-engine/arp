#include "core/math/geometry.h"
#include "core/text/utf8.h"
#include "ui/ui.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace arp::ui {
namespace {
UiResult failure(UiDiagnosticCode code, const char* message)
{
    return UiResult::failure(
        code, arp::core::pmr::String(message, &arp::core::defaultCpuMemoryResource()));
}

UiResult unsupported()
{
    return failure(UiDiagnosticCode::UnsupportedControlBehavior,
        "UI-CONTROL-ACTION-UNSUPPORTED: action is not defined for this object.");
}

bool sameValue(const UiControlValue& a, const UiControlValue& b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case UiControlValueKind::None:
        return true;
    case UiControlValueKind::Text:
        return a.text == b.text;
    case UiControlValueKind::Bool:
        return a.boolean == b.boolean;
    case UiControlValueKind::Number:
        return a.number == b.number;
    case UiControlValueKind::Index:
        return a.index == b.index;
    case UiControlValueKind::Scroll:
        return a.scroll.x == b.scroll.x && a.scroll.y == b.scroll.y;
    case UiControlValueKind::Color:
        return a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b
            && a.color.a == b.color.a;
    }
    return false;
}

bool activationKey(const UiEvent& event)
{
    return event.kind == UiInputEventKind::KeyboardActivate
        || (event.kind == UiInputEventKind::KeyDown && !event.repeat
            && (event.key == arp::input::InputKey::Enter || event.key == arp::input::InputKey::Space
                || event.key == arp::input::InputKey::NumpadEnter));
}

UiResult commit(Control& control, UiControlValue value, UiEventContext& context)
{
    const auto result = control.setValue(std::move(value), UiNotifyPolicy::Notify);
    if (result.ok())
        context.markHandled();
    return result;
}
} // namespace

Control::Control() = default;

UiResult Control::handleEvent(const UiEvent& event, UiEventContext& context)
{
    if (context.phase() == UiEventPhase::Target) {
        if (event.kind == UiInputEventKind::FocusGained)
            onFocusChanged(true);
        if (event.kind == UiInputEventKind::FocusLost)
            onFocusChanged(false);
        if (event.kind == UiInputEventKind::PointerCaptureLost)
            onPointerCaptureLost();
        if (event.kind == UiInputEventKind::PointerCancel)
            onPointerCancelled();
    }
    switch (context.phase()) {
    case UiEventPhase::Capture:
        return onPreviewEvent(event, context);
    case UiEventPhase::Target:
        return onEvent(event, context);
    case UiEventPhase::Bubble:
        return onBubbleEvent(event, context);
    }
    return UiResult::success();
}

UiResult Control::applyDefaultAction(const UiEvent&, UiEventContext&)
{
    return UiResult::success();
}

UiResult Control::applyInteraction(const UiControlInteraction&, UiEventContext&)
{
    return unsupported();
}

UiResult Control::setValue(UiControlValue value, UiNotifyPolicy notify)
{
    if (!owner_)
        return failure(UiDiagnosticCode::InvalidWidgetId,
            "UI-CONTROL-NOT-MOUNTED: value update requires a runtime object.");
    const auto result = owner_->setValue(widgetId(), std::move(value), notify);
    return result.ok() ? UiResult::success()
                       : UiResult::failure(result.diagnostic().code, result.diagnostic().message);
}

void Control::notifyValueChanged(const UiControlValue& previous)
{
    if (!sameValue(previous, value())) {
        notifying_ = true;
        onValueChanged(previous, value());
        notifying_ = false;
    }
}

UiResult Control::press(const UiEvent& event, UiEventContext& context)
{
    if (event.kind != UiInputEventKind::PointerDown
        || event.button != arp::input::InputMouseButton::Left)
        return UiResult::success();
    const auto* widget = context.runtime().canvas().findWidget(widgetId());
    auto result = widget && widget->acceptsFocus() ? context.requestFocus() : UiResult::success();
    if (result.ok())
        result = context.capturePointer();
    if (result.ok())
        context.markHandled();
    return result;
}

bool Control::activates(const UiEvent& event, UiEventContext& context) const
{
    return activationKey(event)
        || (event.kind == UiInputEventKind::PointerUp
            && event.button == arp::input::InputMouseButton::Left
            && context.runtime().pressedControl() == widgetId()
            && context.runtime().capturedControl() == widgetId()
            && context.runtime().pointerInside(widgetId(), event));
}

UiEventContext::UiEventContext(UiControlRuntime& runtime, WidgetId target)
    : runtime_(&runtime), target_(target), current_(target)
{}

UiResult UiEventContext::requestFocus()
{
    return runtime_->requestFocus(current_);
}

UiResult UiEventContext::capturePointer()
{
    return runtime_->capturePointer(current_, button_);
}

UiResult UiEventContext::releasePointer()
{
    return runtime_->releasePointer();
}

UiResult Button::applyDefaultAction(const UiEvent& event, UiEventContext& context)
{
    if (activates(event, context)) {
        auto result = commit(*this, UiControlValue::fromBool(true), context);
        if (result.ok() && context.runtime().eventEligible(widgetId())) {
            context.runtime().markClicked();
            onClick(context);
        }
        return result;
    }
    if (event.kind == UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId())
        context.markHandled();
    return press(event, context);
}

UiResult Button::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    if (event.kind != UiControlInteractionKind::Click)
        return unsupported();
    auto result = commit(*this, UiControlValue::fromBool(true), context);
    if (result.ok() && context.runtime().eventEligible(widgetId())) {
        context.runtime().markClicked();
        onClick(context);
    }
    return result;
}

UiResult Checkbox::applyDefaultAction(const UiEvent& event, UiEventContext& context)
{
    if (activates(event, context))
        return commit(*this, UiControlValue::fromBool(!value().boolean), context);
    if (event.kind == UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId())
        context.markHandled();
    return press(event, context);
}

UiResult Checkbox::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    return event.kind == UiControlInteractionKind::Toggle
        ? commit(*this, UiControlValue::fromBool(!value().boolean), context)
        : unsupported();
}

UiResult Slider::applyDefaultAction(const UiEvent& event, UiEventContext& context)
{
    if (event.kind == UiInputEventKind::PointerDown)
        return press(event, context);
    if (event.kind == UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId())
        context.markHandled();
    if (event.kind != UiInputEventKind::PointerMove
        || context.runtime().capturedControl() != widgetId())
        return UiResult::success();
    const auto layout = context.runtime().canvas().resolveLayout();
    if (!layout.ok())
        return UiResult::failure(layout.diagnostic().code, layout.diagnostic().message);
    const auto* resolved = layout.findWidget(widgetId());
    if (!resolved || resolved->rect.width <= 0.0f)
        return failure(UiDiagnosticCode::InvalidLayoutAnchor,
            "UI-SLIDER-ZERO-WIDTH: drag requires a positive resolved width.");
    return commit(*this,
        UiControlValue::fromNumber(
            std::clamp((event.x - resolved->rect.x) / resolved->rect.width, 0.0f, 1.0f)),
        context);
}

UiResult Slider::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    if (event.kind != UiControlInteractionKind::DragSlider)
        return unsupported();
    return commit(*this, UiControlValue::fromNumber(event.number), context);
}

UiResult TextInput::applyDefaultAction(const UiEvent& event, UiEventContext& context)
{
    if (event.kind == UiInputEventKind::PointerDown)
        return press(event, context);
    const bool backspace = event.kind == UiInputEventKind::Backspace
        || (event.kind == UiInputEventKind::KeyDown
            && event.key == arp::input::InputKey::Backspace);
    if (event.kind == UiInputEventKind::TextInput || backspace) {
        if (const auto* diagnostic = arp::core::Utf8Decoder::validate(value().text))
            return failure(UiDiagnosticCode::InvalidUtf8Input, diagnostic);
        arp::core::pmr::String updated(value().text, &arp::core::defaultCpuMemoryResource());
        if (backspace) {
            if (!updated.empty()) {
                size_t boundary = updated.size() - 1u;
                while (boundary
                    && arp::core::isUtf8ContinuationByte(
                        static_cast<unsigned char>(updated[boundary])))
                    --boundary;
                updated.resize(boundary);
            }
        } else {
            if (const auto* diagnostic = arp::core::Utf8Decoder::validate(event.text))
                return failure(UiDiagnosticCode::InvalidUtf8Input, diagnostic);
            if (event.text.size() > 64u - std::min(updated.size(), size_t(64u)))
                return failure(
                    UiDiagnosticCode::TextEditOverflow, "UI text input limit is 64 UTF-8 bytes.");
            updated += event.text;
        }
        return commit(*this, UiControlValue::fromText(std::move(updated)), context);
    }
    // Text arrives through TextInput, never by interpreting key codes as characters.
    if (event.kind == UiInputEventKind::KeyDown || event.kind == UiInputEventKind::KeyUp)
        context.markHandled();
    if (event.kind == UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId())
        context.markHandled();
    return UiResult::success();
}

UiResult TextInput::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    return event.kind == UiControlInteractionKind::EditText
        ? commit(*this, UiControlValue::fromText(event.text), context)
        : unsupported();
}

UiResult ScrollView::applyDefaultAction(const UiEvent& event, UiEventContext& context)
{
    if (event.kind != UiInputEventKind::ScrollWheel
        && event.kind != UiInputEventKind::ScrollThumbDrag)
        return UiResult::success();
    return commit(*this,
        UiControlValue::fromScroll(value().scroll.x,
            value().scroll.y
                + (event.kind == UiInputEventKind::ScrollWheel ? -event.delta : event.delta)),
        context);
}

UiResult ScrollView::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    return event.kind == UiControlInteractionKind::ScrollBy
        ? commit(*this,
              UiControlValue::fromScroll(value().scroll.x + event.x, value().scroll.y + event.y),
              context)
        : unsupported();
}

UiResult NumericInput::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    return event.kind == UiControlInteractionKind::StepNumber
        ? commit(*this, UiControlValue::fromNumber(value().number + event.number), context)
        : unsupported();
}

UiResult Dropdown::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    if (event.kind != UiControlInteractionKind::Select)
        return unsupported();
    auto next = value();
    const auto result = select(next, event.index, event.optionCount);
    return result.ok() ? commit(*this, std::move(next), context) : result;
}

UiResult Tabs::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    if (event.kind != UiControlInteractionKind::Select)
        return unsupported();
    auto next = value();
    const auto result = select(next, event.index, event.optionCount);
    return result.ok() ? commit(*this, std::move(next), context) : result;
}

UiResult TreeView::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    if (event.kind != UiControlInteractionKind::Select)
        return unsupported();
    auto next = value();
    const auto result = select(next, event.index, event.optionCount);
    return result.ok() ? commit(*this, std::move(next), context) : result;
}

UiResult ColorPicker::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    return event.kind == UiControlInteractionKind::PickColor
        ? commit(*this,
              UiControlValue::fromColor(event.color.r, event.color.g, event.color.b, event.color.a),
              context)
        : unsupported();
}

UiResult Image::applyDefaultAction(const UiEvent& event, UiEventContext& context)
{
    if (activates(event, context)) {
        context.markHandled();
        onHit(context);
        return UiResult::success();
    }
    if (event.kind == UiInputEventKind::PointerUp
        && context.runtime().capturedControl() == widgetId())
        context.markHandled();
    return press(event, context);
}

UiResult Image::applyInteraction(const UiControlInteraction& event, UiEventContext& context)
{
    if (event.kind != UiControlInteractionKind::ImageHitTest)
        return unsupported();
    context.markHandled();
    onHit(context);
    return UiResult::success();
}

void UiControlRuntime::syncValue(Control& control)
{
    auto* widget = canvas_.findWidget(control.widgetId());
    if (widget && control.value().kind == UiControlValueKind::Text)
        widget->setText(control.value().text);
    else if (widget && control.controlType() == UiControlType::Dropdown
        && control.value().kind == UiControlValueKind::Index
        && control.value().index < control.desc().optionLabels.size())
        widget->setText(control.desc().optionLabels[control.value().index]);
}

UiResult Canvas::removeSubtree(WidgetId id)
{
    auto* target = findWidget(id);
    if (!target || id == rootId_)
        return failure(UiDiagnosticCode::InvalidWidgetId,
            "UI-REMOVE-INVALID: cannot remove an unknown widget or canvas root.");
    arp::core::pmr::Vector<WidgetId> removed;
    removed.reserve(desc_.maxWidgetCount);
    collectTraversal(*target, removed);
    if (auto* parent = findWidget(target->parentId()))
        parent->removeChild(id);
    for (const auto widget : removed)
        lookup_.erase(widget.value());
    widgets_.erase(std::remove_if(widgets_.begin(),
                       widgets_.end(),
                       [&](const auto& widget) {
                           return lookup_.find(widget->id().value()) == lookup_.end();
                       }),
        widgets_.end());
    return UiResult::success();
}

bool UiControlRuntime::pendingRemoval(WidgetId id) const
{
    for (const auto root : pendingRemoval_.values())
        if (canvas_.isInSubtree(id, root))
            return true;
    return false;
}

bool UiControlRuntime::eventEligible(WidgetId id) const
{
    if (pendingRemoval(id))
        return false;
    for (const auto& mutation : mutations_.values())
        if (mutation.kind == MutationKind::Create && mutation.widget == id)
            return false;
    auto current = id;
    for (size_t depth = 0; current.valid() && depth < canvas_.desc().maxWidgetCount; ++depth) {
        const auto* widget = canvas_.findWidget(current);
        if (!widget || !widget->visible() || !widget->enabled())
            return false;
        if (current == rootId())
            return true;
        current = widget->parentId();
    }
    return false;
}

bool UiControlRuntime::pointerInside(WidgetId id, const UiEvent& event) const
{
    return event.hasPosition && eventLayout_.clipRect(id).contains({event.x, event.y});
}

LayoutRect CanvasLayoutResult::clipRect(WidgetId id) const
{
    const auto* node = findWidget(id);
    if (!node)
        return {};
    auto clipped =
        arp::core::Rect2<float>{node->rect.x, node->rect.y, node->rect.width, node->rect.height};
    for (size_t depth = 0; node->parentId.valid(); ++depth) {
        if (depth >= widgets_.size())
            return {};
        node = findWidget(node->parentId);
        if (!node)
            return {};
        clipped = arp::core::intersectRect(
            clipped, {node->rect.x, node->rect.y, node->rect.width, node->rect.height});
    }
    return {clipped.x, clipped.y, clipped.width, clipped.height};
}

UiResult UiControlRuntime::directEvent(WidgetId id, UiInputEventKind kind)
{
    auto* control = findControlObject(id);
    if (!control)
        return UiResult::success();
    UiEvent event;
    event.kind = kind;
    event.hasPosition = false;
    UiEventContext context(*this, id);
    return control->handleEvent(event, context);
}

UiResult UiControlRuntime::requestFocus(WidgetId id)
{
    if (id.valid()) {
        const auto* widget = canvas_.findWidget(id);
        if (!findControlObject(id) || !eventEligible(id) || !widget->acceptsFocus())
            return failure(UiDiagnosticCode::InvalidWidgetId,
                "UI-FOCUS-INELIGIBLE: focus requires an enabled, visible, focusable control.");
    }
    if (focusedControl_ == id)
        return UiResult::success();
    const bool outer = !dispatching_;
    if (outer)
        dispatching_ = true;
    const auto previous = focusedControl_;
    focusedControl_ = id;
    auto result = directEvent(previous, UiInputEventKind::FocusLost);
    if (result.ok() && id == focusedControl_ && eventEligible(id))
        result = directEvent(id, UiInputEventKind::FocusGained);
    if (outer) {
        const auto finish = finishMutations();
        if (result.ok())
            result = finish;
    }
    return result;
}

UiResult UiControlRuntime::capturePointer(WidgetId id, arp::input::InputMouseButton button)
{
    if (static_cast<size_t>(button) > static_cast<size_t>(arp::input::InputMouseButton::Middle))
        return failure(UiDiagnosticCode::InvalidStateTransition, "UI-INPUT-BUTTON-INVALID");
    if (!findControlObject(id) || !eventEligible(id))
        return failure(UiDiagnosticCode::InvalidWidgetId,
            "UI-CAPTURE-INELIGIBLE: capture requires an enabled visible control.");
    if (capturedControl_.valid() && capturedControl_ != id)
        return failure(UiDiagnosticCode::DuplicateFocusCapture,
            "UI-CAPTURE-OWNED: release the current capture first.");
    capturedControl_ = id;
    capturedButton_ = button;
    pressedControl_ = id;
    return UiResult::success();
}

UiResult UiControlRuntime::releasePointer()
{
    const bool outer = !dispatching_;
    if (outer)
        dispatching_ = true;
    const auto previous = capturedControl_;
    capturedControl_ = WidgetId::invalid();
    pressedControl_ = WidgetId::invalid();
    auto result = directEvent(previous, UiInputEventKind::PointerCaptureLost);
    if (outer) {
        const auto finish = finishMutations();
        if (result.ok())
            result = finish;
    }
    return result;
}

UiResult UiControlRuntime::cancelSubtree(WidgetId id)
{
    auto belongs = [&](WidgetId candidate) {
        return canvas_.isInSubtree(candidate, id);
    };
    auto result = UiResult::success();
    if (belongs(capturedControl_)) {
        const auto previous = capturedControl_;
        capturedControl_ = WidgetId::invalid();
        pressedControl_ = WidgetId::invalid();
        result = directEvent(previous, UiInputEventKind::PointerCancel);
        const auto lost = directEvent(previous, UiInputEventKind::PointerCaptureLost);
        if (result.ok())
            result = lost;
    }
    if (belongs(focusedControl_)) {
        const auto previous = focusedControl_;
        focusedControl_ = WidgetId::invalid();
        const auto lost = directEvent(previous, UiInputEventKind::FocusLost);
        if (result.ok())
            result = lost;
    }
    if (belongs(hoveredControl_)) {
        const auto previous = hoveredControl_;
        hoveredControl_ = WidgetId::invalid();
        const auto lost = directEvent(previous, UiInputEventKind::PointerLeave);
        if (result.ok())
            result = lost;
    }
    return result;
}

UiResult UiControlRuntime::cancelInput()
{
    const bool outer = !dispatching_;
    if (outer)
        dispatching_ = true;
    pointerValid_ = false;
    inputHeldKeys_.clear();
    inputUiKeys_.clear();
    std::fill(std::begin(inputHeldButtons_), std::end(inputHeldButtons_), false);
    std::fill(std::begin(inputUiButtons_), std::end(inputUiButtons_), false);
    auto result = cancelSubtree(rootId());
    if (outer) {
        const auto finish = finishMutations();
        if (result.ok())
            result = finish;
    }
    return result;
}

UiResult UiControlRuntime::unload()
{
    if (dispatching_)
        return failure(UiDiagnosticCode::ReentrantEventDispatch,
            "UI-UNLOAD-DURING-DISPATCH: request subtree removal from a callback.");
    unloading_ = true;
    auto result = cancelInput();
    while (!controls_.empty()) {
        const auto removed = removeControl(controls_.front()->widgetId());
        if (!removed.ok()) {
            if (result.ok())
                result = removed;
            break;
        }
    }
    inputWindowBound_ = false;
    unloading_ = false;
    return result;
}

UiResult UiControlRuntime::setVisible(WidgetId id, bool visible)
{
    auto* widget = canvas_.findWidget(id);
    if (!widget)
        return failure(UiDiagnosticCode::InvalidWidgetId, "UI-VISIBILITY-INVALID-WIDGET");
    const bool outer = !dispatching_;
    if (outer)
        dispatching_ = true;
    widget->setVisible(visible);
    auto result = !visible ? cancelSubtree(id) : UiResult::success();
    if (outer) {
        const auto finish = finishMutations();
        if (result.ok())
            result = finish;
    }
    return result;
}

UiResult UiControlRuntime::setEnabled(WidgetId id, bool enabled)
{
    auto* widget = canvas_.findWidget(id);
    if (!widget)
        return failure(UiDiagnosticCode::InvalidWidgetId, "UI-ENABLED-INVALID-WIDGET");
    const bool outer = !dispatching_;
    if (outer)
        dispatching_ = true;
    widget->setEnabled(enabled);
    auto result = !enabled ? cancelSubtree(id) : UiResult::success();
    if (outer) {
        const auto finish = finishMutations();
        if (result.ok())
            result = finish;
    }
    return result;
}

UiResult UiControlRuntime::removeControl(WidgetId id)
{
    if (!findControlObject(id) || pendingRemoval(id))
        return failure(UiDiagnosticCode::InvalidWidgetId, "UI-REMOVE-INVALID-WIDGET");
    if (mutations_.values().size() >= canvas_.desc().maxWidgetCount
        || pendingRemoval_.values().size() >= canvas_.desc().maxWidgetCount)
        return failure(UiDiagnosticCode::EventCapacityExceeded, "UI-MUTATION-BUDGET-EXCEEDED");
    pendingRemoval_.append(id);
    mutations_.append({MutationKind::Remove, id, {}});
    return dispatching_ ? UiResult::success() : finishMutations();
}

UiResult UiControlRuntime::reparentControl(WidgetId id, WidgetId parent)
{
    if (!findControlObject(id) || !canvas_.findWidget(parent) || pendingRemoval(id)
        || pendingRemoval(parent))
        return failure(UiDiagnosticCode::InvalidParentId, "UI-REPARENT-INVALID-WIDGET");
    if (!dispatching_) {
        const auto moved = canvas_.reparent(id, parent);
        if (!moved.ok() || eventEligible(id))
            return moved;
        dispatching_ = true;
        auto cancelled = cancelSubtree(id);
        const auto finished = finishMutations();
        return cancelled.ok() ? finished : cancelled;
    }
    if (mutations_.values().size() >= canvas_.desc().maxWidgetCount)
        return failure(UiDiagnosticCode::EventCapacityExceeded, "UI-MUTATION-BUDGET-EXCEEDED");
    mutations_.append({MutationKind::Reparent, id, parent});
    return UiResult::success();
}

UiResult UiControlRuntime::finishMutations()
{
    dispatching_ = true;
    auto result = UiResult::success();
    for (size_t index = 0; index < mutations_.values().size(); ++index) {
        const auto mutation = mutations_.values()[index];
        if (mutation.kind == MutationKind::Remove) {
            const auto cancelled = cancelSubtree(mutation.widget);
            if (result.ok())
                result = cancelled;
            if (canvas_.findWidget(mutation.widget)) {
                const auto removed = canvas_.removeSubtree(mutation.widget);
                if (result.ok())
                    result = removed;
                controls_.erase(std::remove_if(controls_.begin(),
                                    controls_.end(),
                                    [&](const auto& object) {
                                        return !canvas_.findWidget(object->widgetId());
                                    }),
                    controls_.end());
            }
        } else {
            // A parent removed later in this dispatch invalidates an unpublished child.
            if (pendingRemoval(mutation.parent) || pendingRemoval(mutation.widget)) {
                if (mutation.kind == MutationKind::Create && canvas_.findWidget(mutation.widget)) {
                    canvas_.removeSubtree(mutation.widget);
                    controls_.erase(std::remove_if(controls_.begin(),
                                        controls_.end(),
                                        [&](const auto& object) {
                                            return object->widgetId() == mutation.widget;
                                        }),
                        controls_.end());
                }
                continue;
            }
            const auto moved = canvas_.reparent(mutation.widget, mutation.parent);
            if (result.ok())
                result = moved;
            if (moved.ok() && !eventEligible(mutation.widget)) {
                const auto cancelled = cancelSubtree(mutation.widget);
                if (result.ok())
                    result = cancelled;
            }
        }
    }
    mutations_.clear();
    pendingRemoval_.clear();
    dispatching_ = false;
    return result;
}

WidgetId UiControlRuntime::nextFocus(bool backwards) const
{
    const auto traversal = canvas_.traversal();
    arp::core::pmr::Vector<WidgetId> focusable;
    focusable.reserve(canvas_.desc().maxWidgetCount);
    for (const auto id : traversal) {
        const auto* widget = canvas_.findWidget(id);
        if (findControlObject(id) && widget->acceptsFocus() && eventEligible(id))
            focusable.push_back(id);
    }
    if (focusable.empty())
        return WidgetId::invalid();
    const auto found = std::find(focusable.begin(), focusable.end(), focusedControl_);
    if (found == focusable.end())
        return backwards ? focusable.back() : focusable.front();
    const auto index = static_cast<size_t>(found - focusable.begin());
    return focusable[(index + (backwards ? focusable.size() - 1u : 1u)) % focusable.size()];
}

UiControlRuntime::~UiControlRuntime()
{
    unloading_ = true;
    cancelInput();
    lifetime_.advance();
}

UiInputDispatchResult UiControlRuntime::dispatch(UiEvent event)
{
    if (dispatching_)
        return UiInputDispatchResult::failure(UiDiagnosticCode::ReentrantEventDispatch,
            arp::core::pmr::String("UI-EVENT-REENTRANT: recursive dispatch is forbidden.",
                &arp::core::defaultCpuMemoryResource()));
    if ((event.kind == UiInputEventKind::PointerDown || event.kind == UiInputEventKind::PointerUp)
        && static_cast<size_t>(event.button)
            > static_cast<size_t>(arp::input::InputMouseButton::Middle))
        return UiInputDispatchResult::failure(UiDiagnosticCode::InvalidStateTransition,
            arp::core::pmr::String(
                "UI-INPUT-BUTTON-INVALID", &arp::core::defaultCpuMemoryResource()));
    if (event.kind == UiInputEventKind::ControlInteraction && !event.interaction)
        return UiInputDispatchResult::failure(UiDiagnosticCode::InvalidStateTransition,
            arp::core::pmr::String(
                "UI-INTERACTION-PAYLOAD-MISSING", &arp::core::defaultCpuMemoryResource()));
    if (event.kind == UiInputEventKind::WindowFocusLost) {
        const auto result = cancelInput();
        return result.ok()
            ? UiInputDispatchResult::success(
                  arp::input::InputConsumption::none(), {}, {}, {}, {}, false)
            : UiInputDispatchResult::failure(result.diagnostic().code, result.diagnostic().message);
    }
    eventLayout_ = canvas_.resolveLayout();
    if (!eventLayout_.ok())
        return UiInputDispatchResult::failure(
            eventLayout_.diagnostic().code, eventLayout_.diagnostic().message);
    dispatching_ = true;
    clicked_ = false;
    auto result = UiResult::success();
    // Also reconcile legacy callers that changed Widget visibility directly.
    if (capturedControl_.valid() && !eventEligible(capturedControl_))
        result = cancelSubtree(capturedControl_);
    if (focusedControl_.valid() && !eventEligible(focusedControl_)) {
        const auto cancelled = cancelSubtree(focusedControl_);
        if (result.ok())
            result = cancelled;
    }
    const bool pointer = event.kind == UiInputEventKind::PointerMove
        || event.kind == UiInputEventKind::PointerDown || event.kind == UiInputEventKind::PointerUp
        || event.kind == UiInputEventKind::ScrollWheel;
    const bool tab = event.kind == UiInputEventKind::KeyboardNext
        || event.kind == UiInputEventKind::KeyboardPrevious
        || (event.kind == UiInputEventKind::KeyDown && event.key == arp::input::InputKey::Tab
            && !event.repeat);
    WidgetId target;
    if (pointer) {
        if (!event.hasPosition || !std::isfinite(event.x) || !std::isfinite(event.y))
            result = failure(UiDiagnosticCode::MissingPointerPosition,
                "UI-POINTER-POSITION-MISSING: no confirmed absolute position.");
        if (result.ok()) {
            const auto hit = canvas_.hitTest(eventLayout_, {event.x, event.y});
            auto hovered =
                hit.ok() && pointerInside(hit.widgetId(), event) && eventEligible(hit.widgetId())
                ? hit.widgetId()
                : WidgetId::invalid();
            if (hovered != hoveredControl_) {
                const auto previous = hoveredControl_;
                hoveredControl_ = hovered;
                result = directEvent(previous, UiInputEventKind::PointerLeave);
                if (result.ok() && eventEligible(hovered))
                    result = directEvent(hovered, UiInputEventKind::PointerEnter);
            }
            target = (event.kind != UiInputEventKind::ScrollWheel && capturedControl_.valid())
                ? capturedControl_
                : hoveredControl_;
        }
    } else if (event.kind == UiInputEventKind::ScrollThumbDrag
        || event.kind == UiInputEventKind::ControlInteraction) {
        target = event.widgetId;
        if (!findControlObject(target))
            result = failure(UiDiagnosticCode::InvalidWidgetId, "UI-SCROLL-TARGET-MISSING");
    } else
        target = focusedControl_;
    UiEventContext context(*this, target);
    context.button_ = event.button;
    route_.clear();
    if (target.valid() && eventEligible(target)) {
        for (auto id = target; id.valid();) {
            if (route_.size() >= canvas_.desc().maxWidgetCount) {
                result = failure(
                    UiDiagnosticCode::EventCapacityExceeded, "UI-EVENT-ROUTE-BUDGET-EXCEEDED");
                break;
            }
            route_.push_back(id);
            const auto* widget = canvas_.findWidget(id);
            id = widget ? widget->parentId() : WidgetId::invalid();
        }
    }
    auto deliver = [&](WidgetId id, UiEventPhase phase) {
        if (!result.ok() || context.propagationStopped() || !eventEligible(id))
            return;
        auto* object = findControlObject(id);
        if (!object)
            return;
        context.current_ = id;
        context.phase_ = phase;
        const auto* resolved = eventLayout_.findWidget(id);
        context.local_ = event.hasPosition && resolved
            ? UiPoint{event.x - resolved->rect.x, event.y - resolved->rect.y}
            : UiPoint{};
        result = object->handleEvent(event, context);
    };
    for (size_t index = route_.size(); index > 1u; --index)
        deliver(route_[index - 1u], UiEventPhase::Capture);
    if (!route_.empty())
        deliver(route_.front(), UiEventPhase::Target);
    for (size_t index = 1u; index < route_.size(); ++index)
        deliver(route_[index], UiEventPhase::Bubble);
    if (result.ok() && !context.defaultPrevented()) {
        if (tab) {
            const auto next =
                nextFocus(event.kind == UiInputEventKind::KeyboardPrevious || event.shift);
            if (next.valid()) {
                result = requestFocus(next);
                context.markHandled();
            }
        } else {
            const bool scroll = event.kind == UiInputEventKind::ScrollWheel;
            // Handled is independent from preventDefault; the target's default still runs.
            for (size_t index = 0; index < route_.size(); ++index) {
                const auto id = route_[index];
                const bool previouslyHandled = context.handled_;
                if (scroll)
                    context.handled_ = false;
                if (auto* object = findControlObject(id); object && eventEligible(id)) {
                    context.current_ = id;
                    context.phase_ = UiEventPhase::Target;
                    const auto* resolved = eventLayout_.findWidget(id);
                    context.local_ = event.hasPosition && resolved
                        ? UiPoint{event.x - resolved->rect.x, event.y - resolved->rect.y}
                        : UiPoint{};
                    result = event.kind == UiInputEventKind::ControlInteraction
                        ? object->applyInteraction(*event.interaction, context)
                        : object->applyDefaultAction(event, context);
                }
                const bool defaultHandled = context.handled();
                context.handled_ = previouslyHandled || defaultHandled;
                if (!scroll || defaultHandled || !result.ok())
                    break;
            }
        }
    }
    // Release is cleanup even when a handler prevented the activation default.
    if (event.kind == UiInputEventKind::PointerUp && event.button == capturedButton_
        && capturedControl_.valid()) {
        context.markHandled();
        const auto released = releasePointer();
        if (result.ok())
            result = released;
    }
    if (!result.ok() && pointer && capturedControl_.valid()) {
        const auto cancelled = cancelSubtree(capturedControl_);
        (void)cancelled;
    }
    arp::input::InputConsumption consumption = arp::input::InputConsumption::none();
    if (context.handled()) {
        if (pointer || event.kind == UiInputEventKind::ScrollThumbDrag)
            consumption.consumePointer();
        else if (event.kind == UiInputEventKind::TextInput)
            consumption.consumeText();
        else
            consumption.consumeKeyboard();
    }
    const auto finished = finishMutations();
    if (result.ok())
        result = finished;
    return result.ok()
        ? UiInputDispatchResult::success(consumption,
              hoveredControl_,
              pressedControl_,
              focusedControl_,
              capturedControl_,
              clicked_)
        : UiInputDispatchResult::failure(result.diagnostic().code, result.diagnostic().message);
}

UiInputFrameDispatchResult UiControlRuntime::dispatchFrame(
    const arp::input::InputFrame& frame, UiInputFrameRouteDesc desc)
{
    UiInputFrameDispatchResult output;
    auto fail = [&](UiDiagnosticCode code, const char* message) {
        output.diagnostic = {
            code, arp::core::pmr::String(message, &arp::core::defaultCpuMemoryResource())};
    };
    if (dispatching_) {
        fail(UiDiagnosticCode::ReentrantEventDispatch, "UI-EVENT-REENTRANT");
        return output;
    }
    if (!desc.maxEvents || frame.events().size() > desc.maxEvents) {
        fail(UiDiagnosticCode::EventCapacityExceeded, "UI-INPUT-FRAME-BUDGET-EXCEEDED");
        return output;
    }
    if (inputWindowBound_ && inputWindow_ != desc.windowId) {
        fail(UiDiagnosticCode::InvalidStateTransition,
            "UI-INPUT-WINDOW-MISMATCH: one runtime route belongs to one window.");
        return output;
    }
    inputWindowBound_ = true;
    inputWindow_ = desc.windowId;
    for (const auto& raw : frame.events()) {
        if ((raw.kind == arp::input::InputEventKind::KeyPressed
                || raw.kind == arp::input::InputEventKind::KeyReleased)
            && static_cast<size_t>(raw.key) > static_cast<size_t>(arp::input::InputKey::F24)) {
            fail(UiDiagnosticCode::InvalidStateTransition, "UI-INPUT-KEY-INVALID");
            return output;
        }
        if ((raw.kind == arp::input::InputEventKind::MouseButtonPressed
                || raw.kind == arp::input::InputEventKind::MouseButtonReleased)
            && static_cast<size_t>(raw.mouseButton)
                > static_cast<size_t>(arp::input::InputMouseButton::Middle)) {
            fail(UiDiagnosticCode::InvalidStateTransition, "UI-INPUT-BUTTON-INVALID");
            return output;
        }
    }
    output.eventConsumption.reserve(desc.maxEvents);
    for (const auto& raw : frame.events()) {
        output.eventConsumption.push_back(arp::input::InputConsumption::none());
        if (raw.windowId != desc.windowId)
            continue;
        UiEvent event;
        event.windowId = raw.windowId;
        event.hasPosition = false;
        auto keyHeld = [&](arp::input::InputKey key) {
            return std::find(inputHeldKeys_.begin(), inputHeldKeys_.end(), key)
                != inputHeldKeys_.end();
        };
        event.shift =
            keyHeld(arp::input::InputKey::LeftShift) || keyHeld(arp::input::InputKey::RightShift);
        event.control = keyHeld(arp::input::InputKey::LeftControl)
            || keyHeld(arp::input::InputKey::RightControl);
        event.alt =
            keyHeld(arp::input::InputKey::LeftAlt) || keyHeld(arp::input::InputKey::RightAlt);
        bool route = true;
        bool owned = false;
        bool repeated = false;
        const auto isUiKey = [&](arp::input::InputKey key) {
            return std::find(inputUiKeys_.begin(), inputUiKeys_.end(), key) != inputUiKeys_.end();
        };
        switch (raw.kind) {
        case arp::input::InputEventKind::PointerMoved:
            if (!std::isfinite(raw.x) || !std::isfinite(raw.y)) {
                fail(UiDiagnosticCode::MissingPointerPosition, "UI-POINTER-POSITION-NONFINITE");
                return output;
            }
            pointerPosition_ = {raw.x, raw.y};
            pointerValid_ = true;
            event.kind = UiInputEventKind::PointerMove;
            break;
        case arp::input::InputEventKind::MouseButtonPressed:
        case arp::input::InputEventKind::MouseButtonReleased:
            event.kind = raw.kind == arp::input::InputEventKind::MouseButtonPressed
                ? UiInputEventKind::PointerDown
                : UiInputEventKind::PointerUp;
            event.button = raw.mouseButton;
            {
                const auto button = static_cast<size_t>(raw.mouseButton);
                owned = inputUiButtons_[button];
                repeated = inputHeldButtons_[button];
                if (raw.kind == arp::input::InputEventKind::MouseButtonPressed) {
                    if (repeated)
                        route = false;
                    inputHeldButtons_[button] = true;
                } else {
                    if (!owned)
                        route = false;
                    inputHeldButtons_[button] = false;
                    inputUiButtons_[button] = false;
                }
            }
            break;
        case arp::input::InputEventKind::MouseWheel:
            event.kind = UiInputEventKind::ScrollWheel;
            event.delta = raw.y;
            break;
        case arp::input::InputEventKind::KeyPressed:
            event.kind = UiInputEventKind::KeyDown;
            event.key = raw.key;
            event.repeat = keyHeld(raw.key);
            repeated = event.repeat;
            owned = isUiKey(raw.key);
            if (!event.repeat)
                inputHeldKeys_.push_back(raw.key);
            else if (!owned)
                route = false;
            break;
        case arp::input::InputEventKind::KeyReleased:
            event.kind = UiInputEventKind::KeyUp;
            event.key = raw.key;
            owned = isUiKey(raw.key);
            if (!owned)
                route = false;
            inputHeldKeys_.erase(std::remove(inputHeldKeys_.begin(), inputHeldKeys_.end(), raw.key),
                inputHeldKeys_.end());
            inputUiKeys_.erase(
                std::remove(inputUiKeys_.begin(), inputUiKeys_.end(), raw.key), inputUiKeys_.end());
            break;
        case arp::input::InputEventKind::TextInput:
            event.kind = UiInputEventKind::TextInput;
            event.text = raw.text;
            break;
        case arp::input::InputEventKind::FocusLost:
            event.kind = UiInputEventKind::WindowFocusLost;
            break;
        case arp::input::InputEventKind::MouseMoved:
            if (capturedControl_.valid())
                output.eventConsumption.back().consumePointer();
            route = false;
            break;
        }
        if (!route) {
            if (owned) {
                if (raw.kind == arp::input::InputEventKind::KeyPressed
                    || raw.kind == arp::input::InputEventKind::KeyReleased)
                    output.eventConsumption.back().consumeKeyboard();
                else
                    output.eventConsumption.back().consumePointer();
            }
            continue;
        }
        if (event.kind == UiInputEventKind::PointerMove
            || event.kind == UiInputEventKind::PointerDown
            || event.kind == UiInputEventKind::PointerUp
            || event.kind == UiInputEventKind::ScrollWheel) {
            if (raw.hasPosition) {
                pointerPosition_ = {raw.pointerX, raw.pointerY};
                pointerValid_ = true;
            }
            event.x = pointerPosition_.x;
            event.y = pointerPosition_.y;
            event.hasPosition = pointerValid_;
        }
        const auto routed = dispatch(std::move(event));
        if (!routed.ok()) {
            output.diagnostic = routed.diagnostic();
            return output;
        }
        output.eventConsumption.back() = routed.consumption();
        if (raw.kind == arp::input::InputEventKind::KeyPressed && !repeated
            && routed.consumption().keyboardConsumed())
            inputUiKeys_.push_back(raw.key);
        if (raw.kind == arp::input::InputEventKind::MouseButtonPressed && !repeated)
            inputUiButtons_[static_cast<size_t>(raw.mouseButton)] =
                routed.consumption().pointerConsumed();
        if (owned) {
            if (raw.kind == arp::input::InputEventKind::KeyPressed
                || raw.kind == arp::input::InputEventKind::KeyReleased)
                output.eventConsumption.back().consumeKeyboard();
            else if (raw.kind == arp::input::InputEventKind::MouseButtonReleased)
                output.eventConsumption.back().consumePointer();
        }
    }
    return output;
}

} // namespace arp::ui
