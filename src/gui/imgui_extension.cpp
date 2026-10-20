#include "gui/imgui_extension.h"

#include "core/cpu_allocator.h"
#include "core/geometry.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace arp::gui {

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr float kTextHeight = 20.0f;
constexpr float kFrameHeight = 20.0f;
constexpr float kTitleBarHeight = 28.0f;
constexpr float kItemSpacingX = 8.0f;
constexpr float kItemSpacingY = 4.0f;

uint64_t hashBytes(uint64_t hash, const char* data)
{
    for (const auto* cursor = data; *cursor != '\0'; ++cursor) {
        hash ^= static_cast<unsigned char>(*cursor);
        hash *= kFnvPrime;
    }
    return hash;
}

uint64_t hashValue(uint64_t hash, uint64_t value)
{
    for (uint32_t byte = 0; byte < 8; ++byte) {
        hash ^= static_cast<unsigned char>((value >> (byte * 8)) & 0xffu);
        hash *= kFnvPrime;
    }
    return hash;
}

void captureStateRecursive(const DebugNode& DebugNode,
    std::pmr::unordered_map<uint64_t, ImGuiExtension::WidgetState>& state)
{
    state[DebugNode.id] = ImGuiExtension::WidgetState{
        DebugNode.checked,
        DebugNode.value,
        DebugNode.text,
        DebugNode.expanded,
        DebugNode.selectedIndex,
        DebugNode.color,
        DebugNode.scrollY,
    };

    for (const auto& child : DebugNode.children) {
        captureStateRecursive(child, state);
    }
}

std::pmr::string formatFloat(float value)
{
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    std::pmr::string text = buffer;
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text.empty() ? "0" : text;
}

ControlOptions defaultContainerOptions(Size size)
{
    if (size.width <= 0.0f) {
        size.width = 320.0f;
    }
    if (size.height <= 0.0f) {
        size.height = 240.0f;
    }

    return ControlOptions{
        size,
        {},
        {8.0f, 8.0f, 8.0f, 8.0f},
        kItemSpacingY,
        Axis::Column,
        Align::Start,
    };
}

ControlOptions defaultControlOptions(Size size)
{
    return ControlOptions{
        size,
        {},
        {},
        0.0f,
        Axis::Column,
        Align::Start,
    };
}

arp::core::Rect2<float> toCoreRect(const Rect& rect)
{
    return arp::core::Rect2<float>{rect.x, rect.y, rect.width, rect.height};
}

bool intersects(const Rect& lhs, const Rect& rhs)
{
    return arp::core::rectsIntersect(toCoreRect(lhs), toCoreRect(rhs));
}

Rect intersectRect(const Rect& lhs, const Rect& rhs)
{
    const auto result = arp::core::intersectRect(toCoreRect(lhs), toCoreRect(rhs));
    return Rect{
        result.x,
        result.y,
        result.width,
        result.height,
    };
}

bool hasVerticalScrollBar(const DebugNode& DebugNode)
{
    return DebugNode.type == NodeType::ScrollView && DebugNode.maxScrollY > 0.0f
        && DebugNode.rect.height > 0.0f;
}

Rect scrollTrackRect(const DebugNode& DebugNode)
{
    constexpr float trackWidth = 8.0f;
    constexpr float inset = 2.0f;
    return Rect{
        DebugNode.rect.x + DebugNode.rect.width - trackWidth - inset,
        DebugNode.rect.y + inset,
        trackWidth,
        std::max(0.0f, DebugNode.rect.height - inset * 2.0f),
    };
}

Rect scrollThumbRect(const DebugNode& DebugNode)
{
    const auto track = scrollTrackRect(DebugNode);
    if (!hasVerticalScrollBar(DebugNode) || track.height <= 0.0f) {
        return {};
    }

    const auto contentHeight = DebugNode.maxScrollY + DebugNode.rect.height;
    const auto visibleRatio = contentHeight > 0.0f ? DebugNode.rect.height / contentHeight : 1.0f;
    const auto thumbHeight = std::min(track.height, std::max(12.0f, track.height * visibleRatio));
    const auto travel = std::max(0.0f, track.height - thumbHeight);
    const auto scrollRatio =
        DebugNode.maxScrollY > 0.0f ? DebugNode.scrollY / DebugNode.maxScrollY : 0.0f;
    return Rect{
        track.x,
        track.y + travel * scrollRatio,
        track.width,
        thumbHeight,
    };
}

