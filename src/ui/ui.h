#pragma once

#include "core/bounded_sequence.h"
#include "core/cpu_allocator.h"
#include "core/filesystem_path.h"
#include "core/lifetime_epoch.h"
#include "input/input.h"
#include "renderer/runtime_scene.h"
#include "scene/camera.h"
#include "scene/component.h"
#include "scene/ids.h"
#include "scene/scene_traversal_snapshot.h"
#include "ui/document_component_editor_properties.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace arp::scene {
class Scene;
}

namespace arp::ui {

class WidgetId {
public:
    WidgetId() = default;

    static WidgetId invalid() { return WidgetId(); }

    static WidgetId fromValue(uint64_t value) { return WidgetId(value); }

    bool valid() const { return value_ != 0; }

    uint64_t value() const { return value_; }

    friend bool operator==(WidgetId lhs, WidgetId rhs) { return lhs.value_ == rhs.value_; }

    friend bool operator!=(WidgetId lhs, WidgetId rhs) { return !(lhs == rhs); }

private:
    explicit WidgetId(uint64_t value) : value_(value) {}

    uint64_t value_ = 0;
};

class NodeId {
public:
    NodeId() = default;

    static NodeId invalid() { return NodeId(); }

    static NodeId fromValue(uint64_t value) { return NodeId(value); }

    bool valid() const { return value_ != 0; }

    uint64_t value() const { return value_; }

    friend bool operator==(NodeId lhs, NodeId rhs) { return lhs.value_ == rhs.value_; }

    friend bool operator!=(NodeId lhs, NodeId rhs) { return !(lhs == rhs); }

private:
    explicit NodeId(uint64_t value) : value_(value) {}

    uint64_t value_ = 0;
};

enum class UiDiagnosticCode {
    None,
    InvalidWidgetId,
    InvalidParentId,
    LayoutCycle,
    MissingViewportMetadata,
    MissingSafeAreaMetadata,
    DuplicateFocusCapture,
    MissingWorldAnchorTarget,
    UnsupportedWorldAnchorProjection,
    UnsupportedControlBehavior,
    InvalidStateTransition,
    RuntimeLoadFailed,
    MissingFrameworkCapability,
    InvalidFrameworkBoundary,
    InvalidHotPathConstraint,
    InvalidLayoutAnchor,
    ImpossibleLayoutConstraint,
    MissingThemeToken,
    InvalidStyleState,
    ClippingMismatch,
    DisabledControlInput,
    MissingControlOption,
    TextEditOverflow,
    InvalidSelectionIndex,
    MissingRendererResourceBinding,
    EventCapacityExceeded,
    ReentrantEventDispatch,
    MissingPointerPosition,
    InvalidFactoryBinding,
    InvalidUtf8Input,
};

enum class UiControlType {
    Unknown,
    Panel,
    Label,
    Button,
    Slider,
    Checkbox,
    TextInput,
    NumericInput,
    ScrollView,
    Dropdown,
    Tabs,
    TreeView,
    ColorPicker,
    Image,
};

struct UiDiagnostic {
    UiDiagnosticCode code = UiDiagnosticCode::None;
    arp::core::pmr::String message;
};

class UiResult {
public:
    static UiResult success();
    static UiResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit UiResult(UiDiagnostic diagnostic);

    UiDiagnostic diagnostic_;
};

struct UiPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct UiSize {
    float width = 0.0f;
    float height = 0.0f;
};

using Size = UiSize;

struct UiBoxEdges {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    static UiBoxEdges all(float value);
    static UiBoxEdges axis(float horizontal, float vertical);
};

using Spacing = UiBoxEdges;

enum class Axis {
    Row,
    Column,
};

enum class Align {
    Start,
    Center,
    End,
    Stretch,
};

struct Style {
    Style() = default;

    Style(Size fixedSizeValue,
        Size minSizeValue = {},
        Spacing paddingValue = {},
        float gapValue = 0.0f,
        Axis axisValue = Axis::Column,
        Align alignValue = Align::Start,
        bool stretchWidthValue = false,
        bool stretchHeightValue = false,
        Size percentSizeValue = {},
        Size maxSizeValue = {})
        : fixedSize(fixedSizeValue), minSize(minSizeValue), padding(paddingValue), gap(gapValue),
          axis(axisValue), align(alignValue), stretchWidth(stretchWidthValue),
          stretchHeight(stretchHeightValue), percentSize(percentSizeValue), maxSize(maxSizeValue)
    {}

    Size fixedSize;
    Size minSize;
    Spacing padding;
    float gap = 0.0f;
    Axis axis = Axis::Column;
    Align align = Align::Start;
    bool stretchWidth = false;
    bool stretchHeight = false;
    Size percentSize;
    Size maxSize;
};

class LayoutEngine final {
public:
    static float resolveWidth(const Style& style, float parentWidth)
    {
        float width = 0.0f;
        if (style.stretchWidth) {
            width = parentWidth;
        } else if (style.fixedSize.width > 0.0f) {
            width = style.fixedSize.width;
        } else if (style.percentSize.width > 0.0f) {
            width = parentWidth * style.percentSize.width;
        }

        width = width < style.minSize.width ? style.minSize.width : width;
        if (style.maxSize.width > 0.0f && width > style.maxSize.width) {
            width = style.maxSize.width;
        }
        return width;
    }

    static float resolveHeight(const Style& style, float parentHeight)
    {
        float height = 0.0f;
        if (style.stretchHeight) {
            height = parentHeight;
        } else if (style.fixedSize.height > 0.0f) {
            height = style.fixedSize.height;
        } else if (style.percentSize.height > 0.0f) {
            height = parentHeight * style.percentSize.height;
        }

        height = height < style.minSize.height ? style.minSize.height : height;
        if (style.maxSize.height > 0.0f && height > style.maxSize.height) {
            height = style.maxSize.height;
        }
        return height;
    }

    static float alignedOrigin(Align align, float contentOrigin, float contentSize, float childSize)
    {
        switch (align) {
        case Align::Center:
            return contentOrigin + (contentSize - childSize) * 0.5f;
        case Align::End:
            return contentOrigin + contentSize - childSize;
        case Align::Start:
        case Align::Stretch:
            return contentOrigin;
        }

        return contentOrigin;
    }
};

enum class UiLayoutMode {
    None,
    Stack,
    Grid,
    Center,
};

enum class UiStackDirection {
    Vertical,
    Horizontal,
};

struct UiStackLayout {
    UiStackDirection direction = UiStackDirection::Vertical;
    float spacing = 0.0f;
    UiBoxEdges padding;

    static UiStackLayout vertical(float spacing);
    static UiStackLayout horizontal(float spacing);
};

struct UiGridLayout {
    std::uint32_t columns = 0u;
    std::uint32_t rows = 0u;
    float columnGap = 0.0f;
    float rowGap = 0.0f;

