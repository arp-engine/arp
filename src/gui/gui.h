#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "ui/ui.h"
#include "ui/ui_font.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace arp::gui {

using Axis = arp::ui::Axis;
using Align = arp::ui::Align;
using Size = arp::ui::Size;
using Rect = arp::ui::Rect;
using Spacing = arp::ui::Spacing;

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

using Style = arp::ui::Style;

enum class NodeType {
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
    Image
};

enum class PointerEventType { Move, Down, Up };

struct PointerEvent {
    PointerEventType type = PointerEventType::Move;
    float x = 0.0f;
    float y = 0.0f;
    uint32_t button = 0;
};

enum class Key { Tab, Enter, Space, Backspace };

struct KeyEvent {
    Key key = Key::Tab;
    bool shift = false;
};

struct TextInputEvent {
    std::pmr::string text;
};

struct ScrollEvent {
    float x = 0.0f;
    float y = 0.0f;
    float deltaY = 0.0f;
};

struct PointerEventResult {
    uint64_t targetId = 0;
    bool clicked = false;
    bool valueChanged = false;
    float value = 0.0f;
    bool toggled = false;
    bool checked = false;
    bool textChanged = false;
    std::pmr::string text;
    bool expanded = false;
    uint32_t selectedIndex = 0;
};

enum class VisualState { Normal, Hovered, Active, Focused, Disabled };

struct DebugNode {
    DebugNode() = default;

    DebugNode(uint64_t id,
        NodeType type = NodeType::Panel,
        std::pmr::string text = {},
        Style style = {},
        Rect rect = {},
        std::pmr::vector<DebugNode> children = {},
        float minValue = 0.0f,
        float maxValue = 1.0f,
        float value = 0.0f,
        bool checked = false,
        float scrollY = 0.0f,
        float maxScrollY = 0.0f,
        bool disabled = false,
        bool expanded = false,
        uint32_t selectedIndex = 0,
        Color color = {})
        : id(id), type(type), text(std::move(text)), style(std::move(style)), rect(rect),
          children(std::move(children)), minValue(minValue), maxValue(maxValue), value(value),
          checked(checked), scrollY(scrollY), maxScrollY(maxScrollY), disabled(disabled),
          expanded(expanded), selectedIndex(selectedIndex), color(color)
    {}

    uint64_t id = 0;
    NodeType type = NodeType::Panel;
    std::pmr::string text;
    Style style;
    Rect rect;
    std::pmr::vector<DebugNode> children;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.0f;
    bool checked = false;
    float scrollY = 0.0f;
    float maxScrollY = 0.0f;
    bool disabled = false;
    bool expanded = false;
    uint32_t selectedIndex = 0;
    Color color;
};

using Node = DebugNode;

struct ControlOptions {
    ControlOptions() = default;

    ControlOptions(Size controlSize,
        Size controlMinSize = {},
        Spacing controlPadding = {},
        float controlGap = 0.0f,
        Axis controlAxis = Axis::Column,
        Align controlAlign = Align::Start,
        bool controlStretchWidth = false,
        bool controlStretchHeight = false,
        Size controlPercentSize = {},
        Size controlMaxSize = {},
        bool controlDisabled = false)
        : size(controlSize), minSize(controlMinSize), padding(controlPadding), gap(controlGap),
          axis(controlAxis), align(controlAlign), stretchWidth(controlStretchWidth),
          stretchHeight(controlStretchHeight), percentSize(controlPercentSize),
          maxSize(controlMaxSize), disabled(controlDisabled)
    {}

    Size size;
    Size minSize;
    Spacing padding;
    float gap = 0.0f;
    Axis axis = Axis::Column;
    Align align = Align::Start;
    bool stretchWidth = false;
    bool stretchHeight = false;
    Size percentSize;
    Size maxSize;
    bool disabled = false;
};

inline Style styleFromOptions(const ControlOptions& options)
{
    return Style{
        options.size,
        options.minSize,
        options.padding,
        options.gap,
        options.axis,
        options.align,
        options.stretchWidth,
        options.stretchHeight,
        options.percentSize,
        options.maxSize,
    };
}

inline ControlOptions sized(Size size)
{
    ControlOptions options;
    options.size = size;
    return options;
}

