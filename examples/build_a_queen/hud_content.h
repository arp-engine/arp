#pragma once
#include "game.h"
#include "renderer/renderer_result.h"
#include "renderer/ui/ui_draw_plan.h"
#include "rhi/gpu_pipeline.h"
#include "ui/ui.h"

namespace arp::build_a_queen {
inline constexpr const char* kQueenFontToken = "queen.font";

class HudContent {
public:
    HudContent();
    renderer::RendererResult createHud(rhi::Extent2D);
    renderer::RendererResult resizeHud(rhi::Extent2D);
    renderer::RendererResult prepareHud(const Snapshot&, const LevelDesc&);

    const auto& commands() const { return drawCommands_; }

    rhi::ViewportState targetPreviewViewport() const;
    rhi::ScissorState targetPreviewScissor() const;

protected:
    static ui::CanvasDesc makeCanvasDesc(rhi::Extent2D);
    void createWidgets();
    void updateWidgets(const Snapshot&, const LevelDesc&);
    renderer::RendererResult buildCommands();
    ui::Canvas canvas_;
    core::pmr::Vector<renderer::ui::UiDrawStyleColor> styleColors_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<renderer::ui::UiDrawStyleFont> styleFonts_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<renderer::ui::UiDrawCommand> drawCommands_{&core::defaultCpuMemoryResource()};
    rhi::Extent2D extent_{};
    ui::Widget* soundButton_ = nullptr;
    ui::Widget* levelLabel_ = nullptr;
    ui::Widget* progressLabel_ = nullptr;
    ui::Widget* targetTitle_ = nullptr;
    ui::Widget* instructionLabel_ = nullptr;
    std::uint32_t lastDrawCount_ = 0;
};
} // namespace arp::build_a_queen
