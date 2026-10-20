#include "hud_content.h"
#include "renderer/gui/bitmap_text_resolver.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <cstdio>

int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc == 2);
    resource::ResourceManager resources;
    const core::pmr::String atlasId("00000000-0000-4000-8000-000000000311");
    const core::pmr::String fontId("00000000-0000-4000-8000-000000000312");
    const core::pmr::String root(argv[1]);
    assert(resources
            .load<scene2d::SpriteAtlasResource>(
                {}, atlasId, root + "/font.atlas.yaml", root + "/font.png", 1024 * 1024)
            .success);
    assert(resources
            .load<ui::UiBitmapFontResource>(
                {}, fontId, root + "/font.yaml", atlasId, 128 * 1024, 95)
            .success);
    const auto& font = *resources.find<ui::UiBitmapFontResource>(fontId).get();
    build_a_queen::HudContent hud;
    assert(hud.createHud({1280, 720}).success);
    assert(!hud.createHud({1280, 720}).success);
    build_a_queen::Game game;
    assert(hud.prepareHud(game.snapshot(), game.levelDesc()).success);
    core::pmr::Vector<renderer::ui::UiDrawCommand> output;
    assert(renderer::BitmapTextResolver::resolve(hud.commands(), font, 512, output).success);
    assert(output.size() > hud.commands().size());
    for (const auto& command : output)
        assert(command.kind != renderer::ui::UiDrawCommandKind::Text);
    core::pmr::Vector<renderer::ui::UiDrawCommand> input(1);
    auto& text = input[0];
    text.kind = renderer::ui::UiDrawCommandKind::Text;
    text.fontToken = font.fontToken();
    text.text = "AA";
    text.rect = {10, 20, 100, 50};
    assert(renderer::BitmapTextResolver::resolve(input, font, 2, output).success);
    assert(output.size() == 2);
    assert(output[0].rect.x == 10 + font.glyph('A')->bearingX);
    assert(output[1].rect.x - output[0].rect.x == font.glyph('A')->advanceX);
    assert(output[0].atlasPageIndex == font.page() && output[0].hasTextureSourceRect);
    assert(!renderer::BitmapTextResolver::resolve(input, font, 1, output).success);
    text.fontToken = "missing";
    assert(!renderer::BitmapTextResolver::resolve(input, font, 2, output).success);
    text.fontToken = font.fontToken();
    text.text = "\xE4\xB8\xAD";
    assert(!renderer::BitmapTextResolver::resolve(input, font, 2, output).success);
    auto state = game.snapshot();
    state.phase = build_a_queen::Phase::Result;
    assert(hud.prepareHud(state, game.levelDesc()).success);
    bool final = false;
    for (const auto& command : hud.commands())
        final |= command.text == "FINAL LOOK";
    assert(final);
    std::puts("Queen framework HUD and cooked bitmap text contracts passed");
}