inline DebugNode panel(uint64_t id, std::pmr::string text, ControlOptions options = {})
{
    return DebugNode{
        id,
        NodeType::Panel,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode scrollView(uint64_t id, std::pmr::string text, ControlOptions options = {})
{
    return DebugNode{
        id,
        NodeType::ScrollView,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode label(uint64_t id, std::pmr::string text, ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::Label,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode label(uint64_t id, std::pmr::string text, Size size)
{
    return label(id, std::move(text), sized(size));
}

inline DebugNode button(uint64_t id, std::pmr::string text, ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::Button,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode button(uint64_t id, std::pmr::string text, Size size)
{
    return button(id, std::move(text), sized(size));
}

inline DebugNode slider(uint64_t id,
    std::pmr::string text,
    float value,
    float minValue,
    float maxValue,
    ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::Slider,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        minValue,
        maxValue,
        value,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode slider(
    uint64_t id, std::pmr::string text, float value, float minValue, float maxValue, Size size)
{
    return slider(id, std::move(text), value, minValue, maxValue, sized(size));
}

inline DebugNode checkbox(uint64_t id, std::pmr::string text, bool checked, ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::Checkbox,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        checked,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode checkbox(uint64_t id, std::pmr::string text, bool checked, Size size)
{
    return checkbox(id, std::move(text), checked, sized(size));
}

inline DebugNode dropdown(uint64_t id,
    std::pmr::string text,
    bool expanded,
    uint32_t selectedIndex,
    ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::Dropdown,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
        expanded,
        selectedIndex,
    };
}

inline DebugNode dropdown(
    uint64_t id, std::pmr::string text, bool expanded, uint32_t selectedIndex, Size size)
{
    return dropdown(id, std::move(text), expanded, selectedIndex, sized(size));
}

inline DebugNode treeView(uint64_t id,
    std::pmr::string text,
    bool expanded,
    uint32_t selectedIndex,
    ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::TreeView,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
        expanded,
        selectedIndex,
    };
}

inline DebugNode treeView(
    uint64_t id, std::pmr::string text, bool expanded, uint32_t selectedIndex, Size size)
{
    return treeView(id, std::move(text), expanded, selectedIndex, sized(size));
}

inline DebugNode tabs(
    uint64_t id, std::pmr::string text, uint32_t selectedIndex, ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::Tabs,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
        false,
        selectedIndex,
    };
}

inline DebugNode tabs(uint64_t id, std::pmr::string text, uint32_t selectedIndex, Size size)
{
    return tabs(id, std::move(text), selectedIndex, sized(size));
}

inline DebugNode colorPicker(
    uint64_t id, std::pmr::string text, Color color, ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::ColorPicker,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        color.r,
        false,
        0.0f,
        0.0f,
        options.disabled,
        false,
        0,
        color,
    };
}

inline DebugNode colorPicker(uint64_t id, std::pmr::string text, Color color, Size size)
{
    return colorPicker(id, std::move(text), color, sized(size));
}

inline DebugNode textInput(uint64_t id, std::pmr::string text, ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::TextInput,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        0.0f,
        1.0f,
        0.0f,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode textInput(uint64_t id, std::pmr::string text, Size size)
{
    return textInput(id, std::move(text), sized(size));
}

inline DebugNode numericInput(uint64_t id,
    std::pmr::string text,
    float value,
    float minValue,
    float maxValue,
    ControlOptions options)
{
    return DebugNode{
        id,
        NodeType::NumericInput,
        std::move(text),
        styleFromOptions(options),
        {},
        {},
        minValue,
        maxValue,
        value,
        false,
        0.0f,
        0.0f,
        options.disabled,
    };
}

inline DebugNode numericInput(
    uint64_t id, std::pmr::string text, float value, float minValue, float maxValue, Size size)
{
    return numericInput(id, std::move(text), value, minValue, maxValue, sized(size));
}

class ARP_API Builder {
public:
    explicit Builder(arp::ui::Runtime& runtime);

    arp::ui::WidgetId button(uint64_t id, std::pmr::string text, Size size = {96.0f, 24.0f});
    arp::ui::WidgetId sliderFloat(uint64_t id,
        std::pmr::string text,
        float value,
        float minValue,
        float maxValue,
        Size size = {180.0f, 24.0f});
    arp::ui::WidgetId checkbox(
        uint64_t id, std::pmr::string text, bool value, Size size = {160.0f, 24.0f});

private:
    arp::ui::WidgetId createOrUpdate(uint64_t id,
        std::pmr::string text,
        arp::ui::UiControlType type,
        arp::ui::UiControlValue value,
        Size size);

    arp::ui::Runtime& runtime_;
    arp::core::pmr::UnorderedMap<uint64_t, arp::ui::WidgetId> widgets_;
};

struct DebugDrawQuad {
    uint64_t nodeId = 0;
    NodeType type = NodeType::Panel;
    Rect rect;
    VisualState visualState = VisualState::Normal;
    Color color;
    bool customColor = false;
    std::pmr::string textureResourceId;
    Rect textureUvRect;
    bool textured = false;
};

struct DebugDrawTextQuad {
    uint64_t nodeId = 0;
    Rect rect;
    Color color;
};

struct ARP_API DebugDrawData {
    std::pmr::vector<DebugDrawQuad> quads;
    std::pmr::vector<DebugDrawTextQuad> textQuads;
    const DebugDrawQuad* quadFor(uint64_t nodeId) const;
};

struct FontAtlasPlan {
    std::pmr::vector<std::pmr::string> textRuns;
    arp::ui::UiFontLoadResult defaultFont;
};

class ARP_API DebugGuiContext {
public:
    DebugNode& root();
    const DebugNode& root() const;
    DebugNode& addChild(DebugNode& parent, DebugNode child);
    void attachRuntime(arp::ui::Runtime& runtime);
    void layout(Size viewport);
    DebugNode* find(uint64_t id);
    PointerEventResult dispatchPointerEvent(PointerEvent event);
    PointerEventResult dispatchKeyEvent(KeyEvent event);
    PointerEventResult dispatchTextInput(TextInputEvent event);
    PointerEventResult dispatchScrollEvent(ScrollEvent event);
    PointerEventResult consumeLastEventResult();
    DebugDrawData buildDrawData() const;
    FontAtlasPlan buildFontAtlasPlan() const;
    uint64_t hoveredId() const;
    uint64_t activeId() const;
    uint64_t focusedId() const;

private:
    DebugNode root_{1, NodeType::Panel, "root", {}, {}, {}};
    uint64_t hoveredId_ = 0;
    uint64_t activeId_ = 0;
    uint64_t focusedId_ = 0;
    uint64_t scrollDragId_ = 0;
    float scrollDragStartY_ = 0.0f;
    float scrollDragStartScrollY_ = 0.0f;
    PointerEventResult lastEventResult_;
    arp::ui::Runtime* forwardedRuntime_ = nullptr;
    arp::ui::InputRouter forwardedInputRouter_;
};

using GuiContext [[deprecated("Legacy debug facade; product UI uses ui::Runtime")]] =
    DebugGuiContext;

} // namespace arp::gui
