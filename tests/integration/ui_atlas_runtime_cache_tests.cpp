#include "renderer/ui/ui_atlas_runtime.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::filesystem::path projectRoot()
{
    auto current = std::filesystem::current_path();
    while (!current.empty()) {
        if (std::filesystem::exists(
                current / "games" / "pika" / "app" / "visible_character_runtime.cpp")) {
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

void unified_ui_atlas_runtime_lowers_text_to_glyph_quads()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::renderer::ui::UiDrawCommand textCommand;
    textCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    textCommand.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 96.0f, 20.0f);
    textCommand.clipRect = textCommand.rect;
    textCommand.text = text("LEVEL");
    textCommand.styleId = text("text.hud");
    textCommand.fontToken = text("font.ui.body");

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(textCommand));

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    assert(runtime.atlas().valid());
    assert(runtime.logicalAtlasResourceId() == text("texture/pika_ui_atlas"));
    assert(runtime.atlas().extent.height > 128u);
    assert(runtime.telemetry().glyphMisses == 3u);
    assert(runtime.telemetry().glyphHits == 2u);
    assert(runtime.telemetry().pageCount == 1u);
    const auto hasGlyphPixels =
        std::any_of(runtime.atlas().rgba8.begin() + static_cast<std::ptrdiff_t>(4u * 4u * 4u),
            runtime.atlas().rgba8.end(),
            [](const std::uint8_t value) {
                return value != 0u;
            });
    assert(hasGlyphPixels);

    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(resolved.commands().size() == 5u);
    for (const auto& command : resolved.commands()) {
        assert(command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad);
        assert(command.textureResourceId == text("texture/pika_ui_atlas"));
        assert(command.hasTextureSourceRect);
        assert(command.hasAtlasPageIndex);
        assert(command.atlasPageIndex == 0u);
    }
    assert(!runtime.exposesNativeBackendHandles());
}

void glyph_cells_do_not_clip_wide_uppercase_letters()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::renderer::ui::UiDrawCommand textCommand;
    textCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    textCommand.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 260.0f, 20.0f);
    textCommand.clipRect = textCommand.rect;
    textCommand.text = text("TRACKER SPARK");
    textCommand.styleId = text("text.hud");
    textCommand.fontToken = text("font.ui.body");

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(textCommand));

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.maxWidth = 512u;
    desc.maxHeight = 256u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(resolved.commands().size() == std::string("TRACKERSPARK").size());
    float previousRight = 0.0f;
    for (const auto& command : resolved.commands()) {
        assert(command.rect.width < 20.0f);
        assert(command.rect.height <= 20.0f);
        assert(command.textureSourceRect.width > 0.0f);
        if (previousRight > 0.0f) {
            assert(command.rect.x - previousRight < 12.0f);
        }
        previousRight = command.rect.x + command.rect.width;
    }
}

void glyph_quads_are_clipped_to_command_clip_rect()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::renderer::ui::UiDrawCommand textCommand;
    textCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    textCommand.rect = arp::ui::LayoutRect::fromOriginSize(10.0f, 0.0f, 64.0f, 20.0f);
    textCommand.clipRect = textCommand.rect;
    textCommand.text = text("QUEST TRACKER PANEL");
    textCommand.styleId = text("text.hud");
    textCommand.fontToken = text("font.ui.body");

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(textCommand));

    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.maxWidth = 512u;
    desc.maxHeight = 256u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(!resolved.commands().empty());
    assert(resolved.commands().size() < std::string("QUEST TRACKER PANEL").size());
    for (const auto& command : resolved.commands()) {
        assert(command.rect.x >= 10.0f);
        assert(command.rect.x + command.rect.width <= 74.0f);
        assert(command.textureSourceRect.valid());
    }
}

arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> pikaFontFaces()
{
    arp::core::pmr::Vector<arp::ui::UiFontFaceDesc> fonts{&arp::core::defaultCpuMemoryResource()};
    arp::ui::UiFontFaceDesc body;
    body.tokenId = text("font.ui.body");
    body.family = text("Arial");
    body.sourcePath = std::filesystem::path("C:/Windows/Fonts/arial.ttf");
    body.sizePx = 16u;
    body.weight = 500u;
    body.lineHeightPx = 22u;
    fonts.push_back(std::move(body));
    arp::ui::UiFontFaceDesc numeric;
    numeric.tokenId = text("font.ui.numeric");
    numeric.family = text("Consolas");
    numeric.sourcePath = std::filesystem::path("C:/Windows/Fonts/consola.ttf");
    numeric.sizePx = 15u;
    numeric.weight = 600u;
    numeric.lineHeightPx = 20u;
    fonts.push_back(std::move(numeric));
    arp::ui::UiFontFaceDesc hudTitle;
    hudTitle.tokenId = text("font.ui.hud.title");
    hudTitle.family = text("Georgia");
    hudTitle.sourcePath = std::filesystem::path("C:/Windows/Fonts/georgia.ttf");
    hudTitle.sizePx = 16u;
    hudTitle.weight = 700u;
    hudTitle.lineHeightPx = 22u;
    fonts.push_back(std::move(hudTitle));
    return fonts;
}

arp::renderer::ui::UiDrawCommand textCommand(
    const char* value, const char* styleId, const char* fontToken, float width, float height)
{
    arp::renderer::ui::UiDrawCommand command;
    command.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    command.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, width, height);
    command.clipRect = command.rect;
    command.text = text(value);
    command.styleId = text(styleId);
    command.fontToken = text(fontToken);
    return command;
}

void pika_numeric_font_face_keeps_general_label_inside_authored_rect()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::renderer::ui::UiDrawCommand textCommand;
    textCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    textCommand.rect = arp::ui::LayoutRect::fromOriginSize(16.0f, 12.0f, 76.0f, 20.0f);
    textCommand.clipRect = textCommand.rect;
    textCommand.text = text("GENERAL");
    textCommand.styleId = text("text.skill.key");
    textCommand.fontToken = text("font.ui.numeric");

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(textCommand));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 512u;
    desc.maxHeight = 256u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(resolved.commands().size() == std::string("GENERAL").size());
    for (const auto& command : resolved.commands()) {
        assert(command.rect.x >= 16.0f);
        assert(command.rect.x + command.rect.width <= 92.0f);
        assert(command.textureSourceRect.valid());
    }
}

void pika_hud_title_font_face_keeps_target_name_inside_authored_rect()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::renderer::ui::UiDrawCommand textCommand;
    textCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    textCommand.rect = arp::ui::LayoutRect::fromOriginSize(90.0f, 14.0f, 158.0f, 22.0f);
    textCommand.clipRect = textCommand.rect;
    textCommand.text = text("ANCIENT SENTINEL");
    textCommand.styleId = text("text.hud.title");
    textCommand.fontToken = text("font.ui.hud.title");

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(std::move(textCommand));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 512u;
    desc.maxHeight = 256u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(resolved.commands().size() == std::string("ANCIENTSENTINEL").size());
    for (const auto& command : resolved.commands()) {
        assert(command.rect.x >= 90.0f);
        assert(command.rect.x + command.rect.width <= 248.0f);
        assert(command.textureSourceRect.valid());
    }
}

