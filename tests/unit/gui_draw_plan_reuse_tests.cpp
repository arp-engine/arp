#include "renderer/gui/gui_draw_plan.h"

#include <cassert>

int main()
{
    // A reused plan owns its resource text and records each backend's current
    // coordinates. Stable-sized frames must reuse both vector and string storage.
    {
        using namespace arp;
        core::pmr::Vector<renderer::ui::UiDrawCommand> input(2u);
        auto& image = input[0];
        image.kind = renderer::ui::UiDrawCommandKind::Image;
        image.rect = {10, 20, 30, 40};
        image.textureResourceId = "ui/atlas/reusable-resource-identity-beyond-inline-storage";
        image.hasTextureSourceRect = true;
        image.textureSourceRect = {0, 0, 1, 1};
        renderer::GuiDrawPlan reused;
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            renderer::updateGuiDrawPlan(input, {640, 360}, backend, reused);
            assert(reused.success && reused.commands.size() == 2u);
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            for (unsigned frame = 0; frame < 16u; ++frame)
                renderer::updateGuiDrawPlan(input, {640, 360}, backend, reused);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            const auto first = reused.commands[0].textureResourceId.front();
            image.textureResourceId.front() = 'X';
            assert(reused.commands[0].textureResourceId.front() == first);
            image.textureResourceId.front() = first;
            image.kind = renderer::ui::UiDrawCommandKind::Text;
            image.hasTextureSourceRect = false;
            renderer::updateGuiDrawPlan(input, {640, 360}, backend, reused);
            assert(!reused.success && reused.commands.empty());
            image.hasTextureSourceRect = true;
            renderer::updateGuiDrawPlan(input, {640, 360}, backend, reused);
            assert(reused.success && reused.commands[0].text);
            image.kind = renderer::ui::UiDrawCommandKind::Image;
        }
    }
    return 0;
}
