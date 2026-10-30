#pragma once

#include "editor/viewport/editor_scene_viewport_environment.h"
#include "renderer/gui/gui_ordered_renderer.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::editor {

class EditorShell;

// Uses the public ordered executor. Resource reuse follows its original-owner
// contract; command issuance never asserts native GPU completion.
class EditorSceneViewportOrderedRenderer final {
public:
    void create(rhi::GpuDevice&,
        std::shared_ptr<rhi::RenderPipeline>,
        std::shared_ptr<rhi::RenderPipeline> pbr,
        std::shared_ptr<rhi::OriginalOrderedGraphicsExecution>,
        const shader::ShaderReflection&,
        std::shared_ptr<rhi::RenderPipeline> sprite);
    void prepareGeometry(EditorShell&, const EditorSceneViewportFrame&);
    void prepareSprites(EditorShell&, const scene::SceneTraversalSnapshot&, rhi::Extent2D);
    void resize(rhi::GpuDevice&, renderer::GuiOrderedRenderer&, rhi::Extent2D, bool sampleFlipY);
    void addPasses(
        render_graph::FrameGraph&, const EditorSceneViewportCamera&, const EditorSceneViewportSky&);

    bool valid() const noexcept { return color_ && depth_; }

    bool hasGeometry() const noexcept;
    bool hasSprites() const noexcept;

private:
    struct GeometryState;
    void addGeometryPass(render_graph::FrameGraph&,
        render_graph::TextureHandle,
        render_graph::TextureHandle,
        rhi::Extent3D);
    rhi::GpuDevice* device_ = nullptr;
    std::shared_ptr<rhi::RenderPipeline> pipeline_;
    std::shared_ptr<rhi::RenderPipeline> pbr_;
    std::shared_ptr<rhi::RenderPipeline> sprite_;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution_;
    std::shared_ptr<GeometryState> geometry_;
    shader::ShaderReflection pbrReflection_;
    std::shared_ptr<rhi::GpuBuffer> corners_;
    std::shared_ptr<rhi::GpuTexture> color_, depth_;
};

} // namespace arp::editor
