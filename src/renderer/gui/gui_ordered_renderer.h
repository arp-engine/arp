#pragma once

#include "renderer/gui/gui_draw_plan.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_result.h"
#include "rhi/frontend/ordered_graphics_execution.h"
#include "ui/ui_bitmap_font_resource.h"

namespace arp::renderer {

enum class GuiSampleAlpha { Straight, Premultiplied };

struct GuiOrderedTextureDesc {
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    std::shared_ptr<rhi::GpuTexture> texture;
    GuiSampleAlpha alpha = GuiSampleAlpha::Straight;
    bool flipUvY = false;
    bool bitmapGlyphPage = false;
    bool externalTexture = false;
    core::pmr::String fontToken{&core::defaultCpuMemoryResource()};
    std::uint32_t atlasPage = 0;
};

struct GuiOrderedRendererDesc {
    std::uint32_t contractVersion = 0;
    std::uint32_t maxCommands = 0, maxTextures = 0, frameSlots = 0;
    std::uint64_t maxCpuMetadataBytes = 0;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution;
    std::shared_ptr<rhi::RenderPipeline> pipeline;
    core::pmr::String shaderSourceSha256{&core::defaultCpuMemoryResource()};
    GuiCoordinateMapping coordinates;
};

// Execution strategy for the original Ui/GuiDrawPlan. It owns no platform
// handles, never creates a fallback atlas, and does not claim GPU completion.
class GuiOrderedRenderer final {
public:
    explicit GuiOrderedRenderer(rhi::GpuDevice& device) : device_(device) {}

    RendererResult create(const GuiOrderedRendererDesc&);
    RendererResult registerTexture(const GuiOrderedTextureDesc&);
    RendererResult registerBitmapFont(const arp::ui::UiBitmapFontResource&);
    RendererResult unregisterTexture(core::StringView resourceId);
    bool readyForFrame(std::uint64_t) const;
    RendererResult addPass(render_graph::FrameGraph&,
        render_graph::TextureHandle,
        const core::pmr::Vector<ui::UiDrawCommand>&,
        core::StringView boxTextureId,
        std::uint64_t frame,
        rhi::LoadOp,
        rhi::Color clear = {});

private:
    struct State;
    rhi::GpuDevice& device_;
    std::shared_ptr<State> state_;
};
} // namespace arp::renderer
