#include "core/cpu_allocator.h"
#include "gui/imgui_extension.h"

#include <cassert>
#include <cmath>

namespace {

const arp::gui::DebugNode* findByTypeAndText(
    const arp::gui::DebugNode& node, arp::gui::NodeType type, const char* text)
{
    if (node.type == type && node.text == text) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* found = findByTypeAndText(child, type, text)) {
            return found;
        }
    }
    return nullptr;
}

bool isNeutralGray(const arp::gui::Color& color)
{
    return std::abs(color.r - color.g) <= 0.02f && std::abs(color.r - color.b) <= 0.02f;
}

void test_extension_builds_window_and_common_widgets()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool bloom = false;
    float exposure = 1.25f;
    char name[32] = "Material";
    float ior = 1.45f;
    arp::gui::Color tint{0.25f, 0.50f, 0.75f, 1.0f};
    const char* qualityItems[] = {"Low", "Medium", "High"};
    uint32_t quality = 1;
    uint32_t tab = 0;

    ui.newFrame();
    assert(ui.begin("Settings"));
    ui.text("Renderer");
    assert(!ui.button("Apply"));
    assert(!ui.checkbox("Bloom", bloom));
    assert(!ui.sliderFloat("Exposure", exposure, 0.0f, 2.0f));
    assert(!ui.inputText("Name", name, sizeof(name)));
    assert(!ui.inputFloat("IOR", ior, 1.0f, 2.5f));
    assert(!ui.colorEdit4("Tint", tint));
    assert(!ui.combo("Quality", quality, qualityItems, 3));
    assert(!ui.tabs("Tabs", tab, qualityItems, 3));
    assert(ui.treeNode("Extensions"));
    ui.treePop();
    ui.end();

    gui.layout({640.0f, 360.0f});

    assert(gui.root().children.size() == 1);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Panel, "Settings") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "Renderer") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "Apply") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Checkbox, "Bloom") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Slider, "Exposure") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::TextInput, "Material") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::NumericInput, "1.45") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::ColorPicker, "Tint") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Dropdown, "Quality") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Tabs, "Low|Medium|High") != nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::TreeView, "Extensions") != nullptr);
}

void test_extension_writes_back_checkbox_and_slider_state()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool bloom = false;
    float exposure = 0.25f;

    ui.newFrame();
    assert(ui.begin("Settings"));
    assert(!ui.checkbox("Bloom", bloom));
    assert(!ui.sliderFloat("Exposure", exposure, 0.0f, 1.0f));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* checkbox = findByTypeAndText(gui.root(), arp::gui::NodeType::Checkbox, "Bloom");
    const auto* slider = findByTypeAndText(gui.root(), arp::gui::NodeType::Slider, "Exposure");
    assert(checkbox != nullptr);
    assert(slider != nullptr);

    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        checkbox->rect.x + 2.0f,
        checkbox->rect.y + 2.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Up,
        checkbox->rect.x + 2.0f,
        checkbox->rect.y + 2.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        slider->rect.x + slider->rect.width * 0.75f,
        slider->rect.y + 2.0f,
        0,
    });

    ui.newFrame();
    assert(ui.begin("Settings"));
    assert(ui.checkbox("Bloom", bloom));
    assert(bloom);
    assert(ui.sliderFloat("Exposure", exposure, 0.0f, 1.0f));
    assert(exposure > 0.74f && exposure < 0.76f);
    ui.end();
}

void test_same_line_groups_adjacent_widgets_in_a_row()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.begin("Settings"));
    ui.button("A");
    ui.sameLine();
    ui.button("B");
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* first = findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "A");
    const auto* second = findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "B");
    assert(first != nullptr);
    assert(second != nullptr);
    assert(second->rect.y == first->rect.y);
    assert(second->rect.x > first->rect.x);
}

