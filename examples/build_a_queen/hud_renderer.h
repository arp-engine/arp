#pragma once

#include "hud_content.h"
#include "native_hud_source_task.h"
#include "native_shader_source_task.h"
#include "renderer/ui/gui_renderer.h"
#include "renderer/ui/ui_atlas_runtime.h"
#include "ui/ui.h"

namespace arp::build_a_queen {

class HudRenderer final : public HudContent {
public:
    explicit HudRenderer(rhi::GpuDevice& device);

    renderer::RendererResult create(const NativeShaderSources& shaders,
        const NativeHudSources& sources,
        rhi::TextureFormat colorFormat,
        rhi::Extent2D extent);
    renderer::RendererResult resize(rhi::Extent2D extent);
    renderer::RendererResult record(rhi::CommandEncoder& encoder,
        rhi::GpuTexture& target,
        const rhi::AcquiredFrame& frame,
        const Snapshot& state,
        const LevelDesc& level);

    bool readyForFrame(std::uint64_t) const { return rendererReady_; }

    std::uint32_t lastDrawCount() const { return lastDrawCount_; }

private:
    core::SharedPtr<const shader::GeneratedShaderArtifactSet> preparedShader_;
    core::pmr::String shaderPublication_;
    core::SharedPtr<const ui::UiAtlasImageResourceData> baseAtlas_;
    rhi::GpuDevice& device_;
    renderer::GuiRenderer renderer_;
    core::UniquePtr<renderer::ui::UnifiedUiAtlasRuntime> atlasRuntime_;
    core::pmr::Vector<ui::UiFontFaceDesc> fontFaces_{&core::defaultCpuMemoryResource()};
    core::pmr::String shaderPath_{&core::defaultCpuMemoryResource()};
    rhi::TextureFormat colorFormat_ = rhi::TextureFormat::Unknown;
    bool rendererReady_ = false;
};

} // namespace arp::build_a_queen
