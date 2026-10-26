#include "ui/ui.h"

#include "core/math/geometry.h"
#include "core/text/utf8.h"
#include "scene/game_object.h"
#include "scene/scene.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace arp::ui {

namespace {

arp::core::pmr::String makeString(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

arp::core::pmr::String makeString(const char* prefix, const arp::core::pmr::String& value)
{
    arp::core::pmr::String result(prefix, &arp::core::defaultCpuMemoryResource());
    result += value;
    return result;
}

bool valueAllowedForControl(UiControlType type, UiControlValueKind kind)
{
    if (kind == UiControlValueKind::None) {
        return true;
    }

    switch (type) {
    case UiControlType::Label:
    case UiControlType::TextInput:
        return kind == UiControlValueKind::Text;
    case UiControlType::Button:
    case UiControlType::Checkbox:
        return kind == UiControlValueKind::Bool;
    case UiControlType::Slider:
    case UiControlType::NumericInput:
        return kind == UiControlValueKind::Number;
    case UiControlType::ScrollView:
        return kind == UiControlValueKind::Scroll;
    case UiControlType::Dropdown:
    case UiControlType::Tabs:
    case UiControlType::TreeView:
        return kind == UiControlValueKind::Index;
    case UiControlType::ColorPicker:
        return kind == UiControlValueKind::Color;
    case UiControlType::Panel:
    case UiControlType::Image:
    case UiControlType::Unknown:
        return false;
    }
    return false;
}

bool finiteControlValue(const UiControlValue& value)
{
    switch (value.kind) {
    case UiControlValueKind::Number:
        return std::isfinite(value.number);
    case UiControlValueKind::Scroll:
        return std::isfinite(value.scroll.x) && std::isfinite(value.scroll.y);
    case UiControlValueKind::Color:
        return std::isfinite(value.color.r) && std::isfinite(value.color.g)
            && std::isfinite(value.color.b) && std::isfinite(value.color.a);
    case UiControlValueKind::None:
    case UiControlValueKind::Text:
    case UiControlValueKind::Bool:
    case UiControlValueKind::Index:
        return true;
    }
    return false;
}

arp::core::pmr::String frameworkString(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

} // namespace

UiResult::UiResult(UiDiagnostic diagnostic) : diagnostic_(std::move(diagnostic))
{}

UiResult UiResult::success()
{
    return UiResult(UiDiagnostic{});
}

UiResult UiResult::failure(UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiResult(UiDiagnostic{code, std::move(message)});
}

UiResult TextInputContract::validate(std::string_view text, std::uint32_t maxTextBytes)
{
    if (!maxTextBytes)
        return UiResult::failure(UiDiagnosticCode::InvalidTextInputBudget,
            makeString("UI-TEXT-BUDGET-INVALID: maxTextBytes must be positive."));
    if (const auto* diagnostic = arp::core::Utf8Decoder::validate(text))
        return UiResult::failure(UiDiagnosticCode::InvalidUtf8Input, makeString(diagnostic));
    if (text.find_first_of("\r\n") != std::string_view::npos
        || text.find('\0') != std::string_view::npos)
        return UiResult::failure(UiDiagnosticCode::InvalidTextInputContent,
            makeString("UI-TEXT-CONTENT-INVALID: TextInput requires one line without NUL."));
    if (text.size() > maxTextBytes)
        return UiResult::failure(UiDiagnosticCode::TextEditOverflow,
            makeString("UI-TEXT-BUDGET-EXCEEDED: input exceeds declared maxTextBytes."));
    return UiResult::success();
}

LayoutRect LayoutRect::fromOriginSize(
    float xValue, float yValue, float widthValue, float heightValue)
{
    LayoutRect rect;
    rect.x = xValue;
    rect.y = yValue;
    rect.width = widthValue;
    rect.height = heightValue;
    return rect;
}

bool LayoutRect::contains(UiPoint point) const
{
    return arp::core::rectContainsPoint(
        arp::core::Rect2<float>{x, y, width, height}, point.x, point.y);
}

UiViewport UiViewport::explicitPixels(float widthValue, float heightValue)
{
    UiViewport viewport;
    viewport.width = widthValue;
    viewport.height = heightValue;
    viewport.hasExplicitMetadata = true;
    return viewport;
}

UiSafeArea UiSafeArea::none()
{
    return explicitInsets(0.0f, 0.0f, 0.0f, 0.0f);
}

UiSafeArea UiSafeArea::explicitInsets(
    float leftValue, float topValue, float rightValue, float bottomValue)
{
    UiSafeArea safeArea;
    safeArea.left = leftValue;
    safeArea.top = topValue;
    safeArea.right = rightValue;
    safeArea.bottom = bottomValue;
    safeArea.hasExplicitMetadata = true;
    return safeArea;
}

bool UiSafeArea::validFor(UiViewport viewport) const
{
    return hasExplicitMetadata && left >= 0.0f && top >= 0.0f && right >= 0.0f && bottom >= 0.0f
        && left + right < viewport.width && top + bottom < viewport.height;
}

UiScalePolicy UiScalePolicy::fixed(float scaleValue)
{
    UiScalePolicy policy;
    policy.scale = scaleValue;
    policy.hasExplicitMetadata = true;
    return policy;
}

Widget::Widget(WidgetId id, WidgetDesc desc) : id_(id), desc_(std::move(desc))
{}

void Widget::appendChild(WidgetId childId)
{
    childIds_.push_back(childId);
}

void Widget::removeChild(WidgetId childId)
{
    childIds_.erase(std::remove(childIds_.begin(), childIds_.end(), childId), childIds_.end());
}

CanvasLayoutResult::CanvasLayoutResult(
    UiDiagnostic diagnostic, arp::core::pmr::Vector<ResolvedWidget> widgets)
    : diagnostic_(std::move(diagnostic)), widgets_(std::move(widgets))
{}

CanvasLayoutResult CanvasLayoutResult::success(arp::core::pmr::Vector<ResolvedWidget> widgets)
{
    return CanvasLayoutResult(UiDiagnostic{}, std::move(widgets));
}

CanvasLayoutResult CanvasLayoutResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return CanvasLayoutResult(UiDiagnostic{code, std::move(message)}, {});
}

const ResolvedWidget* CanvasLayoutResult::findWidget(WidgetId widgetId) const
{
    const auto found =
        std::find_if(widgets_.begin(), widgets_.end(), [widgetId](const ResolvedWidget& widget) {
            return widget.widgetId == widgetId;
        });
    return found != widgets_.end() ? &*found : nullptr;
}

const ResolvedWidget& CanvasLayoutResult::requireWidget(WidgetId widgetId) const
{
    const auto* widget = findWidget(widgetId);
    assert(widget != nullptr);
    return *widget;
}

UiHitTestResult::UiHitTestResult(UiDiagnostic diagnostic, WidgetId widgetId, uint64_t order)
    : diagnostic_(std::move(diagnostic)), widgetId_(widgetId), order_(order)
{}

UiHitTestResult UiHitTestResult::success(WidgetId widgetId, uint64_t order)
{
    return UiHitTestResult(UiDiagnostic{}, widgetId, order);
}

UiHitTestResult UiHitTestResult::failure(UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiHitTestResult(UiDiagnostic{code, std::move(message)}, WidgetId::invalid(), 0);
}

Canvas::Canvas(arp::core::pmr::String name, CanvasDesc desc) : name_(std::move(name)), desc_(desc)
{
    WidgetDesc rootDesc;
    rootDesc.name = name_;
    rootDesc.visible = true;
    rootDesc.enabled = true;
    auto root = arp::core::makeUnique<Widget>(rootId_, std::move(rootDesc));
    lookup_[rootId_.value()] = root.get();
    widgets_.push_back(std::move(root));
}

Widget& Canvas::createWidget(WidgetDesc desc)
{
    const auto id = WidgetId::fromValue(nextWidgetId_++);
    auto widget = arp::core::makeUnique<Widget>(id, std::move(desc));
    auto& result = *widget;
    widgets_.push_back(std::move(widget));
    try {
        lookup_.emplace(id.value(), &result);
    } catch (...) {
        // Ownership is established before publishing the observation. Removing
        // this new leaf requires no allocation and never rolls the ID back.
        widgets_.pop_back();
        throw;
    }
    return result;
}

void Canvas::discardUnpublishedLeaf(WidgetId id) noexcept
{
    auto* widget = findWidget(id);
    if (!widget)
        return;
    assert(id != rootId_ && widget->childIds().empty());
    if (auto* parent = findWidget(widget->parentId()))
        parent->removeChild(id);
    lookup_.erase(id.value());
    widgets_.erase(std::remove_if(widgets_.begin(),
                       widgets_.end(),
                       [id](const auto& owned) {
                           return owned->id() == id;
                       }),
        widgets_.end());
}

void Canvas::reserveChildSlots(WidgetId parentId, std::size_t count)
{
    auto* parent = findWidget(parentId);
    assert(parent);
    parent->childIds_.reserve(count);
}

Widget* Canvas::findWidget(WidgetId widgetId)
{
    const auto found = lookup_.find(widgetId.value());
    return found != lookup_.end() ? found->second : nullptr;
}

const Widget* Canvas::findWidget(WidgetId widgetId) const
{
    const auto found = lookup_.find(widgetId.value());
    return found != lookup_.end() ? found->second : nullptr;
}

UiResult Canvas::reparent(WidgetId childId, WidgetId parentId)
{
    auto* child = findWidget(childId);
    if (child == nullptr || childId == rootId_) {
        return UiResult::failure(UiDiagnosticCode::InvalidWidgetId,
            "Cannot reparent an unknown widget or the canvas root.");
    }
    auto* parent = findWidget(parentId);
    if (parent == nullptr) {
        return UiResult::failure(
            UiDiagnosticCode::InvalidParentId, "Cannot reparent to an unknown widget parent.");
    }
    if (parentChainContains(parentId, childId)) {
        return UiResult::failure(
            UiDiagnosticCode::LayoutCycle, "Cannot create a cycle in the widget hierarchy.");
    }
    if (child->parentId() == parentId) {
        return UiResult::success();
    }

    // The only potentially allocating hierarchy operation precedes detach.
    parent->childIds_.reserve(parent->childIds_.size() + 1u);
    if (child->parentId().valid()) {
        if (auto* oldParent = findWidget(child->parentId())) {
            oldParent->removeChild(childId);
        }
    }
    child->setParentId(parentId);
    parent->appendChild(childId);
    return UiResult::success();
}

arp::core::pmr::Vector<WidgetId> Canvas::traversal() const
{
    arp::core::pmr::Vector<WidgetId> order;
    if (const auto* root = findWidget(rootId_)) {
        collectTraversal(*root, order);
    }
    return order;
}

CanvasLayoutResult Canvas::resolveLayout() const
{
    if (!desc_.viewport.valid()) {
        return CanvasLayoutResult::failure(UiDiagnosticCode::MissingViewportMetadata,
            "Canvas layout requires explicit viewport metadata.");
    }
    if (!desc_.safeArea.validFor(desc_.viewport)) {
        return CanvasLayoutResult::failure(UiDiagnosticCode::MissingSafeAreaMetadata,
            "Canvas layout requires explicit safe-area metadata.");
    }
    if (!desc_.scalePolicy.valid()) {
        return CanvasLayoutResult::failure(UiDiagnosticCode::MissingViewportMetadata,
            "Canvas layout requires explicit scale metadata.");
    }

    arp::core::pmr::Vector<ResolvedWidget> widgets;
    widgets.reserve(widgets_.size());
    const LayoutRect rootRect = LayoutRect::fromOriginSize(desc_.safeArea.left,
        desc_.safeArea.top,
        desc_.viewport.width - desc_.safeArea.left - desc_.safeArea.right,
        desc_.viewport.height - desc_.safeArea.top - desc_.safeArea.bottom);
    if (const auto* root = findWidget(rootId_)) {
        const auto collected = collectLayout(*root, rootRect, true, true, widgets);
        if (!collected.ok()) {
            return CanvasLayoutResult::failure(
                collected.diagnostic().code, collected.diagnostic().message);
        }
    }
    return CanvasLayoutResult::success(std::move(widgets));
}

UiHitTestResult Canvas::hitTest(const CanvasLayoutResult& layout, UiPoint point) const
{
    if (!layout.ok()) {
        return UiHitTestResult::failure(layout.diagnostic().code, layout.diagnostic().message);
    }

    WidgetId best;
    uint64_t bestOrder = 0;
    bool found = false;
    for (const auto& widget : layout.resolvedWidgets()) {
        const auto* sourceWidget = findWidget(widget.widgetId);
        if (widget.widgetId == rootId_ || !widget.visibleInHierarchy || !widget.enabledInHierarchy
            || sourceWidget == nullptr || !sourceWidget->desc().acceptsPointerInput) {
            continue;
        }
        if (!layout.clipRect(widget.widgetId).contains(point)) {
            continue;
        }
        if (!found || widget.order >= bestOrder) {
            found = true;
            best = widget.widgetId;
            bestOrder = widget.order;
        }
    }
    return UiHitTestResult::success(best, bestOrder);
}

Widget* Canvas::requireMutableWidget(WidgetId widgetId)
{
    return findWidget(widgetId);
}

bool Canvas::parentChainContains(WidgetId startParentId, WidgetId childId) const
{
    auto current = startParentId;
    while (current.valid()) {
        if (current == childId) {
            return true;
        }
        const auto* widget = findWidget(current);
        if (widget == nullptr) {
            return false;
        }
        current = widget->parentId();
    }
    return false;
}

void Canvas::collectTraversal(const Widget& widget, arp::core::pmr::Vector<WidgetId>& order) const
{
    order.push_back(widget.id());
    for (const auto childId : widget.childIds()) {
        if (const auto* child = findWidget(childId)) {
            collectTraversal(*child, order);
        }
    }
}

UiResult Canvas::collectLayout(const Widget& widget,
    const LayoutRect& parentRect,
    bool parentVisible,
    bool parentEnabled,
    arp::core::pmr::Vector<ResolvedWidget>& widgets) const
{
    LayoutRect rect = parentRect;
    if (widget.id() != rootId_) {
        const auto& local = widget.desc().localRect;
        const auto scale = desc_.scalePolicy.scale;
        if (widget.desc().layoutMode == UiLayoutMode::Center) {
            const auto w = local.width * scale;
            const auto h = local.height * scale;
            rect = LayoutRect::fromOriginSize(
                parentRect.x + (parentRect.width - w) * 0.5f + local.x * scale,
                parentRect.y + (parentRect.height - h) * 0.5f + local.y * scale,
                w,
                h);
        } else {
            rect = LayoutRect::fromOriginSize(parentRect.x + local.x * scale,
                parentRect.y + local.y * scale,
                local.width * scale,
                local.height * scale);
        }
    }

    return collectResolvedLayout(widget, rect, parentVisible, parentEnabled, widgets);
}

UiResult Canvas::collectResolvedLayout(const Widget& widget,
    const LayoutRect& rect,
    bool parentVisible,
    bool parentEnabled,
    arp::core::pmr::Vector<ResolvedWidget>& widgets) const
{
    const auto visible = parentVisible && widget.visible();
    const auto enabled = parentEnabled && widget.enabled();
    widgets.push_back(ResolvedWidget{
        widget.id(),
        widget.parentId(),
        rect,
        visible,
        enabled,
        widget.desc().order,
        widget.acceptsFocus(),
        widget.desc().material,
        widget.desc().texture,
        widget.desc().controlType,
        widget.desc().text,
        widget.desc().textureResourceId,
        widget.desc().textureSourceRect,
        widget.desc().hasTextureSourceRect,
        widget.desc().sliceLeft,
        widget.desc().sliceTop,
        widget.desc().sliceRight,
        widget.desc().sliceBottom,
        widget.desc().hasNineSlice,
        widget.desc().styleId,
        widget.desc().fontToken,
    });

    const auto scale = desc_.scalePolicy.scale;
    if (widget.desc().layoutMode == UiLayoutMode::Stack) {
        const auto& stack = widget.desc().stackLayout;
        const auto contentX = rect.x + stack.padding.left * scale;
        const auto contentY = rect.y + stack.padding.top * scale;
        const auto contentWidth = rect.width - (stack.padding.left + stack.padding.right) * scale;
        const auto contentHeight = rect.height - (stack.padding.top + stack.padding.bottom) * scale;
        if (contentWidth < 0.0f || contentHeight < 0.0f) {
            return UiResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                makeString("UI stack layout padding cannot fit inside widget: ", widget.name()));
        }

        auto cursor = stack.direction == UiStackDirection::Vertical ? contentY : contentX;
        for (const auto childId : widget.childIds()) {
            const auto* child = findWidget(childId);
            if (child == nullptr) {
                continue;
            }
            if (child->desc().layoutParticipation != UiLayoutParticipation::StackItem) {
                const auto collected = collectLayout(*child, rect, visible, enabled, widgets);
                if (!collected.ok()) {
                    return collected;
                }
                continue;
            }

            const auto& local = child->desc().localRect;
            LayoutRect childRect;
            if (stack.direction == UiStackDirection::Vertical) {
                const auto childHeight = local.height * scale;
                const auto childWidth = local.width > 0.0f ? local.width * scale : contentWidth;
                if (childHeight <= 0.0f || childWidth <= 0.0f || childWidth > contentWidth) {
                    return UiResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                        makeString(
                            "UI vertical stack item has invalid explicit size: ", child->name()));
                }
                childRect = LayoutRect::fromOriginSize(
                    contentX + local.x * scale, cursor, childWidth, childHeight);
                cursor += childHeight + stack.spacing * scale;
            } else {
                const auto childWidth = local.width * scale;
                const auto childHeight = local.height > 0.0f ? local.height * scale : contentHeight;
                if (childWidth <= 0.0f || childHeight <= 0.0f || childHeight > contentHeight) {
                    return UiResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                        makeString(
                            "UI horizontal stack item has invalid explicit size: ", child->name()));
                }
                childRect = LayoutRect::fromOriginSize(
                    cursor, contentY + local.y * scale, childWidth, childHeight);
                cursor += childWidth + stack.spacing * scale;
            }