void test_button_returns_true_after_retained_click()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.begin("Settings"));
    assert(!ui.button("Reload"));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* button = findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "Reload");
    assert(button != nullptr);
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        button->rect.x + 2.0f,
        button->rect.y + 2.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Up,
        button->rect.x + 2.0f,
        button->rect.y + 2.0f,
        0,
    });

    ui.newFrame();
    assert(ui.begin("Settings"));
    assert(ui.button("Reload"));
    ui.end();

    ui.newFrame();
    assert(ui.begin("Settings"));
    assert(!ui.button("Reload"));
    ui.end();
}

void test_text_and_numeric_inputs_write_back_retained_text_state()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    char name[32] = "Mat";
    float ior = 1.45f;

    ui.newFrame();
    assert(ui.begin("Inputs"));
    assert(!ui.inputText("Name", name, sizeof(name)));
    assert(!ui.inputFloat("IOR", ior, 1.0f, 2.5f));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* textInput = findByTypeAndText(gui.root(), arp::gui::NodeType::TextInput, "Mat");
    const auto* numericInput =
        findByTypeAndText(gui.root(), arp::gui::NodeType::NumericInput, "1.45");
    assert(textInput != nullptr);
    assert(numericInput != nullptr);

    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        textInput->rect.x + 4.0f,
        textInput->rect.y + 4.0f,
        0,
    });
    gui.dispatchTextInput(arp::gui::TextInputEvent{"X"});

    ui.newFrame();
    assert(ui.begin("Inputs"));
    assert(ui.inputText("Name", name, sizeof(name)));
    assert(std::pmr::string(name) == "MatX");
    assert(!ui.inputFloat("IOR", ior, 1.0f, 2.5f));
    ui.end();
    gui.layout({640.0f, 360.0f});

    numericInput = findByTypeAndText(gui.root(), arp::gui::NodeType::NumericInput, "1.45");
    assert(numericInput != nullptr);
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        numericInput->rect.x + 4.0f,
        numericInput->rect.y + 4.0f,
        0,
    });
    gui.dispatchTextInput(arp::gui::TextInputEvent{"2"});

    ui.newFrame();
    assert(ui.begin("Inputs"));
    assert(!ui.inputText("Name", name, sizeof(name)));
    assert(ui.inputFloat("IOR", ior, 1.0f, 2.5f));
    assert(ior > 1.451f && ior < 1.453f);
    ui.end();
}

void test_color_edit_writes_back_retained_color_state()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    arp::gui::Color tint{0.20f, 0.30f, 0.40f, 1.0f};

    ui.newFrame();
    assert(ui.begin("Color"));
    assert(!ui.colorEdit4("Tint", tint));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* picker = findByTypeAndText(gui.root(), arp::gui::NodeType::ColorPicker, "Tint");
    assert(picker != nullptr);
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        picker->rect.x + picker->rect.width * 0.80f,
        picker->rect.y + 4.0f,
        0,
    });

    ui.newFrame();
    assert(ui.begin("Color"));
    assert(ui.colorEdit4("Tint", tint));
    assert(tint.r > 0.79f && tint.r < 0.81f);
    assert(tint.g == 0.30f);
    assert(tint.b == 0.40f);
    ui.end();
}

