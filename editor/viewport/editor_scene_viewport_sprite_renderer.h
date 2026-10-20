#pragma once

#include "2d/sprite_draw_plan.h"
#include "editor/viewport/editor_scene_viewport_composite.h"
#include "editor/viewport/editor_scene_viewport_draw_executor.h"
#include "editor/viewport/editor_scene_viewport_shader_package.h"
#include "resource/resource_manager.h"

namespace arp::editor {

class EditorSceneViewportSpriteRenderer final : public resource::ResourceLifecycleObserver {
public:
    EditorSceneViewportSpriteRenderer() = default;
    EditorSceneViewportSpriteRenderer(const EditorSceneViewportSpriteRenderer&) = delete;
    EditorSceneViewportSpriteRenderer& operator=(const EditorSceneViewportSpriteRenderer&) = delete;
    ~EditorSceneViewportSpriteRenderer() override;

    bool create(rhi::GpuDevice& device,
        resource::ResourceManager& resources,
        const EditorSceneViewportShaderArtifacts& artifacts,
        std::uint32_t maxInstances,
        std::uint32_t maxAtlases,
        std::uint64_t maxUploadBytes,
        core::pmr::String& diagnostics);

    bool prepare(const scene2d::SpriteDrawPlan& plan, core::pmr::String& diagnostics);

    bool valid() const noexcept
    {
        return device_ != nullptr && resources_ != nullptr && pipeline_.layout && pipeline_.pipeline
            && sampler_ && instances_;
    }

    EditorSceneViewportDrawExecutionResult record(rhi::GpuTexture& colorTarget,
        rhi::GpuTexture& depthTarget,
        const EditorSceneViewportRect& viewport,
        const scene2d::SpriteDrawPlan& plan,
        const core::pmr::Vector<scene2d::SpriteInstance>& projectedInstances);

    bool beforeResourceUnload(resource::NameView id) noexcept override;
    bool beforeRetiredResourcesRelease() noexcept override;

private:
    struct AtlasOwner {
        resource::ResourceHandle<scene2d::SpriteAtlasResource> source;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
    };

    rhi::GpuDevice* device_ = nullptr;
    resource::ResourceManager* resources_ = nullptr;
    EditorSceneViewportDevicePipelineResources pipeline_;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    core::UniquePtr<rhi::GpuBuffer> instances_;
    core::pmr::Vector<AtlasOwner> atlases_{&core::defaultCpuMemoryResource()};
    std::uint32_t maxInstances_ = 0u;
    std::uint32_t maxAtlases_ = 0u;
    std::uint64_t maxUploadBytes_ = 0u;
};

} // namespace arp::editor