            if (childRect.x < contentX || childRect.y < contentY
                || childRect.x + childRect.width > contentX + contentWidth
                || childRect.y + childRect.height > contentY + contentHeight) {
                return UiResult::failure(UiDiagnosticCode::ImpossibleLayoutConstraint,
                    makeString("UI stack item cannot fit inside its container: ", child->name()));
            }

            const auto collected =
                collectResolvedLayout(*child, childRect, visible, enabled, widgets);
            if (!collected.ok()) {
                return collected;
            }
        }
        return UiResult::success();
    }

    for (const auto childId : widget.childIds()) {
        if (const auto* child = findWidget(childId)) {
            const auto collected = collectLayout(*child, rect, visible, enabled, widgets);
            if (!collected.ok()) {
                return collected;
            }
        }
    }
    return UiResult::success();
}

void RenderQueue::addItem(UIRenderItem item)
{
    items_.push_back(item);
}

namespace {

UIRenderItemKind renderItemKindFor(const ResolvedWidget& widget)
{
    if (widget.controlType == UiControlType::Image) {
        return UIRenderItemKind::Image;
    }
    if (!widget.text.empty() || widget.controlType == UiControlType::Label
        || widget.controlType == UiControlType::TextInput
        || widget.controlType == UiControlType::NumericInput) {
        return UIRenderItemKind::Text;
    }
    return UIRenderItemKind::Box;
}

DrawRecordKind drawRecordKindFor(UIRenderItemKind kind)
{
    switch (kind) {
    case UIRenderItemKind::Text:
        return DrawRecordKind::Text;
    case UIRenderItemKind::Image:
        return DrawRecordKind::Image;
    case UIRenderItemKind::Box:
    default:
        return DrawRecordKind::Box;
    }
}

} // namespace

