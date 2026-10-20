#pragma once
#include "2d/sprite_draw_plan.h"
#include "renderer/materials/texture_upload.h"
#include "renderer/render_graph/render_graph.h"
#include "renderer/renderer_common.h"
#include "rhi/frontend/ordered_graphics_execution.h"

namespace arp::renderer {
struct Scene2DRendererDesc {
    core::pmr::String shaderPath{&core::defaultCpuMemoryResource()};
    rhi::TextureFormat colorFormat = rhi::TextureFormat::Unknown;
    std::uint32_t maxInstances = 0, maxAtlases = 0, frameSlots = 0;
    bool preserveColorAttachment = false;
};

struct Scene2DOrderedRendererDesc {
    std::uint32_t contractVersion = 0;
    std::uint32_t maxSprites = 0, maxAtlases = 0, frameSlots = 0;
    std::uint64_t maxVertexBytesPerSlot = 0;
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution;
    std::shared_ptr<rhi::RenderPipeline> pipeline;
    core::pmr::String shaderSourceSha256{&core::defaultCpuMemoryResource()};
};

struct Scene2DOrderedFrame {
    // Explicit world-to-clip affine camera; no native/backend coordinate inference.
    core::Vec4 projection;
    rhi::ScissorState clip;
    std::uint64_t frameIndex = 0;
};

class Scene2DRenderer final : public resource::ResourceLifecycleObserver {
public:
    Scene2DRenderer(rhi::GpuDevice& device, resource::ResourceManager& resources)
        : device_(device), resources_(resources)
    {
        resources_.addLifecycleObserver(*this);
    }

    ~Scene2DRenderer();
    RendererResult create(const Scene2DRendererDesc&);
    RendererResult createOrdered(const Scene2DOrderedRendererDesc&);
    RendererResult addOrderedPass(render_graph::FrameGraph&,
        render_graph::TextureHandle target,
        const scene2d::SpriteDrawPlan&,
        const Scene2DOrderedFrame&,
        rhi::LoadOp,
        rhi::Color clearColor = {});
    RendererResult prepareAtlas(resource::ResourceHandle<scene2d::SpriteAtlasResource> atlas);
    RendererResult draw(rhi::GpuTexture& target,
        const scene2d::SpriteDrawPlan& plan,
        const scene2d::Viewport2DMapping& mapping,
        core::Vec2 camera,
        float pixelsPerUnit,
        std::uint64_t frameIndex);
    RendererResult recordInto(rhi::CommandEncoder& encoder,
        rhi::GpuTexture& target,
        const scene2d::SpriteDrawPlan& plan,
        const scene2d::Viewport2DMapping& mapping,
        core::Vec2 camera,
        float pixelsPerUnit,
        std::uint64_t frameIndex);

    bool readyForFrame(std::uint64_t frameIndex) const
    {
        if (ordered_)
            return readyOrderedFrame(frameIndex);
        if (slots_.empty())
            return false;
        const auto& slot = slots_[frameIndex % slots_.size()];
        return !slot.pending || slot.completion->getValue() >= slot.submittedValue;
    }

    std::uint32_t lastDrawCount() const { return lastDrawCount_; }

    bool beforeResourceUnload(resource::NameView) noexcept override;
    bool beforeRetiredResourcesRelease() noexcept override;

private:
    struct OrderedState;
    bool readyOrderedFrame(std::uint64_t) const;
    bool releaseOrderedReferences() noexcept;
    bool unloadOrderedAtlas(resource::NameView) noexcept;
    RendererResult prepareOrderedAtlas(resource::ResourceHandle<scene2d::SpriteAtlasResource>);

    struct Atlas {
        resource::ResourceHandle<scene2d::SpriteAtlasResource> source;
        core::UniquePtr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
    };

    struct Slot {
        core::UniquePtr<rhi::GpuBuffer> instances;
        core::UniquePtr<rhi::GpuTimelineSemaphore> completion;
        std::uint64_t submittedValue = 0;
        core::UniquePtr<rhi::CommandBuffer> commands;
        bool pending = false;
    };

    rhi::GpuDevice& device_;
    resource::ResourceManager& resources_;
    Scene2DRendererDesc desc_;
    rhi::ShaderPackageDesc package_;
    core::UniquePtr<rhi::PipelineLayout> layout_;
    core::UniquePtr<rhi::RenderPipeline> pipeline_;
    core::UniquePtr<rhi::GpuSampler> sampler_;
    core::pmr::Vector<Atlas> atlases_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<Slot> slots_{&core::defaultCpuMemoryResource()};
    std::uint32_t lastDrawCount_ = 0;
    std::shared_ptr<OrderedState> ordered_;
};
} // namespace arp::renderer