    static UiGridLayout fixed(
        std::uint32_t columns, std::uint32_t rows, float columnGap, float rowGap);
    bool valid() const;
};

enum class UiLayoutParticipation {
    Absolute,
    StackItem,
};

struct ARP_API LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    static LayoutRect fromOriginSize(float x, float y, float width, float height);

    bool valid() const { return width >= 0.0f && height >= 0.0f; }

    bool contains(UiPoint point) const;

    friend bool operator==(LayoutRect lhs, LayoutRect rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.width == rhs.width
            && lhs.height == rhs.height;
    }

    friend bool operator!=(LayoutRect lhs, LayoutRect rhs) { return !(lhs == rhs); }
};

using Rect = LayoutRect;

struct UiViewport {
    float width = 0.0f;
    float height = 0.0f;
    bool hasExplicitMetadata = false;

    static UiViewport explicitPixels(float width, float height);

    bool valid() const { return hasExplicitMetadata && width > 0.0f && height > 0.0f; }
};

struct UiSafeArea {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    bool hasExplicitMetadata = false;

    static UiSafeArea none();
    static UiSafeArea explicitInsets(float left, float top, float right, float bottom);
    bool validFor(UiViewport viewport) const;
};

struct UiScalePolicy {
    float scale = 1.0f;
    bool hasExplicitMetadata = false;

    static UiScalePolicy fixed(float scale);

    bool valid() const { return hasExplicitMetadata && scale > 0.0f; }
};

struct CanvasDesc {
    UiViewport viewport;
    UiSafeArea safeArea;
    UiScalePolicy scalePolicy;
    size_t maxWidgetCount = 1024;
};

struct WidgetDesc {
    arp::core::pmr::String name;
    LayoutRect localRect;
    arp::renderer::MaterialHandle material;
    arp::renderer::TextureHandle texture;
    UiControlType controlType = UiControlType::Unknown;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
    LayoutRect textureSourceRect;
    bool hasTextureSourceRect = false;
    float sliceLeft = 0.0f;
    float sliceTop = 0.0f;
    float sliceRight = 0.0f;
    float sliceBottom = 0.0f;
    bool hasNineSlice = false;
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    UiLayoutMode layoutMode = UiLayoutMode::None;
    UiStackLayout stackLayout;
    UiLayoutParticipation layoutParticipation = UiLayoutParticipation::Absolute;
    uint64_t order = 0;
    bool visible = true;
    bool enabled = true;
    bool acceptsFocus = false;
    // Decorative foreground widgets can render while allowing pointer input through.
    bool acceptsPointerInput = true;
};

class Widget {
public:
    WidgetId id() const { return id_; }

    WidgetId parentId() const { return parentId_; }

    const arp::core::pmr::Vector<WidgetId>& childIds() const { return childIds_; }

    const arp::core::pmr::String& name() const { return desc_.name; }

    const WidgetDesc& desc() const { return desc_; }

    bool visible() const { return desc_.visible; }

    bool enabled() const { return desc_.enabled; }

    bool acceptsFocus() const { return desc_.acceptsFocus; }

    void setVisible(bool visible) { desc_.visible = visible; }

    void setEnabled(bool enabled) { desc_.enabled = enabled; }

    void setText(arp::core::pmr::String text) { desc_.text = std::move(text); }

    void setTextureResourceId(arp::core::pmr::String resourceId)
    {
        desc_.textureResourceId = std::move(resourceId);
    }

    void setTextureSourceRect(LayoutRect rect)
    {
        desc_.textureSourceRect = rect;
        desc_.hasTextureSourceRect = rect.valid();
    }

    void setAcceptsPointerInput(bool value) { desc_.acceptsPointerInput = value; }

    void setLocalRect(LayoutRect rect) { desc_.localRect = rect; }

private:
    template <class T, class... Args>
    friend arp::core::UniquePtr<T> arp::core::makeUnique(Args&&... args);
    friend class Canvas;

    Widget(WidgetId id, WidgetDesc desc);
    void appendChild(WidgetId childId);
    void removeChild(WidgetId childId);

    void setParentId(WidgetId parentId) { parentId_ = parentId; }

    WidgetId id_;
    WidgetId parentId_;
    WidgetDesc desc_;
    arp::core::pmr::Vector<WidgetId> childIds_;
};

struct ResolvedWidget {
    WidgetId widgetId;
    WidgetId parentId;
    LayoutRect rect;
    bool visibleInHierarchy = true;
    bool enabledInHierarchy = true;
    uint64_t order = 0;
    bool acceptsFocus = false;
    arp::renderer::MaterialHandle material;
    arp::renderer::TextureHandle texture;
    UiControlType controlType = UiControlType::Unknown;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
    LayoutRect textureSourceRect;
    bool hasTextureSourceRect = false;
    float sliceLeft = 0.0f;
    float sliceTop = 0.0f;
    float sliceRight = 0.0f;
    float sliceBottom = 0.0f;
    bool hasNineSlice = false;
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
};

class CanvasLayoutResult {
public:
    static CanvasLayoutResult success(arp::core::pmr::Vector<ResolvedWidget> widgets);
    static CanvasLayoutResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::core::pmr::Vector<ResolvedWidget>& resolvedWidgets() const { return widgets_; }

    LayoutRect clipRect(WidgetId widgetId) const;
    const ResolvedWidget* findWidget(WidgetId widgetId) const;
    const ResolvedWidget& requireWidget(WidgetId widgetId) const;

private:
    CanvasLayoutResult(UiDiagnostic diagnostic, arp::core::pmr::Vector<ResolvedWidget> widgets);

    UiDiagnostic diagnostic_;
    arp::core::pmr::Vector<ResolvedWidget> widgets_;
};

class UiHitTestResult {
public:
    static UiHitTestResult success(WidgetId widgetId, uint64_t order);
    static UiHitTestResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    WidgetId widgetId() const { return widgetId_; }

    uint64_t order() const { return order_; }

private:
    UiHitTestResult(UiDiagnostic diagnostic, WidgetId widgetId, uint64_t order);

    UiDiagnostic diagnostic_;
    WidgetId widgetId_;
    uint64_t order_ = 0;
};

class Canvas {
public:
    Canvas(arp::core::pmr::String name, CanvasDesc desc);

    WidgetId rootId() const { return rootId_; }

    const CanvasDesc& desc() const { return desc_; }

    Widget& createWidget(WidgetDesc desc);
    Widget* findWidget(WidgetId widgetId);
    const Widget* findWidget(WidgetId widgetId) const;
    UiResult reparent(WidgetId childId, WidgetId parentId);
    UiResult removeSubtree(WidgetId widgetId);

    bool isInSubtree(WidgetId candidate, WidgetId root) const
    {
        return parentChainContains(candidate, root);
    }