void pika_character_panel_strings_are_warmed_before_tab_switch()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> seedCommands{
        &arp::core::defaultCpuMemoryResource()};
    seedCommands.push_back(
        textCommand("PIKA", "text.hud.title", "font.ui.hud.title", 160.0f, 22.0f));
    seedCommands.push_back(
        textCommand("PIKA STANDARD", "text.character.value", "font.ui.body", 190.0f, 22.0f));
    seedCommands.push_back(
        textCommand("OUTLINE ENABLED", "text.character.label", "font.ui.body", 210.0f, 24.0f));
    seedCommands.push_back(
        textCommand("XRAY NEEDS CORE MAPS", "text.character.label", "font.ui.body", 218.0f, 24.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &seedCommands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 512u;
    desc.maxHeight = 256u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto missesAfterBuild = runtime.telemetry().glyphMisses;

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> tabCommands{
        &arp::core::defaultCpuMemoryResource()};
    tabCommands.push_back(
        textCommand("PIKA STANDARD", "text.character.value", "font.ui.body", 190.0f, 22.0f));
    tabCommands.push_back(
        textCommand("OUTLINE ENABLED", "text.character.label", "font.ui.body", 210.0f, 24.0f));
    tabCommands.push_back(
        textCommand("XRAY NEEDS CORE MAPS", "text.character.label", "font.ui.body", 218.0f, 24.0f));

    const auto resolved = runtime.resolve(tabCommands);
    assert(resolved.ok());
    assert(runtime.telemetry().glyphMisses == missesAfterBuild);
    assert(resolved.commands().size()
        == std::string("PIKASTANDARD").size() + std::string("OUTLINEENABLED").size()
            + std::string("XRAYNEEDSCOREMAPS").size());
    for (const auto& command : resolved.commands()) {
        assert(command.kind == arp::renderer::ui::UiDrawCommandKind::GlyphQuad);
        assert(command.textureSourceRect.valid());
    }
}

void pika_chat_small_text_allocates_ceiled_glyph_cells()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(textCommand(
        "[Party] Luma: ready when you are", "text.hud.small", "font.ui.body", 292.0f, 18.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 512u;
    desc.maxHeight = 256u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(!resolved.commands().empty());
    for (const auto& command : resolved.commands()) {
        const auto sourcePixelHeight =
            command.textureSourceRect.height * static_cast<float>(runtime.atlas().extent.height);
        const auto sourcePixelWidth =
            command.textureSourceRect.width * static_cast<float>(runtime.atlas().extent.width);
        assert(sourcePixelHeight + 0.01f >= std::ceil(command.rect.height));
        assert(sourcePixelWidth + 0.01f >= std::ceil(command.rect.width));
    }
}

void unicode_text_requires_explicit_shaping_capability_in_runtime()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(textCommand(u8"Caf\xC3\xA9", "text.hud", "font.ui.body", 96.0f, 20.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(!runtime.ok());
    assert(runtime.diagnostics()
        == text("UI text layout requires explicit UTF-8 shaping capability for non-ASCII text."));
}

void unicode_text_rasterizes_through_utf8_codepoints_when_capability_is_enabled()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(textCommand(u8"Caf\xC3\xA9", "text.hud", "font.ui.body", 96.0f, 20.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;
    desc.shapingCapability = arp::renderer::ui::TextShapingCapability::BasicLatinUtf8;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    const auto resolved = runtime.resolve(commands);
    assert(resolved.ok());
    assert(resolved.commands().size() == 4u);
    assert(runtime.telemetry().glyphMisses == 4u);
    const auto hasGlyphPixels =
        std::any_of(runtime.atlas().rgba8.begin() + static_cast<std::ptrdiff_t>(4u * 4u * 4u),
            runtime.atlas().rgba8.end(),
            [](const std::uint8_t value) {
                return value != 0u;
            });
    assert(hasGlyphPixels);
}

void pending_uploads_tracks_new_glyphs_across_resolve_calls()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> seedCommands{
        &arp::core::defaultCpuMemoryResource()};
    seedCommands.push_back(textCommand("ABC", "text.hud", "font.ui.body", 96.0f, 20.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &seedCommands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    // Build resolves the seed commands, so pending uploads reflect the warmup glyphs.
    assert(!runtime.pendingUploads().empty());

    // Same text: cache hits, no pending uploads.
    const auto cached = runtime.resolve(seedCommands);
    assert(cached.ok());
    assert(runtime.pendingUploads().empty());

    // New text: cache misses, pending uploads recorded.
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> newCommands{
        &arp::core::defaultCpuMemoryResource()};
    newCommands.push_back(textCommand("XYZ", "text.hud", "font.ui.body", 96.0f, 20.0f));

    const auto withNew = runtime.resolve(newCommands);
    assert(withNew.ok());
    assert(!runtime.pendingUploads().empty());
    for (const auto& upload : runtime.pendingUploads()) {
        assert(upload.frameSequence > 0u);
        assert(upload.pageIndex == 0u);
        assert(upload.atlasRect.width > 0.0f);
        assert(upload.atlasRect.height > 0.0f);
        assert(upload.byteSize > 0u);
    }
}

// combinedAtlasPendingUploads() must return schedules whose atlasRect is in
// combined-atlas absolute coordinates (atlasRect.y offset by glyphPageOriginY_,
// which equals baseAtlas.extent.height). pendingUploads() returns page-local
// coordinates (origin 0,0 within the glyph page). GuiRenderer::updateAtlasTexture
// reads from the combined atlas buffer using the schedule's atlasRect directly,
// so handing it page-local coordinates would sample the base-atlas region
// (blank/wrong pixels) instead of the glyph-page region — the root cause of
// the disabled dynamic-upload block in pika and the "Warrior"->"rror" bug in
// WoCC. This test guards the coordinate translation contract.
void combined_atlas_pending_uploads_offset_by_glyph_page_origin()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/wocc_ui_atlas");
    baseAtlas.extent = {16u, 8u}; // base atlas height = 8 -> glyphPageOriginY = 8
    baseAtlas.rgba8.resize(16u * 8u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> seedCommands{
        &arp::core::defaultCpuMemoryResource()};
    seedCommands.push_back(textCommand("AB", "text.hud", "font.ui.body", 96.0f, 20.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &seedCommands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;

    const auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());

    // Drain the seed-time uploads by resolving the same text (cache hits).
    const auto cached = runtime.resolve(seedCommands);
    assert(cached.ok());
    assert(runtime.pendingUploads().empty());

    // Trigger cache misses with new glyphs.
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> newCommands{
        &arp::core::defaultCpuMemoryResource()};
    newCommands.push_back(textCommand("XYZ", "text.hud", "font.ui.body", 96.0f, 20.0f));
    const auto withNew = runtime.resolve(newCommands);
    assert(withNew.ok());
    assert(!runtime.pendingUploads().empty());

    const auto pageLocal = runtime.pendingUploads();
    const auto combinedAbs = runtime.combinedAtlasPendingUploads();
    assert(pageLocal.size() == combinedAbs.size());
    assert(!pageLocal.empty());

    const float glyphPageOriginY = 8.0f; // baseAtlas.extent.height
    for (std::size_t i = 0u; i < pageLocal.size(); ++i) {
        // x, width, height preserved.
        assert(pageLocal[i].atlasRect.x == combinedAbs[i].atlasRect.x);
        assert(pageLocal[i].atlasRect.width == combinedAbs[i].atlasRect.width);
        assert(pageLocal[i].atlasRect.height == combinedAbs[i].atlasRect.height);
        // y offset by glyphPageOriginY.
        assert(combinedAbs[i].atlasRect.y == pageLocal[i].atlasRect.y + glyphPageOriginY);
        // Combined-atlas y must land inside the glyph-page region of the
        // combined atlas (>= glyphPageOriginY, < combined extent height).
        const auto combinedHeight = static_cast<float>(runtime.atlas().extent.height);
        assert(combinedAbs[i].atlasRect.y >= glyphPageOriginY);
        assert(combinedAbs[i].atlasRect.y < combinedHeight);
        assert(combinedAbs[i].atlasRect.y + combinedAbs[i].atlasRect.height <= combinedHeight);
    }
}

void glyph_generation_and_atlas_rect_preserved_across_runtime_rebuild()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> seedCommands{
        &arp::core::defaultCpuMemoryResource()};
    seedCommands.push_back(textCommand("ABC", "text.hud", "font.ui.body", 96.0f, 20.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = baseAtlas;
    desc.commands = &seedCommands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;
    desc.initialGeneration = 7u;

    const auto runtime1 = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime1.ok());
    assert(runtime1.glyphAtlas().generation() == 7u);

    // Verify seeded glyphs have the assigned generation and valid atlas rects.
    const auto& entries1 = runtime1.glyphAtlas().entries();
    assert(!entries1.empty());
    for (const auto& entry : entries1) {
        assert(entry.generation == 7u);
        assert(entry.atlasRect.width > 0.0f);
        assert(entry.atlasRect.height > 0.0f);
    }

    // Verify upload schedules carry generation, frame sequence, and byte count.
    const auto& uploads1 = runtime1.pendingUploads();
    assert(!uploads1.empty());
    for (const auto& upload : uploads1) {
        assert(upload.generation == 7u);
        assert(upload.frameSequence > 0u);
        assert(upload.byteSize > 0u);
        assert(upload.atlasRect.width > 0.0f);
        assert(upload.atlasRect.height > 0.0f);
    }

    // Rebuild with preserved cache state and bumped generation.
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc2;
    desc2.baseAtlas = std::move(baseAtlas);
    desc2.fontFaces = &fonts;
    desc2.maxWidth = 256u;
    desc2.maxHeight = 128u;
    desc2.initialGeneration = 8u;
    desc2.seedEntries = &entries1;
    desc2.seedPages = &runtime1.glyphAtlas().pages();

    const auto runtime2 = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc2));
    assert(runtime2.ok());
    assert(runtime2.glyphAtlas().generation() == 8u);

    // Resolve same commands in rebuilt runtime: cache hits from seeds, no uploads.
    const auto cached = runtime2.resolve(seedCommands);
    assert(cached.ok());
    assert(runtime2.pendingUploads().empty());

    // Seeded entries retain original generation and atlas rect.
    const auto& entries2 = runtime2.glyphAtlas().entries();
    assert(entries2.size() == entries1.size());
    for (std::size_t i = 0u; i < entries1.size(); ++i) {
        assert(entries2[i].generation == entries1[i].generation);
        assert(entries2[i].atlasRect == entries1[i].atlasRect);
        assert(entries2[i].entryId == entries1[i].entryId);
    }

    // New glyph in bumped generation gets the new generation.
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> newCommands{
        &arp::core::defaultCpuMemoryResource()};
    newCommands.push_back(textCommand("XYZ", "text.hud", "font.ui.body", 96.0f, 20.0f));

    const auto withNew = runtime2.resolve(newCommands);
    assert(withNew.ok());
    assert(!runtime2.pendingUploads().empty());
    for (const auto& upload : runtime2.pendingUploads()) {
        assert(upload.generation == 8u);
        assert(upload.byteSize > 0u);
        assert(upload.atlasRect.width > 0.0f);
    }
}

void telemetry_tracks_hits_misses_unsupported_script_and_upload_completion()
{
    arp::renderer::GuiAtlasTextureDesc baseAtlas;
    baseAtlas.resourceId = std::pmr::string("texture/pika_ui_atlas");
    baseAtlas.extent = {4u, 4u};
    baseAtlas.rgba8.resize(4u * 4u * 4u, 255u);

    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> commands{
        &arp::core::defaultCpuMemoryResource()};
    commands.push_back(textCommand("AB", "text.hud", "font.ui.body", 96.0f, 20.0f));

    auto fonts = pikaFontFaces();
    arp::renderer::ui::UnifiedUiAtlasRuntimeBuildDesc desc;
    desc.baseAtlas = std::move(baseAtlas);
    desc.commands = &commands;
    desc.fontFaces = &fonts;
    desc.maxWidth = 256u;
    desc.maxHeight = 128u;

    auto runtime = arp::renderer::ui::UnifiedUiAtlasRuntime::build(std::move(desc));
    assert(runtime.ok());
    assert(runtime.telemetry().glyphMisses > 0u);
    assert(runtime.telemetry().glyphHits == 0u);
    assert(runtime.telemetry().fallbackCount == 0u);
    assert(runtime.telemetry().unsupportedScriptCount == 0u);
    assert(runtime.telemetry().uploadsCompleted == 0u);

    // Cache hits on second resolve.
    const auto cached = runtime.resolve(commands);
    assert(cached.ok());
    assert(runtime.telemetry().glyphHits > 0u);

    // Record upload completion.
    runtime.recordUploadsCompleted(3u);
    assert(runtime.telemetry().uploadsCompleted == 3u);
    assert(runtime.pendingUploads().empty());
    assert(runtime.combinedAtlasPendingUploads().empty());

    // Unsupported script triggers deterministic diagnostic and telemetry.
    arp::core::pmr::Vector<arp::renderer::ui::UiDrawCommand> rtlCommands{
        &arp::core::defaultCpuMemoryResource()};
    arp::renderer::ui::UiDrawCommand rtlCommand;
    rtlCommand.kind = arp::renderer::ui::UiDrawCommandKind::Text;
    rtlCommand.text = text(u8"\u0627\u0644\u0633\u0644\u0627\u0645");
    rtlCommand.styleId = text("text.hud");
    rtlCommand.fontToken = text("font.ui.body");
    rtlCommand.rect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 96.0f, 20.0f);
    rtlCommand.clipRect = arp::ui::LayoutRect::fromOriginSize(0.0f, 0.0f, 96.0f, 20.0f);
    rtlCommands.push_back(std::move(rtlCommand));

    const auto rtlResult = runtime.resolve(rtlCommands);
    assert(!rtlResult.ok());
    assert(rtlResult.diagnostics().find("shaping") != arp::core::pmr::String::npos);
    assert(runtime.telemetry().unsupportedScriptCount == 1u);
}

void pika_visible_runtime_uses_atlas_runtime_service()
{
    const auto source =
        readFile(projectRoot() / "games" / "pika" / "app" / "visible_character_runtime.cpp");
    const auto runtimeSource =
        readFile(projectRoot() / "src" / "renderer" / "ui" / "ui_atlas_runtime.h");
    assert(source.find("UnifiedUiAtlasRuntime::build") != std::string::npos);
    assert(source.find("atlasRuntimeDesc.fontFaces = &data->theme.fonts") != std::string::npos);
    assert(source.find("UiTextAtlasBuilder().build") == std::string::npos);
    assert(source.find("UiTextAtlasBuildDesc textAtlasDesc") == std::string::npos);
    assert(runtimeSource.find("renderer/ui/ui_text_layout.h") != std::string::npos);
    assert(runtimeSource.find("renderer/ui/ui_text_atlas.h") == std::string::npos);
    assert(runtimeSource.find("UiTextAtlasBuilder") == std::string::npos);
    assert(runtimeSource.find("UiDrawCommandKind::GlyphQuad") != std::string::npos);
}

} // namespace

int main()
{
    unified_ui_atlas_runtime_lowers_text_to_glyph_quads();
    glyph_cells_do_not_clip_wide_uppercase_letters();
    glyph_quads_are_clipped_to_command_clip_rect();
    pika_numeric_font_face_keeps_general_label_inside_authored_rect();
    pika_hud_title_font_face_keeps_target_name_inside_authored_rect();
    pika_character_panel_strings_are_warmed_before_tab_switch();
    pika_chat_small_text_allocates_ceiled_glyph_cells();
    unicode_text_requires_explicit_shaping_capability_in_runtime();
    unicode_text_rasterizes_through_utf8_codepoints_when_capability_is_enabled();
    pending_uploads_tracks_new_glyphs_across_resolve_calls();
    combined_atlas_pending_uploads_offset_by_glyph_page_origin();
    glyph_generation_and_atlas_rect_preserved_across_runtime_rebuild();
    telemetry_tracks_hits_misses_unsupported_script_and_upload_completion();
    pika_visible_runtime_uses_atlas_runtime_service();
    return 0;
}
