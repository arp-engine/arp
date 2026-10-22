#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "gui/gui.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace arp::gui {

class ARP_API ImGuiExtension {
public:
    explicit ImGuiExtension(DebugGuiContext& gui);

    void newFrame();
    DebugDrawData buildDrawData() const;

    bool begin(const std::pmr::string& label, Size size = {320.0f, 0.0f});
    void end();
    bool beginPanel(uint64_t id,
        const std::pmr::string& label,
        Size size,
        Axis axis = Axis::Column,
        float gap = 4.0f,
        Spacing padding = {8.0f, 8.0f, 8.0f, 8.0f});
    void endPanel();
    bool beginChild(const std::pmr::string& label, Size size = {0.0f, 0.0f});
    void endChild();
    bool beginScrollView(uint64_t id,
        const std::pmr::string& label,
        Size size,
        float gap = 4.0f,
        Spacing padding = {8.0f, 8.0f, 8.0f, 8.0f});
    void endScrollView();
    void sameLine();
    void separator();
    void spacing();

    void text(const std::pmr::string& value);
    void text(uint64_t id, const std::pmr::string& value, Size size = {240.0f, 20.0f});
    bool button(const std::pmr::string& label);
    bool button(uint64_t id, const std::pmr::string& label, Size size = {96.0f, 24.0f});
    bool checkbox(const std::pmr::string& label, bool& value);
    bool checkbox(
        uint64_t id, const std::pmr::string& label, bool& value, Size size = {160.0f, 24.0f});
    bool sliderFloat(const std::pmr::string& label, float& value, float minValue, float maxValue);
    bool sliderFloat(uint64_t id,
        const std::pmr::string& label,
        float& value,
        float minValue,
        float maxValue,
        Size size = {180.0f, 24.0f});
    bool inputText(const std::pmr::string& label, char* buffer, size_t bufferSize);
    bool inputFloat(const std::pmr::string& label, float& value, float minValue, float maxValue);
    bool colorEdit4(const std::pmr::string& label, Color& value);
    bool colorEdit4(
        uint64_t id, const std::pmr::string& label, Color& value, Size size = {160.0f, 24.0f});
    bool dropdown(uint64_t id,
        const std::pmr::string& label,
        bool expanded,
        uint32_t selectedIndex,
        Size size = {160.0f, 24.0f},
        bool disabled = false);
    bool combo(const std::pmr::string& label,
        uint32_t& selectedIndex,
        const char* const* items,
        uint32_t itemCount);
    bool tabs(const std::pmr::string& label,
        uint32_t& selectedIndex,
        const char* const* items,
        uint32_t itemCount);
    bool treeNode(const std::pmr::string& label);
    bool treeNode(uint64_t id,
        const std::pmr::string& label,
        bool expanded,
        uint32_t selectedIndex,
        Size size = {180.0f, 24.0f});
    void treePop();

private:
    struct LabelParts {
        std::pmr::string visible;
        std::pmr::string stable;
    };

public:
    struct WidgetState {
        bool checked = false;
        float value = 0.0f;
        std::pmr::string text;
        bool expanded = false;
        uint32_t selectedIndex = 0;
        Color color;
        float scrollY = 0.0f;
    };

private:
    DebugGuiContext& gui_;
    std::pmr::unordered_map<uint64_t, WidgetState> previousState_;
    PointerEventResult previousEvent_;
    std::pmr::vector<DebugNode*> containerStack_;
    std::pmr::vector<uint64_t> idStack_;
    uint64_t generatedOrdinal_ = 0;
    bool sameLineRequested_ = false;

    static LabelParts splitLabel(const std::pmr::string& label);
    uint64_t makeId(const std::pmr::string& label) const;
    uint64_t nextGeneratedId();
    DebugNode& currentContainer();
    DebugNode& addWidget(DebugNode DebugNode);
    bool applyTextSnapshot(uint64_t id, char* buffer, size_t bufferSize);
    std::pmr::string itemText(const char* const* items, uint32_t itemCount) const;
};

} // namespace arp::gui