void test_dropdown_and_combo_report_retained_toggle_state()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    uint32_t quality = 1;
    const char* items[] = {"Low", "Medium", "High"};

    ui.newFrame();
    assert(ui.begin("Dropdowns"));
    assert(!ui.dropdown(4200, "Alpha Mode OPAQUE", false, 0u, {180.0f, 20.0f}));
    assert(!ui.dropdown(4201, "Disabled", false, 0u, {180.0f, 20.0f}, true));
    assert(!ui.combo("Quality", quality, items, 3u));
    ui.end();
    gui.layout({640.0f, 360.0f});

    auto* dropdown = gui.find(4200);
    auto* disabled = gui.find(4201);
    assert(dropdown != nullptr);
    assert(disabled != nullptr);

    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        dropdown->rect.x + 4.0f,
        dropdown->rect.y + 4.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Up,
        dropdown->rect.x + 4.0f,
        dropdown->rect.y + 4.0f,
        0,
    });

    ui.newFrame();
    assert(ui.begin("Dropdowns"));
    assert(ui.dropdown(4200, "Alpha Mode OPAQUE", false, 0u, {180.0f, 20.0f}));
    assert(!ui.dropdown(4201, "Disabled", false, 0u, {180.0f, 20.0f}, true));
    assert(!ui.combo("Quality", quality, items, 3u));
    ui.end();
    gui.layout({640.0f, 360.0f});

    disabled = gui.find(4201);
    auto* combo = const_cast<arp::gui::DebugNode*>(
        findByTypeAndText(gui.root(), arp::gui::NodeType::Dropdown, "Quality"));
    assert(disabled != nullptr);
    assert(combo != nullptr);

    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        disabled->rect.x + 4.0f,
        disabled->rect.y + 4.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Up,
        disabled->rect.x + 4.0f,
        disabled->rect.y + 4.0f,
        0,
    });
    combo->selectedIndex = 2u;

    ui.newFrame();
    assert(ui.begin("Dropdowns"));
    assert(!ui.dropdown(4200, "Alpha Mode OPAQUE", false, 0u, {180.0f, 20.0f}));
    assert(!ui.dropdown(4201, "Disabled", false, 0u, {180.0f, 20.0f}, true));
    assert(ui.combo("Quality", quality, items, 3u));
    assert(quality == 2u);
    ui.end();
}

void test_tabs_keyboard_activation_cycles_selected_index()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    uint32_t tab = 0;
    const char* tabs[] = {"Material", "Textures", "Info"};

    ui.newFrame();
    assert(ui.begin("Tabs"));
    assert(!ui.tabs("Views", tab, tabs, 3u));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* tabsNode =
        findByTypeAndText(gui.root(), arp::gui::NodeType::Tabs, "Material|Textures|Info");
    assert(tabsNode != nullptr);
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        tabsNode->rect.x + 4.0f,
        tabsNode->rect.y + 4.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Up,
        tabsNode->rect.x + 4.0f,
        tabsNode->rect.y + 4.0f,
        0,
    });

    ui.newFrame();
    assert(ui.begin("Tabs"));
    assert(ui.tabs("Views", tab, tabs, 3u));
    assert(tab == 1u);
    ui.end();
}

void test_tree_node_persists_collapsed_state_and_scopes_generated_ids()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.begin("Root"));
    assert(ui.treeNode("Extensions"));
    ui.text("Inside");
    ui.treePop();
    ui.end();
    gui.layout({640.0f, 360.0f});

    auto* tree = const_cast<arp::gui::DebugNode*>(
        findByTypeAndText(gui.root(), arp::gui::NodeType::TreeView, "Extensions"));
    const auto* inside = findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "Inside");
    assert(tree != nullptr);
    assert(inside != nullptr);
    tree->expanded = false;

    ui.newFrame();
    assert(ui.begin("Root"));
    assert(!ui.treeNode("Extensions"));
    ui.text("Outside");
    ui.end();
    gui.layout({640.0f, 360.0f});

    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "Inside") == nullptr);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "Outside") != nullptr);
}

void test_spacing_and_separator_take_layout_space()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.begin("Spacing"));
    ui.text("A");
    ui.spacing();
    ui.separator();
    ui.text("B");
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* first = findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "A");
    const auto* second = findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "B");
    assert(first != nullptr);
    assert(second != nullptr);
    assert(second->rect.y - first->rect.y > 28.0f);
}

void test_hash_id_suffix_keeps_identity_while_visible_label_changes()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.begin("Stable IDs"));
    assert(!ui.button("Apply###primary-action"));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* firstButton = findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "Apply");
    assert(firstButton != nullptr);
    const auto stableId = firstButton->id;
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Down,
        firstButton->rect.x + 4.0f,
        firstButton->rect.y + 4.0f,
        0,
    });
    gui.dispatchPointerEvent(arp::gui::PointerEvent{
        arp::gui::PointerEventType::Up,
        firstButton->rect.x + 4.0f,
        firstButton->rect.y + 4.0f,
        0,
    });

    ui.newFrame();
    assert(ui.begin("Stable IDs"));
    assert(ui.button("Reload###primary-action"));
    ui.end();
    gui.layout({640.0f, 360.0f});

    const auto* renamedButton = findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "Reload");
    assert(renamedButton != nullptr);
    assert(renamedButton->id == stableId);
    assert(findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "Apply") == nullptr);
}

