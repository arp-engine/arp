#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "renderer/global_illumination.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_result.h"
#include "rhi/gpu_device.h"
#include "rhi/gpu_swapchain.h"
#include "rhi/semantics/render_target_coordinates.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace arp::renderer {

struct RenderPipelineDesc {
    std::pmr::string name = "RenderPipeline";
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::TextureFormat depthFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
    bool usesDepth = false;
};

struct RenderView {
    rhi::Extent2D extent;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    uint64_t frameIndex = 0;
    float deltaSeconds = 0.0f;
};

struct RenderFrameRequest {
    RenderView view;
    const void* pipelinePayload = nullptr;
    const char* pipelinePayloadContractName = nullptr;
    std::uint32_t pipelinePayloadContractVersion = 0;
};

struct RenderPipelineCreateContext {
    rhi::GpuDevice& device;
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    rhi::TextureFormat depthFormat = rhi::TextureFormat::Unknown;
    rhi::Extent2D extent;
    rhi::semantics::TextureUvYTransform sampledColorInputUvYTransform =
        rhi::semantics::TextureUvYTransform::Identity;
};

struct RenderPipelineContext {
    rhi::AcquiredFrame frame;
    rhi::RenderTargetDesc colorTarget;
    const rhi::DepthTargetDesc* depthTarget = nullptr;
    rhi::GpuTexture* colorTexture = nullptr;
    rhi::GpuTexture* depthTexture = nullptr;
    const RenderFrameRequest& request;
};

enum class RenderGraphPostprocessPass {
    Fxaa,
    Taa,
};

struct RenderGraphPostprocessContext {
    RenderGraphPostprocessPass pass = RenderGraphPostprocessPass::Fxaa;
    rhi::GpuTexture* sourceColor = nullptr;

    bool valid() const { return sourceColor != nullptr; }
};

struct RenderGraphColorTargetContext {
    rhi::RenderTargetDesc colorTarget;
    rhi::GpuTexture* colorTexture = nullptr;

    bool colorValid() const
    {
        return colorTexture != nullptr && colorTarget.target.isValid()
            && colorTarget.extent.width > 0u && colorTarget.extent.height > 0u
            && colorTarget.format != rhi::TextureFormat::Unknown;
    }

    bool valid() const { return colorValid(); }
};

struct RenderGraphSceneDepthContext : RenderGraphColorTargetContext {
    rhi::DepthTargetDesc depthTarget;
    rhi::GpuTexture* depthTexture = nullptr;
    rhi::GpuTexture* shadowTexture = nullptr;

    bool depthValid() const
    {
        return depthTexture != nullptr && depthTarget.target.isValid()
            && depthTarget.extent.width > 0u && depthTarget.extent.height > 0u
            && depthTarget.format != rhi::TextureFormat::Unknown;
    }

    bool valid() const { return colorValid() && depthValid(); }
};

struct RenderGraphPostprocessTargetContext : RenderGraphColorTargetContext {
    rhi::GpuTexture* sourceColor = nullptr;

    bool valid() const { return colorValid() && sourceColor != nullptr; }
};

struct RenderGraphDepthPostprocessTargetContext : RenderGraphPostprocessTargetContext {
    rhi::GpuTexture* sourceDepth = nullptr;

    bool valid() const
    {
        return RenderGraphPostprocessTargetContext::valid() && sourceDepth != nullptr
            && sourceDepth->desc().format != rhi::TextureFormat::Unknown
            && sourceDepth->desc().extent.width > 0u && sourceDepth->desc().extent.height > 0u;
    }
};

struct RenderPipelineGraphCompositionContext {
    rhi::AcquiredFrame frame;
    RenderFrameRequest request;
    bool sceneEnabled = false;
};

struct RenderPipelineFrameFeatures {
    std::function<void(render_graph::RenderContext&, const RenderGraphColorTargetContext&)>
        particlesWithColor;
    render_graph::FrameGraph::ExecuteCallback gui;
    render_graph::FrameGraph::ExecuteCallback debugOverlay;
    GlobalIlluminationDesc globalIllumination =
        globalIlluminationDescForProfile(QualityProfile::Low);
};

struct RenderTargetRead {
    render_graph::TextureHandle texture;
    rhi::ResourceState state = rhi::ResourceState::ShaderRead;
};

struct RenderTargetWrite {
    render_graph::TextureHandle texture;
    rhi::ResourceState state = rhi::ResourceState::ColorAttachment;
};

class ARP_API RenderPipelineBuilder {
public:
    using ExecuteCallback = render_graph::FrameGraph::ExecuteCallback;

    RenderPipelineBuilder(render_graph::FrameGraph& graph,
        render_graph::TextureHandle backbuffer,
        RenderPipelineDesc desc);

    render_graph::FrameGraph& graph();
    render_graph::TextureHandle backbuffer() const;
    const RenderPipelineDesc& desc() const;

    render_graph::TextureHandle createTransientColor(const std::pmr::string& name);
    void addGraphicsPass(const std::pmr::string& name,
        const std::pmr::vector<RenderTargetRead>& reads,
        const std::pmr::vector<RenderTargetWrite>& writes,
        ExecuteCallback callback);

private:
    render_graph::FrameGraph& graph_;
    render_graph::TextureHandle backbuffer_;
    RenderPipelineDesc desc_;
};

class RenderFeature {
public:
    virtual ~RenderFeature() = default;
    virtual const char* name() const = 0;
    virtual void registerPasses(RenderPipelineBuilder& builder) = 0;
};

class ARP_API RenderPipeline {
public:
    virtual ~RenderPipeline() = default;
    virtual const RenderPipelineDesc& desc() const = 0;
    virtual RendererResult create(const RenderPipelineCreateContext& context) = 0;
    virtual RendererResult resize(rhi::Extent2D extent) = 0;

    // Declared mutable input requirement only; platform owns frame admission.
    // Default means this pipeline declares no pending input lease.
    virtual bool frameInputsReady(std::uint32_t) const { return true; }

    virtual RendererResult render(RenderPipelineContext& context) = 0;
    virtual RendererResult recordGraph(
        render_graph::RenderContext& graphContext, RenderPipelineContext& context);
    virtual RendererResult composeGraph(render_graph::FrameGraph& graph,
        render_graph::TextureHandle backbuffer,
        const RenderPipelineGraphCompositionContext& context,
        RenderPipelineFrameFeatures frameFeatures);

    virtual bool supportsGraphPostprocess(RenderGraphPostprocessPass) const { return false; }

    virtual RendererResult recordGraphPostprocess(
        render_graph::RenderContext&, RenderPipelineContext&, const RenderGraphPostprocessContext&)
    {
        return RendererResult{
            false, "render pipeline does not support the requested graph postprocess pass"};
    }
};

} // namespace arp::renderer
