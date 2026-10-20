#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << path.string() << ": failed to open required boundary file\n";
        std::exit(1);
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void requireText(const std::filesystem::path& path, const std::string& text, const char* token)
{
    if (text.find(token) == std::string::npos) {
        std::cerr << path.string() << ": missing required boundary token: " << token << '\n';
        std::exit(1);
    }
}

void forbidText(const std::filesystem::path& path, const std::string& text, const char* token)
{
    if (text.find(token) != std::string::npos) {
        std::cerr << path.string() << ": forbidden boundary token: " << token << '\n';
        std::exit(1);
    }
}

void boundary_document_locks_unified_ui_runtime_responsibilities()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto docPath = root / "docs" / "ui-runtime-boundary.md";
    const auto doc = readText(docPath);

    requireText(docPath, doc, "Status: Contract Complete");
    requireText(docPath, doc, "src/ui is the only formal product UI runtime");
    requireText(docPath, doc, "src/gui is a debug/tool/editor facade only");
    requireText(docPath, doc, "src/renderer/ui is draw lowering only");
    requireText(docPath, doc, "scene owns only UI entry and bridge components");
    requireText(docPath,
        doc,
        "src/ui and src/2d do not depend on each other's runtime node implementations");
    requireText(docPath, doc, "Allowed 2D/UI relationships");
    requireText(docPath, doc, "Forbidden 2D/UI relationships");
    requireText(docPath, doc, "Pika product UI cannot include gui/imgui_extension.h");
    requireText(docPath, doc, "Pika product UI cannot depend on gui::GuiContext");
    requireText(docPath, doc, "Phase 9 source state");
    requireText(docPath, doc, "src/gui/gui.h no longer exports gui::Node");
    requireText(docPath, doc, "src/gui/gui.h no longer exports gui::DrawData");
    requireText(docPath, doc, "src/gui/gui.h no longer exports gui::GuiContext");
    requireText(docPath, doc, "gui::Builder as the facade over ui::Runtime");
}

void current_gui_runtime_duplication_is_removed_after_phase_9()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto guiHeaderPath = root / "src" / "gui" / "gui.h";
    const auto docPath = root / "docs" / "ui-runtime-boundary.md";
    const auto guiHeader = readText(guiHeaderPath);
    const auto doc = readText(docPath);

    forbidText(guiHeaderPath, guiHeader, "struct Node");
    forbidText(guiHeaderPath, guiHeader, "struct ARP_API DrawData");
    forbidText(guiHeaderPath, guiHeader, "class ARP_API GuiContext");
    forbidText(guiHeaderPath, guiHeader, "using node =");
    forbidText(guiHeaderPath, guiHeader, "using DrawData =");
    forbidText(guiHeaderPath, guiHeader, "using GuiContext =");
    requireText(guiHeaderPath, guiHeader, "using GuiContext [[deprecated(");

    requireText(docPath, doc, "Phase 9");
    requireText(docPath, doc, "DebugNode");
    requireText(docPath, doc, "DebugDrawData");
    requireText(docPath, doc, "DebugGuiContext");
    requireText(docPath, doc, "deterministic contract failure");
}

void product_ui_contract_does_not_grant_gui_runtime_exceptions()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto docPath = root / "docs" / "ui-runtime-boundary.md";
    const auto doc = readText(docPath);

    forbidText(docPath, doc, "src/gui may own product UI runtime");
    forbidText(docPath, doc, "Pika product UI may use gui::GuiContext");
    forbidText(docPath, doc, "sprite component owns UI node pointer");
    forbidText(docPath, doc, "UI node owns sprite pointer");
}

} // namespace

int main()
{
    boundary_document_locks_unified_ui_runtime_responsibilities();
    current_gui_runtime_duplication_is_removed_after_phase_9();
    product_ui_contract_does_not_grant_gui_runtime_exceptions();
    return 0;
}