    arp::core::pmr::Vector<WidgetId> traversal() const;
    CanvasLayoutResult resolveLayout() const;
    UiHitTestResult hitTest(const CanvasLayoutResult& layout, UiPoint point) const;

    bool usesGameObjectHierarchyForScreenSpaceUi() const { return false; }

    bool requiresCameraForScreenSpaceLayout() const { return false; }

private:
    Widget* requireMutableWidget(WidgetId widgetId);
    bool parentChainContains(WidgetId startParentId, WidgetId childId) const;
    void collectTraversal(const Widget& widget, arp::core::pmr::Vector<WidgetId>& order) const;
    UiResult collectLayout(const Widget& widget,
        const LayoutRect& parentRect,
        bool parentVisible,
        bool parentEnabled,
        arp::core::pmr::Vector<ResolvedWidget>& widgets) const;
    UiResult collectResolvedLayout(const Widget& widget,
        const LayoutRect& rect,
        bool parentVisible,
        bool parentEnabled,
        arp::core::pmr::Vector<ResolvedWidget>& widgets) const;

    arp::core::pmr::String name_;
    CanvasDesc desc_;
    WidgetId rootId_ = WidgetId::fromValue(1);
    uint64_t nextWidgetId_ = 2;
    arp::core::pmr::Vector<arp::core::UniquePtr<Widget>> widgets_;
    arp::core::pmr::UnorderedMap<uint64_t, Widget*> lookup_;
};

enum class UIRenderItemKind {
    Box,
    Text,
    Image,
};

struct UIRenderItem {
    UIRenderItemKind kind = UIRenderItemKind::Box;
    WidgetId widgetId;
    LayoutRect rect;
    LayoutRect clipRect;
    arp::renderer::MaterialHandle material;
    arp::renderer::TextureHandle texture;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
    LayoutRect textureSourceRect;
    bool hasTextureSourceRect = false;
    float sliceLeft = 0.0f;
    float sliceTop = 0.0f;
    float sliceRight = 0.0f;
    float sliceBottom = 0.0f;
    bool hasNineSlice = false;
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    uint64_t sortKey = 0;
};

class RenderQueue {
public:
    void addItem(UIRenderItem item);

    const arp::core::pmr::Vector<UIRenderItem>& items() const { return items_; }

    bool exposesNativeBackendHandles() const { return false; }

    size_t nativeBackendHandleCount() const { return 0; }

    bool generatesRawStaticVertexDataInSceneHotPath() const { return false; }

    size_t rawStaticVertexDataCount() const { return 0; }

private:
    arp::core::pmr::Vector<UIRenderItem> items_;
};

enum class DrawRecordKind {
    Box,
    Text,
    Image,
};

struct DrawRecord {
    DrawRecordKind kind = DrawRecordKind::Box;
    WidgetId widgetId;
    LayoutRect rect;
    LayoutRect clipRect;
    arp::renderer::MaterialHandle material;
    arp::renderer::TextureHandle texture;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String textureResourceId{&arp::core::defaultCpuMemoryResource()};
    LayoutRect textureSourceRect;
    bool hasTextureSourceRect = false;
    float sliceLeft = 0.0f;
    float sliceTop = 0.0f;
    float sliceRight = 0.0f;
    float sliceBottom = 0.0f;
    bool hasNineSlice = false;
    arp::core::pmr::String styleId{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String fontToken{&arp::core::defaultCpuMemoryResource()};
    uint64_t sortKey = 0;
    uint64_t zOrder = 0;
};

class DrawPlan final {
public:
    void addRecord(DrawRecord record);

    const arp::core::pmr::Vector<DrawRecord>& records() const { return records_; }

    arp::core::pmr::Vector<DrawRecord>& mutableRecords() { return records_; }

    std::size_t recordCount() const { return records_.size(); }

    std::size_t textRecordCount() const;
    std::size_t imageRecordCount() const;

    bool exposesNativeBackendHandles() const { return false; }

    std::size_t nativeBackendHandleCount() const { return 0; }

private:
    arp::core::pmr::Vector<DrawRecord> records_;
};

class DrawPlanResult final {
public:
    static DrawPlanResult success(DrawPlan plan);
    static DrawPlanResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const DrawPlan& plan() const { return plan_; }

private:
    DrawPlanResult(UiDiagnostic diagnostic, DrawPlan plan);

    UiDiagnostic diagnostic_;
    DrawPlan plan_;
};

class DrawPlanner final {
public:
    DrawPlanResult plan(const RenderQueue& queue) const;
};

class UIRenderExtractionResult {
public:
    static UIRenderExtractionResult success(RenderQueue queue);
    static UIRenderExtractionResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const RenderQueue& queue() const { return queue_; }

private:
    UIRenderExtractionResult(UiDiagnostic diagnostic, RenderQueue queue);

    UiDiagnostic diagnostic_;
    RenderQueue queue_;
};

class RenderExtractor {
public:
    UIRenderExtractionResult extract(const Canvas& canvas, const CanvasLayoutResult& layout) const;
};

class UIInputRouteResult {
public:
    static UIInputRouteResult success(
        arp::input::InputConsumption consumption, WidgetId focusedWidget, WidgetId capturedWidget);
    static UIInputRouteResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::input::InputConsumption& consumption() const { return consumption_; }

    WidgetId focusedWidget() const { return focusedWidget_; }

    WidgetId capturedWidget() const { return capturedWidget_; }

private:
    UIInputRouteResult(UiDiagnostic diagnostic,
        arp::input::InputConsumption consumption,
        WidgetId focusedWidget,
        WidgetId capturedWidget);

    UiDiagnostic diagnostic_;
    arp::input::InputConsumption consumption_;
    WidgetId focusedWidget_;
    WidgetId capturedWidget_;
};

class Runtime;

enum class UiInputEventKind {
    PointerMove,
    PointerDown,
    PointerUp,
    KeyboardNext,
    KeyboardPrevious,
    KeyboardActivate,
    TextInput,
    Backspace,
    ScrollWheel,
    ScrollThumbDrag,
    KeyDown,
    KeyUp,
    PointerEnter,
    PointerLeave,
    FocusGained,
    FocusLost,
    PointerCaptureLost,
    PointerCancel,
    WindowFocusLost,
    ControlInteraction,
};

struct UiControlInteraction;

struct UiInputEvent {
    UiInputEventKind kind = UiInputEventKind::PointerMove;
    arp::input::InputKey key = arp::input::InputKey::Enter;
    arp::input::InputMouseButton button = arp::input::InputMouseButton::Left;
    uint64_t windowId = 0;
    bool hasPosition = true;
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool repeat = false;
    WidgetId widgetId;
    // Synchronous compatibility command, owned by interact() for this dispatch.
    const UiControlInteraction* interaction = nullptr;
    float x = 0.0f;
    float y = 0.0f;
    float delta = 0.0f;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};

