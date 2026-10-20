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
        std::cerr << path.string() << ": missing required unified atlas token: " << token << '\n';
        std::exit(1);
    }
}

void forbidText(const std::filesystem::path& path, const std::string& text, const char* token)
{
    if (text.find(token) != std::string::npos) {
        std::cerr << path.string() << ": forbidden unified atlas token: " << token << '\n';
        std::exit(1);
    }
}

void unified_atlas_document_defines_single_product_render_upstream()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto docPath = root / "docs" / "ui-unified-atlas-runtime.md";
    const auto doc = readText(docPath);

    requireText(docPath, doc, "Status: Contract Complete");
    requireText(docPath, doc, "one logical UI atlas resource path for image slices and glyphs");
    requireText(docPath, doc, "Pika RuntimeScene is the only product render source of truth");
    requireText(docPath, doc, "GameObject + ui::DocumentComponent");
    requireText(docPath,
        doc,
        "RuntimeScene -> ui::DocumentComponent -> ui::Runtime -> ui::RenderExtractor");
    requireText(docPath, doc, "ui.atlas/v2");
    requireText(docPath, doc, "ui.theme/v1");
    requireText(docPath, doc, "fontToken is the font source of truth");
    requireText(docPath, doc, "logical atlas resource id");
}

void unified_atlas_document_defines_page_policy_and_lowering_boundary()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto docPath = root / "docs" / "ui-unified-atlas-runtime.md";
    const auto doc = readText(docPath);

    requireText(docPath, doc, "Logical Atlas");
    requireText(docPath, doc, "Physical Atlas");
    requireText(docPath, doc, "singleTexture");
    requireText(docPath, doc, "textureArray");
    requireText(docPath, doc, "multiPage");
    requireText(docPath, doc, "Vulkan");
    requireText(docPath, doc, "Metal");
    requireText(docPath, doc, "WebGPU");
    requireText(docPath, doc, "Null");
    requireText(docPath, doc, "deterministic diagnostic");
    requireText(docPath, doc, "capability");
    requireText(docPath, doc, "lowering");
}

void unified_atlas_document_forbids_split_text_and_backend_fallbacks()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto docPath = root / "docs" / "ui-unified-atlas-runtime.md";
    const auto doc = readText(docPath);

    requireText(docPath, doc, "Forbidden Behavior");
    requireText(docPath, doc, "product UI text bypasses atlas");
    requireText(docPath, doc, "product UI uses debug GUI text fallback");
    requireText(docPath, doc, "renderer parses YAML");
    requireText(docPath, doc, "renderer reads font files per frame");
    requireText(docPath, doc, "backend silently creates missing glyphs");
    requireText(docPath, doc, "No per-frame font file IO");
    requireText(docPath, doc, "No per-frame atlas rebuild");
    requireText(docPath, doc, "No native backend handles exposed");

    forbidText(docPath, doc, "product UI may bypass atlas");
    forbidText(docPath, doc, "renderer may parse YAML");
    forbidText(docPath, doc, "backend may silently create missing glyphs");
}

void unified_atlas_document_defines_module_ownership_and_status_labels()
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto docPath = root / "docs" / "ui-unified-atlas-runtime.md";
    const auto doc = readText(docPath);

    requireText(docPath, doc, "src/ui");
    requireText(docPath, doc, "src/renderer/ui");
    requireText(docPath, doc, "src/renderer/gui");
    requireText(docPath, doc, "src/gui");
    requireText(docPath, doc, "games/pika");
    requireText(docPath, doc, "assets/pika/ui");
    requireText(docPath, doc, "Contract Complete");
    requireText(docPath, doc, "Prototype Complete");
    requireText(docPath, doc, "Backend Validated");
    requireText(docPath, doc, "Platform Validated");
    requireText(docPath, doc, "Production Ready");
    requireText(docPath, doc, "Contract Complete is not Backend Validated");
    requireText(docPath, doc, "Platform Validated is not Production Ready");
}

} // namespace

int main()
{
    unified_atlas_document_defines_single_product_render_upstream();
    unified_atlas_document_defines_page_policy_and_lowering_boundary();
    unified_atlas_document_forbids_split_text_and_backend_fallbacks();
    unified_atlas_document_defines_module_ownership_and_status_labels();
    return 0;
}
