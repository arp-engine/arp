#pragma once

#include "core/utf8.h"
#include "renderer/renderer_result.h"
#include "renderer/ui/ui_draw_plan.h"
#include "ui/ui_bitmap_font_resource.h"

namespace arp::renderer {

// Resolves explicitly cooked bitmap glyphs. No rasterization, shaping guesses,
// fallback font, replacement glyph or runtime atlas growth is permitted.
class BitmapTextResolver final {
public:
    static RendererResult resolve(const core::pmr::Vector<ui::UiDrawCommand>& input,
        const arp::ui::UiBitmapFontResource& font,
        std::uint32_t maxCommands,
        core::pmr::Vector<ui::UiDrawCommand>& output)
    {
        if (!font.valid() || !maxCommands)
            return {false, "GUI-BITMAP-TEXT-CONFIG"};
        output.clear();
        output.reserve(maxCommands);
        const auto& atlas = *font.atlas().get();
        const auto& image = atlas.image();
        for (const auto& command : input) {
            if (command.kind != ui::UiDrawCommandKind::Text) {
                if (output.size() == maxCommands)
                    return {false, "GUI-BITMAP-TEXT-CAPACITY"};
                output.push_back(command);
                continue;
            }
            if (command.fontToken != font.fontToken())
                return {false, "GUI-BITMAP-TEXT-FONT-MISMATCH"};
            const char* failure = nullptr;
            float cursor = 0;
            const auto* utf8 = core::Utf8Decoder::visit(command.text, [&](std::uint32_t codepoint) {
                if (failure)
                    return;
                const auto* glyph = font.glyph(codepoint);
                if (!glyph) {
                    failure = "GUI-BITMAP-TEXT-GLYPH-MISSING";
                    return;
                }
                const auto* slice = atlas.slice(glyph->sliceName);
                if (!slice || output.size() == maxCommands) {
                    failure = "GUI-BITMAP-TEXT-CAPACITY";
                    return;
                }
                auto quad = command;
                quad.kind = ui::UiDrawCommandKind::GlyphQuad;
                quad.text.clear();
                quad.rect = {command.rect.x + cursor + glyph->bearingX,
                    command.rect.y + glyph->bearingY,
                    float(slice->width),
                    float(slice->height)};
                quad.textureResourceId = font.atlas().id();
                quad.textureSourceRect = {float(slice->x) / image.width,
                    float(slice->y) / image.height,
                    float(slice->width) / image.width,
                    float(slice->height) / image.height};
                quad.hasTextureSourceRect = quad.hasAtlasPageIndex = true;
                quad.atlasPageIndex = font.page();
                quad.sdfMode = false;
                output.push_back(std::move(quad));
                cursor += glyph->advanceX;
            });
            if (utf8 || failure)
                return {false, utf8 ? utf8 : failure};
        }
        return {true, {}};
    }
};
} // namespace arp::renderer