    static UiInputEvent pointerMove(float x, float y);
    static UiInputEvent pointerDown(float x, float y);
    static UiInputEvent pointerUp(float x, float y);
    static UiInputEvent keyboardNext();
    static UiInputEvent keyboardPrevious();
    static UiInputEvent keyboardActivate();
    static UiInputEvent textInput(arp::core::pmr::String text);
    static UiInputEvent backspace();
    static UiInputEvent scrollWheel(float x, float y, float delta);
    static UiInputEvent scrollThumbDrag(WidgetId widgetId, float delta);
};

using UiEvent = UiInputEvent;
enum class UiEventPhase { Capture, Target, Bubble };
enum class UiNotifyPolicy { Silent, Notify };
class UiControlRuntime;

class UiEventContext {
public:
    UiEventContext(UiControlRuntime& runtime, WidgetId target);

    WidgetId target() const { return target_; }

    WidgetId currentTarget() const { return current_; }

    UiEventPhase phase() const { return phase_; }

    UiPoint localPosition() const { return local_; }

    UiControlRuntime& runtime() const { return *runtime_; }

    bool handled() const { return handled_; }

    bool propagationStopped() const { return stopped_; }

    bool defaultPrevented() const { return prevented_; }

    void markHandled() { handled_ = true; }

    void stopPropagation() { stopped_ = true; }

    void preventDefault() { prevented_ = true; }

    UiResult requestFocus();
    UiResult capturePointer();
    UiResult releasePointer();

private:
    friend class UiControlRuntime;
    UiControlRuntime* runtime_;
    WidgetId target_;
    WidgetId current_;
    UiEventPhase phase_ = UiEventPhase::Target;
    UiPoint local_;
    arp::input::InputMouseButton button_ = arp::input::InputMouseButton::Left;
    bool handled_ = false;
    bool stopped_ = false;
    bool prevented_ = false;
};

class IUiEventTarget {
public:
    virtual ~IUiEventTarget() = default;
    virtual UiResult handleEvent(const UiEvent& event, UiEventContext& context) = 0;
};

struct UiInputFrameRouteDesc {
    uint64_t windowId = 0;
    // Required by frame ingress; no implicit unbounded event queue.
    size_t maxEvents = 0;
};

struct UiInputFrameDispatchResult {
    UiDiagnostic diagnostic;
    arp::core::pmr::Vector<arp::input::InputConsumption> eventConsumption;

    bool ok() const { return diagnostic.code == UiDiagnosticCode::None; }
};

class UiInputDispatchResult final {
public:
    static UiInputDispatchResult success(arp::input::InputConsumption consumption,
        WidgetId hoveredWidget,
        WidgetId activeWidget,
        WidgetId focusedWidget,
        WidgetId capturedWidget,
        bool clicked);
    static UiInputDispatchResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::input::InputConsumption& consumption() const { return consumption_; }

    WidgetId hoveredWidget() const { return hoveredWidget_; }

    WidgetId activeWidget() const { return activeWidget_; }

    WidgetId focusedWidget() const { return focusedWidget_; }

    WidgetId capturedWidget() const { return capturedWidget_; }

    bool clicked() const { return clicked_; }

private:
    UiInputDispatchResult(UiDiagnostic diagnostic,
        arp::input::InputConsumption consumption,
        WidgetId hoveredWidget,
        WidgetId activeWidget,
        WidgetId focusedWidget,
        WidgetId capturedWidget,
        bool clicked);

    UiDiagnostic diagnostic_;
    arp::input::InputConsumption consumption_;
    WidgetId hoveredWidget_;
    WidgetId activeWidget_;
    WidgetId focusedWidget_;
    WidgetId capturedWidget_;
    bool clicked_ = false;
};

class InputRouter {
public:
    // Legacy snapshot route: cannot preserve ordered events or press ownership.
    UIInputRouteResult routeLegacySnapshot(const Canvas& canvas,
        const CanvasLayoutResult& layout,
        const arp::input::InputSystem& inputSystem);

    UIInputRouteResult route(const Canvas& canvas,
        const CanvasLayoutResult& layout,
        const arp::input::InputSystem& inputSystem)
    {
        return routeLegacySnapshot(canvas, layout, inputSystem);
    }

    UiResult beginCapture(const Canvas& canvas, WidgetId widgetId);
    UiInputDispatchResult dispatch(Runtime& runtime, UiInputEvent event);
    UiInputFrameDispatchResult dispatchFrame(
        Runtime& runtime, const arp::input::InputFrame& frame, UiInputFrameRouteDesc desc);

    WidgetId focusedWidget() const;
    WidgetId capturedWidget() const;
    WidgetId hoveredWidget() const;
    WidgetId activeWidget() const;

private:
    arp::core::LifetimeObserver<UiControlRuntime> runtimeObserver_;
    bool runtimeBound_ = false;
    WidgetId focusableWidgetAt(const CanvasLayoutResult& layout, UiPoint point) const;

    WidgetId focusedWidget_;
    WidgetId capturedWidget_;
    WidgetId hoveredWidget_;
    WidgetId activeWidget_;
};

enum class UiControlValueKind {
    None,
    Text,
    Bool,
    Number,
    Index,
    Scroll,
    Color,
};

struct UiControlColorValue {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct UiControlScrollValue {
    float x = 0.0f;
    float y = 0.0f;
};

struct UiControlValue {
    UiControlValueKind kind = UiControlValueKind::None;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    bool boolean = false;
    float number = 0.0f;
    std::uint32_t index = 0u;
    UiControlScrollValue scroll;
    UiControlColorValue color;

    static UiControlValue none();
    static UiControlValue fromText(arp::core::pmr::String value);
    static UiControlValue fromBool(bool value);
    static UiControlValue fromNumber(float value);
    static UiControlValue fromIndex(std::uint32_t value);
    static UiControlValue fromScroll(float x, float y);
    static UiControlValue fromColor(float r, float g, float b, float a);
};

struct UiControlDesc {
    UiControlType type = UiControlType::Unknown;
    WidgetDesc widget;
    UiControlValue initialValue;
    arp::core::pmr::Vector<arp::core::pmr::String> optionLabels{
        &arp::core::defaultCpuMemoryResource()};
};

struct ControlDesc : public UiControlDesc {};

class UiControl final {
public:
    UiControl(WidgetId widgetId, UiControlType type, UiControlValue value);

    WidgetId widgetId() const { return widgetId_; }

    UiControlType type() const { return type_; }

    const UiControlValue& value() const { return value_; }

    void setValue(UiControlValue value) { value_ = std::move(value); }

private:
    WidgetId widgetId_;
    UiControlType type_ = UiControlType::Unknown;
    UiControlValue value_;
};

struct UiControlInteraction;

class Control : public IUiEventTarget {
public:
    Control();
    virtual ~Control() = default;
    Control(const Control&) = delete;
    Control& operator=(const Control&) = delete;

    explicit Control(ControlDesc desc) : desc_(std::move(desc)) {}