void test_nested_child_stack_keeps_following_widgets_as_siblings()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.beginPanel(4300, "Root", {260.0f, 180.0f}));
    assert(ui.beginChild("Child###child", {180.0f, 48.0f}));
    ui.text("Inside");
    ui.endChild();
    ui.text("After");
    ui.endPanel();
    gui.layout({640.0f, 360.0f});

    auto* root = gui.find(4300);
    const auto* child = findByTypeAndText(gui.root(), arp::gui::NodeType::Panel, "Child");
    const auto* inside = findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "Inside");
    const auto* after = findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "After");
    assert(root != nullptr);
    assert(child != nullptr);
    assert(inside != nullptr);
    assert(after != nullptr);
    assert(root->children.size() == 2u);
    assert(root->children[0].id == child->id);
    assert(root->children[1].id == after->id);
    assert(inside->rect.y >= child->rect.y);
    assert(inside->rect.y + inside->rect.height <= child->rect.y + child->rect.height);
    assert(after->rect.y > child->rect.y + child->rect.height);
}

void test_explicit_id_container_helpers_build_retained_nodes()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool checked = true;
    float roughness = 0.65f;
    arp::gui::Color tint{0.2f, 0.3f, 0.4f, 1.0f};

    ui.newFrame();
    assert(ui.beginPanel(4000, "Inspector", {300.0f, 220.0f}));
    ui.text(4001, "Material", {180.0f, 20.0f});
    ui.button(4002, "Reload", {80.0f, 24.0f});
    ui.checkbox(4003, "Double Sided", checked, {180.0f, 24.0f});
    ui.sliderFloat(4004, "Roughness", roughness, 0.0f, 1.0f, {180.0f, 24.0f});
    ui.colorEdit4(4005, "Tint", tint, {180.0f, 24.0f});
    ui.dropdown(4006, "Alpha Mode MASK", true, 1u, {180.0f, 24.0f});
    ui.dropdown(4011, "Disabled File", false, 0u, {180.0f, 24.0f}, true);
    assert(ui.beginScrollView(4007, "Textures", {220.0f, 60.0f}));
    ui.text(4008, "Base Tex", {180.0f, 20.0f});
    ui.endScrollView();
    assert(ui.treeNode(4009, "", true, 2u, {220.0f, 48.0f}));
    ui.text(4010, "Material A", {180.0f, 20.0f});
    ui.treePop();
    ui.endPanel();

    gui.layout({640.0f, 360.0f});

    assert(gui.find(4000)->type == arp::gui::NodeType::Panel);
    assert(gui.find(4001)->type == arp::gui::NodeType::Label);
    assert(gui.find(4002)->type == arp::gui::NodeType::Button);
    assert(gui.find(4003)->type == arp::gui::NodeType::Checkbox);
    assert(gui.find(4004)->type == arp::gui::NodeType::Slider);
    assert(gui.find(4005)->type == arp::gui::NodeType::ColorPicker);
    assert(gui.find(4006)->type == arp::gui::NodeType::Dropdown);
    assert(gui.find(4011)->disabled);
    assert(gui.find(4007)->type == arp::gui::NodeType::ScrollView);
    assert(gui.find(4009)->type == arp::gui::NodeType::TreeView);
    assert(gui.find(4004)->value == 0.65f);
    assert(gui.find(4006)->expanded);
    assert(gui.find(4009)->selectedIndex == 2u);
}