VisualState visualStateFor(
    const DebugNode& DebugNode, uint64_t hoveredId, uint64_t activeId, uint64_t focusedId)
{
    if (DebugNode.disabled) {
        return VisualState::Disabled;
    }
    if (DebugNode.id == activeId) {
        return VisualState::Active;
    }
    if (DebugNode.id == hoveredId) {
        return VisualState::Hovered;
    }
    if (DebugNode.id == focusedId) {
        return VisualState::Focused;
    }
    return VisualState::Normal;
}

float checkboxBoxSize(const DebugNode& DebugNode)
{
    return std::max(8.0f, std::min(14.0f, DebugNode.rect.height - 6.0f));
}

Rect checkboxBoxRect(const DebugNode& DebugNode)
{
    const auto boxSize = checkboxBoxSize(DebugNode);
    return Rect{
        DebugNode.rect.x + 4.0f,
        DebugNode.rect.y + (DebugNode.rect.height - boxSize) * 0.5f,
        boxSize,
        boxSize,
    };
}

float colorSwatchSize(const DebugNode& DebugNode)
{
    return std::max(8.0f, std::min(16.0f, DebugNode.rect.height - 4.0f));
}

Rect colorSwatchRect(const DebugNode& DebugNode)
{
    const auto swatchSize = colorSwatchSize(DebugNode);
    return Rect{
        DebugNode.rect.x + 3.0f,
        DebugNode.rect.y + (DebugNode.rect.height - swatchSize) * 0.5f,
        swatchSize,
        swatchSize,
    };
}

float themedTextInset(const DebugNode& DebugNode)
{
    if (DebugNode.type == NodeType::Checkbox) {
        return 4.0f + checkboxBoxSize(DebugNode) + 6.0f;
    }
    if (DebugNode.type == NodeType::ColorPicker) {
        return 3.0f + colorSwatchSize(DebugNode) + 9.0f;
    }
    return 4.0f;
}

Color windowBg()
{
    return {0.12f, 0.12f, 0.12f, 0.96f};
}

Color titleBg()
{
    return {0.0f, 0.0f, 0.0f, 1.0f};
}

Color frameBg(VisualState state)
{
    if (state == VisualState::Hovered) {
        return {0.38f, 0.38f, 0.38f, 0.92f};
    }
    if (state == VisualState::Active) {
        return {0.48f, 0.48f, 0.48f, 0.96f};
    }
    if (state == VisualState::Disabled) {
        return {0.20f, 0.20f, 0.20f, 0.18f};
    }
    return {0.28f, 0.28f, 0.28f, 0.88f};
}

void appendQuad(DebugDrawData& DebugDrawData,
    uint64_t nodeId,
    NodeType type,
    Rect rect,
    VisualState visualState,
    Color color,
    const Rect* clipRect)
{
    if (clipRect) {
        rect = intersectRect(rect, *clipRect);
        if (rect.width <= 0.0f || rect.height <= 0.0f) {
            return;
        }
    }

    DebugDrawData.quads.push_back(DebugDrawQuad{
        nodeId,
        type,
        rect,
        visualState,
        color,
        true,
    });
}

void appendBorder(DebugDrawData& DebugDrawData,
    const DebugNode& DebugNode,
    const Rect& rect,
    VisualState state,
    const Rect* clipRect)
{
    constexpr float thickness = 1.0f;
    const Color border{0.0f, 0.0f, 0.0f, state == VisualState::Disabled ? 0.55f : 1.0f};
    appendQuad(DebugDrawData,
        DebugNode.id,
        DebugNode.type,
        {rect.x, rect.y, rect.width, thickness},
        state,
        border,
        clipRect);
    appendQuad(DebugDrawData,
        DebugNode.id,
        DebugNode.type,
        {rect.x, rect.y + rect.height - thickness, rect.width, thickness},
        state,
        border,
        clipRect);
    appendQuad(DebugDrawData,
        DebugNode.id,
        DebugNode.type,
        {rect.x, rect.y, thickness, rect.height},
        state,
        border,
        clipRect);
    appendQuad(DebugDrawData,
        DebugNode.id,
        DebugNode.type,
        {rect.x + rect.width - thickness, rect.y, thickness, rect.height},
        state,
        border,
        clipRect);
}

bool hasBackground(NodeType type)
{
    return type != NodeType::Label;
}