void DrawPlan::addRecord(DrawRecord record)
{
    records_.push_back(std::move(record));
}

std::size_t DrawPlan::textRecordCount() const
{
    return static_cast<std::size_t>(
        std::count_if(records_.begin(), records_.end(), [](const DrawRecord& record) {
            return record.kind == DrawRecordKind::Text;
        }));
}

std::size_t DrawPlan::imageRecordCount() const
{
    return static_cast<std::size_t>(
        std::count_if(records_.begin(), records_.end(), [](const DrawRecord& record) {
            return record.kind == DrawRecordKind::Image;
        }));
}

DrawPlanResult::DrawPlanResult(UiDiagnostic diagnostic, DrawPlan plan)
    : diagnostic_(std::move(diagnostic)), plan_(std::move(plan))
{}

DrawPlanResult DrawPlanResult::success(DrawPlan plan)
{
    return DrawPlanResult(UiDiagnostic{}, std::move(plan));
}

DrawPlanResult DrawPlanResult::failure(UiDiagnosticCode code, arp::core::pmr::String message)
{
    return DrawPlanResult(UiDiagnostic{code, std::move(message)}, {});
}

DrawPlanResult DrawPlanner::plan(const RenderQueue& queue) const
{
    DrawPlan plan;
    for (const auto& item : queue.items()) {
        if (!item.rect.valid() || !item.clipRect.valid()) {
            return DrawPlanResult::failure(UiDiagnosticCode::InvalidLayoutAnchor,
                makeString("UI renderer draw item requires valid rect and clip rect"));
        }
        if (item.kind == UIRenderItemKind::Image && item.textureResourceId.empty()
            && !item.texture.valid()) {
            return DrawPlanResult::failure(UiDiagnosticCode::MissingRendererResourceBinding,
                makeString("UI image draw item requires texture resource binding"));
        }

        DrawRecord record;
        record.kind = drawRecordKindFor(item.kind);
        record.widgetId = item.widgetId;
        record.rect = item.rect;
        record.clipRect = item.clipRect;
        record.material = item.material;
        record.texture = item.texture;
        record.text = item.text;
        record.textureResourceId = item.textureResourceId;
        record.textureSourceRect = item.textureSourceRect;
        record.hasTextureSourceRect = item.hasTextureSourceRect;
        record.sliceLeft = item.sliceLeft;
        record.sliceTop = item.sliceTop;
        record.sliceRight = item.sliceRight;
        record.sliceBottom = item.sliceBottom;
        record.hasNineSlice = item.hasNineSlice;
        record.styleId = item.styleId;
        record.fontToken = item.fontToken;
        record.sortKey = item.sortKey;
        record.zOrder = item.sortKey;
        plan.addRecord(std::move(record));
    }
    return DrawPlanResult::success(std::move(plan));
}

UIRenderExtractionResult::UIRenderExtractionResult(UiDiagnostic diagnostic, RenderQueue queue)
    : diagnostic_(std::move(diagnostic)), queue_(std::move(queue))
{}

UIRenderExtractionResult UIRenderExtractionResult::success(RenderQueue queue)
{
    return UIRenderExtractionResult(UiDiagnostic{}, std::move(queue));
}

UIRenderExtractionResult UIRenderExtractionResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UIRenderExtractionResult(UiDiagnostic{code, std::move(message)}, {});
}

