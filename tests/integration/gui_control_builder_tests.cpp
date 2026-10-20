#include "core/cpu_allocator.h"
#include "gui/gui.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void assertContains(const std::string& text, const char* token)
{
    assert(text.find(token) != std::string::npos);
}

void assertDoesNotContain(const std::string& text, const char* token)
{
    assert(text.find(token) == std::string::npos);
}

void test_gui_header_exports_builder_and_debug_facade_only()
{
    const auto root = std::filesystem::path(ARP_SOURCE_DIR);
    const auto guiHeader = readText(root / "src" / "gui" / "gui.h");

    assertContains(guiHeader, "class ARP_API Builder");
    assertContains(guiHeader, "struct DebugNode");
    assertContains(guiHeader, "struct ARP_API DebugDrawData");
    assertContains(guiHeader, "class ARP_API DebugGuiContext");

    assertDoesNotContain(guiHeader, "struct Node");
    assertDoesNotContain(guiHeader, "struct ARP_API DrawData");
    assertDoesNotContain(guiHeader, "class ARP_API GuiContext");
    assertDoesNotContain(guiHeader, "using node =");
    assertDoesNotContain(guiHeader, "using DrawData =");
    assertDoesNotContain(guiHeader, "using GuiContext =");
    assertContains(guiHeader, "using GuiContext [[deprecated(");
}

void test_builder_facade_creates_and_updates_ui_runtime_controls()
{
    arp::ui::CanvasDesc desc;
    desc.viewport = arp::ui::UiViewport::explicitPixels(320.0f, 180.0f);
    desc.safeArea = arp::ui::UiSafeArea::none();
    desc.scalePolicy = arp::ui::UiScalePolicy::fixed(1.0f);
    desc.maxWidgetCount = 16u;
    arp::ui::Runtime runtime(
        arp::core::pmr::String("gui-builder-runtime", &arp::core::defaultCpuMemoryResource()),
        desc);

    arp::gui::Builder builder(runtime);
    const auto button = builder.button(5001, "Apply", {96.0f, 24.0f});
    const auto slider = builder.sliderFloat(5002, "Exposure", 0.25f, 0.0f, 1.0f, {128.0f, 24.0f});
    const auto checkbox = builder.checkbox(5003, "Bloom", false, {128.0f, 24.0f});

    assert(runtime.controlCount() == 3u);
    assert(runtime.findControl(button)->type() == arp::ui::UiControlType::Button);
    assert(runtime.findControl(slider)->type() == arp::ui::UiControlType::Slider);
    assert(runtime.findControl(slider)->value().number == 0.25f);
    assert(runtime.findControl(checkbox)->type() == arp::ui::UiControlType::Checkbox);
    assert(!runtime.findControl(checkbox)->value().boolean);

    const auto updatedSlider =
        builder.sliderFloat(5002, "Exposure", 0.75f, 0.0f, 1.0f, {128.0f, 24.0f});
    const auto updatedCheckbox = builder.checkbox(5003, "Bloom", true, {128.0f, 24.0f});
    assert(updatedSlider == slider);
    assert(updatedCheckbox == checkbox);
    assert(runtime.controlCount() == 3u);
    assert(runtime.findControl(slider)->value().number == 0.75f);
    assert(runtime.findControl(checkbox)->value().boolean);
}

} // namespace

int main()
{
    test_gui_header_exports_builder_and_debug_facade_only();
    test_builder_facade_creates_and_updates_ui_runtime_controls();
    return 0;
}