void appendThemedBackground(const DebugNode& DebugNode,
    DebugDrawData& DebugDrawData,
    VisualState state,
    const Rect* clipRect)
{
    if (DebugNode.rect.width <= 0.0f || DebugNode.rect.height <= 0.0f
        || !hasBackground(DebugNode.type)) {
        return;
    }

    if (DebugNode.type == NodeType::Panel || DebugNode.type == NodeType::ScrollView) {
        appendQuad(DebugDrawData,
            DebugNode.id,
            DebugNode.type,
            DebugNode.rect,
            state,
            windowBg(),
            clipRect);
        appendBorder(DebugDrawData, DebugNode, DebugNode.rect, state, clipRect);
        if (!DebugNode.text.empty() && DebugNode.rect.height >= kTitleBarHeight) {
            appendQuad(DebugDrawData,
                DebugNode.id,
                DebugNode.type,
                {DebugNode.rect.x, DebugNode.rect.y, DebugNode.rect.width, kTitleBarHeight},
                state,
                titleBg(),
                clipRect);
        }
        if (hasVerticalScrollBar(DebugNode)) {
            appendQuad(DebugDrawData,
                DebugNode.id,
                DebugNode.type,
                scrollTrackRect(DebugNode),
                state,
                {0.03f, 0.04f, 0.05f, 0.55f},
                clipRect);
            appendQuad(DebugDrawData,
                DebugNode.id,
                DebugNode.type,
                scrollThumbRect(DebugNode),
                state,
                state == VisualState::Active ? Color{0.54f, 0.54f, 0.54f, 0.95f}
                                             : Color{0.42f, 0.42f, 0.42f, 0.62f},
                clipRect);
        }
        return;
    }

    if (DebugNode.type == NodeType::Checkbox) {
        const auto box = checkboxBoxRect(DebugNode);
        appendQuad(DebugDrawData,
            DebugNode.id,
            DebugNode.type,
            box,
            state,
            {0.10f, 0.10f, 0.10f, 1.0f},
            clipRect);
        appendBorder(DebugDrawData, DebugNode, box, state, clipRect);
        if (DebugNode.checked) {
            appendQuad(DebugDrawData,
                DebugNode.id,
                DebugNode.type,
                {box.x + 3.0f, box.y + 3.0f, box.width - 6.0f, box.height - 6.0f},
                state,
                {0.72f, 0.72f, 0.72f, 1.0f},
                clipRect);
        }
        return;
    }

    appendQuad(DebugDrawData,
        DebugNode.id,
        DebugNode.type,
        DebugNode.rect,
        state,
        frameBg(state),
        clipRect);
    appendBorder(DebugDrawData, DebugNode, DebugNode.rect, state, clipRect);

    if (DebugNode.type == NodeType::ColorPicker) {
        const auto swatch = colorSwatchRect(DebugNode);
        appendQuad(
            DebugDrawData, DebugNode.id, DebugNode.type, swatch, state, DebugNode.color, clipRect);
        appendBorder(DebugDrawData, DebugNode, swatch, state, clipRect);
    }

    if (DebugNode.type == NodeType::Slider || DebugNode.type == NodeType::ColorPicker) {
        const auto denominator = DebugNode.maxValue - DebugNode.minValue;
        const auto normalized = denominator > 0.0f
            ? std::clamp((DebugNode.value - DebugNode.minValue) / denominator, 0.0f, 1.0f)
            : std::clamp(DebugNode.color.r, 0.0f, 1.0f);
        const auto grabWidth = std::min(10.0f, std::max(6.0f, DebugNode.rect.width * 0.08f));
        const auto grabX =
            DebugNode.rect.x + std::max(0.0f, DebugNode.rect.width - grabWidth) * normalized;
        const Rect grabRect{
            grabX,
            DebugNode.rect.y + 2.0f,
            grabWidth,
            std::max(4.0f, DebugNode.rect.height - 4.0f),
        };
        const Color grabColor = state == VisualState::Active ? Color{0.70f, 0.70f, 0.70f, 1.0f}
                                                             : Color{0.58f, 0.58f, 0.58f, 0.78f};
        appendQuad(
            DebugDrawData, DebugNode.id, DebugNode.type, grabRect, state, grabColor, clipRect);
    }
}