    const ControlDesc& desc() const { return desc_; }

    UiControlType controlType() const { return desc_.type; }

    WidgetId widgetId() const { return state_.widgetId(); }

    const UiControlValue& value() const { return state_.value(); }

    const UiControl& state() const { return state_; }

    UiResult handleEvent(const UiEvent& event, UiEventContext& context) final;
    virtual UiResult applyDefaultAction(const UiEvent& event, UiEventContext& context);
    virtual UiResult applyInteraction(
        const UiControlInteraction& interaction, UiEventContext& context);
    UiResult setValue(UiControlValue value, UiNotifyPolicy notify = UiNotifyPolicy::Silent);

protected:
    UiControlRuntime* runtime() const { return owner_; }

    virtual UiResult onPreviewEvent(const UiEvent&, UiEventContext&) { return UiResult::success(); }

    virtual UiResult onEvent(const UiEvent&, UiEventContext&) { return UiResult::success(); }

    virtual UiResult onBubbleEvent(const UiEvent&, UiEventContext&) { return UiResult::success(); }

    virtual void onValueChanged(const UiControlValue&, const UiControlValue&) {}

    virtual void onFocusChanged(bool) {}

    virtual void onPointerCaptureLost() {}

    virtual void onPointerCancelled() {}

    UiResult press(const UiEvent& event, UiEventContext& context);
    bool activates(const UiEvent& event, UiEventContext& context) const;

    explicit Control(UiControlType type) { desc_.type = type; }

private:
    friend class UiControlRuntime;
    ControlDesc desc_;
    UiControl state_{WidgetId::invalid(), UiControlType::Unknown, UiControlValue::none()};
    UiControlRuntime* owner_ = nullptr;
    bool notifying_ = false;
    void notifyValueChanged(const UiControlValue& previous);
};

class Panel : public Control {
public:
    Panel() : Control(UiControlType::Panel) {}
};

class Text : public Control {
public:
    Text() : Control(UiControlType::Label) {}

    UiResult setText(arp::core::pmr::String value) const
    {
        return value.size() <= 1024u
            ? UiResult::success()
            : UiResult::failure(UiDiagnosticCode::TextEditOverflow,
                  arp::core::pmr::String("UI text value exceeds the explicit text control limit.",
                      &arp::core::defaultCpuMemoryResource()));
    }
};

class Image : public Control {
protected:
    virtual void onHit(UiEventContext&) {}

public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;
    UiResult applyDefaultAction(const UiEvent&, UiEventContext&) override;

    Image() : Control(UiControlType::Image) {}

    UiResult validateTextureBinding(const arp::core::pmr::String& resourceId) const
    {
        return !resourceId.empty()
            ? UiResult::success()
            : UiResult::failure(UiDiagnosticCode::MissingRendererResourceBinding,
                  arp::core::pmr::String("UI image requires an explicit texture binding.",
                      &arp::core::defaultCpuMemoryResource()));
    }
};

class Button : public Control {
protected:
    virtual void onClick(UiEventContext&) {}

public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;
    UiResult applyDefaultAction(const UiEvent&, UiEventContext&) override;

    Button() : Control(UiControlType::Button) {}

    UiResult click(UiControlValue& value) const
    {
        value = UiControlValue::fromBool(true);
        return UiResult::success();
    }
};

class Slider : public Control {
public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;
    UiResult applyDefaultAction(const UiEvent&, UiEventContext&) override;

    Slider() : Control(UiControlType::Slider) {}

    UiResult drag(UiControlValue& value, float number) const
    {
        value = UiControlValue::fromNumber(number);
        return UiResult::success();
    }
};

class Checkbox : public Control {
public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;
    UiResult applyDefaultAction(const UiEvent&, UiEventContext&) override;

    Checkbox() : Control(UiControlType::Checkbox) {}

    UiResult toggle(UiControlValue& value) const
    {
        value = UiControlValue::fromBool(!value.boolean);
        return UiResult::success();
    }
};

class TextInput : public Control {
protected:
    virtual void onTextChanged(const arp::core::pmr::String&, const arp::core::pmr::String&) {}

    void onValueChanged(const UiControlValue& before, const UiControlValue& after) override
    {
        onTextChanged(before.text, after.text);
    }

public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;
    UiResult applyDefaultAction(const UiEvent&, UiEventContext&) override;

    TextInput() : Control(UiControlType::TextInput) {}

    UiResult editText(UiControlValue& value, arp::core::pmr::String text) const
    {
        if (text.size() > 64u) {
            return UiResult::failure(UiDiagnosticCode::TextEditOverflow,
                arp::core::pmr::String("UI text input edit exceeds the explicit text input limit.",
                    &arp::core::defaultCpuMemoryResource()));
        }
        value = UiControlValue::fromText(std::move(text));
        return UiResult::success();
    }
};

class NumericInput : public Control {
public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;

    NumericInput() : Control(UiControlType::NumericInput) {}

    UiResult step(UiControlValue& value, float delta) const
    {
        value = UiControlValue::fromNumber(value.number + delta);
        return UiResult::success();
    }
};

class ScrollView : public Control {
public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;
    UiResult applyDefaultAction(const UiEvent&, UiEventContext&) override;

    ScrollView() : Control(UiControlType::ScrollView) {}

    UiResult scrollBy(UiControlValue& value, float x, float y) const
    {
        value = UiControlValue::fromScroll(value.scroll.x + x, value.scroll.y + y);
        return UiResult::success();
    }
};

class Dropdown : public Control {
protected:
    virtual void onSelectionChanged(uint32_t, uint32_t) {}

    void onValueChanged(const UiControlValue& before, const UiControlValue& after) override
    {
        onSelectionChanged(before.index, after.index);
    }

public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;

    Dropdown() : Control(UiControlType::Dropdown) {}

    UiResult select(UiControlValue& value, std::uint32_t index, std::uint32_t optionCount) const
    {
        const auto& labels = desc().optionLabels;
        if (optionCount == 0u) {
            return UiResult::failure(UiDiagnosticCode::MissingControlOption,
                arp::core::pmr::String("UI selection requires explicit option metadata.",
                    &arp::core::defaultCpuMemoryResource()));
        }
        if (!labels.empty() && optionCount != labels.size()) {
            return UiResult::failure(UiDiagnosticCode::InvalidSelectionIndex,
                arp::core::pmr::String("UI selection count differs from its declared options.",
                    &arp::core::defaultCpuMemoryResource()));
        }
        if (index >= optionCount) {
            return UiResult::failure(UiDiagnosticCode::InvalidSelectionIndex,
                arp::core::pmr::String("UI selection index is outside the explicit option range.",
                    &arp::core::defaultCpuMemoryResource()));
        }
        value = UiControlValue::fromIndex(index);
        return UiResult::success();
    }
};

class Tabs : public Control {
protected:
    virtual void onSelectionChanged(uint32_t, uint32_t) {}

