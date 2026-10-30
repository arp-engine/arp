#pragma once

#include "2d/sprite_draw_plan.h"
#include "editor/viewport/editor_scene_viewport_composite.h"
#include "editor/viewport/editor_scene_viewport_draw_executor.h"
#include "editor/viewport/editor_scene_viewport_shader_package.h"
#include "renderer/render_graph/render_graph.h"
#include "resource/resource_manager.h"
#include "rhi/frontend/ordered_graphics_execution.h"

namespace arp::editor {

class EditorSceneViewportSpriteRenderer final
    : public resource::ResourceLifecycleObserver,
      public std::enable_shared_from_this<EditorSceneViewportSpriteRenderer> {
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
        core::pmr::String& diagnostics,
        EditorSceneViewportSurfaceProfile profile = {});

    bool createOrdered(rhi::GpuDevice&,
        resource::ResourceManager&,
        std::shared_ptr<rhi::RenderPipeline>,
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>,
        std::uint32_t maxInstances,
        std::uint32_t maxAtlases,
        std::uint64_t maxUploadBytes,
        core::pmr::String&);
    bool addPass(render_graph::FrameGraph&,
        render_graph::TextureHandle,
        render_graph::TextureHandle,
        const scene2d::SpriteDrawPlan&,
        const core::pmr::Vector<scene2d::SpriteInstance>&,
        core::pmr::String&);

    bool prepare(const scene2d::SpriteDrawPlan& plan, core::pmr::String& diagnostics);

    bool valid() const noexcept
    {
        return device_ != nullptr && resources_ != nullptr && activePipeline() && sampler_
            && instances_ && corners_;
    }

    EditorSceneViewportDrawExecutionResult record(rhi::GpuTexture& colorTarget,
        rhi::GpuTexture& depthTarget,
        const EditorSceneViewportRect& viewport,
        const scene2d::SpriteDrawPlan& plan,
        const core::pmr::Vector<scene2d::SpriteInstance>& projectedInstances);

    bool beforeResourceUnload(resource::NameView id) noexcept override;
    bool beforeRetiredResourcesRelease() noexcept override;

private:
    bool createStorage(rhi::GpuDevice&,
        resource::ResourceManager&,
        std::uint32_t,
        std::uint32_t,
        std::uint64_t,
        core::pmr::String&);

    rhi::RenderPipeline* activePipeline() const noexcept
    {
        return orderedPipeline_ ? orderedPipeline_.get() : pipeline_.pipeline.get();
    }

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
    std::shared_ptr<rhi::RenderPipeline> orderedPipeline_;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution_;
    core::UniquePtr<rhi::GpuBuffer> corners_;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    core::UniquePtr<rhi::GpuBuffer> instances_;
    core::pmr::Vector<AtlasOwner> atlases_{&core::defaultCpuMemoryResource()};
    std::uint32_t maxInstances_ = 0u;
    std::uint32_t maxAtlases_ = 0u;
    std::uint64_t maxUploadBytes_ = 0u;
};

} // namespace arp::editor