void appendThemedDrawData(const DebugNode& DebugNode,
    DebugDrawData& DebugDrawData,
    uint64_t hoveredId,
    uint64_t activeId,
    uint64_t focusedId,
    const Rect* clipRect = nullptr)
{
    if (clipRect && !intersects(DebugNode.rect, *clipRect)) {
        return;
    }

    const auto state = visualStateFor(DebugNode, hoveredId, activeId, focusedId);
    appendThemedBackground(DebugNode, DebugDrawData, state, clipRect);

    Rect childClip;
    const Rect* nextClip = clipRect;
    if (DebugNode.type == NodeType::ScrollView) {
        childClip = clipRect ? intersectRect(DebugNode.rect, *clipRect) : DebugNode.rect;
        nextClip = &childClip;
    }

    for (const auto& child : DebugNode.children) {
        appendThemedDrawData(child, DebugDrawData, hoveredId, activeId, focusedId, nextClip);
    }
}

void collectNodes(const DebugNode& node, std::pmr::unordered_map<uint64_t, const DebugNode*>& nodes)
{
    nodes[node.id] = &node;
    for (const auto& child : node.children) {
        collectNodes(child, nodes);
    }
}

} // namespace

ImGuiExtension::ImGuiExtension(DebugGuiContext& gui) : gui_(gui)
{}

DebugDrawData ImGuiExtension::buildDrawData() const
{
    DebugDrawData DebugDrawData;
    appendThemedDrawData(
        gui_.root(), DebugDrawData, gui_.hoveredId(), gui_.activeId(), gui_.focusedId());

    std::pmr::unordered_map<uint64_t, const DebugNode*> nodes;
    collectNodes(gui_.root(), nodes);

    auto baseDrawData = gui_.buildDrawData();
    for (auto textQuad : baseDrawData.textQuads) {
        const auto found = nodes.find(textQuad.nodeId);
        if (found != nodes.end()) {
            textQuad.rect.x += themedTextInset(*found->second) - 4.0f;
        }
        DebugDrawData.textQuads.push_back(textQuad);
    }
    return DebugDrawData;
}

void ImGuiExtension::newFrame()
{
    previousState_.clear();
    captureStateRecursive(gui_.root(), previousState_);
    previousEvent_ = gui_.consumeLastEventResult();
    gui_.root().children.clear();
    containerStack_.clear();
    idStack_.clear();
    generatedOrdinal_ = 0;
    sameLineRequested_ = false;
}

bool ImGuiExtension::begin(const std::pmr::string& label, Size size)
{
    const auto parts = splitLabel(label);
    const auto id = makeId(parts.stable);
    return beginPanel(id, parts.visible, size);
}

void ImGuiExtension::end()
{
    if (!containerStack_.empty()) {
        containerStack_.pop_back();
    }
    if (!idStack_.empty()) {
        idStack_.pop_back();
    }
}

bool ImGuiExtension::beginPanel(
    uint64_t id, const std::pmr::string& label, Size size, Axis axis, float gap, Spacing padding)
{
    auto options = defaultContainerOptions(size);
    options.axis = axis;
    options.gap = gap;
    options.padding = padding;
    if (!label.empty()) {
        options.size.height += kTitleBarHeight;
        options.padding.top += kTitleBarHeight;
    }
    auto& DebugNode = addWidget(panel(id, label, options));
    containerStack_.push_back(&DebugNode);
    idStack_.push_back(id);
    return true;
}

void ImGuiExtension::endPanel()
{
    end();
}

bool ImGuiExtension::beginChild(const std::pmr::string& label, Size size)
{
    const auto parts = splitLabel(label);
    const auto id = makeId(parts.stable);
    return beginPanel(id, parts.visible, size);
}

void ImGuiExtension::endChild()
{
    end();
}

bool ImGuiExtension::beginScrollView(
    uint64_t id, const std::pmr::string& label, Size size, float gap, Spacing padding)
{
    auto options = defaultContainerOptions(size);
    options.gap = gap;
    options.padding = padding;
    if (!label.empty()) {
        options.padding.top += kTitleBarHeight;
    }
    auto& DebugNode = addWidget(scrollView(id, label, options));
    if (const auto found = previousState_.find(id); found != previousState_.end()) {
        DebugNode.scrollY = found->second.scrollY;
    }
    containerStack_.push_back(&DebugNode);
    idStack_.push_back(id);
    return true;
}

void ImGuiExtension::endScrollView()
{
    end();
}

void ImGuiExtension::sameLine()
{
    sameLineRequested_ = true;
}

void ImGuiExtension::separator()
{
    addWidget(panel(nextGeneratedId(), "", defaultControlOptions({240.0f, 1.0f})));
}

void ImGuiExtension::spacing()
{
    addWidget(panel(nextGeneratedId(), "", defaultControlOptions({1.0f, kItemSpacingY})));
}