    void onValueChanged(const UiControlValue& before, const UiControlValue& after) override
    {
        onSelectionChanged(before.index, after.index);
    }

public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;

    Tabs() : Control(UiControlType::Tabs) {}

    UiResult select(UiControlValue& value, std::uint32_t index, std::uint32_t optionCount) const
    {
        return Dropdown().select(value, index, optionCount);
    }
};

class TreeView : public Control {
protected:
    virtual void onSelectionChanged(uint32_t, uint32_t) {}

    void onValueChanged(const UiControlValue& before, const UiControlValue& after) override
    {
        onSelectionChanged(before.index, after.index);
    }

public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;

    TreeView() : Control(UiControlType::TreeView) {}

    UiResult select(UiControlValue& value, std::uint32_t index, std::uint32_t optionCount) const
    {
        return Dropdown().select(value, index, optionCount);
    }
};

class ColorPicker : public Control {
public:
    UiResult applyInteraction(const UiControlInteraction&, UiEventContext&) override;

    ColorPicker() : Control(UiControlType::ColorPicker) {}

    UiResult pickColor(UiControlValue& value, float r, float g, float b, float a) const
    {
        value = UiControlValue::fromColor(r, g, b, a);
        return UiResult::success();
    }
};

class UiControlRuntimeResult final {
public:
    static UiControlRuntimeResult success(WidgetId widgetId);
    static UiControlRuntimeResult failure(UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    WidgetId widgetId() const { return widgetId_; }

private:
    UiControlRuntimeResult(UiDiagnostic diagnostic, WidgetId widgetId);

    UiDiagnostic diagnostic_;
    WidgetId widgetId_;
};

enum class UiControlInteractionKind {
    Click,
    Toggle,
    DragSlider,
    EditText,
    StepNumber,
    ScrollBy,
    Select,
    PickColor,
    ImageHitTest,
    KeyboardNavigateNext,
    ReleasePointerCapture,
};

struct UiControlInteraction {
    UiControlInteractionKind kind = UiControlInteractionKind::Click;
    WidgetId widgetId;
    arp::core::pmr::String text{&arp::core::defaultCpuMemoryResource()};
    float number = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    UiControlColorValue color;
    std::uint32_t index = 0u;
    std::uint32_t optionCount = 0u;

    static UiControlInteraction click(WidgetId widgetId);
    static UiControlInteraction toggle(WidgetId widgetId);
    static UiControlInteraction dragSlider(WidgetId widgetId, float value);
    static UiControlInteraction editText(WidgetId widgetId, arp::core::pmr::String value);
    static UiControlInteraction stepNumber(WidgetId widgetId, float delta);
    static UiControlInteraction scrollBy(WidgetId widgetId, float x, float y);
    static UiControlInteraction select(
        WidgetId widgetId, std::uint32_t index, std::uint32_t optionCount);
    static UiControlInteraction pickColor(WidgetId widgetId, float r, float g, float b, float a);
    static UiControlInteraction imageHitTest(WidgetId widgetId);
    static UiControlInteraction keyboardNavigateNext();
    static UiControlInteraction releasePointerCapture();
};

class UiControlInteractionResult final {
public:
    static UiControlInteractionResult success(
        WidgetId widgetId, WidgetId focusedWidget, WidgetId capturedWidget);
    static UiControlInteractionResult failure(
        UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    WidgetId widgetId() const { return widgetId_; }

    WidgetId focusedWidget() const { return focusedWidget_; }

    WidgetId capturedWidget() const { return capturedWidget_; }

private:
    UiControlInteractionResult(UiDiagnostic diagnostic,
        WidgetId widgetId,
        WidgetId focusedWidget,
        WidgetId capturedWidget);

    UiDiagnostic diagnostic_;
    WidgetId widgetId_;
    WidgetId focusedWidget_;
    WidgetId capturedWidget_;
};

class UiControlRuntime final {
public:
    UiControlRuntime(arp::core::pmr::String name, CanvasDesc desc);
    ~UiControlRuntime();

    arp::core::LifetimeEpoch::Ticket lifetimeTicket() const { return lifetime_.ticket(); }

    WidgetId rootId() const { return canvas_.rootId(); }

    Canvas& canvas() { return canvas_; }

    const Canvas& canvas() const { return canvas_; }

    std::size_t controlCount() const { return controls_.size(); }

    const UiControl* findControl(WidgetId widgetId) const;
    Control* findControlObject(WidgetId widgetId);
    const Control* findControlObject(WidgetId widgetId) const;

    WidgetId focusedControl() const { return focusedControl_; }

    WidgetId capturedControl() const { return capturedControl_; }

    UiControlRuntimeResult createControl(UiControlDesc desc, WidgetId parentId);
    UiControlRuntimeResult createControl(
        arp::core::UniquePtr<Control> control, UiControlDesc desc, WidgetId parentId);
    UiInputDispatchResult dispatch(UiEvent event);
    UiInputFrameDispatchResult dispatchFrame(
        const arp::input::InputFrame& frame, UiInputFrameRouteDesc desc);
    UiResult removeControl(WidgetId widgetId);
    UiResult reparentControl(WidgetId widgetId, WidgetId parentId);
    UiResult setVisible(WidgetId widgetId, bool visible);
    UiResult setEnabled(WidgetId widgetId, bool enabled);
    UiResult cancelInput();
    UiResult unload();
    UiResult requestFocus(WidgetId widgetId);
    UiResult capturePointer(WidgetId widgetId,
        arp::input::InputMouseButton button = arp::input::InputMouseButton::Left);
    UiResult releasePointer();

    WidgetId hoveredControl() const { return hoveredControl_; }

    WidgetId pressedControl() const { return pressedControl_; }

    bool eventEligible(WidgetId widgetId) const;
    bool pointerInside(WidgetId widgetId, const UiEvent& event) const;

    void markClicked() { clicked_ = true; }

    UiControlRuntimeResult setValue(
        WidgetId widgetId, UiControlValue value, UiNotifyPolicy notify = UiNotifyPolicy::Silent);
    UiControlRuntimeResult beginFocusCapture(WidgetId widgetId);
    UiControlInteractionResult interact(UiControlInteraction interaction);

private:
    WidgetId nextFocusableControl() const;

    Canvas canvas_;
    friend class Control;
    friend class UiEventContext;
    arp::core::LifetimeEpoch lifetime_;
    arp::core::pmr::Vector<arp::core::UniquePtr<Control>> controls_;
    arp::core::pmr::Vector<WidgetId> route_;
    enum class MutationKind { Remove, Reparent, Create };

    struct Mutation {
        MutationKind kind;
        WidgetId widget;
        WidgetId parent;
    };

