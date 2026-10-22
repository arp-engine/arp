#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/gui/gui_rhi_draw_commands.h"
#include "rhi/gpu_device.h"

namespace arp::editor {

inline constexpr const char* kEditorSceneViewportTextureId = "arp.editor.scene-viewport";

class EditorSceneViewportComposite final {
public:
    EditorSceneViewportComposite() = default;
    EditorSceneViewportComposite(const EditorSceneViewportComposite&) = delete;
    EditorSceneViewportComposite& operator=(const EditorSceneViewportComposite&) = delete;

    // Owns all sampled color target resources used by the UI descriptor. The
    // owner must outlive recorded work and may only be recreated after queue idle.
    bool create(rhi::GpuDevice& device, rhi::Extent2D extent);
    bool valid() const noexcept;

    rhi::GpuTexture* colorTarget() noexcept { return colorTarget_.get(); }

    rhi::GpuTexture* depthTarget() noexcept { return depthTarget_.get(); }

    rhi::Extent2D extent() const noexcept;
    renderer::GuiExternalTextureSetBinding guiBinding() noexcept;

private:
    const rhi::GpuDevice* ownerDevice_ = nullptr;
    core::UniquePtr<rhi::GpuTexture> colorTarget_;
    core::UniquePtr<rhi::GpuTexture> depthTarget_;
    core::UniquePtr<rhi::GpuTextureView> colorView_;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    core::UniquePtr<rhi::DescriptorArena> descriptorArena_;
    core::UniquePtr<rhi::DescriptorSet> guiDescriptorSet_;
};

} // namespace arp::editor