UIRenderExtractionResult RenderExtractor::extract(
    const Canvas& canvas, const CanvasLayoutResult& layout) const
{
    if (!layout.ok()) {
        return UIRenderExtractionResult::failure(
            layout.diagnostic().code, layout.diagnostic().message);
    }

    RenderQueue queue;
    for (const auto& widget : layout.resolvedWidgets()) {
        if (widget.widgetId == canvas.rootId() || !widget.visibleInHierarchy
            || !widget.enabledInHierarchy) {
            continue;
        }
        const auto clip = layout.clipRect(widget.widgetId);
        if (clip.width <= 0.0f || clip.height <= 0.0f)
            continue;
        if (!widget.textureResourceId.empty() && widget.controlType != UiControlType::Image) {
            UIRenderItem imageItem;
            imageItem.kind = UIRenderItemKind::Image;
            imageItem.widgetId = widget.widgetId;
            imageItem.rect = widget.rect;
            imageItem.clipRect = clip;
            imageItem.material = widget.material;
            imageItem.texture = widget.texture;
            imageItem.textureResourceId = widget.textureResourceId;
            imageItem.textureSourceRect = widget.textureSourceRect;
            imageItem.hasTextureSourceRect = widget.hasTextureSourceRect;
            imageItem.sliceLeft = widget.sliceLeft;
            imageItem.sliceTop = widget.sliceTop;
            imageItem.sliceRight = widget.sliceRight;
            imageItem.sliceBottom = widget.sliceBottom;
            imageItem.hasNineSlice = widget.hasNineSlice;
            imageItem.styleId = widget.styleId;
            imageItem.fontToken = widget.fontToken;
            imageItem.sortKey = widget.order;
            queue.addItem(std::move(imageItem));
            if (!widget.text.empty()) {
                UIRenderItem textItem;
                textItem.kind = UIRenderItemKind::Text;
                textItem.widgetId = widget.widgetId;
                textItem.rect = widget.rect;
                textItem.clipRect = clip;
                textItem.material = widget.material;
                textItem.texture = widget.texture;
                textItem.text = widget.text;
                textItem.styleId = widget.styleId;
                textItem.fontToken = widget.fontToken;
                textItem.sortKey = widget.order;
                queue.addItem(std::move(textItem));
            }
            continue;
        }
        if (!widget.text.empty() && widget.controlType != UiControlType::Label
            && widget.controlType != UiControlType::TextInput
            && widget.controlType != UiControlType::NumericInput) {
            UIRenderItem boxItem;
            boxItem.kind = UIRenderItemKind::Box;
            boxItem.widgetId = widget.widgetId;
            boxItem.rect = widget.rect;
            boxItem.clipRect = clip;
            boxItem.material = widget.material;
            boxItem.styleId = widget.styleId;
            boxItem.fontToken = widget.fontToken;
            boxItem.sortKey = widget.order;
            queue.addItem(std::move(boxItem));

            UIRenderItem textItem;
            textItem.kind = UIRenderItemKind::Text;
            textItem.widgetId = widget.widgetId;
            textItem.rect = widget.rect;
            textItem.clipRect = clip;
            textItem.material = widget.material;
            textItem.text = widget.text;
            textItem.styleId = widget.styleId;
            textItem.fontToken = widget.fontToken;
            textItem.sortKey = widget.order;
            queue.addItem(std::move(textItem));
            continue;
        }
        UIRenderItem item;
        item.kind = renderItemKindFor(widget);
        item.widgetId = widget.widgetId;
        item.rect = widget.rect;
        item.clipRect = clip;
        item.material = widget.material;
        item.texture = widget.texture;
        item.text = widget.text;
        item.textureResourceId = widget.textureResourceId;
        item.textureSourceRect = widget.textureSourceRect;
        item.hasTextureSourceRect = widget.hasTextureSourceRect;
        item.styleId = widget.styleId;
        item.fontToken = widget.fontToken;
        item.sortKey = widget.order;
        queue.addItem(std::move(item));
    }
    return UIRenderExtractionResult::success(std::move(queue));
}

UIInputRouteResult::UIInputRouteResult(UiDiagnostic diagnostic,
    arp::input::InputConsumption consumption,
    WidgetId focusedWidget,
    WidgetId capturedWidget)
    : diagnostic_(std::move(diagnostic)), consumption_(consumption), focusedWidget_(focusedWidget),
      capturedWidget_(capturedWidget)
{}

UIInputRouteResult UIInputRouteResult::success(
    arp::input::InputConsumption consumption, WidgetId focusedWidget, WidgetId capturedWidget)
{
    return UIInputRouteResult(UiDiagnostic{}, consumption, focusedWidget, capturedWidget);
}

UIInputRouteResult UIInputRouteResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UIInputRouteResult(UiDiagnostic{code, std::move(message)},
        arp::input::InputConsumption::none(),
        WidgetId::invalid(),
        WidgetId::invalid());
}

UiInputEvent UiInputEvent::pointerMove(float x, float y)
{
    UiInputEvent event;
    event.kind = UiInputEventKind::PointerMove;
    event.x = x;
    event.y = y;
    return event;
}

UiInputEvent UiInputEvent::pointerDown(float x, float y)
{
    UiInputEvent event;
    event.kind = UiInputEventKind::PointerDown;
    event.x = x;
    event.y = y;
    return event;
}

UiInputEvent UiInputEvent::pointerUp(float x, float y)
{
    UiInputEvent event;
    event.kind = UiInputEventKind::PointerUp;
    event.x = x;
    event.y = y;
    return event;
}

UiInputEvent UiInputEvent::keyboardNext()
{
    UiInputEvent event;
    event.kind = UiInputEventKind::KeyboardNext;
    return event;
}

UiInputEvent UiInputEvent::keyboardPrevious()
{
    UiInputEvent event;
    event.kind = UiInputEventKind::KeyboardPrevious;
    return event;
}

UiInputEvent UiInputEvent::keyboardActivate()
{
    UiInputEvent event;
    event.kind = UiInputEventKind::KeyboardActivate;
    return event;
}

UiInputEvent UiInputEvent::textInput(arp::core::pmr::String text)
{
    UiInputEvent event;
    event.kind = UiInputEventKind::TextInput;
    event.text = std::move(text);
    return event;
}

UiInputEvent UiInputEvent::backspace()
{
    UiInputEvent event;
    event.kind = UiInputEventKind::Backspace;
    return event;
}

UiInputEvent UiInputEvent::scrollWheel(float x, float y, float delta)
{
    UiInputEvent event;
    event.kind = UiInputEventKind::ScrollWheel;
    event.x = x;
    event.y = y;
    event.delta = delta;
    return event;
}

UiInputEvent UiInputEvent::scrollThumbDrag(WidgetId widgetId, float delta)
{
    UiInputEvent event;
    event.kind = UiInputEventKind::ScrollThumbDrag;
    event.widgetId = widgetId;
    event.delta = delta;
    return event;
}

UiInputDispatchResult::UiInputDispatchResult(UiDiagnostic diagnostic,
    arp::input::InputConsumption consumption,
    WidgetId hoveredWidget,
    WidgetId activeWidget,
    WidgetId focusedWidget,
    WidgetId capturedWidget,
    bool clicked)
    : diagnostic_(std::move(diagnostic)), consumption_(consumption), hoveredWidget_(hoveredWidget),
      activeWidget_(activeWidget), focusedWidget_(focusedWidget), capturedWidget_(capturedWidget),
      clicked_(clicked)
{}

UiInputDispatchResult UiInputDispatchResult::success(arp::input::InputConsumption consumption,
    WidgetId hoveredWidget,
    WidgetId activeWidget,
    WidgetId focusedWidget,
    WidgetId capturedWidget,
    bool clicked)
{
    return UiInputDispatchResult(UiDiagnostic{},
        consumption,
        hoveredWidget,
        activeWidget,
        focusedWidget,
        capturedWidget,
        clicked);
}

