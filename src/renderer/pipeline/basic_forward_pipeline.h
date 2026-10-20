#pragma once

#include "renderer/pipeline/basic_forward_contract.h"
#include "renderer/pipeline/render_pipeline.h"
#include "rhi/frontend/ordered_graphics_execution.h"

namespace arp::renderer {
struct BasicForwardPipelineDesc {
    std::uint32_t contractVersion = 0, maxMeshes = 0, maxMaterialsPerMesh = 0;
    std::uint32_t maxSectionsPerFrame = 0, frameSlots = 0;
    std::uint64_t maxCpuMetadataBytes = 0;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution;
    // Opaque single/double-sided, then Blend single/double-sided.
    std::array<std::shared_ptr<rhi::RenderPipeline>, 4> pipelines;
    core::pmr::String shaderSourceSha256{&core::defaultCpuMemoryResource()};
    core::pmr::String shaderPackageId{&core::defaultCpuMemoryResource()};
};

struct BasicForwardMaterialBinding {
    const resource::MaterialResource* material = nullptr;
    // Exact declared UUID for a textured material; explicit source even for an
    // untextured material. No placeholder is manufactured by this pipeline.
    core::pmr::String textureId{&core::defaultCpuMemoryResource()};
    std::shared_ptr<rhi::GpuTexture> linearStraightAlphaTexture;
};

struct BasicForwardFrame {
    core::Mat4 viewProjection;
    core::ArrayView<std::uint32_t> meshes;
    std::uint64_t frameIndex = 0;
    std::shared_ptr<rhi::GpuTexture> depth;
    rhi::LoadOp colorLoad = rhi::LoadOp::Clear;
    rhi::Color clear;
};

// BasicForwardFrame/1 is a graph snapshot. Registration is cold; frame input
// overwrite is gated by graph reservations AND the original resource owner.
class BasicForwardRenderPipeline final : public RenderPipeline {
public:
    explicit BasicForwardRenderPipeline(const BasicForwardPipelineDesc& d) : config_(d) {}

    const RenderPipelineDesc& desc() const override { return desc_; }

    RendererResult create(const RenderPipelineCreateContext&) override;
    RendererResult resize(rhi::Extent2D) override;
    bool frameInputsReady(std::uint32_t) const override;
    RendererResult render(RenderPipelineContext&) override;
    RendererResult composeGraph(render_graph::FrameGraph&,
        render_graph::TextureHandle,
        const RenderPipelineGraphCompositionContext&,
        RenderPipelineFrameFeatures) override;
    RendererResult registerMesh(const assets::MeshAsset&,
        const BasicWorldMeshProfile&,
        core::ArrayView<BasicForwardMaterialBinding>,
        const resource::ShaderPackageResource&,
        std::string_view shaderId,
        const BasicForwardLighting&,
        std::uint32_t& meshId);
    RendererResult addPass(
        render_graph::FrameGraph&, render_graph::TextureHandle, const BasicForwardFrame&);

private:
    struct State;
    BasicForwardPipelineDesc config_;
    RenderPipelineDesc desc_;
    rhi::GpuDevice* device_ = nullptr;
    std::shared_ptr<State> state_;
};
} // namespace arp::renderer
