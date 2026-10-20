#include "renderer/pipeline/render_pipeline.h"

#include "core/cpu_allocator.h"
#include "renderer/render_frame.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace arp::renderer {

namespace {

RenderGraphColorTargetContext colorTargetContext(
    render_graph::FrameGraph& graph, const render_graph::TextureHandle color)
{
    auto& colorTexture = graph.texture(color);
    return RenderGraphColorTargetContext{
        rhi::RenderTargetDesc{
            colorTexture.desc().label,
            colorTexture.id(),
            {colorTexture.desc().extent.width, colorTexture.desc().extent.height},
            colorTexture.desc().format,
            rhi::ResourceState::ColorAttachment,
        },
        &colorTexture,
    };
}

rhi::TextureFormat depthFormatFor(const RenderPipelineDesc& desc)
{
    if (desc.depthFormat != rhi::TextureFormat::Unknown) {
        return desc.depthFormat;
    }
    return rhi::TextureFormat::D32Float;
}

render_graph::PassBuilder& writeColorAttachment(
    render_graph::PassBuilder& pass, render_graph::TextureHandle target)
{
    return pass.writeAttachment(
        target, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store);
}

render_graph::PassBuilder& writeDepthAttachment(render_graph::PassBuilder& pass,
    render_graph::TextureHandle target,
    rhi::LoadOp loadOp = rhi::LoadOp::Clear,
    rhi::StoreOp storeOp = rhi::StoreOp::Store)
{
    return pass.writeAttachment(target, rhi::ResourceState::DepthAttachment, loadOp, storeOp);
}

} // namespace

RenderPipelineBuilder::RenderPipelineBuilder(render_graph::FrameGraph& graph,
    render_graph::TextureHandle backbuffer,
    RenderPipelineDesc desc)
    : graph_(graph), backbuffer_(backbuffer), desc_(std::move(desc))
{}

render_graph::FrameGraph& RenderPipelineBuilder::graph()
{
    return graph_;
}

render_graph::TextureHandle RenderPipelineBuilder::backbuffer() const
{
    return backbuffer_;
}

const RenderPipelineDesc& RenderPipelineBuilder::desc() const
{
    return desc_;
}

render_graph::TextureHandle RenderPipelineBuilder::createTransientColor(
    const std::pmr::string& name)
{
    return graph_.createTransientTexture(rhi::TextureDesc{
        name,
        {desc_.extent.width, desc_.extent.height, 1u},
        desc_.colorFormat,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::Transient,
        rhi::ResourceState::ColorAttachment,
        1,
    });
}

void RenderPipelineBuilder::addGraphicsPass(const std::pmr::string& name,
    const std::pmr::vector<RenderTargetRead>& reads,
    const std::pmr::vector<RenderTargetWrite>& writes,
    ExecuteCallback callback)
{
    auto pass = graph_.addPass(name);
    for (const auto& read : reads) {
        pass.read(read.texture, read.state);
    }
    for (const auto& write : writes) {
        pass.write(write.texture, write.state);
    }
    pass.execute(std::move(callback));
}

RendererResult RenderPipeline::recordGraph(render_graph::RenderContext&, RenderPipelineContext&)
{
    return RendererResult{false, "render pipeline does not support direct graph pass recording"};
}

RendererResult RenderPipeline::composeGraph(render_graph::FrameGraph& graph,
    render_graph::TextureHandle backbuffer,
    const RenderPipelineGraphCompositionContext& context,
    RenderPipelineFrameFeatures frameFeatures)
{
    const auto backbufferExtent = graph.textureDesc(backbuffer).extent;
    render_graph::TextureHandle sceneDepth;
    if (context.sceneEnabled && desc().usesDepth) {
        sceneDepth = graph.createTransientTexture(rhi::TextureDesc{
            "SceneDepth",
            backbufferExtent,
            depthFormatFor(desc()),
            rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
            rhi::MemoryUsage::Transient,
            rhi::ResourceState::Undefined,
            1u,
        });
    }

    if (context.sceneEnabled) {
        auto mainScene = graph.addPass("MainScene");
        writeColorAttachment(mainScene, backbuffer);
        if (desc().usesDepth) {
            writeDepthAttachment(mainScene, sceneDepth);
        }
        mainScene.execute([this, &graph, backbuffer, sceneDepth, context](
                              render_graph::RenderContext& graphContext) {
            const auto colorContext = colorTargetContext(graph, backbuffer);
            RenderFrameContext frameContext{
                context.frame,
                colorContext.colorTarget,
                nullptr,
                colorContext.colorTexture,
                nullptr,
            };
            rhi::DepthTargetDesc depthTarget;
            if (sceneDepth.id != 0u) {
                auto& depthTexture = graph.texture(sceneDepth);
                depthTarget = rhi::DepthTargetDesc{
                    "SceneDepth",
                    depthTexture.id(),
                    {depthTexture.desc().extent.width, depthTexture.desc().extent.height},
                    depthTexture.desc().format,
                    rhi::ResourceState::DepthAttachment,
                };
                frameContext.depthTarget = &depthTarget;
                frameContext.depthTexture = &depthTexture;
            }
            RenderPipelineContext pipelineContext{
                context.frame,
                frameContext.colorTarget,
                frameContext.depthTarget,
                frameContext.colorTexture,
                frameContext.depthTexture,
                context.request,
            };
            const auto recorded = recordGraph(graphContext, pipelineContext);
            if (!recorded.success) {
                std::string message = "Custom graph scene pass failed: ";
                message += recorded.diagnostics.c_str();
                throw std::runtime_error(message);
            }
        });
    }

    if (frameFeatures.particlesWithColor) {
        auto particles = graph.addPass("Particles");
        particles.read(backbuffer, rhi::ResourceState::ColorAttachment);
        writeColorAttachment(particles, backbuffer)
            .execute([&graph, backbuffer, callback = std::move(frameFeatures.particlesWithColor)](
                         render_graph::RenderContext& graphContext) {
                callback(graphContext, colorTargetContext(graph, backbuffer));
            });
    }

    if (frameFeatures.gui) {
        auto gui = graph.addPass("Gui");
        gui.read(backbuffer, rhi::ResourceState::ColorAttachment);
        writeColorAttachment(gui, backbuffer).execute(std::move(frameFeatures.gui));
    }

    graph.addPass("DebugOverlay")
        .read(backbuffer, rhi::ResourceState::Present)
        .execute(frameFeatures.debugOverlay ? std::move(frameFeatures.debugOverlay)
                                            : [](render_graph::RenderContext&) {});

    return RendererResult{true, ""};
}

} // namespace arp::renderer