UiInputDispatchResult UiInputDispatchResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiInputDispatchResult(UiDiagnostic{code, std::move(message)},
        arp::input::InputConsumption::none(),
        WidgetId::invalid(),
        WidgetId::invalid(),
        WidgetId::invalid(),
        WidgetId::invalid(),
        false);
}

UIInputRouteResult InputRouter::routeLegacySnapshot(const Canvas& canvas,
    const CanvasLayoutResult& layout,
    const arp::input::InputSystem& inputSystem)
{
    runtimeObserver_ = {};
    runtimeBound_ = false;
    if (!layout.ok()) {
        return UIInputRouteResult::failure(layout.diagnostic().code, layout.diagnostic().message);
    }

    auto consumption = arp::input::InputConsumption::none();
    const auto pointer = inputSystem.pointer();
    if (pointer.hasPosition && (pointer.primaryPressed || pointer.primaryHeld)) {
        const auto hit = canvas.hitTest(layout, {pointer.x, pointer.y});
        if (!hit.ok()) {
            return UIInputRouteResult::failure(hit.diagnostic().code, hit.diagnostic().message);
        }
        if (hit.widgetId().valid()) {
            consumption.consumePointer();
            capturedWidget_ = hit.widgetId();
            const auto focus = focusableWidgetAt(layout, {pointer.x, pointer.y});
            if (focus.valid()) {
                focusedWidget_ = focus;
                capturedWidget_ = focus;
            }
        }
    }

    if (inputSystem.hasTextInput() && focusedWidget_.valid()) {
        consumption.consumeText();
    }

    return UIInputRouteResult::success(consumption, focusedWidget_, capturedWidget_);
}

UiResult InputRouter::beginCapture(const Canvas& canvas, WidgetId widgetId)
{
    if (canvas.findWidget(widgetId) == nullptr) {
        return UiResult::failure(
            UiDiagnosticCode::InvalidWidgetId, "Cannot capture input for an unknown widget.");
    }
    if (capturedWidget_.valid() && capturedWidget_ != widgetId) {
        return UiResult::failure(UiDiagnosticCode::DuplicateFocusCapture,
            "Input capture is already owned by another widget.");
    }
    capturedWidget_ = widgetId;
    focusedWidget_ = widgetId;
    return UiResult::success();
}

UiControlValue UiControlValue::none()
{
    return {};
}

UiControlValue UiControlValue::fromText(arp::core::pmr::String value)
{
    UiControlValue result;
    result.kind = UiControlValueKind::Text;
    result.text = std::move(value);
    return result;
}

UiControlValue UiControlValue::fromBool(bool value)
{
    UiControlValue result;
    result.kind = UiControlValueKind::Bool;
    result.boolean = value;
    return result;
}

UiControlValue UiControlValue::fromNumber(float value)
{
    UiControlValue result;
    result.kind = UiControlValueKind::Number;
    result.number = value;
    return result;
}

UiControlValue UiControlValue::fromIndex(std::uint32_t value)
{
    UiControlValue result;
    result.kind = UiControlValueKind::Index;
    result.index = value;
    return result;
}

UiControlValue UiControlValue::fromScroll(float x, float y)
{
    UiControlValue result;
    result.kind = UiControlValueKind::Scroll;
    result.scroll = {x, y};
    return result;
}

UiControlValue UiControlValue::fromColor(float r, float g, float b, float a)
{
    UiControlValue result;
    result.kind = UiControlValueKind::Color;
    result.color = {r, g, b, a};
    return result;
}

UiControl::UiControl(WidgetId widgetId, UiControlType type, UiControlValue value)
    : widgetId_(widgetId), type_(type), value_(std::move(value))
{}

UiControlRuntimeResult::UiControlRuntimeResult(UiDiagnostic diagnostic, WidgetId widgetId)
    : diagnostic_(std::move(diagnostic)), widgetId_(widgetId)
{}

UiControlRuntimeResult UiControlRuntimeResult::success(WidgetId widgetId)
{
    return UiControlRuntimeResult(UiDiagnostic{}, widgetId);
}

UiControlRuntimeResult UiControlRuntimeResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiControlRuntimeResult(UiDiagnostic{code, std::move(message)}, WidgetId::invalid());
}

UiControlInteraction UiControlInteraction::click(WidgetId widgetId)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::Click;
    interaction.widgetId = widgetId;
    return interaction;
}

UiControlInteraction UiControlInteraction::toggle(WidgetId widgetId)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::Toggle;
    interaction.widgetId = widgetId;
    return interaction;
}

UiControlInteraction UiControlInteraction::dragSlider(WidgetId widgetId, float value)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::DragSlider;
    interaction.widgetId = widgetId;
    interaction.number = value;
    return interaction;
}

UiControlInteraction UiControlInteraction::editText(WidgetId widgetId, arp::core::pmr::String value)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::EditText;
    interaction.widgetId = widgetId;
    interaction.text = std::move(value);
    return interaction;
}

UiControlInteraction UiControlInteraction::stepNumber(WidgetId widgetId, float delta)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::StepNumber;
    interaction.widgetId = widgetId;
    interaction.number = delta;
    return interaction;
}

UiControlInteraction UiControlInteraction::scrollBy(WidgetId widgetId, float x, float y)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::ScrollBy;
    interaction.widgetId = widgetId;
    interaction.x = x;
    interaction.y = y;
    return interaction;
}

UiControlInteraction UiControlInteraction::select(
    WidgetId widgetId, std::uint32_t index, std::uint32_t optionCount)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::Select;
    interaction.widgetId = widgetId;
    interaction.index = index;
    interaction.optionCount = optionCount;
    return interaction;
}

UiControlInteraction UiControlInteraction::pickColor(
    WidgetId widgetId, float r, float g, float b, float a)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::PickColor;
    interaction.widgetId = widgetId;
    interaction.color = {r, g, b, a};
    return interaction;
}

UiControlInteraction UiControlInteraction::imageHitTest(WidgetId widgetId)
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::ImageHitTest;
    interaction.widgetId = widgetId;
    return interaction;
}

UiControlInteraction UiControlInteraction::keyboardNavigateNext()
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::KeyboardNavigateNext;
    return interaction;
}

UiControlInteraction UiControlInteraction::releasePointerCapture()
{
    UiControlInteraction interaction;
    interaction.kind = UiControlInteractionKind::ReleasePointerCapture;
    return interaction;
}

UiControlInteractionResult::UiControlInteractionResult(
    UiDiagnostic diagnostic, WidgetId widgetId, WidgetId focusedWidget, WidgetId capturedWidget)
    : diagnostic_(std::move(diagnostic)), widgetId_(widgetId), focusedWidget_(focusedWidget),
      capturedWidget_(capturedWidget)
{}

UiControlInteractionResult UiControlInteractionResult::success(
    WidgetId widgetId, WidgetId focusedWidget, WidgetId capturedWidget)
{
    return UiControlInteractionResult(UiDiagnostic{}, widgetId, focusedWidget, capturedWidget);
}

UiControlInteractionResult UiControlInteractionResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiControlInteractionResult(UiDiagnostic{code, std::move(message)},
        WidgetId::invalid(),
        WidgetId::invalid(),
        WidgetId::invalid());
}

UiControlRuntime::UiControlRuntime(arp::core::pmr::String name, CanvasDesc desc)
    : canvas_(std::move(name), desc), mutations_(desc.maxWidgetCount),
      pendingRemoval_(desc.maxWidgetCount)
{
    controls_.reserve(desc.maxWidgetCount);
    route_.reserve(desc.maxWidgetCount);
    inputHeldKeys_.reserve(static_cast<size_t>(arp::input::InputKey::F24) + 1u);
    inputUiKeys_.reserve(static_cast<size_t>(arp::input::InputKey::F24) + 1u);
}

