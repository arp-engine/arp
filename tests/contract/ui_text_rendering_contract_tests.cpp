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
    assert(result.run().heightPx == 32.0f);
    assert(result.run().inkBounds.y == rect.y);
    assert(result.run().inkBounds.height == rect.height);
}

void explicit_vertical_clip_crops_ink_and_uv_in_both_paths()
{
    auto fonts = arp::tests::preparedUiFontFaces("font.body");
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas.resourceId = "texture/test";
    desc.baseAtlas.extent = {4, 4};
    desc.baseAtlas.rgba8.resize(64, 255);
    desc.fontFaces = &fonts;
    desc.maxWidth = 128;
    desc.maxHeight = 128;
    auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    command.text = "g";
    command.fontToken = "font.body";
    command.styleId = "text.test";
    command.rect = arp::ui::LayoutRect::fromOriginSize(10, 20, 100, 32);
    command.clipRect = command.rect;
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{command};
    auto full = runtime.resolve(commands);
    assert(full.ok() && full.commands().size() == 1);
    const auto original = full.commands()[0];
    auto clipped = original.rect;
    clipped.y += clipped.height / 4;
    clipped.height /= 2;
    commands[0].clipRect = clipped;
    for (bool fast : {false, true}) {
        auto result = fast ? runtime.resolveTextFast(commands) : runtime.resolve(commands);
        assert(result.ok() && result.commands().size() == 1);
        const auto& actual = result.commands()[0];
        assert(std::abs(actual.rect.y - clipped.y) < .001f);
        assert(std::abs(actual.rect.height - clipped.height) < .001f);
        assert(std::abs(actual.textureSourceRect.y - original.textureSourceRect.y
                   - original.textureSourceRect.height / 4)
            < .00001f);
        assert(std::abs(actual.textureSourceRect.height - original.textureSourceRect.height / 2)
            < .00001f);
    }
    commands[0].clipRect = arp::ui::LayoutRect::fromOriginSize(10, 100, 100, 10);
    auto absent = runtime.resolve(commands);
    assert(absent.ok() && absent.commands().empty());
    auto fastAbsent = runtime.resolveTextFast(commands);
    assert(fastAbsent.ok() && fastAbsent.commands().empty());
}

void physical_font_sizes_rasterize_new_cells_and_preserve_line_height()
{
    auto fonts = arp::tests::preparedUiFontFaces("font.body");
    auto resized = fonts[0];
    assert(resized.resizePixels(27));
    assert(resized.sizePx == 27 && resized.lineHeightPx == 38);
    assert(fonts[0].sizePx == 16 && fonts[0].lineHeightPx == 22);
    assert(!resized.resizePixels(0) && resized.sizePx == 27);
    resized.sizePx = 1;
    resized.lineHeightPx = UINT32_MAX;
    assert(!resized.resizePixels(UINT32_MAX) && resized.sizePx == 1);

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas.resourceId = "texture/physical-font";
    desc.baseAtlas.extent = {4, 4};
    desc.baseAtlas.rgba8.resize(64, 255);
    desc.fontFaces = &fonts;
    desc.maxWidth = 256;
    desc.maxHeight = 256;
    auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    arp::renderer::ui::TextLayoutCache layout(fonts);
    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    command.text = "g";
    command.fontToken = "font.body";
    command.styleId = "text.physical";
    command.rect = command.clipRect = arp::ui::LayoutRect::fromOriginSize(10, 20, 200, 100);
    for (auto pixels : {11u, 19u, 27u}) {
        command.fontSizePx = pixels;
        arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{command};
        arp::renderer::ui::TextLayoutKey key;
        key.text = command.text;
        key.styleId = command.styleId;
        key.fontToken = command.fontToken;
        key.locale = "en-US";
        key.sizePx = pixels;
        key.maxWidthPx = 200;
        key.scaleBucket = 1;
        const auto measured = layout.resolve(key);
        assert(measured.ok());
        const auto ink = measured.run().glyphQuads[0].localRect;
        for (bool fast : {false, true}) {
            const auto result =
                fast ? runtime.resolveTextFast(commands) : runtime.resolve(commands);
            assert(result.ok() && result.commands().size() == 1);
            const auto& glyph = result.commands()[0];
            assert(glyph.rect.x == 10 + ink.x && glyph.rect.y == 20 + ink.y);
            assert(glyph.rect.width == ink.width && glyph.rect.height == ink.height);
            assert(glyph.textureSourceRect.width * runtime.atlas().extent.width == ink.width);
        }
    }
    assert(runtime.glyphAtlas().entries().size() == 3);
}

void sdf_physical_size_reuses_the_sample_and_scales_normal_and_cached_paths()
{
    auto fonts = arp::tests::preparedUiFontFaces("font.sdf");
    fonts[0].renderMode = "sdf";
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas.resourceId = "texture/sdf-physical";
    desc.baseAtlas.extent = {4, 4};
    desc.baseAtlas.rgba8.resize(64, 255);
    desc.fontFaces = &fonts;
    desc.maxWidth = 256;
    desc.maxHeight = 256;
    auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    command.text = "g";
    command.fontToken = "font.sdf";
    command.styleId = "text.physical";
    command.fontSizePx = 16;
    command.rect = arp::ui::LayoutRect::fromOriginSize(30, 30, 150, 150);
    command.clipRect = arp::ui::LayoutRect::fromOriginSize(0, 0, 200, 200);
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{command};
    const auto small = runtime.resolve(commands);
    assert(small.ok() && small.commands().size() == 1);
    commands[0].fontSizePx = 32;
    for (bool fast : {false, true}) {
        const auto large = fast ? runtime.resolveTextFast(commands) : runtime.resolve(commands);
        assert(large.ok() && large.commands().size() == 1);
        const auto& quad = large.commands()[0];
        assert(std::abs(quad.rect.width - small.commands()[0].rect.width * 2) < .001f);
        assert(std::abs(quad.rect.height - small.commands()[0].rect.height * 2) < .001f);
        assert(quad.sdfMode);
    }
    assert(runtime.glyphAtlas().entries().size() == 1);
    assert(runtime.glyphAtlas().entries()[0].key.sizePx == 48);
}

} // namespace

int main()
{
    fallback_glyph_uses_the_resolved_font_for_atlas_pixels();
    declared_line_height_preserves_descender_ink();
    explicit_vertical_clip_crops_ink_and_uv_in_both_paths();
    physical_font_sizes_rasterize_new_cells_and_preserve_line_height();
    sdf_physical_size_reuses_the_sample_and_scales_normal_and_cached_paths();
    return 0;
}