    arp::core::BoundedSequence<Mutation> mutations_;
    arp::core::BoundedSequence<WidgetId> pendingRemoval_;
    bool dispatching_ = false;
    bool clicked_ = false;
    bool unloading_ = false;
    WidgetId hoveredControl_;
    WidgetId pressedControl_;
    arp::input::InputMouseButton capturedButton_ = arp::input::InputMouseButton::Left;
    CanvasLayoutResult eventLayout_ = CanvasLayoutResult::success({});
    uint64_t inputWindow_ = 0;
    bool inputWindowBound_ = false;
    UiPoint pointerPosition_;
    bool pointerValid_ = false;
    arp::core::pmr::Vector<arp::input::InputKey> inputHeldKeys_;
    arp::core::pmr::Vector<arp::input::InputKey> inputUiKeys_;
    bool inputHeldButtons_[3] = {};
    bool inputUiButtons_[3] = {};
    void syncValue(Control& control);
    UiResult directEvent(WidgetId id, UiInputEventKind kind);
    UiResult cancelSubtree(WidgetId id);
    UiResult finishMutations();
    bool pendingRemoval(WidgetId id) const;
    WidgetId nextFocus(bool backwards) const;
    WidgetId focusedControl_;
    WidgetId capturedControl_;
};

class Runtime final {
public:
    Runtime(arp::core::pmr::String name, CanvasDesc desc) : runtime_(std::move(name), desc) {}

    WidgetId rootId() const { return runtime_.rootId(); }

    Canvas& canvas() { return runtime_.canvas(); }

    const Canvas& canvas() const { return runtime_.canvas(); }

    std::size_t controlCount() const { return runtime_.controlCount(); }

    const UiControl* findControl(WidgetId widgetId) const { return runtime_.findControl(widgetId); }

    WidgetId focusedControl() const { return runtime_.focusedControl(); }

    WidgetId capturedControl() const { return runtime_.capturedControl(); }

    WidgetId hoveredControl() const { return runtime_.hoveredControl(); }

    WidgetId pressedControl() const { return runtime_.pressedControl(); }

    Control* findControlObject(WidgetId id) { return runtime_.findControlObject(id); }

    const Control* findControlObject(WidgetId id) const { return runtime_.findControlObject(id); }

    UiControlRuntimeResult createControl(
        arp::core::UniquePtr<Control> control, ControlDesc desc, WidgetId parent)
    {
        return runtime_.createControl(std::move(control), std::move(desc), parent);
    }

    UiInputDispatchResult dispatch(UiEvent event) { return runtime_.dispatch(std::move(event)); }

    UiInputFrameDispatchResult dispatchFrame(
        const arp::input::InputFrame& frame, UiInputFrameRouteDesc desc)
    {
        return runtime_.dispatchFrame(frame, desc);
    }

    UiResult removeControl(WidgetId id) { return runtime_.removeControl(id); }

    UiResult reparentControl(WidgetId id, WidgetId parent)
    {
        return runtime_.reparentControl(id, parent);
    }

    UiResult setVisible(WidgetId id, bool value) { return runtime_.setVisible(id, value); }

    UiResult setEnabled(WidgetId id, bool value) { return runtime_.setEnabled(id, value); }

    UiResult cancelInput() { return runtime_.cancelInput(); }

    UiResult unload() { return runtime_.unload(); }

    UiResult requestFocus(WidgetId id) { return runtime_.requestFocus(id); }

    UiResult capturePointer(
        WidgetId id, arp::input::InputMouseButton button = arp::input::InputMouseButton::Left)
    {
        return runtime_.capturePointer(id, button);
    }

    UiResult releasePointer() { return runtime_.releasePointer(); }

    UiControlRuntimeResult createControl(ControlDesc desc, WidgetId parentId)
    {
        return runtime_.createControl(std::move(desc), parentId);
    }

    UiControlRuntimeResult setValue(
        WidgetId widgetId, UiControlValue value, UiNotifyPolicy notify = UiNotifyPolicy::Silent)
    {
        return runtime_.setValue(widgetId, std::move(value), notify);
    }

    UiControlRuntimeResult beginFocusCapture(WidgetId widgetId)
    {
        return runtime_.beginFocusCapture(widgetId);
    }

    UiControlInteractionResult interact(UiControlInteraction interaction)
    {
        return runtime_.interact(std::move(interaction));
    }

    UiControlRuntime& legacyRuntimeForMigration() { return runtime_; }

    const UiControlRuntime& legacyRuntimeForMigration() const { return runtime_; }

private:
    UiControlRuntime runtime_;
};

struct DocumentComponentDesc {
    arp::core::pmr::String documentSource{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String resourceId{&arp::core::defaultCpuMemoryResource()};
    uint32_t layer = 0;
    arp::core::pmr::String inputRoute{&arp::core::defaultCpuMemoryResource()};
    arp::core::pmr::String renderRoute{&arp::core::defaultCpuMemoryResource()};
};

class DocumentComponent final : public arp::scene::Component {
public:
    const char* scriptTypeName() const override { return "DocumentComponent"; }

    DocumentComponent() = default;

    explicit DocumentComponent(arp::core::pmr::String documentSource)
    {
        desc_.documentSource = std::move(documentSource);
    }

    explicit DocumentComponent(DocumentComponentDesc desc) : desc_(std::move(desc)) {}

    const DocumentComponentDesc& desc() const { return desc_; }

    const arp::core::pmr::String& documentSource() const { return desc_.documentSource; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        DocumentComponentEditorProperties properties;
        properties.documentSource = desc_.documentSource;
        properties.layer = desc_.layer;
        return arp::reflection::object(std::move(properties));
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<DocumentComponentEditorProperties>() || runtimeHandle_.valid())
            return false;
        const auto& properties = value.cast<const DocumentComponentEditorProperties&>();
        const auto source =
            std::string_view(properties.documentSource.data(), properties.documentSource.size());
        if (!source.empty()
            && (!arp::core::FilePath::isSafeRelativePath(source) || source.size() < 6u
                || source.substr(source.size() - 6u) != ".arpui"))
            return false;
        desc_.documentSource = properties.documentSource;
        desc_.layer = properties.layer;
        return true;
    }

    WidgetId runtimeHandle() const { return runtimeHandle_; }

    WidgetId runtimeRootWidget() const { return runtimeRootWidget_; }

    uint32_t mountedControlCount() const { return mountedControlCount_; }

    uint32_t mountedDrawRecordCount() const { return mountedDrawRecordCount_; }

    const DrawPlan& mountedDrawPlan() const { return mountedDrawPlan_; }

    bool exposesChildUiNodesAsSceneComponents() const { return false; }