Control* UiControlRuntime::findControlObject(WidgetId id)
{
    const auto found = std::find_if(controls_.begin(), controls_.end(), [id](const auto& control) {
        return control->widgetId() == id;
    });
    return found == controls_.end() ? nullptr : found->get();
}

const Control* UiControlRuntime::findControlObject(WidgetId id) const
{
    return const_cast<UiControlRuntime*>(this)->findControlObject(id);
}

const UiControl* UiControlRuntime::findControl(WidgetId id) const
{
    const auto* control = findControlObject(id);
    return control == nullptr ? nullptr : &control->state();
}

UiControlRuntimeResult UiControlRuntime::createControl(UiControlDesc desc, WidgetId parent)
{
    arp::core::UniquePtr<Control> object;
    switch (desc.type) {
    case UiControlType::Panel:
        object = arp::core::makeUnique<Panel>();
        break;
    case UiControlType::Label:
        object = arp::core::makeUnique<Text>();
        break;
    case UiControlType::Image:
        object = arp::core::makeUnique<Image>();
        break;
    case UiControlType::Button:
        object = arp::core::makeUnique<Button>();
        break;
    case UiControlType::Checkbox:
        object = arp::core::makeUnique<Checkbox>();
        break;
    case UiControlType::Slider:
        object = arp::core::makeUnique<Slider>();
        break;
    case UiControlType::TextInput:
        object = arp::core::makeUnique<TextInput>();
        break;
    case UiControlType::NumericInput:
        object = arp::core::makeUnique<NumericInput>();
        break;
    case UiControlType::ScrollView:
        object = arp::core::makeUnique<ScrollView>();
        break;
    case UiControlType::Dropdown:
        object = arp::core::makeUnique<Dropdown>();
        break;
    case UiControlType::Tabs:
        object = arp::core::makeUnique<Tabs>();
        break;
    case UiControlType::TreeView:
        object = arp::core::makeUnique<TreeView>();
        break;
    case UiControlType::ColorPicker:
        object = arp::core::makeUnique<ColorPicker>();
        break;
    default:
        return UiControlRuntimeResult::failure(UiDiagnosticCode::UnsupportedControlBehavior,
            makeString("UI-CONTROL-TYPE-UNSUPPORTED: no standard control factory."));
    }
    return createControl(std::move(object), std::move(desc), parent);
}

UiControlRuntimeResult UiControlRuntime::createControl(
    arp::core::UniquePtr<Control> object, UiControlDesc desc, WidgetId parent)
{
    if (unloading_ || !object || object->owner_ || object->controlType() != desc.type
        || (desc.widget.controlType != UiControlType::Unknown
            && desc.widget.controlType != desc.type)) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidFactoryBinding,
            makeString("UI-FACTORY-TYPE-MISMATCH: object and descriptor must match."));
    }
    if (canvas_.findWidget(parent) == nullptr || pendingRemoval(parent)) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidParentId,
            makeString("Cannot create UI control under an unknown/pending parent."));
    }
    if (controls_.size() + 1u >= canvas_.desc().maxWidgetCount
        || (dispatching_ && mutations_.values().size() >= canvas_.desc().maxWidgetCount)) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::EventCapacityExceeded,
            makeString("UI-WIDGET-BUDGET-EXCEEDED: explicit canvas capacity exhausted."));
    }
    if (!valueAllowedForControl(desc.type, desc.initialValue.kind)
        || !finiteControlValue(desc.initialValue)) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidStateTransition,
            makeString("Initial UI control value is not valid for the control type."));
    }
    if (desc.type == UiControlType::Dropdown && !desc.optionLabels.empty()) {
        if (desc.initialValue.kind != UiControlValueKind::Index
            || desc.initialValue.index >= desc.optionLabels.size())
            return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidStateTransition,
                makeString("Initial dropdown selection is outside its declared options."));
        for (const auto& label : desc.optionLabels) {
            if (label.empty() || arp::core::Utf8Decoder::validate(label))
                return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidUtf8Input,
                    makeString("Dropdown option labels must be non-empty valid UTF-8."));
        }
    }
    if (desc.type == UiControlType::TextInput) {
        const auto checked = TextInputContract::validate(desc.initialValue.text, desc.maxTextBytes);
        if (!checked.ok())
            return UiControlRuntimeResult::failure(
                checked.diagnostic().code, checked.diagnostic().message);
    } else if (desc.maxTextBytes != TextInputContract::defaultMaxTextBytes) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidTextInputBudget,
            makeString("UI-TEXT-BUDGET-TYPE: only TextInput accepts maxTextBytes."));
    }
    if (desc.initialValue.kind == UiControlValueKind::Text) {
        if (const auto* diagnostic = arp::core::Utf8Decoder::validate(desc.initialValue.text)) {
            return UiControlRuntimeResult::failure(
                UiDiagnosticCode::InvalidUtf8Input, makeString(diagnostic));
        }
    }
    desc.widget.controlType = desc.type;
    // Prepare both descriptor copies and displayed text while the object is
    // still unmounted. No virtual value/input callback participates in creation.
    auto widgetDesc = desc.widget;
    if (desc.initialValue.kind == UiControlValueKind::Text)
        widgetDesc.text = desc.initialValue.text;
    else if (desc.type == UiControlType::Dropdown
        && desc.initialValue.kind == UiControlValueKind::Index
        && desc.initialValue.index < desc.optionLabels.size())
        widgetDesc.text = desc.optionLabels[desc.initialValue.index];
    ControlDesc typedDesc;
    static_cast<UiControlDesc&>(typedDesc) = std::move(desc);
    object->desc_ = std::move(typedDesc);
    prepareParentAttachment(parent);
    auto& widget = canvas_.createWidget(std::move(widgetDesc));
    const auto id = widget.id();
    try {
        object->state_ =
            UiControl(id, object->controlType(), std::move(object->desc_.initialValue));
        if (!dispatching_) {
            const auto attached = canvas_.reparent(id, parent);
            if (!attached.ok()) {
                canvas_.discardUnpublishedLeaf(id);
                return UiControlRuntimeResult::failure(
                    attached.diagnostic().code, attached.diagnostic().message);
            }
        }
    } catch (...) {
        canvas_.discardUnpublishedLeaf(id);
        throw;
    }
    // Constructor-reserved control storage and the bounded trivial mutation
    // record cannot allocate. Only now may the runtime observe the Control.
    object->owner_ = this;
    controls_.push_back(std::move(object));
    if (dispatching_)
        mutations_.append({MutationKind::Create, id, parent});
    return UiControlRuntimeResult::success(id);
}

