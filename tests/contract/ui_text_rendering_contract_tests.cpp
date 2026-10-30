#include "renderer/ui/ui_atlas_runtime.h"
#include "renderer/ui/ui_text_layout.h"
#include "tests/fixtures/ui_prepared_fonts.h"

#include <cassert>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

void fallback_glyph_uses_the_resolved_font_for_atlas_pixels()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fonts{&arp::core::defaultCpuMemoryResource()};
    arp::ui::UiFontFaceDesc primary;
    primary.tokenId = text("font.primary");
    primary.family = text("Unavailable");
    primary.sourcePath = "assets/ui/fonts/unavailable.ttf";
    primary.sizePx = 18u;
    primary.lineHeightPx = 24u;
    primary.fallbackChain.push_back(text("font.fallback"));
    arp::tests::setCorruptUiFontSource(primary);
    fonts.push_back(std::move(primary));

    arp::ui::UiFontFaceDesc fallback;
    fallback.tokenId = text("font.fallback");
    fallback.family = text("Arial");
    fallback.sourcePath = "C:/Windows/Fonts/arial.ttf";
    fallback.sizePx = 18u;
    fallback.lineHeightPx = 24u;
    fallback.source = arp::tests::preparedUiFontFace("font.fallback").source;
    fallback.sourcePath = ARP_FONT_IO_FIXTURE_PATH;
    fonts.push_back(std::move(fallback));

    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    command.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 64.0f, 24.0f);
    command.clipRect = command.rect;
    command.text = text("A");
    command.styleId = text("text.body");
    command.fontToken = text("font.primary");

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(command));

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 128u;
    desc.maxHeight = 64u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(resolved.commands().size() == 1u);
    assert(resolved.commands().front().fontToken == text("font.fallback"));
}

void declared_line_height_preserves_descender_ink()
{
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fonts{&arp::core::defaultCpuMemoryResource()};
    arp::ui::UiFontFaceDesc face;
    face.tokenId = text("font.body");
    face.family = text("Segoe UI");
    face.sourcePath = "C:/Windows/Fonts/segoeui.ttf";
    face.sizePx = 22u;
    face.lineHeightPx = 32u;
    arp::core::pmr::String diagnostic;
    face.source = arp::ui::acquireUiFontSource(face, diagnostic);
    assert(face.source);
    fonts.push_back(std::move(face));

    arp::ui::UiFontFaceCache faceCache;
    arp::renderer::ui::TextLayoutCache layout(fonts);
    layout.setFontFaceCache(&faceCache);
    arp::renderer::ui::TextLayoutKey key;
    key.text = text("g");
    key.styleId = text("text.body");
    key.fontToken = text("font.body");
    key.locale = text("en-US");
    key.sizePx = 22u;
    key.maxWidthPx = 64u;
    key.scaleBucket = 1u;

    const auto result = layout.resolve(key);
    assert(result.ok());
    assert(result.run().glyphQuads.size() == 1u);
    const auto& rect = result.run().glyphQuads.front().localRect;
    assert(rect.y + rect.height > static_cast<float>(key.sizePx));
    assert(rect.y + rect.height <= 32.0f);
}

} // namespace

int main()
{
    fallback_glyph_uses_the_resolved_font_for_atlas_pixels();
    declared_line_height_preserves_descender_ink();
    return 0;
}
