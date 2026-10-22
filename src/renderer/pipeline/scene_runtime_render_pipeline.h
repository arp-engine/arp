#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/pipeline/render_pipeline.h"
#include "renderer/scene_runtime_frame.h"

#include <array>
#include <cstddef>

namespace arp::renderer {

inline constexpr const char* kSceneRuntimeWorldUiPassName = "SceneRuntimeWorldUiPass";
inline constexpr std::size_t kSceneRuntimeRenderPipelineMaxQueues = 3u;

struct SceneRuntimeRenderPipelineQueueSemantic {
    RendererSceneRuntimeQueueKind kind = RendererSceneRuntimeQueueKind::Scene3D;
    std::size_t order = 0;
    std::size_t drawItemCount = 0;
};

struct SceneRuntimeRenderPipelineSemanticRecord {
    const char* passName = nullptr;
    std::size_t physicalPassCount = 0;
    std::size_t queueCount = 0;
    std::array<SceneRuntimeRenderPipelineQueueSemantic, kSceneRuntimeRenderPipelineMaxQueues>
        queues{};
};

class ARP_API SceneRuntimeRenderPipeline final : public RenderPipeline {
public:
    SceneRuntimeRenderPipeline();

    const RenderPipelineDesc& desc() const override;
    RendererResult create(const RenderPipelineCreateContext& context) override;
    RendererResult resize(rhi::Extent2D extent) override;
    RendererResult render(RenderPipelineContext& context) override;
    RendererResult composeGraph(render_graph::FrameGraph& graph,
        render_graph::TextureHandle backbuffer,
        const RenderPipelineGraphCompositionContext& context,
        RenderPipelineFrameFeatures frameFeatures) override;

    const SceneRuntimeRenderPipelineSemanticRecord& lastSemanticRecord() const;

private:
    RenderPipelineDesc desc_;
    rhi::GpuDevice* device_ = nullptr;
    SceneRuntimeRenderPipelineSemanticRecord lastSemanticRecord_;
};

} // namespace arp::renderer