void ImGuiExtension::text(const std::pmr::string& value)
{
    text(nextGeneratedId(), value, {240.0f, kTextHeight});
}

void ImGuiExtension::text(uint64_t id, const std::pmr::string& value, Size size)
{
    addWidget(label(id, value, size));
}

bool ImGuiExtension::button(const std::pmr::string& labelText)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    return button(id, parts.visible, {80.0f, kFrameHeight});
}

bool ImGuiExtension::button(uint64_t id, const std::pmr::string& labelText, Size size)
{
    addWidget(::arp::gui::button(id, labelText, size));
    return previousEvent_.targetId == id && previousEvent_.clicked;
}

bool ImGuiExtension::checkbox(const std::pmr::string& labelText, bool& value)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.checked != value) {
        value = found->second.checked;
        changed = true;
    }
    addWidget(::arp::gui::checkbox(id, parts.visible, value, {160.0f, kFrameHeight}));
    return changed;
}

bool ImGuiExtension::checkbox(
    uint64_t id, const std::pmr::string& labelText, bool& value, Size size)
{
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.checked != value) {
        value = found->second.checked;
        changed = true;
    }
    addWidget(::arp::gui::checkbox(id, labelText, value, size));
    return changed;
}

bool ImGuiExtension::sliderFloat(
    const std::pmr::string& labelText, float& value, float minValue, float maxValue)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    return sliderFloat(id, parts.visible, value, minValue, maxValue, {180.0f, kFrameHeight});
}

bool ImGuiExtension::sliderFloat(uint64_t id,
    const std::pmr::string& labelText,
    float& value,
    float minValue,
    float maxValue,
    Size size)
{
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.value != value) {
        value = found->second.value;
        changed = true;
    }
    addWidget(slider(id, labelText, value, minValue, maxValue, size));
    return changed;
}

bool ImGuiExtension::inputText(const std::pmr::string& labelText, char* buffer, size_t bufferSize)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    const auto changed = applyTextSnapshot(id, buffer, bufferSize);
    addWidget(textInput(id, buffer ? buffer : "", {180.0f, kFrameHeight}));
    return changed;
}

bool ImGuiExtension::inputFloat(
    const std::pmr::string& labelText, float& value, float minValue, float maxValue)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.value != value) {
        value = found->second.value;
        changed = true;
    }
    addWidget(
        numericInput(id, formatFloat(value), value, minValue, maxValue, {120.0f, kFrameHeight}));
    return changed;
}

bool ImGuiExtension::colorEdit4(const std::pmr::string& labelText, Color& value)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    return colorEdit4(id, parts.visible, value, {160.0f, kFrameHeight});
}

bool ImGuiExtension::colorEdit4(
    uint64_t id, const std::pmr::string& labelText, Color& value, Size size)
{
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.color.r != value.r) {
        value = found->second.color;
        changed = true;
    }
    addWidget(colorPicker(id, labelText, value, size));
    return changed;
}

bool ImGuiExtension::dropdown(uint64_t id,
    const std::pmr::string& labelText,
    bool expanded,
    uint32_t selectedIndex,
    Size size,
    bool disabled)
{
    auto options = sized(size);
    options.disabled = disabled;
    addWidget(::arp::gui::dropdown(id, labelText, expanded, selectedIndex, options));
    return previousEvent_.targetId == id && (previousEvent_.clicked || previousEvent_.toggled);
}

bool ImGuiExtension::combo(
    const std::pmr::string& labelText, uint32_t& selectedIndex, const char* const*, uint32_t)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.selectedIndex != selectedIndex) {
        selectedIndex = found->second.selectedIndex;
        changed = true;
    }
    const auto expanded =
        previousState_.find(id) != previousState_.end() && previousState_[id].expanded;
    addWidget(dropdown(id, parts.visible, expanded, selectedIndex, {160.0f, 24.0f}));
    return changed;
}

bool ImGuiExtension::tabs(const std::pmr::string& labelText,
    uint32_t& selectedIndex,
    const char* const* items,
    uint32_t itemCount)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    bool changed = false;
    if (const auto found = previousState_.find(id);
        found != previousState_.end() && found->second.selectedIndex != selectedIndex) {
        selectedIndex = found->second.selectedIndex;
        changed = true;
    }
    addWidget(
        ::arp::gui::tabs(id, itemText(items, itemCount), selectedIndex, {240.0f, kFrameHeight}));
    return changed;
}

