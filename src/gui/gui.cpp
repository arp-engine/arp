#include "gui/gui.h"

#include "core/cpu_allocator.h"
#include "core/geometry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <unordered_set>
#include <utility>
#include <vector>

namespace arp::gui {

namespace {

float resolvedWidth(const DebugNode& DebugNode, float parentWidth)
{
    return arp::ui::LayoutEngine::resolveWidth(DebugNode.style, parentWidth);
}

float resolvedHeight(const DebugNode& DebugNode, float parentHeight)
{
    return arp::ui::LayoutEngine::resolveHeight(DebugNode.style, parentHeight);
}

DebugNode* findRecursive(DebugNode& DebugNode, uint64_t id)
{
    if (DebugNode.id == id) {
        return &DebugNode;
    }

    for (auto& child : DebugNode.children) {
        if (auto* found = findRecursive(child, id)) {
            return found;
        }
    }

    return nullptr;
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

bool hasRenderableText(NodeType type);

std::array<const char*, 7> glyphRows(char value)
{
    switch (value) {
    case 'a':
        return {"00000", "00000", "01110", "00001", "01111", "10001", "01111"};
    case 'b':
        return {"10000", "10000", "10110", "11001", "10001", "10001", "11110"};
    case 'c':
        return {"00000", "00000", "01110", "10000", "10000", "10001", "01110"};
    case 'd':
        return {"00001", "00001", "01101", "10011", "10001", "10001", "01111"};
    case 'e':
        return {"00000", "00000", "01110", "10001", "11111", "10000", "01110"};
    case 'f':
        return {"00110", "01001", "01000", "11100", "01000", "01000", "01000"};
    case 'g':
        return {"00000", "00000", "01111", "10001", "01111", "00001", "01110"};
    case 'h':
        return {"10000", "10000", "10110", "11001", "10001", "10001", "10001"};
    case 'i':
        return {"00100", "00000", "01100", "00100", "00100", "00100", "01110"};
    case 'j':
        return {"00010", "00000", "00110", "00010", "00010", "10010", "01100"};
    case 'k':
        return {"10000", "10000", "10010", "10100", "11000", "10100", "10010"};
    case 'l':
        return {"01100", "00100", "00100", "00100", "00100", "00100", "01110"};
    case 'm':
        return {"00000", "00000", "11010", "10101", "10101", "10101", "10101"};
    case 'n':
        return {"00000", "00000", "10110", "11001", "10001", "10001", "10001"};
    case 'o':
        return {"00000", "00000", "01110", "10001", "10001", "10001", "01110"};
    case 'p':
        return {"00000", "00000", "11110", "10001", "11110", "10000", "10000"};
    case 'q':
        return {"00000", "00000", "01111", "10001", "01111", "00001", "00001"};
    case 'r':
        return {"00000", "00000", "10110", "11001", "10000", "10000", "10000"};
    case 's':
        return {"00000", "00000", "01111", "10000", "01110", "00001", "11110"};
    case 't':
        return {"01000", "01000", "11100", "01000", "01000", "01001", "00110"};
    case 'u':
        return {"00000", "00000", "10001", "10001", "10001", "10011", "01101"};
    case 'v':
        return {"00000", "00000", "10001", "10001", "10001", "01010", "00100"};
    case 'w':
        return {"00000", "00000", "10001", "10101", "10101", "10101", "01010"};
    case 'x':
        return {"00000", "00000", "10001", "01010", "00100", "01010", "10001"};
    case 'y':
        return {"00000", "00000", "10001", "10001", "01111", "00001", "01110"};
    case 'z':
        return {"00000", "00000", "11111", "00010", "00100", "01000", "11111"};
    case 'A':
        return {"01110", "10001", "10001", "11111", "10001", "10001", "10001"};
    case 'B':
        return {"11110", "10001", "10001", "11110", "10001", "10001", "11110"};
    case 'C':
        return {"01111", "10000", "10000", "10000", "10000", "10000", "01111"};
    case 'D':
        return {"11110", "10001", "10001", "10001", "10001", "10001", "11110"};
    case 'E':
        return {"11111", "10000", "10000", "11110", "10000", "10000", "11111"};
    case 'F':
        return {"11111", "10000", "10000", "11110", "10000", "10000", "10000"};
    case 'G':
        return {"01111", "10000", "10000", "10011", "10001", "10001", "01111"};
    case 'H':
        return {"10001", "10001", "10001", "11111", "10001", "10001", "10001"};
    case 'I':
        return {"11111", "00100", "00100", "00100", "00100", "00100", "11111"};
    case 'J':
        return {"00111", "00010", "00010", "00010", "00010", "10010", "01100"};
    case 'K':
        return {"10001", "10010", "10100", "11000", "10100", "10010", "10001"};
    case 'L':
        return {"10000", "10000", "10000", "10000", "10000", "10000", "11111"};
    case 'M':
        return {"10001", "11011", "10101", "10101", "10001", "10001", "10001"};
    case 'N':
        return {"10001", "11001", "10101", "10011", "10001", "10001", "10001"};
    case 'O':
        return {"01110", "10001", "10001", "10001", "10001", "10001", "01110"};
    case 'P':
        return {"11110", "10001", "10001", "11110", "10000", "10000", "10000"};
    case 'Q':
        return {"01110", "10001", "10001", "10001", "10101", "10010", "01101"};
    case 'R':
        return {"11110", "10001", "10001", "11110", "10100", "10010", "10001"};
    case 'S':
        return {"01111", "10000", "10000", "01110", "00001", "00001", "11110"};
    case 'T':
        return {"11111", "00100", "00100", "00100", "00100", "00100", "00100"};
    case 'U':
        return {"10001", "10001", "10001", "10001", "10001", "10001", "01110"};
    case 'V':
        return {"10001", "10001", "10001", "10001", "10001", "01010", "00100"};
    case 'W':
        return {"10001", "10001", "10001", "10101", "10101", "10101", "01010"};
    case 'X':
        return {"10001", "10001", "01010", "00100", "01010", "10001", "10001"};
    case 'Y':
        return {"10001", "10001", "01010", "00100", "00100", "00100", "00100"};
    case 'Z':
        return {"11111", "00001", "00010", "00100", "01000", "10000", "11111"};
    case '0':
        return {"01110", "10011", "10101", "10101", "11001", "10001", "01110"};
    case '1':
        return {"00100", "01100", "00100", "00100", "00100", "00100", "01110"};
    case '2':
        return {"01110", "10001", "00001", "00010", "00100", "01000", "11111"};
    case '3':
        return {"11110", "00001", "00001", "01110", "00001", "00001", "11110"};
    case '4':
        return {"00010", "00110", "01010", "10010", "11111", "00010", "00010"};
    case '5':
        return {"11111", "10000", "10000", "11110", "00001", "00001", "11110"};
    case '6':
        return {"01111", "10000", "10000", "11110", "10001", "10001", "01110"};
    case '7':
        return {"11111", "00001", "00010", "00100", "01000", "01000", "01000"};
    case '8':
        return {"01110", "10001", "10001", "01110", "10001", "10001", "01110"};
    case '9':
        return {"01110", "10001", "10001", "01111", "00001", "00001", "11110"};
    case '.':
        return {"00000", "00000", "00000", "00000", "00000", "01100", "01100"};
    case ',':
        return {"00000", "00000", "00000", "00000", "01100", "00100", "01000"};
    case ':':
        return {"00000", "01100", "01100", "00000", "01100", "01100", "00000"};
    case '-':
        return {"00000", "00000", "00000", "11111", "00000", "00000", "00000"};
    case '+':
        return {"00000", "00100", "00100", "11111", "00100", "00100", "00000"};
    case '/':
        return {"00001", "00010", "00010", "00100", "01000", "01000", "10000"};
    case '|':
        return {"00100", "00100", "00100", "00100", "00100", "00100", "00100"};
    case '_':
        return {"00000", "00000", "00000", "00000", "00000", "00000", "11111"};
    case '<':
        return {"00010", "00100", "01000", "10000", "01000", "00100", "00010"};
    case '>':
        return {"01000", "00100", "00010", "00001", "00010", "00100", "01000"};
    case '[':
        return {"01110", "01000", "01000", "01000", "01000", "01000", "01110"};
    case ']':
        return {"01110", "00010", "00010", "00010", "00010", "00010", "01110"};
    case '(':
        return {"00010", "00100", "01000", "01000", "01000", "00100", "00010"};
    case ')':
        return {"01000", "00100", "00010", "00010", "00010", "00100", "01000"};
    case '?':
        return {"01110", "10001", "00001", "00010", "00100", "00000", "00100"};
    case '!':
        return {"00100", "00100", "00100", "00100", "00100", "00000", "00100"};
    default:
        return {"11111", "10001", "00010", "00100", "01000", "10001", "11111"};
    }
}

bool textOriginFor(const DebugNode& DebugNode, float& outX, float& outY)
{
    constexpr float pixelSize = 2.0f;
    constexpr float glyphHeight = 7.0f * pixelSize;
    if (!hasRenderableText(DebugNode.type) || DebugNode.text.empty() || DebugNode.rect.width <= 8.0f
        || DebugNode.rect.height <= pixelSize) {
        return false;
    }

    const auto centeredOffset = std::max(0.0f, (DebugNode.rect.height - glyphHeight) * 0.5f);
    outX = DebugNode.rect.x + 4.0f;
    outY = DebugNode.rect.y + std::min(4.0f, centeredOffset);
    return true;
}

void appendTextQuads(const DebugNode& DebugNode, DebugDrawData& DebugDrawData, const Rect* clipRect)
{
    if (clipRect
        && (DebugNode.rect.y < clipRect->y
            || DebugNode.rect.y + DebugNode.rect.height > clipRect->y + clipRect->height)) {
        return;
    }

    float originX = 0.0f;
    float originY = 0.0f;
    if (!textOriginFor(DebugNode, originX, originY)) {
        return;
    }

    constexpr float pixelSize = 2.0f;
    constexpr float glyphAdvance = 12.0f;
    const Color textColor{1.0f, 1.0f, 1.0f, 1.0f};
    float cursorX = originX;
    for (const char value : DebugNode.text) {
        if (value == ' ') {
            cursorX += glyphAdvance;
            continue;
        }
        if (cursorX + 5.0f * pixelSize > DebugNode.rect.x + DebugNode.rect.width - 2.0f) {
            break;
        }

        const auto rows = glyphRows(value);
        for (size_t row = 0; row < rows.size(); ++row) {
            for (size_t column = 0; column < 5; ++column) {
                if (rows[row][column] != '1') {
                    continue;
                }

                const Rect pixelRect{
                    cursorX + static_cast<float>(column) * pixelSize,
                    originY + static_cast<float>(row) * pixelSize,
                    pixelSize,
                    pixelSize,
                };
                if (pixelRect.y + pixelRect.height
                    > DebugNode.rect.y + DebugNode.rect.height - 1.0f) {
                    continue;
                }
                if (clipRect && !intersects(pixelRect, *clipRect)) {
                    continue;
                }

                DebugDrawData.textQuads.push_back(DebugDrawTextQuad{
                    DebugNode.id,
                    pixelRect,
                    textColor,
                });
            }
        }
        cursorX += glyphAdvance;
    }
}

bool hasRenderableBackground(NodeType type)
{
    return type != NodeType::Label;
}

bool hasVerticalScrollBar(const DebugNode& DebugNode);
Rect scrollTrackRect(const DebugNode& DebugNode);
Rect scrollThumbRect(const DebugNode& DebugNode);

void appendQuad(DebugDrawData& DebugDrawData,
    uint64_t nodeId,
    NodeType type,
    Rect rect,
    VisualState visualState,
    Color color)
{
    DebugDrawData.quads.push_back(DebugDrawQuad{
        nodeId,
        type,
        rect,
        visualState,
        color,
        true,
    });
}

void appendClippedQuad(DebugDrawData& DebugDrawData,
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
    appendQuad(DebugDrawData, nodeId, type, rect, visualState, color);
}

Color defaultPanelColor()
{
    return {0.12f, 0.12f, 0.12f, 0.96f};
}

Color defaultControlColor(VisualState state)
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

void appendDefaultBackground(const DebugNode& DebugNode,
    DebugDrawData& DebugDrawData,
    VisualState state,
    const Rect* clipRect)
{
    if (DebugNode.rect.width <= 0.0f || DebugNode.rect.height <= 0.0f
        || !hasRenderableBackground(DebugNode.type)) {
        return;
    }

    if (DebugNode.type == NodeType::Panel || DebugNode.type == NodeType::ScrollView) {
        appendClippedQuad(DebugDrawData,
            DebugNode.id,
            DebugNode.type,
            DebugNode.rect,
            state,
            defaultPanelColor(),
            clipRect);
        if (hasVerticalScrollBar(DebugNode)) {
            appendClippedQuad(DebugDrawData,
                DebugNode.id,
                DebugNode.type,
                scrollTrackRect(DebugNode),
                state,
                {0.03f, 0.04f, 0.05f, 0.55f},
                clipRect);
            appendClippedQuad(DebugDrawData,
                DebugNode.id,
                DebugNode.type,
                scrollThumbRect(DebugNode),
                state,
                {0.42f, 0.42f, 0.42f, 0.62f},
                clipRect);
        }
        return;
    }

    appendClippedQuad(DebugDrawData,
        DebugNode.id,
        DebugNode.type,
        DebugNode.rect,
        state,
        defaultControlColor(state),
        clipRect);
}

void appendDrawData(const DebugNode& DebugNode,
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
    appendDefaultBackground(DebugNode, DebugDrawData, state, clipRect);
    appendTextQuads(DebugNode, DebugDrawData, clipRect);

    Rect childClip;
    const Rect* nextClip = clipRect;
    if (DebugNode.type == NodeType::ScrollView) {
        childClip = clipRect ? intersectRect(DebugNode.rect, *clipRect) : DebugNode.rect;
        nextClip = &childClip;
    }

    for (const auto& child : DebugNode.children) {
        appendDrawData(child, DebugDrawData, hoveredId, activeId, focusedId, nextClip);
    }
}

bool hasRenderableText(NodeType type)
{
    return type == NodeType::Label || type == NodeType::Button || type == NodeType::Slider
        || type == NodeType::Checkbox || type == NodeType::TextInput
        || type == NodeType::NumericInput || type == NodeType::Dropdown || type == NodeType::Tabs
        || type == NodeType::TreeView || type == NodeType::ColorPicker;
}

void appendFontAtlasPlan(const DebugNode& DebugNode,
    FontAtlasPlan& plan,
    std::pmr::unordered_set<std::pmr::string>& seen)
{
    if (hasRenderableText(DebugNode.type) && !DebugNode.text.empty()
        && seen.insert(DebugNode.text).second) {
        plan.textRuns.push_back(DebugNode.text);
    }

    for (const auto& child : DebugNode.children) {
        appendFontAtlasPlan(child, plan, seen);
    }
}

bool containsPoint(const Rect& rect, float x, float y)
{
    return arp::core::rectContainsPoint(toCoreRect(rect), x, y);
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

bool isInteractive(NodeType type)
{
    return type == NodeType::Button || type == NodeType::Slider || type == NodeType::Checkbox
        || type == NodeType::TextInput || type == NodeType::NumericInput
        || type == NodeType::Dropdown || type == NodeType::Tabs || type == NodeType::TreeView
        || type == NodeType::ColorPicker;
}

DebugNode* hitTestInteractive(DebugNode& DebugNode, float x, float y)
{
    if (!containsPoint(DebugNode.rect, x, y)) {
        return nullptr;
    }

    for (auto it = DebugNode.children.rbegin(); it != DebugNode.children.rend(); ++it) {
        if (auto* hit = hitTestInteractive(*it, x, y)) {
            return hit;
        }
    }

    if (!DebugNode.disabled && isInteractive(DebugNode.type)) {
        return &DebugNode;
    }

    return nullptr;
}

DebugNode* hitTestScrollView(DebugNode& DebugNode, float x, float y)
{
    if (!containsPoint(DebugNode.rect, x, y)) {
        return nullptr;
    }

    for (auto it = DebugNode.children.rbegin(); it != DebugNode.children.rend(); ++it) {
        if (auto* hit = hitTestScrollView(*it, x, y)) {
            return hit;
        }
    }

    if (DebugNode.type == NodeType::ScrollView) {
        return &DebugNode;
    }

    return nullptr;
}

DebugNode* hitTestScrollThumb(DebugNode& DebugNode, float x, float y)
{
    if (!containsPoint(DebugNode.rect, x, y)) {
        return nullptr;
    }

    for (auto it = DebugNode.children.rbegin(); it != DebugNode.children.rend(); ++it) {
        if (auto* hit = hitTestScrollThumb(*it, x, y)) {
            return hit;
        }
    }

    if (hasVerticalScrollBar(DebugNode) && containsPoint(scrollThumbRect(DebugNode), x, y)) {
        return &DebugNode;
    }

    return nullptr;
}

void collectInteractiveNodes(DebugNode& node, std::pmr::vector<DebugNode*>& nodes)
{
    if (!node.disabled && isInteractive(node.type)) {
        nodes.push_back(&node);
    }

    for (auto& child : node.children) {
        collectInteractiveNodes(child, nodes);
    }
}

float clamp(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

bool updateSliderValue(DebugNode& DebugNode, float pointerX, PointerEventResult& result)
{
    if (DebugNode.type != NodeType::Slider || DebugNode.rect.width <= 0.0f) {
        return false;
    }

    const auto denominator = DebugNode.maxValue - DebugNode.minValue;
    if (denominator <= 0.0f) {
        return false;
    }

    const auto normalized = clamp((pointerX - DebugNode.rect.x) / DebugNode.rect.width, 0.0f, 1.0f);
    const auto nextValue = DebugNode.minValue + normalized * denominator;
    result.targetId = DebugNode.id;
    result.value = nextValue;

    if (nextValue != DebugNode.value) {
        DebugNode.value = nextValue;
        result.valueChanged = true;
    }

    return true;
}

bool updateColorPickerValue(DebugNode& DebugNode, float pointerX, PointerEventResult& result)
{
    if (DebugNode.type != NodeType::ColorPicker || DebugNode.rect.width <= 0.0f) {
        return false;
    }

    const auto nextValue = clamp((pointerX - DebugNode.rect.x) / DebugNode.rect.width, 0.0f, 1.0f);
    result.targetId = DebugNode.id;
    result.value = nextValue;
    if (nextValue != DebugNode.color.r) {
        DebugNode.color.r = nextValue;
        result.valueChanged = true;
    }
    return true;
}

bool updateScrollThumbValue(DebugNode& DebugNode,
    float pointerY,
    float dragStartY,
    float dragStartScrollY,
    PointerEventResult& result)
{
    if (!hasVerticalScrollBar(DebugNode)) {
        return false;
    }

    const auto track = scrollTrackRect(DebugNode);
    const auto thumb = scrollThumbRect(DebugNode);
    const auto travel = std::max(0.0f, track.height - thumb.height);
    if (travel <= 0.0f || DebugNode.maxScrollY <= 0.0f) {
        return false;
    }

    const auto deltaY = pointerY - dragStartY;
    const auto nextScrollY = clamp(
        dragStartScrollY + (deltaY / travel) * DebugNode.maxScrollY, 0.0f, DebugNode.maxScrollY);
    result.targetId = DebugNode.id;
    result.value = nextScrollY;
    if (nextScrollY != DebugNode.scrollY) {
        DebugNode.scrollY = nextScrollY;
        result.valueChanged = true;
    }
    return true;
}

void updateNumericValue(DebugNode& DebugNode, PointerEventResult& result)
{
    if (DebugNode.type != NodeType::NumericInput || DebugNode.text.empty()) {
        return;
    }

    char* parseEnd = nullptr;
    const auto parsed = std::strtof(DebugNode.text.c_str(), &parseEnd);
    if (parseEnd == DebugNode.text.c_str()) {
        return;
    }

    const auto clamped = clamp(parsed, DebugNode.minValue, DebugNode.maxValue);
    result.value = clamped;
    if (clamped != DebugNode.value) {
        DebugNode.value = clamped;
        result.valueChanged = true;
    }
}

size_t tabItemCount(const DebugNode& DebugNode)
{
    if (!DebugNode.children.empty()) {
        return DebugNode.children.size();
    }
    if (DebugNode.text.empty()) {
        return 1u;
    }

    size_t count = 1u;
    for (const auto character : DebugNode.text) {
        if (character == '|') {
            ++count;
        }
    }
    return count;
}

void activateNode(DebugNode& DebugNode, PointerEventResult& result)
{
    result.targetId = DebugNode.id;
    if (DebugNode.type == NodeType::Button || DebugNode.type == NodeType::Checkbox
        || DebugNode.type == NodeType::Dropdown || DebugNode.type == NodeType::Tabs
        || DebugNode.type == NodeType::TreeView || DebugNode.type == NodeType::ColorPicker) {
        result.clicked = true;
    }
    if (DebugNode.type == NodeType::Checkbox) {
        DebugNode.checked = !DebugNode.checked;
        result.toggled = true;
        result.checked = DebugNode.checked;
    }
    if (DebugNode.type == NodeType::Dropdown || DebugNode.type == NodeType::TreeView) {
        DebugNode.expanded = !DebugNode.expanded;
        result.toggled = true;
        result.expanded = DebugNode.expanded;
    }
    if (DebugNode.type == NodeType::Tabs) {
        const auto tabCount = std::max<size_t>(1, tabItemCount(DebugNode));
        DebugNode.selectedIndex = static_cast<uint32_t>((DebugNode.selectedIndex + 1) % tabCount);
        result.valueChanged = true;
        result.selectedIndex = DebugNode.selectedIndex;
    }
}

float alignedOrigin(Align align, float contentOrigin, float contentSize, float childSize)
{
    return arp::ui::LayoutEngine::alignedOrigin(align, contentOrigin, contentSize, childSize);
}

float contentLengthFor(const DebugNode& DebugNode, float contentWidth, float contentHeight)
{
    float length = 0.0f;
    for (size_t index = 0; index < DebugNode.children.size(); ++index) {
        const auto& child = DebugNode.children[index];
        length += DebugNode.style.axis == Axis::Column ? resolvedHeight(child, contentHeight)
                                                       : resolvedWidth(child, contentWidth);
        if (index + 1 < DebugNode.children.size()) {
            length += DebugNode.style.gap;
        }
    }
    return length;
}

void layoutChildren(DebugNode& DebugNode)
{
    const auto contentX = DebugNode.rect.x + DebugNode.style.padding.left;
    const auto contentY = DebugNode.rect.y + DebugNode.style.padding.top;
    const auto contentWidth = std::max(
        0.0f, DebugNode.rect.width - DebugNode.style.padding.left - DebugNode.style.padding.right);
    const auto contentHeight = std::max(
        0.0f, DebugNode.rect.height - DebugNode.style.padding.top - DebugNode.style.padding.bottom);

    if (DebugNode.type == NodeType::ScrollView) {
        DebugNode.maxScrollY = std::max(
            0.0f, contentLengthFor(DebugNode, contentWidth, contentHeight) - contentHeight);
        DebugNode.scrollY = clamp(DebugNode.scrollY, 0.0f, DebugNode.maxScrollY);
    }

    const auto scrollOffsetY = DebugNode.type == NodeType::ScrollView ? DebugNode.scrollY : 0.0f;
    float cursor = DebugNode.style.axis == Axis::Column ? contentY - scrollOffsetY : contentX;

    for (auto& child : DebugNode.children) {
        auto childWidth = resolvedWidth(child, contentWidth);
        auto childHeight = resolvedHeight(child, contentHeight);

        if (DebugNode.style.axis == Axis::Column) {
            if (DebugNode.style.align == Align::Stretch) {
                childWidth = contentWidth;
            }
            child.rect.x = alignedOrigin(DebugNode.style.align, contentX, contentWidth, childWidth);
            child.rect.y = cursor;
            child.rect.width = childWidth;
            child.rect.height = childHeight;
            cursor += childHeight + DebugNode.style.gap;
        } else {
            child.rect.x = cursor;
            if (DebugNode.style.align == Align::Stretch) {
                childHeight = contentHeight;
            }
            child.rect.y =
                alignedOrigin(DebugNode.style.align, contentY, contentHeight, childHeight);
            child.rect.width = childWidth;
            child.rect.height = childHeight;
            cursor += childWidth + DebugNode.style.gap;
        }

        layoutChildren(child);
    }
}

PointerEventResult toPointerEventResult(const arp::ui::UiInputDispatchResult& routed)
{
    PointerEventResult result;
    result.targetId = routed.hoveredWidget().valid() ? routed.hoveredWidget().value()
                                                     : routed.focusedWidget().value();
    result.clicked = routed.clicked();
    result.valueChanged = routed.consumption().anyConsumed();
    return result;
}

} // namespace

Builder::Builder(arp::ui::Runtime& runtime) : runtime_(runtime)
{}

arp::ui::WidgetId Builder::button(uint64_t id, std::pmr::string text, Size size)
{
    return createOrUpdate(id,
        std::move(text),
        arp::ui::UiControlType::Button,
        arp::ui::UiControlValue::fromBool(false),
        size);
}

arp::ui::WidgetId Builder::sliderFloat(
    uint64_t id, std::pmr::string text, float value, float, float, Size size)
{
    return createOrUpdate(id,
        std::move(text),
        arp::ui::UiControlType::Slider,
        arp::ui::UiControlValue::fromNumber(value),
        size);
}

arp::ui::WidgetId Builder::checkbox(uint64_t id, std::pmr::string text, bool value, Size size)
{
    return createOrUpdate(id,
        std::move(text),
        arp::ui::UiControlType::Checkbox,
        arp::ui::UiControlValue::fromBool(value),
        size);
}

arp::ui::WidgetId Builder::createOrUpdate(uint64_t id,
    std::pmr::string text,
    arp::ui::UiControlType type,
    arp::ui::UiControlValue value,
    Size size)
{
    const auto found = widgets_.find(id);
    if (found != widgets_.end()) {
        const auto updated = runtime_.setValue(found->second, std::move(value));
        return updated.ok() ? found->second : arp::ui::WidgetId::invalid();
    }

    arp::ui::ControlDesc desc;
    desc.type = type;
    desc.widget.name = arp::core::pmr::String(text.c_str(), &arp::core::defaultCpuMemoryResource());
    desc.widget.text = arp::core::pmr::String(text.c_str(), &arp::core::defaultCpuMemoryResource());
    desc.widget.localRect = arp::ui::LayoutRect::fromOriginSize(
        0.0f, static_cast<float>(widgets_.size()) * (size.height + 4.0f), size.width, size.height);
    desc.widget.controlType = type;
    desc.widget.acceptsFocus = true;
    desc.initialValue = std::move(value);
    const auto created = runtime_.createControl(std::move(desc), runtime_.rootId());
    if (!created.ok()) {
        return arp::ui::WidgetId::invalid();
    }
    widgets_.emplace(id, created.widgetId());
    return created.widgetId();
}

DebugNode& DebugGuiContext::root()
{
    return root_;
}

const DebugNode& DebugGuiContext::root() const
{
    return root_;
}

DebugNode& DebugGuiContext::addChild(DebugNode& parent, DebugNode child)
{
    parent.children.push_back(std::move(child));
    return parent.children.back();
}

void DebugGuiContext::attachRuntime(arp::ui::Runtime& runtime)
{
    forwardedRuntime_ = &runtime;
}

void DebugGuiContext::layout(Size viewport)
{
    root_.rect = Rect{0.0f, 0.0f, viewport.width, viewport.height};
    layoutChildren(root_);
}

DebugNode* DebugGuiContext::find(uint64_t id)
{
    return findRecursive(root_, id);
}

PointerEventResult DebugGuiContext::dispatchPointerEvent(PointerEvent event)
{
    if (forwardedRuntime_ != nullptr) {
        arp::ui::UiInputEvent routedEvent;
        switch (event.type) {
        case PointerEventType::Move:
            routedEvent = arp::ui::UiInputEvent::pointerMove(event.x, event.y);
            break;
        case PointerEventType::Down:
            routedEvent = arp::ui::UiInputEvent::pointerDown(event.x, event.y);
            break;
        case PointerEventType::Up:
            routedEvent = arp::ui::UiInputEvent::pointerUp(event.x, event.y);
            break;
        }
        routedEvent.button = static_cast<arp::input::InputMouseButton>(event.button);
        const auto routed =
            forwardedInputRouter_.dispatch(*forwardedRuntime_, std::move(routedEvent));
        lastEventResult_ = routed.ok() ? toPointerEventResult(routed) : PointerEventResult{};
        return lastEventResult_;
    }

    auto* target = hitTestInteractive(root_, event.x, event.y);
    hoveredId_ = target ? target->id : 0;

    PointerEventResult result;

    switch (event.type) {
    case PointerEventType::Move:
        if (scrollDragId_ != 0) {
            if (auto* scroll = find(scrollDragId_)) {
                if (updateScrollThumbValue(
                        *scroll, event.y, scrollDragStartY_, scrollDragStartScrollY_, result)) {
                    layoutChildren(root_);
                    break;
                }
                result.targetId = scrollDragId_;
                break;
            }
            scrollDragId_ = 0;
        }
        if (activeId_ != 0) {
            if (auto* active = find(activeId_)) {
                if (updateSliderValue(*active, event.x, result)) {
                    break;
                }
                if (updateColorPickerValue(*active, event.x, result)) {
                    break;
                }
            }
        }
        result.targetId = hoveredId_;
        break;
    case PointerEventType::Down:
        if (auto* scroll = hitTestScrollThumb(root_, event.x, event.y)) {
            scrollDragId_ = scroll->id;
            scrollDragStartY_ = event.y;
            scrollDragStartScrollY_ = scroll->scrollY;
            activeId_ = scroll->id;
            focusedId_ = scroll->id;
            result.targetId = scroll->id;
            break;
        }
        activeId_ = hoveredId_;
        focusedId_ = hoveredId_;
        result.targetId = hoveredId_;
        if (auto* active = find(activeId_)) {
            if (!updateSliderValue(*active, event.x, result)) {
                updateColorPickerValue(*active, event.x, result);
            }
        }
        break;
    case PointerEventType::Up:
        if (scrollDragId_ != 0) {
            result.targetId = scrollDragId_;
            scrollDragId_ = 0;
            activeId_ = 0;
            break;
        }
        if (activeId_ != 0 && activeId_ == hoveredId_) {
            if (auto* active = find(activeId_)) {
                activateNode(*active, result);
            }
        }
        activeId_ = 0;
        break;
    }

    lastEventResult_ = result;
    return result;
}

PointerEventResult DebugGuiContext::dispatchKeyEvent(KeyEvent event)
{
    if (forwardedRuntime_ != nullptr) {
        arp::ui::UiInputEvent routedEvent;
        if (event.key == Key::Tab) {
            routedEvent = event.shift ? arp::ui::UiInputEvent::keyboardPrevious()
                                      : arp::ui::UiInputEvent::keyboardNext();
        } else if (event.key == Key::Enter || event.key == Key::Space) {
            routedEvent = arp::ui::UiInputEvent::keyboardActivate();
        } else {
            routedEvent = arp::ui::UiInputEvent::backspace();
        }
        const auto routed =
            forwardedInputRouter_.dispatch(*forwardedRuntime_, std::move(routedEvent));
        lastEventResult_ = routed.ok() ? toPointerEventResult(routed) : PointerEventResult{};
        return lastEventResult_;
    }

    PointerEventResult result;

    if (event.key == Key::Tab) {
        std::pmr::vector<DebugNode*> interactiveNodes;
        collectInteractiveNodes(root_, interactiveNodes);
        if (interactiveNodes.empty()) {
            lastEventResult_ = result;
            return result;
        }

        auto focused = std::find_if(
            interactiveNodes.begin(), interactiveNodes.end(), [&](const DebugNode* DebugNode) {
                return DebugNode->id == focusedId_;
            });
        size_t index = 0;
        if (focused == interactiveNodes.end()) {
            index = event.shift ? interactiveNodes.size() - 1 : 0;
        } else {
            const auto focusedIndex = static_cast<size_t>(focused - interactiveNodes.begin());
            if (event.shift) {
                index = focusedIndex == 0 ? interactiveNodes.size() - 1 : focusedIndex - 1;
            } else {
                index = (focusedIndex + 1) % interactiveNodes.size();
            }
        }

        focusedId_ = interactiveNodes[index]->id;
        result.targetId = focusedId_;
        lastEventResult_ = result;
        return result;
    }

    if (event.key == Key::Enter || event.key == Key::Space) {
        if (auto* focused = find(focusedId_)) {
            activateNode(*focused, result);
        }
    }

    if (event.key == Key::Backspace) {
        if (auto* focused = find(focusedId_)) {
            if (focused->type == NodeType::TextInput || focused->type == NodeType::NumericInput) {
                result.targetId = focused->id;
                if (!focused->text.empty()) {
                    focused->text.pop_back();
                    result.textChanged = true;
                    result.text = focused->text;
                    updateNumericValue(*focused, result);
                }
            }
        }
    }

    lastEventResult_ = result;
    return result;
}

PointerEventResult DebugGuiContext::dispatchTextInput(TextInputEvent event)
{
    if (forwardedRuntime_ != nullptr) {
        const auto routed = forwardedInputRouter_.dispatch(*forwardedRuntime_,
            arp::ui::UiInputEvent::textInput(arp::core::pmr::String(
                event.text.c_str(), &arp::core::defaultCpuMemoryResource())));
        lastEventResult_ = routed.ok() ? toPointerEventResult(routed) : PointerEventResult{};
        if (routed.ok()) {
            lastEventResult_.textChanged = true;
            lastEventResult_.text = event.text;
        }
        return lastEventResult_;
    }

    PointerEventResult result;
    auto* focused = find(focusedId_);
    if (!focused || event.text.empty()) {
        lastEventResult_ = result;
        return result;
    }
    if (focused->type != NodeType::TextInput && focused->type != NodeType::NumericInput) {
        lastEventResult_ = result;
        return result;
    }

    focused->text += event.text;
    result.targetId = focused->id;
    result.textChanged = true;
    result.text = focused->text;
    updateNumericValue(*focused, result);
    lastEventResult_ = result;
    return result;
}

PointerEventResult DebugGuiContext::dispatchScrollEvent(ScrollEvent event)
{
    if (forwardedRuntime_ != nullptr) {
        const auto routed = forwardedInputRouter_.dispatch(
            *forwardedRuntime_, arp::ui::UiInputEvent::scrollWheel(event.x, event.y, event.deltaY));
        lastEventResult_ = routed.ok() ? toPointerEventResult(routed) : PointerEventResult{};
        return lastEventResult_;
    }

    PointerEventResult result;
    auto* target = hitTestScrollView(root_, event.x, event.y);
    if (!target) {
        lastEventResult_ = result;
        return result;
    }

    const auto nextScrollY = clamp(target->scrollY + event.deltaY, 0.0f, target->maxScrollY);
    result.targetId = target->id;
    result.value = nextScrollY;
    if (nextScrollY != target->scrollY) {
        target->scrollY = nextScrollY;
        result.valueChanged = true;
        layoutChildren(root_);
    }
    lastEventResult_ = result;
    return result;
}

PointerEventResult DebugGuiContext::consumeLastEventResult()
{
    const auto result = lastEventResult_;
    lastEventResult_ = {};
    return result;
}

DebugDrawData DebugGuiContext::buildDrawData() const
{
    DebugDrawData DebugDrawData;
    appendDrawData(root_, DebugDrawData, hoveredId_, activeId_, focusedId_);
    return DebugDrawData;
}

const DebugDrawQuad* DebugDrawData::quadFor(uint64_t nodeId) const
{
    for (const auto& quad : quads) {
        if (quad.nodeId == nodeId) {
            return &quad;
        }
    }
    return nullptr;
}

FontAtlasPlan DebugGuiContext::buildFontAtlasPlan() const
{
    FontAtlasPlan plan;
    plan.defaultFont = arp::ui::loadDefaultUiFontFace();
    std::pmr::unordered_set<std::pmr::string> seen;
    appendFontAtlasPlan(root_, plan, seen);
    return plan;
}

uint64_t DebugGuiContext::hoveredId() const
{
    return hoveredId_;
}

uint64_t DebugGuiContext::activeId() const
{
    return activeId_;
}

uint64_t DebugGuiContext::focusedId() const
{
    return focusedId_;
}

} // namespace arp::gui
