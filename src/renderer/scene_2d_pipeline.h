#pragma once
#include "renderer/pipeline/render_pipeline.h"
#include "renderer/scene_2d_renderer.h"

#include <cstring>

namespace arp::renderer {
struct Scene2DFramePayload {
    static constexpr const char* contract = "Scene2DFramePayload";
    static constexpr std::uint32_t version = 1;
    const scene2d::SpriteDrawPlan* sprites = nullptr;
    scene2d::Viewport2DMapping mapping;
    core::Vec2 camera{};
    float pixelsPerUnit = 0;
};

class Scene2DRenderPipeline final : public RenderPipeline {
public:
    Scene2DRenderPipeline(resource::ResourceManager& resources, Scene2DRendererDesc configuration)
        : resources_(resources), configuration_(std::move(configuration))
    {
        desc_.name = "Scene2D";
    }

    const RenderPipelineDesc& desc() const override { return desc_; }

    RendererResult create(const RenderPipelineCreateContext& context) override
    {
        if (renderer_)
            return {false, "ARP-2D-PIPELINE-ALREADY-CREATED"};
        configuration_.colorFormat = context.colorFormat;
        renderer_ = core::makeUnique<Scene2DRenderer>(context.device, resources_);
        auto result = renderer_->create(configuration_);
        if (!result.success) {
            renderer_.reset();
            return result;
        }
        desc_.colorFormat = context.colorFormat;
        desc_.extent = context.extent;
        return {true, {}};
    }

    RendererResult prepareAtlas(resource::ResourceHandle<scene2d::SpriteAtlasResource> atlas)
    {
        return renderer_ ? renderer_->prepareAtlas(atlas)
                         : RendererResult{false, "ARP-2D-PIPELINE-NOT-CREATED"};
    }

    bool readyForFrame(std::uint64_t index) const
    {
        return renderer_ && renderer_->readyForFrame(index);
    }

    std::uint32_t lastDrawCount() const { return renderer_ ? renderer_->lastDrawCount() : 0; }

    RendererResult resize(rhi::Extent2D extent) override
    {
        if (!extent.width || !extent.height)
            return {false, "ARP-2D-PIPELINE-EXTENT"};
        desc_.extent = extent;
        return {true, {}};
    }

    RendererResult render(RenderPipelineContext& context) override
    {
        const auto& request = context.request;
        if (!renderer_ || !context.colorTexture || !request.pipelinePayload
            || !request.pipelinePayloadContractName
            || std::strcmp(request.pipelinePayloadContractName, Scene2DFramePayload::contract) != 0
            || request.pipelinePayloadContractVersion != Scene2DFramePayload::version)
            return {false, "ARP-2D-PIPELINE-PAYLOAD"};
        const auto& payload = *static_cast<const Scene2DFramePayload*>(request.pipelinePayload);
        if (!payload.sprites)
            return {false, "ARP-2D-PIPELINE-PLAN"};
        return renderer_->draw(*context.colorTexture,
            *payload.sprites,
            payload.mapping,
            payload.camera,
            payload.pixelsPerUnit,
            request.view.frameIndex);
    }

private:
    resource::ResourceManager& resources_;
    Scene2DRendererDesc configuration_;
    RenderPipelineDesc desc_;
    core::UniquePtr<Scene2DRenderer> renderer_;
};
} // namespace arp::renderer
