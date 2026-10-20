#include "renderer/gui/gui_ordered_renderer.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>

int main()
{
    {
        using namespace arp;
        core::pmr::Vector<renderer::ui::UiDrawCommand> commands{&core::defaultCpuMemoryResource()};
        renderer::ui::UiDrawCommand image;
        image.kind = renderer::ui::UiDrawCommandKind::Image;
        image.rect = {4, 8, 16, 12};
        image.clipRect = {0, 0, 64, 32};
        image.hasTextureSourceRect = true;
        image.textureSourceRect = {.25f, .125f, .5f, .25f};
        image.textureResourceId = "ExactImage";
        commands.push_back(image);
        const auto identity = renderer::buildGuiDrawPlan(
            commands, {64, 32}, renderer::GuiCoordinateMapping{1, false, false});
        const auto flipped = renderer::buildGuiDrawPlan(
            commands, {64, 32}, renderer::GuiCoordinateMapping{1, true, true});
        assert(identity.success && flipped.success);
        assert(identity.commands[0].pushConstants.rectOrigin[1] == 8);
        assert(flipped.commands[0].pushConstants.rectOrigin[1] == 12);
        assert(flipped.commands[0].pushConstants.uvOrigin[1] == .375f);
        assert(flipped.commands[0].pushConstants.uvSize[1] == -.25f);
        assert(!renderer::buildGuiDrawPlan(commands, {64, 32}, renderer::GuiCoordinateMapping{})
                .success);
        auto device = rhi::createNullDevice();
        renderer::GuiOrderedRenderer ordered(*device);
        assert(!ordered.create({}).success && !ordered.readyForFrame(0));
        assert(!ordered.registerTexture({}).success);
    }
}