bool ImGuiExtension::treeNode(const std::pmr::string& labelText)
{
    const auto parts = splitLabel(labelText);
    const auto id = makeId(parts.stable);
    auto expanded = true;
    if (const auto found = previousState_.find(id); found != previousState_.end()) {
        expanded = found->second.expanded;
    }
    return treeNode(id, parts.visible, expanded, 0, {180.0f, kFrameHeight});
}

bool ImGuiExtension::treeNode(uint64_t id,
    const std::pmr::string& labelText,
    bool expanded,
    uint32_t selectedIndex,
    Size size)
{
    if (const auto found = previousState_.find(id); found != previousState_.end()) {
        expanded = found->second.expanded;
        selectedIndex = found->second.selectedIndex;
    }
    auto& DebugNode = addWidget(treeView(id, labelText, expanded, selectedIndex, size));
    if (expanded) {
        containerStack_.push_back(&DebugNode);
        idStack_.push_back(id);
    }
    return expanded;
}

void ImGuiExtension::treePop()
{
    end();
}

ImGuiExtension::LabelParts ImGuiExtension::splitLabel(const std::pmr::string& label)
{
    const auto marker = label.find("###");
    if (marker == std::pmr::string::npos) {
        return LabelParts{label, label};
    }
    return LabelParts{label.substr(0, marker), label.substr(marker + 3)};
}

uint64_t ImGuiExtension::makeId(const std::pmr::string& label) const
{
    auto hash = kFnvOffset;
    for (const auto id : idStack_) {
        hash = hashValue(hash, id);
    }
    return hashBytes(hash, label.c_str());
}

uint64_t ImGuiExtension::nextGeneratedId()
{
    return makeId(std::pmr::string("__generated_")
        + arp::core::toPmrString(std::to_string(generatedOrdinal_++)));
}

DebugNode& ImGuiExtension::currentContainer()
{
    return containerStack_.empty() ? gui_.root() : *containerStack_.back();
}

DebugNode& ImGuiExtension::addWidget(DebugNode DebugNode)
{
    auto& container = currentContainer();
    if (!sameLineRequested_) {
        return gui_.addChild(container, std::move(DebugNode));
    }

    sameLineRequested_ = false;
    if (container.children.empty()) {
        return gui_.addChild(container, std::move(DebugNode));
    }

    auto& last = container.children.back();
    if (last.type == NodeType::Panel && last.text.empty() && last.style.axis == Axis::Row) {
        const auto nextWidth = DebugNode.style.fixedSize.width;
        const auto nextHeight = DebugNode.style.fixedSize.height;
        last.style.fixedSize.width += nextWidth + last.style.gap;
        last.style.fixedSize.height = std::max(last.style.fixedSize.height, nextHeight);
        return gui_.addChild(last, std::move(DebugNode));
    }

    auto previous = std::move(container.children.back());
    container.children.pop_back();

    const auto rowWidth =
        previous.style.fixedSize.width + DebugNode.style.fixedSize.width + kItemSpacingX;
    const auto rowHeight =
        std::max(previous.style.fixedSize.height, DebugNode.style.fixedSize.height);
    auto row = panel(nextGeneratedId(),
        "",
        ControlOptions{
            {rowWidth, rowHeight},
            {},
            {},
            kItemSpacingX,
            Axis::Row,
            Align::Start,
        });
    row.children.push_back(std::move(previous));
    row.children.push_back(std::move(DebugNode));
    auto& rowRef = gui_.addChild(container, std::move(row));
    return rowRef.children.back();
}

bool ImGuiExtension::applyTextSnapshot(uint64_t id, char* buffer, size_t bufferSize)
{
    if (!buffer || bufferSize == 0) {
        return false;
    }
    const auto found = previousState_.find(id);
    if (found == previousState_.end() || found->second.text == buffer) {
        return false;
    }

    const auto copyLength = std::min(bufferSize - 1, found->second.text.size());
    std::memcpy(buffer, found->second.text.data(), copyLength);
    buffer[copyLength] = '\0';
    return true;
}

std::pmr::string ImGuiExtension::itemText(const char* const* items, uint32_t itemCount) const
{
    std::ostringstream stream;
    for (uint32_t index = 0; index < itemCount; ++index) {
        if (index > 0) {
            stream << '|';
        }
        stream << (items[index] ? items[index] : "");
    }
    return arp::core::toPmrString(stream.str());
}

} // namespace arp::gui