    void markMountedForRuntime(
        WidgetId runtimeHandle, WidgetId rootWidget, uint32_t controlCount, DrawPlan drawPlan)
    {
        runtimeHandle_ = runtimeHandle;
        runtimeRootWidget_ = rootWidget;
        mountedControlCount_ = controlCount;
        mountedDrawRecordCount_ = static_cast<uint32_t>(drawPlan.recordCount());
        mountedDrawPlan_ = std::move(drawPlan);
    }

private:
    DocumentComponentDesc desc_;
    WidgetId runtimeHandle_;
    WidgetId runtimeRootWidget_;
    uint32_t mountedControlCount_ = 0;
    uint32_t mountedDrawRecordCount_ = 0;
    DrawPlan mountedDrawPlan_;
};

ARP_API bool isSupportedControlType(UiControlType controlType);
ARP_API const char* controlTypeName(UiControlType controlType);

enum class UiFrameworkCapability {
    Layout,
    Style,
    Interaction,
    Text,
    Image,
    ResourceBinding,
    RenderExtraction,
    Diagnostics,
    HotPathConstraints,
};

enum class UiFrameworkStatusLabel {
    ContractComplete,
    PrototypeComplete,
    BackendValidated,
    PlatformValidated,
    ProductionReady,
};

struct UiFrameworkCapabilityRecord {
    UiFrameworkCapability capability = UiFrameworkCapability::Layout;
    UiFrameworkStatusLabel status = UiFrameworkStatusLabel::ContractComplete;
    arp::core::pmr::String contractId{&arp::core::defaultCpuMemoryResource()};
};

class UiFrameworkContract final {
public:
    static UiFrameworkContract contractComplete();

    UiFrameworkStatusLabel status() const { return status_; }

    void setStatus(UiFrameworkStatusLabel status) { status_ = status; }

    void addCapability(UiFrameworkCapabilityRecord record);
    const UiFrameworkCapabilityRecord* findCapability(UiFrameworkCapability capability) const;

    const arp::core::pmr::Vector<UiFrameworkCapabilityRecord>& capabilities() const
    {
        return capabilities_;
    }

    bool allowsRuntimeDesignSourceParsing() const { return allowsRuntimeDesignSourceParsing_; }

    bool exposesNativeBackendHandles() const { return exposesNativeBackendHandles_; }

    bool allowsPerFrameSourceImport() const { return allowsPerFrameSourceImport_; }

    bool allowsPerFrameShaderCompilation() const { return allowsPerFrameShaderCompilation_; }

    bool declaresBoundedRuntimeAllocation() const { return declaresBoundedRuntimeAllocation_; }

    void setAllowsRuntimeDesignSourceParsingForTest(bool value)
    {
        allowsRuntimeDesignSourceParsing_ = value;
    }

    void setExposesNativeBackendHandlesForTest(bool value) { exposesNativeBackendHandles_ = value; }

    void setAllowsPerFrameSourceImportForTest(bool value) { allowsPerFrameSourceImport_ = value; }

    void setAllowsPerFrameShaderCompilationForTest(bool value)
    {
        allowsPerFrameShaderCompilation_ = value;
    }

    void setDeclaresBoundedRuntimeAllocationForTest(bool value)
    {
        declaresBoundedRuntimeAllocation_ = value;
    }

private:
    UiFrameworkStatusLabel status_ = UiFrameworkStatusLabel::ContractComplete;
    arp::core::pmr::Vector<UiFrameworkCapabilityRecord> capabilities_;
    bool allowsRuntimeDesignSourceParsing_ = false;
    bool exposesNativeBackendHandles_ = false;
    bool allowsPerFrameSourceImport_ = false;
    bool allowsPerFrameShaderCompilation_ = false;
    bool declaresBoundedRuntimeAllocation_ = true;
};

class UiFrameworkContractValidationResult final {
public:
    static UiFrameworkContractValidationResult success();
    static UiFrameworkContractValidationResult failure(
        UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

private:
    explicit UiFrameworkContractValidationResult(UiDiagnostic diagnostic);

    UiDiagnostic diagnostic_;
};

class UiFrameworkContractValidator final {
public:
    UiFrameworkContractValidationResult validate(const UiFrameworkContract& contract) const;
};

arp::core::pmr::String statusLabelName(UiFrameworkStatusLabel status);
const char* capabilityName(UiFrameworkCapability capability);

struct WorldAnchorComponentDesc {
    WidgetId widget;
    UiSize pixelSize;
    UiPoint viewportOffset;
};

class WorldAnchorComponent final : public arp::scene::Component {
public:
    explicit WorldAnchorComponent(WorldAnchorComponentDesc desc);

    const WorldAnchorComponentDesc& desc() const { return desc_; }

private:
    WorldAnchorComponentDesc desc_;
};

class World2DAnchorComponent final : public arp::scene::Component {
public:
    explicit World2DAnchorComponent(WorldAnchorComponentDesc desc) : desc_(desc) {}

    const WorldAnchorComponentDesc& desc() const { return desc_; }

private:
    WorldAnchorComponentDesc desc_;
};

class ScreenAnchorComponent final : public arp::scene::Component {
public:
    explicit ScreenAnchorComponent(WidgetId widget) : widget_(widget) {}

    WidgetId widget() const { return widget_; }

private:
    WidgetId widget_;
};

struct WorldAnchorProjection {
    arp::scene::GameObjectId objectId;
    WidgetId widgetId;
    LayoutRect screenRect;
};

class WorldAnchorProjectionResult {
public:
    static WorldAnchorProjectionResult success(
        arp::core::pmr::Vector<WorldAnchorProjection> anchors);
    static WorldAnchorProjectionResult failure(
        UiDiagnosticCode code, arp::core::pmr::String message);

    bool ok() const { return diagnostic_.code == UiDiagnosticCode::None; }

    const UiDiagnostic& diagnostic() const { return diagnostic_; }

    const arp::core::pmr::Vector<WorldAnchorProjection>& anchors() const { return anchors_; }

    bool usesGameObjectHierarchyForUiTree() const { return false; }

    bool exposesNativeBackendHandles() const { return false; }

private:
    WorldAnchorProjectionResult(
        UiDiagnostic diagnostic, arp::core::pmr::Vector<WorldAnchorProjection> anchors);

    UiDiagnostic diagnostic_;
    arp::core::pmr::Vector<WorldAnchorProjection> anchors_;
};

class WorldAnchorProjector {
public:
    WorldAnchorProjectionResult project(const arp::scene::Scene& scene,
        const arp::scene::SceneTraversalSnapshot& snapshot,
        const arp::scene::ResolvedCamera& camera,
        const Canvas& canvas) const;

    WorldAnchorProjectionResult projectMissingTargetForTest(WidgetId widgetId,
        const arp::scene::Scene& scene,
        const arp::scene::SceneTraversalSnapshot& snapshot,
        const arp::scene::ResolvedCamera& camera,
        const Canvas& canvas) const;

private:
    LayoutRect projectAnchor(const arp::scene::SceneTraversalNode& node,
        const arp::scene::ResolvedCamera& camera,
        const Canvas& canvas,
        const WorldAnchorComponentDesc& desc) const;
};

} // namespace arp::ui
