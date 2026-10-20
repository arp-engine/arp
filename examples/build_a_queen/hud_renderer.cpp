#include "hud_renderer.h"

#include "renderer/ui/ui_draw_plan.h"
#include "ui/ui_font.h"

#include <algorithm>
#include <cstdio>

namespace arp::build_a_queen {
HudRenderer::HudRenderer(rhi::GpuDevice& device) : device_(device), renderer_(device)
{}

renderer::RendererResult HudRenderer::create(
    const core::pmr::String& shaderPath, rhi::TextureFormat colorFormat, rhi::Extent2D extent)
{
    if (rendererReady_ || shaderPath.empty() || colorFormat == rhi::TextureFormat::Unknown
        || extent.width == 0u || extent.height == 0u)
        return {false, "ARP-QUEEN-HUD-CONFIG"};

    auto font = ui::loadDefaultUiFontFace();
    if (!font.ok())
        return {false, font.diagnostic().message};
    auto fontDesc = font.face().desc;
    fontDesc.tokenId = kQueenFontToken;
    fontDesc.sizePx = 18u;
    fontDesc.lineHeightPx = 22u;
    fontFaces_.push_back(std::move(fontDesc));

    const auto content = createHud(extent);
    if (!content.success)
        return content;

    shaderPath_ = shaderPath;
    colorFormat_ = colorFormat;
    const auto resized = resize(extent);
    if (!resized.success)
        return resized;
    rendererReady_ = true;
    return {true, {}};
}

renderer::RendererResult HudRenderer::resize(rhi::Extent2D extent)
{
    if (extent.width == 0u || extent.height == 0u)
        return {false, "ARP-QUEEN-HUD-EXTENT"};
    const auto content = resizeHud(extent);
    if (!content.success)
        return content;

    if (!atlasRuntime_) {
        renderer::GuiAtlasTextureDesc baseAtlas;
        baseAtlas.resourceId = "queen.ui.framework.atlas";
        baseAtlas.extent = {1u, 1u};
        baseAtlas.rgba8 = {255u, 255u, 255u, 255u};
        renderer::ui::UnifiedUiAtlasRuntimeBuildDesc atlasDesc;
        atlasDesc.baseAtlas = std::move(baseAtlas);
        atlasDesc.fontFaces = &fontFaces_;
        atlasDesc.maxWidth = 1024u;
        atlasDesc.maxHeight = 512u;
        atlasDesc.dynamicGlyphBudgetPerFrame = 256u;
        auto atlas = renderer::ui::UnifiedUiAtlasRuntime::build(std::move(atlasDesc));
        if (!atlas.ok())
            return {false, atlas.diagnostics()};
        atlasRuntime_ = core::makeUnique<renderer::ui::UnifiedUiAtlasRuntime>(std::move(atlas));

        renderer::GuiRendererDesc rendererDesc;
        rendererDesc.shaderPath = shaderPath_;
        rendererDesc.colorFormat = colorFormat_;
        rendererDesc.extent = extent_;
        rendererDesc.atlasTexture = atlasRuntime_->atlas();
        rendererDesc.hasAtlasTexture = true;
        return renderer_.create(rendererDesc);
    }
    return renderer_.onResize(extent_);
}

renderer::RendererResult HudRenderer::record(rhi::CommandEncoder& encoder,
    rhi::GpuTexture& target,
    const rhi::AcquiredFrame& frame,
    const Snapshot& state,
    const LevelDesc& level)
{
    if (!rendererReady_ || target.desc().extent.width != extent_.width
        || target.desc().extent.height != extent_.height)
        return {false, "ARP-QUEEN-HUD-RESIZE-REQUIRED"};
    updateWidgets(state, level);
    auto built = buildCommands();
    if (!built.success)
        return built;
    const auto resolved = atlasRuntime_->resolve(drawCommands_);
    if (!resolved.ok())
        return {false, resolved.diagnostics()};
    drawCommands_ = resolved.commands();

    const auto uploads = atlasRuntime_->combinedAtlasPendingUploads();
    if (!uploads.empty()) {
        const auto updated = renderer_.updateAtlasTexture(atlasRuntime_->atlas(), uploads);
        if (!updated.success)
            return updated;
        atlasRuntime_->recordUploadsCompleted(uploads.size());
    }

    renderer::RenderFrameContext frameContext;
    frameContext.frame = frame;
    frameContext.colorTexture = &target;
    frameContext.colorTarget.label = target.desc().label;
    frameContext.colorTarget.target = target.id();
    frameContext.colorTarget.extent = {target.desc().extent.width, target.desc().extent.height};
    frameContext.colorTarget.format = target.desc().format;
    frameContext.colorTarget.finalState = rhi::ResourceState::ColorAttachment;
    return renderer_.recordUiCommands(encoder, frameContext, drawCommands_);
}

} // namespace arp::build_a_queen