void test_extension_draw_data_emits_layered_dear_imgui_decorations()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool checked = true;
    float value = 0.50f;

    ui.newFrame();
    assert(ui.beginPanel(4100, "Inspector", {220.0f, 80.0f}));
    ui.checkbox(4101, "Enabled", checked, {120.0f, 20.0f});
    ui.sliderFloat(4102, "Value", value, 0.0f, 1.0f, {160.0f, 20.0f});
    ui.endPanel();
    gui.layout({320.0f, 180.0f});

    const auto DrawData = ui.buildDrawData();
    uint32_t checkboxQuads = 0;
    uint32_t sliderQuads = 0;
    bool checkboxHasCheckMark = false;
    bool sliderHasGrab = false;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId == 4101) {
            ++checkboxQuads;
            checkboxHasCheckMark = checkboxHasCheckMark || quad.rect.width <= 10.0f;
        }
        if (quad.nodeId == 4102) {
            ++sliderQuads;
            sliderHasGrab =
                sliderHasGrab || (quad.rect.width <= 12.0f && quad.rect.height >= 16.0f);
        }
    }
    assert(checkboxQuads >= 3);
    assert(sliderQuads >= 3);
    assert(checkboxHasCheckMark);
    assert(sliderHasGrab);
}

void test_extension_window_title_and_border_use_dark_imgui_colors()
{
    constexpr float expectedTitleBarHeight = 28.0f;
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.beginPanel(4106, "Window", {180.0f, 80.0f}));
    ui.button(4107, "Apply", {120.0f, 20.0f});
    ui.endPanel();
    gui.layout({320.0f, 180.0f});

    const auto* panel = gui.find(4106);
    const auto* button = gui.find(4107);
    assert(panel != nullptr);
    assert(button != nullptr);
    assert(button->rect.y >= panel->rect.y + expectedTitleBarHeight + 8.0f);
    const auto DrawData = ui.buildDrawData();

    bool hasDeepGrayWindowBg = false;
    bool hasBlackTitle = false;
    bool hasTallTitle = false;
    bool hasLowLumaBorder = false;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId != 4106 || !quad.customColor) {
            continue;
        }
        const auto luma = (quad.color.r + quad.color.g + quad.color.b) / 3.0f;
        if (quad.rect.x == panel->rect.x && quad.rect.y == panel->rect.y
            && quad.rect.width == panel->rect.width && quad.rect.height == panel->rect.height) {
            hasDeepGrayWindowBg = quad.color.r >= 0.10f && quad.color.r <= 0.16f
                && quad.color.r == quad.color.g && quad.color.r == quad.color.b;
        }
        if (quad.rect.x == panel->rect.x && quad.rect.y == panel->rect.y
            && quad.rect.width == panel->rect.width && quad.rect.height == expectedTitleBarHeight) {
            hasTallTitle = true;
            hasBlackTitle = quad.color.r == 0.0f && quad.color.g == 0.0f && quad.color.b == 0.0f
                && quad.color.a == 1.0f;
        }
        if ((quad.rect.height == 1.0f || quad.rect.width == 1.0f) && luma < 0.35f
            && std::abs(quad.color.r - quad.color.b) < 0.08f) {
            hasLowLumaBorder = true;
        }
    }

    assert(hasDeepGrayWindowBg);
    assert(hasTallTitle);
    assert(hasBlackTitle);
    assert(hasLowLumaBorder);
}

void test_extension_border_uses_black_and_text_uses_white()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.beginPanel(4108, "Window", {180.0f, 80.0f}));
    ui.button(4109, "Apply", {120.0f, 20.0f});
    ui.endPanel();
    gui.layout({320.0f, 180.0f});

    const auto* panel = gui.find(4108);
    assert(panel != nullptr);
    const auto DrawData = ui.buildDrawData();

    bool hasBlackBorder = false;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId != 4108 || !quad.customColor) {
            continue;
        }
        if (quad.rect.height == 1.0f || quad.rect.width == 1.0f) {
            hasBlackBorder = hasBlackBorder
                || (quad.color.r == 0.0f && quad.color.g == 0.0f && quad.color.b == 0.0f
                    && quad.color.a >= 0.95f);
        }
    }
    assert(hasBlackBorder);

    for (const auto& textQuad : DrawData.textQuads) {
        assert(textQuad.color.r == 1.0f);
        assert(textQuad.color.g == 1.0f);
        assert(textQuad.color.b == 1.0f);
        assert(textQuad.color.a > 0.0f);
        assert(textQuad.color.a <= 1.0f);
    }
}

