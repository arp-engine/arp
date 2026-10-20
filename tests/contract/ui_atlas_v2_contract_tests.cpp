#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void requireToken(const std::string& source, const char* token)
{
    assert(source.find(token) != std::string::npos);
}

void forbidToken(const std::string& source, const char* token)
{
    assert(source.find(token) == std::string::npos);
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(current / "assets" / "pika" / "ui" / "pika_ui_atlas.arpui")) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return std::filesystem::current_path();
}

void pika_atlas_declares_v2_base_image_and_glyph_metadata()
{
    const auto root = projectRoot();
    const auto atlas = readFile(root / "assets" / "pika" / "ui" / "pika_ui_atlas.arpui");

    requireToken(atlas, "type: \"ui.atlas\"");
    requireToken(atlas, "contractVersion: \"ui.atlas/v2\"");
    requireToken(atlas, "status: \"Contract Complete\"");
    requireToken(atlas, "atlasId: \"pika.ui.atlas\"");
    requireToken(atlas, "resourceId: \"texture/pika_ui_atlas\"");
    forbidToken(atlas, "contractVersion: \"ui.atlas/v1\"");

    requireToken(atlas, "baseImageAtlas:");
    requireToken(atlas, "sourcePath: \"assets/pika/ui/textures/pika_ui_atlas.png\"");
    requireToken(atlas, "width: 224");
    requireToken(atlas, "height: 96");
    requireToken(atlas, "colorSpace: \"srgb\"");
    requireToken(atlas, "alphaMode: \"premultiplied\"");
    requireToken(atlas, "mipIntent: \"none\"");
    requireToken(atlas, "compressionIntent: \"ui-lossless\"");
    requireToken(atlas, "samplerIntent: \"linear-clamp\"");

    requireToken(atlas, "fontFaces:");
    requireToken(atlas, "tokenId: \"font.ui.body\"");
    requireToken(atlas, "family: \"Inter\"");
    requireToken(atlas, "sizeRangePx:");
    requireToken(atlas, "min: 12");
    requireToken(atlas, "max: 32");
    requireToken(atlas, "lineHeightPx: 22");
    requireToken(atlas, "fallbackChain:");
    requireToken(atlas, "supportedScripts:");
    requireToken(atlas, "charset: \"pika.visible.ascii\"");

    requireToken(atlas, "glyphPolicy:");
    requireToken(atlas, "mode: \"hybrid\"");
    requireToken(atlas, "pagePolicy:");
    requireToken(atlas, "mode: \"singleTexture\"");
    requireToken(atlas, "preloadGlyphSets:");
    requireToken(atlas, "glyphSetId: \"pika.visible.ascii\"");
    requireToken(atlas, "coverage: \"ascii-printable\"");
    requireToken(atlas, "diagnostics:");
    requireToken(atlas, "missingFontToken: \"error\"");
    requireToken(atlas, "missingGlyphSet: \"error\"");
    requireToken(atlas, "invalidAtlasDimensions: \"error\"");
    requireToken(atlas, "invalidPagePolicy: \"error\"");
    requireToken(atlas, "unsupportedDynamicGlyphBehavior: \"error\"");
}

void pika_visible_loader_uses_v2_base_image_metadata()
{
    const auto source =
        readFile(projectRoot() / "games" / "pika" / "app" / "visible_character_runtime.cpp");

    requireToken(source, "\"baseImageAtlas\"");
    requireToken(source, "Visible UI atlas requires contractVersion ui.atlas/v2");
    requireToken(source, "Visible UI atlas requires explicit baseImageAtlas metadata.");
    requireToken(
        source, "Visible UI atlas requires explicit numeric baseImageAtlas width and height.");
    forbidToken(source, "Visible UI atlas requires explicit numeric texture width and height.");
}

} // namespace

int main()
{
    pika_atlas_declares_v2_base_image_and_glyph_metadata();
    pika_visible_loader_uses_v2_base_image_metadata();
    return 0;
}