UiControlRuntimeResult UiControlRuntime::setValue(
    WidgetId id, UiControlValue value, UiNotifyPolicy notify)
{
    auto* control = findControlObject(id);
    if (control == nullptr || pendingRemoval(id)) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidWidgetId,
            makeString("Cannot update an unknown/pending UI control."));
    }
    if (notify == UiNotifyPolicy::Notify && pendingCreation(id))
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidStateTransition,
            makeString("UI-CONTROL-UNPUBLISHED: notifying values require a mounted control."));
    if (!valueAllowedForControl(control->controlType(), value.kind) || !finiteControlValue(value)) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidStateTransition,
            makeString("UI control value is invalid for its type."));
    }
    if (control->controlType() == UiControlType::Dropdown && !control->desc().optionLabels.empty()
        && (value.kind != UiControlValueKind::Index
            || value.index >= control->desc().optionLabels.size()))
        return UiControlRuntimeResult::failure(UiDiagnosticCode::InvalidStateTransition,
            makeString("Dropdown selection is outside its declared options."));
    if (value.kind == UiControlValueKind::Text) {
        if (const auto* diagnostic = arp::core::Utf8Decoder::validate(value.text)) {
            return UiControlRuntimeResult::failure(
                UiDiagnosticCode::InvalidUtf8Input, makeString(diagnostic));
        }
        if (control->controlType() == UiControlType::TextInput) {
            const auto checked =
                TextInputContract::validate(value.text, control->desc().maxTextBytes);
            if (!checked.ok())
                return UiControlRuntimeResult::failure(
                    checked.diagnostic().code, checked.diagnostic().message);
        }
    }
    if (notify == UiNotifyPolicy::Notify && control->notifying_)
        return UiControlRuntimeResult::failure(UiDiagnosticCode::ReentrantEventDispatch,
            makeString("UI-VALUE-NOTIFICATION-REENTRANT: same-object notifying update forbidden."));
    const auto before = control->value();
    control->state_.setValue(std::move(value));
    syncValue(*control);
    if (notify == UiNotifyPolicy::Notify) {
        const bool outer = !dispatching_;
        if (outer)
            dispatching_ = true;
        try {
            control->notifyValueChanged(before);
        } catch (...) {
            if (outer)
                dispatching_ = false;
            throw;
        }
        if (outer) {
            dispatching_ = false;
            const auto result = finishMutations();
            if (!result.ok())
                return UiControlRuntimeResult::failure(
                    result.diagnostic().code, result.diagnostic().message);
        }
    }
    return UiControlRuntimeResult::success(id);
}

UiControlRuntimeResult UiControlRuntime::beginFocusCapture(WidgetId id)
{
    if (capturedControl_.valid() && capturedControl_ != id) {
        return UiControlRuntimeResult::failure(UiDiagnosticCode::DuplicateFocusCapture,
            makeString("UI capture already belongs to another control."));
    }
    auto result = requestFocus(id);
    if (result.ok())
        result = capturePointer(id);
    return result.ok()
        ? UiControlRuntimeResult::success(id)
        : UiControlRuntimeResult::failure(result.diagnostic().code, result.diagnostic().message);
}

WidgetId UiControlRuntime::nextFocusableControl() const
{
    return nextFocus(false);
}

UiControlInteractionResult UiControlRuntime::interact(UiControlInteraction interaction)
{
    if (dispatching_)
        return UiControlInteractionResult::failure(UiDiagnosticCode::ReentrantEventDispatch,
            makeString("UI-EVENT-REENTRANT: use value/structure requests from callbacks."));
    if (interaction.kind == UiControlInteractionKind::ReleasePointerCapture) {
        const auto result = releasePointer();
        return result.ok()
            ? UiControlInteractionResult::success({}, focusedControl_, capturedControl_)
            : UiControlInteractionResult::failure(
                  result.diagnostic().code, result.diagnostic().message);
    }
    if (interaction.kind == UiControlInteractionKind::KeyboardNavigateNext) {
        const auto result = requestFocus(nextFocus(false));
        return result.ok() ? UiControlInteractionResult::success(
                                 focusedControl_, focusedControl_, capturedControl_)
                           : UiControlInteractionResult::failure(
                                 result.diagnostic().code, result.diagnostic().message);
    }
    auto* object = findControlObject(interaction.widgetId);
    if (!object)
        return UiControlInteractionResult::failure(UiDiagnosticCode::InvalidWidgetId,
            makeString("Cannot interact with an unknown UI control."));
    if (!eventEligible(interaction.widgetId))
        return UiControlInteractionResult::failure(UiDiagnosticCode::DisabledControlInput,
            makeString("Hidden/disabled/pending controls do not accept interactions."));
    UiEvent event;
    event.kind = UiInputEventKind::ControlInteraction;
    event.widgetId = interaction.widgetId;
    event.hasPosition = false;
    event.interaction = &interaction;
    const auto result = dispatch(std::move(event));
    return result.ok() ? UiControlInteractionResult::success(
                             interaction.widgetId, focusedControl_, capturedControl_)
                       : UiControlInteractionResult::failure(
                             result.diagnostic().code, result.diagnostic().message);
}

WidgetId InputRouter::focusableWidgetAt(const CanvasLayoutResult& layout, UiPoint point) const
{
    WidgetId best;
    uint64_t bestOrder = 0;
    bool found = false;
    for (const auto& widget : layout.resolvedWidgets()) {
        if (!widget.acceptsFocus || !widget.visibleInHierarchy || !widget.enabledInHierarchy) {
            continue;
        }
        if (!widget.rect.contains(point)) {
            continue;
        }
        if (!found || widget.order >= bestOrder) {
            found = true;
            best = widget.widgetId;
            bestOrder = widget.order;
        }
    }
    return best;
}

UiInputDispatchResult InputRouter::dispatch(Runtime& runtime, UiInputEvent event)
{
    auto& owner = runtime.legacyRuntimeForMigration();
    runtimeObserver_ =
        arp::core::LifetimeObserver<UiControlRuntime>(&owner, owner.lifetimeTicket());
    runtimeBound_ = true;
    return owner.dispatch(std::move(event));
}

UiInputFrameDispatchResult InputRouter::dispatchFrame(
    Runtime& runtime, const arp::input::InputFrame& frame, UiInputFrameRouteDesc desc)
{
    auto& owner = runtime.legacyRuntimeForMigration();
    runtimeObserver_ =
        arp::core::LifetimeObserver<UiControlRuntime>(&owner, owner.lifetimeTicket());
    runtimeBound_ = true;
    return owner.dispatchFrame(frame, desc);
}

WidgetId InputRouter::focusedWidget() const
{
    const auto* owner = runtimeObserver_.get();
    return owner ? owner->focusedControl() : (runtimeBound_ ? WidgetId::invalid() : focusedWidget_);
}

WidgetId InputRouter::capturedWidget() const
{
    const auto* owner = runtimeObserver_.get();
    return owner ? owner->capturedControl()
                 : (runtimeBound_ ? WidgetId::invalid() : capturedWidget_);
}

WidgetId InputRouter::hoveredWidget() const
{
    const auto* owner = runtimeObserver_.get();
    return owner ? owner->hoveredControl() : (runtimeBound_ ? WidgetId::invalid() : hoveredWidget_);
}

WidgetId InputRouter::activeWidget() const
{
    const auto* owner = runtimeObserver_.get();
    return owner ? owner->pressedControl() : (runtimeBound_ ? WidgetId::invalid() : activeWidget_);
}

UiFrameworkContract UiFrameworkContract::contractComplete()
{
    UiFrameworkContract contract;
    contract.setStatus(UiFrameworkStatusLabel::ContractComplete);
    contract.addCapability({
        UiFrameworkCapability::Layout,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-layout-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::Style,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-style-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::Interaction,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-interaction-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::Text,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-text-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::Image,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-image-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::ResourceBinding,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-resource-binding-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::RenderExtraction,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-render-extraction-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::Diagnostics,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-diagnostics-contract-v1"),
    });
    contract.addCapability({
        UiFrameworkCapability::HotPathConstraints,
        UiFrameworkStatusLabel::ContractComplete,
        frameworkString("ui-hot-path-contract-v1"),
    });
    return contract;
}

void UiFrameworkContract::addCapability(UiFrameworkCapabilityRecord record)
{
    capabilities_.push_back(std::move(record));
}

const UiFrameworkCapabilityRecord* UiFrameworkContract::findCapability(
    UiFrameworkCapability capability) const
{
    const auto found = std::find_if(capabilities_.begin(),
        capabilities_.end(),
        [capability](const UiFrameworkCapabilityRecord& record) {
            return record.capability == capability;
        });
    return found == capabilities_.end() ? nullptr : &*found;
}

UiFrameworkContractValidationResult::UiFrameworkContractValidationResult(UiDiagnostic diagnostic)
    : diagnostic_(std::move(diagnostic))
{}

UiFrameworkContractValidationResult UiFrameworkContractValidationResult::success()
{
    return UiFrameworkContractValidationResult(UiDiagnostic{});
}