void test_extension_checkbox_draw_data_uses_square_frame_not_full_row_fill()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool checked = true;
    ui.newFrame();
    ui.checkbox(4103, "Visible", checked, {140.0f, 20.0f});
    gui.layout({320.0f, 120.0f});

    const auto* checkbox = gui.find(4103);
    assert(checkbox != nullptr);
    const auto DrawData = ui.buildDrawData();

    bool hasFullRowFill = false;
    bool hasSquareFrame = false;
    bool textStartsAfterSquare = true;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId != 4103) {
            continue;
        }
        hasFullRowFill = hasFullRowFill
            || (quad.rect.x == checkbox->rect.x && quad.rect.y == checkbox->rect.y
                && quad.rect.width == checkbox->rect.width
                && quad.rect.height == checkbox->rect.height);
        hasSquareFrame = hasSquareFrame || (quad.rect.width <= 14.0f && quad.rect.height <= 14.0f);
    }
    for (const auto& quad : DrawData.textQuads) {
        if (quad.nodeId == 4103 && quad.rect.x < checkbox->rect.x + 24.0f) {
            textStartsAfterSquare = false;
        }
    }

    assert(!hasFullRowFill);
    assert(hasSquareFrame);
    assert(textStartsAfterSquare);
}

void test_extension_color_edit_draw_data_emits_left_swatch()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    arp::gui::Color tint{0.80f, 0.20f, 0.10f, 1.0f};
    ui.newFrame();
    ui.colorEdit4(4104, "Tint", tint, {180.0f, 20.0f});
    gui.layout({320.0f, 120.0f});

    const auto* colorEdit = gui.find(4104);
    assert(colorEdit != nullptr);
    const auto DrawData = ui.buildDrawData();

    bool hasLeftSwatch = false;
    bool textStartsAfterSwatch = true;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId != 4104 || !quad.customColor) {
            continue;
        }
        const auto colorMatches =
            quad.color.r == 0.80f && quad.color.g == 0.20f && quad.color.b == 0.10f;
        hasLeftSwatch = hasLeftSwatch
            || (colorMatches && quad.rect.x <= colorEdit->rect.x + 4.0f && quad.rect.width <= 18.0f
                && quad.rect.height <= 18.0f);
    }
    for (const auto& quad : DrawData.textQuads) {
        if (quad.nodeId == 4104 && quad.rect.x < colorEdit->rect.x + 28.0f) {
            textStartsAfterSwatch = false;
        }
    }

    assert(hasLeftSwatch);
    assert(textStartsAfterSwatch);
}

void test_extension_draw_data_includes_scrollbar_for_overflowing_scroll_view()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.beginScrollView(4105, "Properties", {160.0f, 60.0f}, 0.0f, {}));
    for (uint64_t index = 0; index < 6; ++index) {
        ui.text(4106 + index, "Row", {120.0f, 20.0f});
    }
    ui.endScrollView();
    gui.layout({320.0f, 180.0f});

    const auto* scroll = gui.find(4105);
    assert(scroll != nullptr);
    assert(scroll->maxScrollY > 0.0f);
    const auto DrawData = ui.buildDrawData();

    uint32_t scrollbarQuads = 0;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId == 4105 && quad.rect.x >= scroll->rect.x + scroll->rect.width - 12.0f
            && quad.rect.width <= 8.0f) {
            ++scrollbarQuads;
        }
    }
    assert(scrollbarQuads >= 2u);
}

