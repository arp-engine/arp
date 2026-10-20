#pragma once
#include "hud_content.h"
#include "platform/render_system_ordered_graphics.h"
#include "renderer/gui/gui_ordered_renderer.h"
#include "scene_content.h"

namespace arp::build_a_queen {
struct PortableQueenDesc {
    static constexpr std::uint32_t kUiCommands = 512;
    static constexpr rhi::Extent2D kExtent{1280, 720};
    static constexpr std::uint32_t kShadowSize = 1024;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution;
    // Sky, real geometry shadow, lit scene, framework GUI.
    std::array<std::shared_ptr<rhi::RenderPipeline>, 4> pipelines;
    bool attachmentSampleFlipY = false;
};

class PortableQueenRenderer final : public platform::OrderedGraphicsFrameWorkload {
public:
    PortableQueenRenderer(rhi::GpuDevice&, PortableQueenDesc);
    renderer::RendererResult create(const core::pmr::String& characterDirectory);
    renderer::RendererResult update(const Game&, float seconds);
    core::ArrayView<std::shared_ptr<rhi::RenderPipeline>> pipelines() const noexcept override;
    void build(render_graph::FrameGraph&, render_graph::TextureHandle) override;

    std::uint32_t worldVertices() const { return static_cast<std::uint32_t>(frame_.world.size()); }

    std::uint32_t previewVertices() const
    {
        return static_cast<std::uint32_t>(frame_.preview.size());
    }

private:
    std::shared_ptr<rhi::GpuTexture> target(const char*, rhi::Extent2D, bool depth);
    void draw(render_graph::RenderPassContext&,
        std::uint32_t pipeline,
        const rhi::GpuBuffer&,
        std::uint32_t vertices,
        const core::Mat4&,
        bool shadows,
        rhi::Extent2D);
    rhi::GpuDevice& device_;
    PortableQueenDesc config_;
    SceneContent content_;
    HudContent hud_;
    SceneContent::PreparedFrame frame_;
    renderer::GuiOrderedRenderer gui_;
    resource::ResourceManager resources_;
    const ui::UiBitmapFontResource* font_ = nullptr;
    std::shared_ptr<rhi::GpuBuffer> world_, preview_, corners_;
    std::shared_ptr<rhi::GpuTexture> color_, depth_, previewColor_, previewDepth_, shadowColor_,
        shadowDepth_;
    core::UniquePtr<rhi::GpuTextureView> shadowView_;
    core::UniquePtr<rhi::GpuSampler> shadowSampler_;
    core::UniquePtr<rhi::DescriptorArena> shadowArena_;
    core::UniquePtr<rhi::DescriptorSet> shadowSet_;
    core::Mat4 light_;
    core::pmr::Vector<renderer::ui::UiDrawCommand> resolved_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<renderer::ui::UiDrawCommand> composed_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::build_a_queen