UiFrameworkContractValidationResult UiFrameworkContractValidationResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return UiFrameworkContractValidationResult(UiDiagnostic{code, std::move(message)});
}

UiFrameworkContractValidationResult UiFrameworkContractValidator::validate(
    const UiFrameworkContract& contract) const
{
    const UiFrameworkCapability required[] = {
        UiFrameworkCapability::Layout,
        UiFrameworkCapability::Style,
        UiFrameworkCapability::Interaction,
        UiFrameworkCapability::Text,
        UiFrameworkCapability::Image,
        UiFrameworkCapability::ResourceBinding,
        UiFrameworkCapability::RenderExtraction,
        UiFrameworkCapability::Diagnostics,
        UiFrameworkCapability::HotPathConstraints,
    };

    for (const auto capability : required) {
        const auto* record = contract.findCapability(capability);
        if (record == nullptr) {
            arp::core::pmr::String message("UI framework contract is missing required capability: ",
                &arp::core::defaultCpuMemoryResource());
            message += capabilityName(capability);
            return UiFrameworkContractValidationResult::failure(
                UiDiagnosticCode::MissingFrameworkCapability, std::move(message));
        }
        if (record->contractId.empty()) {
            arp::core::pmr::String message(
                "UI framework capability is missing an explicit contract id: ",
                &arp::core::defaultCpuMemoryResource());
            message += capabilityName(capability);
            return UiFrameworkContractValidationResult::failure(
                UiDiagnosticCode::MissingFrameworkCapability, std::move(message));
        }
    }

    if (contract.allowsRuntimeDesignSourceParsing() || contract.exposesNativeBackendHandles()) {
        return UiFrameworkContractValidationResult::failure(
            UiDiagnosticCode::InvalidFrameworkBoundary,
            frameworkString(
                "UI framework contract must not allow runtime design-source parsing or native backend handles."));
    }
    if (contract.allowsPerFrameSourceImport() || contract.allowsPerFrameShaderCompilation()
        || !contract.declaresBoundedRuntimeAllocation()) {
        return UiFrameworkContractValidationResult::failure(
            UiDiagnosticCode::InvalidHotPathConstraint,
            frameworkString("UI framework contract must declare bounded hot-path behavior."));
    }

    return UiFrameworkContractValidationResult::success();
}

arp::core::pmr::String statusLabelName(UiFrameworkStatusLabel status)
{
    switch (status) {
    case UiFrameworkStatusLabel::ContractComplete:
        return frameworkString("Contract Complete");
    case UiFrameworkStatusLabel::PrototypeComplete:
        return frameworkString("Prototype Complete");
    case UiFrameworkStatusLabel::BackendValidated:
        return frameworkString("Backend Validated");
    case UiFrameworkStatusLabel::PlatformValidated:
        return frameworkString("Platform Validated");
    case UiFrameworkStatusLabel::ProductionReady:
        return frameworkString("Production Ready");
    }
    return frameworkString("Contract Complete");
}

const char* capabilityName(UiFrameworkCapability capability)
{
    switch (capability) {
    case UiFrameworkCapability::Layout:
        return "Layout";
    case UiFrameworkCapability::Style:
        return "Style";
    case UiFrameworkCapability::Interaction:
        return "Interaction";
    case UiFrameworkCapability::Text:
        return "Text";
    case UiFrameworkCapability::Image:
        return "Image";
    case UiFrameworkCapability::ResourceBinding:
        return "ResourceBinding";
    case UiFrameworkCapability::RenderExtraction:
        return "RenderExtraction";
    case UiFrameworkCapability::Diagnostics:
        return "Diagnostics";
    case UiFrameworkCapability::HotPathConstraints:
        return "HotPathConstraints";
    }
    return "Layout";
}

WorldAnchorComponent::WorldAnchorComponent(WorldAnchorComponentDesc desc) : desc_(desc)
{}

WorldAnchorProjectionResult::WorldAnchorProjectionResult(
    UiDiagnostic diagnostic, arp::core::pmr::Vector<WorldAnchorProjection> anchors)
    : diagnostic_(std::move(diagnostic)), anchors_(std::move(anchors))
{}

WorldAnchorProjectionResult WorldAnchorProjectionResult::success(
    arp::core::pmr::Vector<WorldAnchorProjection> anchors)
{
    return WorldAnchorProjectionResult(UiDiagnostic{}, std::move(anchors));
}

WorldAnchorProjectionResult WorldAnchorProjectionResult::failure(
    UiDiagnosticCode code, arp::core::pmr::String message)
{
    return WorldAnchorProjectionResult(UiDiagnostic{code, std::move(message)}, {});
}

WorldAnchorProjectionResult WorldAnchorProjector::project(const arp::scene::Scene& scene,
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::scene::ResolvedCamera& camera,
    const Canvas& canvas) const
{
    if (camera.projection != arp::scene::CameraProjection::Orthographic) {
        return WorldAnchorProjectionResult::failure(
            UiDiagnosticCode::UnsupportedWorldAnchorProjection,
            "World UI anchors require an orthographic ResolvedCamera in V1.");
    }

    arp::core::pmr::Vector<WorldAnchorProjection> anchors;
    for (const auto& node : snapshot.nodes()) {
        if (!node.activeInHierarchy) {
            continue;
        }
        const auto* object = scene.findObject(node.objectId);
        if (object == nullptr) {
            continue;
        }
        for (const auto& component : object->components()) {
            if (!component->enabled()) {
                continue;
            }
            const auto* anchor = dynamic_cast<const WorldAnchorComponent*>(component.get());
            if (anchor == nullptr) {
                continue;
            }
            if (canvas.findWidget(anchor->desc().widget) == nullptr) {
                return WorldAnchorProjectionResult::failure(
                    UiDiagnosticCode::MissingWorldAnchorTarget,
                    "World UI anchor references an unknown widget.");
            }
            anchors.push_back(WorldAnchorProjection{
                node.objectId,
                anchor->desc().widget,
                projectAnchor(node, camera, canvas, anchor->desc()),
            });
        }
    }

    return WorldAnchorProjectionResult::success(std::move(anchors));
}

WorldAnchorProjectionResult WorldAnchorProjector::projectMissingTargetForTest(WidgetId widgetId,
    const arp::scene::Scene& scene,
    const arp::scene::SceneTraversalSnapshot& snapshot,
    const arp::scene::ResolvedCamera& camera,
    const Canvas& canvas) const
{
    if (canvas.findWidget(widgetId) == nullptr) {
        return WorldAnchorProjectionResult::failure(UiDiagnosticCode::MissingWorldAnchorTarget,
            "World UI anchor references an unknown widget.");
    }
    return project(scene, snapshot, camera, canvas);
}

LayoutRect WorldAnchorProjector::projectAnchor(const arp::scene::SceneTraversalNode& node,
    const arp::scene::ResolvedCamera& camera,
    const Canvas& canvas,
    const WorldAnchorComponentDesc& desc) const
{
    const auto width = camera.orthographicWidth > 0.0f ? camera.orthographicWidth : 1.0f;
    const auto height = camera.orthographicHeight > 0.0f ? camera.orthographicHeight : 1.0f;
    const auto normalizedX =
        0.5f + (node.worldTransform.translation.x - camera.camera.position.x) / width;
    const auto normalizedY =
        0.5f - (node.worldTransform.translation.y - camera.camera.position.y) / height;
    const auto screenX = normalizedX * canvas.desc().viewport.width + desc.viewportOffset.x
        - desc.pixelSize.width * 0.5f;
    const auto screenY = normalizedY * canvas.desc().viewport.height + desc.viewportOffset.y
        - desc.pixelSize.height * 0.5f;
    return LayoutRect::fromOriginSize(
        screenX, screenY, desc.pixelSize.width, desc.pixelSize.height);
}

} // namespace arp::ui