void test_scroll_view_preserves_scroll_offset_across_new_frame_rebuild()
{
    constexpr float expectedTitleBarHeight = 28.0f;
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.beginScrollView(4110, "Properties", {160.0f, 60.0f}, 0.0f, {}));
    for (uint64_t index = 0; index < 6; ++index) {
        ui.text(4111 + index, "Row", {120.0f, 20.0f});
    }
    ui.endScrollView();
    gui.layout({320.0f, 180.0f});

    auto* scroll = gui.find(4110);
    assert(scroll != nullptr);
    assert(scroll->maxScrollY > 0.0f);
    const auto scrolled = gui.dispatchScrollEvent(arp::gui::ScrollEvent{
        scroll->rect.x + 4.0f,
        scroll->rect.y + 4.0f,
        32.0f,
    });
    assert(scrolled.valueChanged);
    assert(scroll->scrollY == 32.0f);

    ui.newFrame();
    assert(ui.beginScrollView(4110, "Properties", {160.0f, 60.0f}, 0.0f, {}));
    for (uint64_t index = 0; index < 6; ++index) {
        ui.text(4111 + index, "Row", {120.0f, 20.0f});
    }
    ui.endScrollView();
    gui.layout({320.0f, 180.0f});

    scroll = gui.find(4110);
    assert(scroll != nullptr);
    assert(scroll->scrollY == 32.0f);
    assert(gui.find(4111)->rect.y == expectedTitleBarHeight - 32.0f);
}

void test_extension_draw_data_clips_scroll_view_children()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    ui.newFrame();
    assert(ui.beginScrollView(4115, "Clip", {160.0f, 40.0f}, 0.0f, {}));
    ui.button(4116, "Visible", {140.0f, 20.0f});
    ui.button(4117, "Hidden", {140.0f, 20.0f});
    ui.button(4118, "FarHidden", {140.0f, 20.0f});
    ui.endScrollView();
    gui.layout({320.0f, 180.0f});

    auto DrawData = ui.buildDrawData();
    bool foundFar = false;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId == 4118) {
            foundFar = true;
        }
    }
    assert(!foundFar);

    gui.dispatchScrollEvent(arp::gui::ScrollEvent{
        8.0f,
        8.0f,
        48.0f,
    });
    DrawData = ui.buildDrawData();
    bool foundVisible = false;
    foundFar = false;
    for (const auto& quad : DrawData.quads) {
        if (quad.nodeId == 4116) {
            foundVisible = true;
        }
        if (quad.nodeId == 4118) {
            foundFar = true;
        }
    }
    assert(!foundVisible);
    assert(foundFar);
}

void test_extension_control_backgrounds_use_neutral_gray_palette()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool checked = true;
    float value = 0.5f;

    ui.newFrame();
    ui.button(4120, "Button", {120.0f, 20.0f});
    ui.checkbox(4121, "Check", checked, {120.0f, 20.0f});
    ui.sliderFloat(4122, "Value", value, 0.0f, 1.0f, {120.0f, 20.0f});
    ui.dropdown(4123, "Quality", false, 0u, {120.0f, 20.0f});
    gui.layout({320.0f, 180.0f});

    const auto DrawData = ui.buildDrawData();
    const auto* button = gui.find(4120);
    const auto* slider = gui.find(4122);
    const auto* dropdown = gui.find(4123);
    assert(button != nullptr);
    assert(slider != nullptr);
    assert(dropdown != nullptr);
    for (const auto& quad : DrawData.quads) {
        if (!quad.customColor) {
            continue;
        }
        if (quad.nodeId == 4120 || quad.nodeId == 4121 || quad.nodeId == 4122
            || quad.nodeId == 4123) {
            assert(isNeutralGray(quad.color));
        }
        const auto isMainControlFill =
            (quad.nodeId == 4120 && quad.rect.x == button->rect.x && quad.rect.y == button->rect.y
                && quad.rect.width == button->rect.width && quad.rect.height == button->rect.height)
            || (quad.nodeId == 4122 && quad.rect.x == slider->rect.x
                && quad.rect.y == slider->rect.y && quad.rect.width == slider->rect.width
                && quad.rect.height == slider->rect.height)
            || (quad.nodeId == 4123 && quad.rect.x == dropdown->rect.x
                && quad.rect.y == dropdown->rect.y && quad.rect.width == dropdown->rect.width
                && quad.rect.height == dropdown->rect.height);
        if (isMainControlFill) {
            assert(quad.color.a >= 0.80f);
        }
    }
}

void test_default_metrics_match_dear_imgui_style()
{
    arp::gui::DebugGuiContext gui;
    arp::gui::ImGuiExtension ui(gui);

    bool enabled = true;
    float value = 0.5f;
    arp::gui::Color color{0.1f, 0.2f, 0.3f, 1.0f};

    ui.newFrame();
    assert(ui.begin("Style Metrics"));
    ui.text("Label");
    ui.button("Button");
    ui.checkbox("Check", enabled);
    ui.sliderFloat("Float", value, 0.0f, 1.0f);
    ui.colorEdit4("Color", color);
    ui.end();
    gui.layout({480.0f, 240.0f});

    const auto* panel = findByTypeAndText(gui.root(), arp::gui::NodeType::Panel, "Style Metrics");
    const auto* label = findByTypeAndText(gui.root(), arp::gui::NodeType::Label, "Label");
    const auto* button = findByTypeAndText(gui.root(), arp::gui::NodeType::Button, "Button");
    const auto* checkbox = findByTypeAndText(gui.root(), arp::gui::NodeType::Checkbox, "Check");
    const auto* slider = findByTypeAndText(gui.root(), arp::gui::NodeType::Slider, "Float");
    const auto* picker = findByTypeAndText(gui.root(), arp::gui::NodeType::ColorPicker, "Color");
    assert(panel != nullptr);
    assert(label != nullptr);
    assert(button != nullptr);
    assert(checkbox != nullptr);
    assert(slider != nullptr);
    assert(picker != nullptr);
    assert(panel->style.padding.left == 8.0f);
    assert(panel->style.padding.top == 36.0f);
    assert(panel->style.gap == 4.0f);
    assert(label->rect.height == 20.0f);
    assert(button->rect.height == 20.0f);
    assert(checkbox->rect.height == 20.0f);
    assert(slider->rect.height == 20.0f);
    assert(picker->rect.height == 20.0f);
    assert(button->rect.y - label->rect.y == 24.0f);
}

} // namespace

int main()
{
    test_extension_builds_window_and_common_widgets();
    test_extension_writes_back_checkbox_and_slider_state();
    test_same_line_groups_adjacent_widgets_in_a_row();
    test_button_returns_true_after_retained_click();
    test_text_and_numeric_inputs_write_back_retained_text_state();
    test_color_edit_writes_back_retained_color_state();
    test_dropdown_and_combo_report_retained_toggle_state();
    test_tabs_keyboard_activation_cycles_selected_index();
    test_tree_node_persists_collapsed_state_and_scopes_generated_ids();
    test_spacing_and_separator_take_layout_space();
    test_hash_id_suffix_keeps_identity_while_visible_label_changes();
    test_nested_child_stack_keeps_following_widgets_as_siblings();
    test_explicit_id_container_helpers_build_retained_nodes();
    test_extension_draw_data_emits_layered_dear_imgui_decorations();
    test_extension_window_title_and_border_use_dark_imgui_colors();
    test_extension_border_uses_black_and_text_uses_white();
    test_extension_checkbox_draw_data_uses_square_frame_not_full_row_fill();
    test_extension_color_edit_draw_data_emits_left_swatch();
    test_extension_draw_data_includes_scrollbar_for_overflowing_scroll_view();
    test_scroll_view_preserves_scroll_offset_across_new_frame_rebuild();
    test_extension_draw_data_clips_scroll_view_children();
    test_extension_control_backgrounds_use_neutral_gray_palette();
    test_default_metrics_match_dear_imgui_style();
    return 0;
}
