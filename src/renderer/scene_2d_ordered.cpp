#include "2d/sprite_vertex_draw_plan.h"
#include "core/diagnostics/diagnostic_error.h"
#include "renderer/scene_2d_renderer.h"
#include "rhi/frontend/descriptor_set_validation.h"

namespace arp::renderer {
struct Scene2DRenderer::OrderedState {
    struct Atlas {
        resource::ResourceHandle<scene2d::SpriteAtlasResource> source;
        std::shared_ptr<rhi::GpuTexture> texture;
        std::shared_ptr<rhi::GpuSampler> sampler;
        core::UniquePtr<rhi::GpuTextureView> view;
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
    };

    struct Draw {
        std::shared_ptr<Atlas> atlas;
        std::uint32_t first = 0, count = 0;
    };

    struct Slot {
        std::shared_ptr<rhi::GpuBuffer> buffer;
        core::pmr::Vector<Draw> draws{&core::defaultCpuMemoryResource()};
    };

    explicit OrderedState(const Scene2DOrderedRendererDesc& d)
        : execution(d.execution), pipeline(d.pipeline), maxAtlases(d.maxAtlases),
          vertices({1, d.maxSprites, d.maxVertexBytesPerSlot})
    {}

    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution;
    std::shared_ptr<rhi::RenderPipeline> pipeline;
    std::uint32_t maxAtlases;
    scene2d::SpriteVertexDrawPlan vertices;
    std::shared_ptr<rhi::GpuSampler> sampler;
    core::pmr::Vector<std::shared_ptr<Atlas>> atlases{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<Slot>> slots{&core::defaultCpuMemoryResource()};
};

RendererResult Scene2DRenderer::createOrdered(const Scene2DOrderedRendererDesc& d)
{
    if (pipeline_ || ordered_ || d.contractVersion != 1 || !d.execution || !d.pipeline
        || !d.maxSprites || !d.maxAtlases || !d.frameSlots)
        return {false, "ARP-2D-ORDERED-CONFIG"};
    const char* id = d.execution->diagnostic(device_);
    const auto* definition = id ? nullptr : d.execution->pipelineDefinition(*d.pipeline, id);
    if (!definition)
        return {false, id ? id : "ARP-2D-ORDERED-PIPELINE-OWNER"};
    const auto& p = *definition;
    if (d.shaderSourceSha256.size() != 64
        || !std::all_of(d.shaderSourceSha256.begin(),
            d.shaderSourceSha256.end(),
            [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            })
        || d.pipeline->shaderReflection().sourceSha256 != d.shaderSourceSha256)
        return {false, "ARP-2D-ORDERED-SHADER-SOURCE-IDENTITY"};
    const auto* layout = d.pipeline->layout();
    if (!layout || p.colorFormat != rhi::TextureFormat::Rgba8Unorm || p.colorTargets.size() != 1
        || p.depthFormat != rhi::TextureFormat::Unknown
        || p.depthStencil.format != rhi::TextureFormat::Unknown || p.depthStencil.depthTestEnabled
        || p.depthStencil.depthWriteEnabled || p.depthStencil.stencilEnabled
        || p.cullMode != rhi::CullMode::None || p.topology != rhi::PrimitiveTopology::TriangleList
        || p.hasViewport || p.hasScissor || !p.dynamicViewportScissor
        || p.multisample.sampleCount != 1 || p.vertexBuffers.size() != 1
        || p.vertexShader.entryPoint != "vertexMainExpanded"
        || p.fragmentShader.entryPoint != "fragmentMain"
        || layout->desc().descriptorLayouts.size() != 1 || layout->desc().descriptorSets.size() != 1
        || !layout->desc().descriptorTables.empty() || layout->desc().descriptorSets[0].set != 0
        || layout->pushConstantByteCount() != 16)
        return {false, "ARP-2D-ORDERED-PIPELINE-CONTRACT"};
    const auto& color = p.colorTargets[0];
    for (const auto blend : {color.colorBlend, color.alphaBlend})
        if (blend.sourceFactor != rhi::BlendFactor::One
            || blend.destinationFactor != rhi::BlendFactor::OneMinusSrcAlpha
            || blend.operation != rhi::BlendOp::Add)
            return {false, "ARP-2D-ORDERED-BLEND-CONTRACT"};
    if (color.format != p.colorFormat || !color.blendEnabled
        || color.writeMask != rhi::ColorWriteMask::All || color.resolveTargetRequired)
        return {false, "ARP-2D-ORDERED-BLEND-CONTRACT"};
    const auto& stream = p.vertexBuffers[0];
    if (stream.slot != 0 || stream.strideBytes != sizeof(scene2d::SpriteVertex)
        || stream.inputRate != rhi::VertexInputRate::Vertex || stream.attributes.size() != 3)
        return {false, "ARP-2D-ORDERED-VERTEX-CONTRACT"};
    const std::array formats{
        rhi::VertexFormat::Float32x2, rhi::VertexFormat::Float32x2, rhi::VertexFormat::Float32x4};
    const std::array offsets{0u, 8u, 16u};
    for (std::size_t i = 0; i < formats.size(); ++i)
        if (stream.attributes[i].shaderLocation != i
            || stream.attributes[i].offsetBytes != offsets[i]
            || stream.attributes[i].format != formats[i])
            return {false, "ARP-2D-ORDERED-VERTEX-CONTRACT"};
    const rhi::DescriptorLayoutDesc atlasLayout{"SpriteAtlasSet",
        {{0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment}}};
    if (!rhi::descriptorLayoutDefinitionMatches(atlasLayout, layout->desc().descriptorLayouts[0])
        || layout->desc().descriptorSets[0].layoutName != atlasLayout.name)
        return {false, "ARP-2D-ORDERED-ATLAS-LAYOUT"};
    try {
        auto state = core::makeShared<OrderedState>(d);
        rhi::SamplerDesc sampler;
        sampler.label = "SpriteOrderedNearest";
        sampler.minFilter = sampler.magFilter = rhi::SamplerFilter::Nearest;
        sampler.mipFilter = rhi::SamplerMipFilter::None;
        state->sampler = core::shareUnique(device_.createSampler(sampler));
        if (!state->sampler)
            return {false, "ARP-2D-ORDERED-SAMPLER"};
        state->atlases.reserve(d.maxAtlases);
        state->slots.reserve(d.frameSlots);
        for (std::uint32_t i = 0; i < d.frameSlots; ++i) {
            auto slot = core::makeShared<OrderedState::Slot>();
            slot->draws.reserve(d.maxSprites);
            slot->buffer = core::shareUnique(device_.createBuffer({"SpriteOrderedVertices",
                std::uint64_t{d.maxSprites} * core::QuadTriangleTopology::vertexCount
                    * sizeof(scene2d::SpriteVertex),
                rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
                rhi::MemoryUsage::CpuToGpu,
                rhi::ResourceState::ShaderRead}));
            if (!slot->buffer)
                return {false, "ARP-2D-ORDERED-FRAME-SLOT"};
            state->slots.push_back(std::move(slot));
        }
        ordered_ = std::move(state);
        return {true, {}};
    } catch (const core::DiagnosticError& error) {
        return {false, core::toPmrString(error.what())};
    } catch (const std::bad_alloc&) {
        return {false, "ARP-2D-ORDERED-CPU-ALLOCATION"};
    }
}

bool Scene2DRenderer::readyOrderedFrame(std::uint64_t frame) const
{
    const auto& state = *ordered_;
    const auto& slot = state.slots[frame % state.slots.size()];
    return !state.execution->diagnostic(device_) && slot.use_count() == 1
        && !state.execution->resourceReuseDiagnostic(*slot->buffer);
}

bool Scene2DRenderer::releaseOrderedReferences() noexcept
{
    auto& state = *ordered_;
    if (state.execution->diagnostic(device_))
        return false;
    for (const auto& slot : state.slots)
        if (slot.use_count() != 1 || state.execution->resourceReuseDiagnostic(*slot->buffer))
            return false;
    for (const auto& atlas : state.atlases)
        if (state.execution->resourceReuseDiagnostic(*atlas->texture))
            return false;
    for (auto& slot : state.slots)
        slot->draws.clear();
    return true;
}

bool Scene2DRenderer::unloadOrderedAtlas(resource::NameView id) noexcept
{
    auto& state = *ordered_;
    const auto found =
        std::find_if(state.atlases.begin(), state.atlases.end(), [&](const auto& atlas) {
            const auto* source = atlas->source.get();
            return source && source->reference().id == id;
        });
    if (found == state.atlases.end())
        return true;
    if (!releaseOrderedReferences())
        return false;
    state.atlases.erase(found);
    return true;
}

RendererResult Scene2DRenderer::prepareOrderedAtlas(
    resource::ResourceHandle<scene2d::SpriteAtlasResource> source)
try {
    auto& state = *ordered_;
    if (const auto* id = state.execution->diagnostic(device_))
        return {false, id};
    if (!source.ready())
        return {false, "ARP-2D-ATLAS-NOT-READY"};
    for (const auto& atlas : state.atlases)
        if (atlas->source.get() == source.get())
            return {true, {}};
    if (state.atlases.size() >= state.maxAtlases)
        return {false, "ARP-2D-ATLAS-CAPACITY"};
    const auto& image = source.get()->image();
    if (image.srgb || !image.width || !image.height || image.width > UINT32_MAX / 4
        || image.rgba8.size() != std::uint64_t{image.width} * image.height * 4)
        return {false, "ARP-2D-ORDERED-ATLAS-LINEAR-RGBA8-REQUIRED"};
    auto atlas = core::makeShared<OrderedState::Atlas>();
    atlas->source = source;
    atlas->sampler = state.sampler;
    atlas->texture = core::shareUnique(device_.createTexture({source.id(),
        {image.width, image.height, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead}));
    if (!atlas->texture)
        return {false, "ARP-2D-ATLAS-TEXTURE"};
    rhi::semantics::ClientTextureUpload upload;
    upload.contractVersion = 1;
    upload.extent = {image.width, image.height, 1};
    upload.bytesPerRow = image.width * 4;
    upload.bytes = image.rgba8;
    const auto uploaded = state.execution->uploadTexture(*atlas->texture, upload);
    if (!uploaded.success)
        return {false, uploaded.diagnostics};
    atlas->view = device_.createTextureView(*atlas->texture,
        {"SpriteOrderedAtlasView",
            rhi::TextureFormat::Rgba8Unorm,
            rhi::TextureUsage::Sampled,
            rhi::TextureViewDimension::Texture2D});
    atlas->arena = device_.createDescriptorArena({"SpriteOrderedAtlasArena", 1, 2});
    if (!atlas->view || !atlas->arena)
        return {false, "ARP-2D-ATLAS-BINDING"};
    atlas->set = atlas->arena->allocateDescriptorSet(
        {"SpriteAtlasSet", state.pipeline->layout()->desc().descriptorLayouts[0]});
    if (!atlas->set)
        return {false, "ARP-2D-ATLAS-BINDING"};
    auto status = atlas->set->write({0,
        rhi::DescriptorType::SampledTexture,
        atlas->view->id(),
        rhi::DescriptorResourceKind::TextureView});
    if (status.success)
        status = atlas->set->write({1,
            rhi::DescriptorType::Sampler,
            state.sampler->id(),
            rhi::DescriptorResourceKind::Sampler});
    if (!status.success)
        return {false, status.diagnostics};
    state.atlases.push_back(std::move(atlas));
    return {true, {}};
} catch (const core::DiagnosticError& error) {
    return {false, core::toPmrString(error.what())};
} catch (const std::bad_alloc&) {
    return {false, "ARP-2D-ORDERED-CPU-ALLOCATION"};
}

RendererResult Scene2DRenderer::addOrderedPass(render_graph::FrameGraph& graph,
    render_graph::TextureHandle target,
    const scene2d::SpriteDrawPlan& plan,
    const Scene2DOrderedFrame& frame,
    rhi::LoadOp load,
    rhi::Color clear)
{
    if (!ordered_ || !readyOrderedFrame(frame.frameIndex))
        return {false, "ARP-2D-ORDERED-FRAME-RESERVED"};
    const auto& texture = graph.texture(target);
    const auto extent = texture.desc().extent;
    if (texture.desc().format != rhi::TextureFormat::Rgba8Unorm || !extent.width || !extent.height
        || !frame.clip.width || !frame.clip.height || frame.clip.x < 0 || frame.clip.y < 0
        || std::uint64_t{static_cast<std::uint32_t>(frame.clip.x)} + frame.clip.width > extent.width
        || std::uint64_t{static_cast<std::uint32_t>(frame.clip.y)} + frame.clip.height
            > extent.height
        || !std::isfinite(frame.projection.x) || !std::isfinite(frame.projection.y)
        || !std::isfinite(frame.projection.z) || !std::isfinite(frame.projection.w))
        return {false, "ARP-2D-ORDERED-FRAME-CONTRACT"};
    auto& state = *ordered_;
    if (!state.vertices.build(plan))
        return {false, state.vertices.diagnostic()};
    auto slot = state.slots[frame.frameIndex % state.slots.size()];
    slot->draws.clear();
    for (const auto& batch : state.vertices.batches()) {
        const auto atlas =
            std::find_if(state.atlases.begin(), state.atlases.end(), [&](const auto& a) {
                return a->source.get() == batch.atlas.get();
            });
        if (atlas == state.atlases.end()) {
            slot->draws.clear();
            return {false, "ARP-2D-DRAW-ATLAS-UNPREPARED"};
        }
        slot->draws.push_back({*atlas, batch.firstVertex, batch.vertexCount});
    }
    if (!state.vertices.vertices().empty()) {
        const auto written = device_.writeBuffer(*slot->buffer,
            state.vertices.vertices().data(),
            state.vertices.vertices().size() * sizeof(scene2d::SpriteVertex));
        if (!written.success)
            return {false, written.diagnostics};
    }
    const auto vertex = graph.importBufferRef(slot->buffer);
    auto pass = graph.addPass("Scene2DOrdered");
    pass.access(vertex,
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::VertexAttributeRead},
            0,
            slot->buffer->desc().sizeBytes)
        .writeAttachment(
            target, rhi::ResourceState::ColorAttachment, load, rhi::StoreOp::Store, {}, clear);
    for (const auto& draw : slot->draws)
        pass.read(graph.importTextureRef(draw.atlas->texture), rhi::ResourceState::ShaderRead);
    auto pipeline = state.pipeline;
    auto issuer = state.execution;
    pass.executeRenderPass([slot = std::move(slot), pipeline, issuer, frame, extent](
                               render_graph::RenderPassContext& context) {
        auto& encoder = context.renderPass();
        core::requireSuccessfulResult(encoder.setPipeline(*pipeline), "ARP-2D-ORDERED-PIPELINE");
        core::requireSuccessfulResult(
            encoder.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
            "ARP-2D-ORDERED-VIEWPORT");
        core::requireSuccessfulResult(encoder.setScissor(frame.clip), "ARP-2D-ORDERED-CLIP");
        core::requireSuccessfulResult(
            encoder.pushConstants(
                rhi::ShaderStage::Vertex, 0, sizeof(frame.projection), &frame.projection),
            "ARP-2D-ORDERED-PROJECTION");
        core::requireSuccessfulResult(
            encoder.bindVertexBuffer(0, *slot->buffer), "ARP-2D-ORDERED-VERTEX");
        for (const auto& draw : slot->draws) {
            if (!draw.atlas->source.ready())
                throw core::DiagnosticError("ARP-2D-DRAW-ATLAS-STALE");
            core::requireSuccessfulResult(
                encoder.bindDescriptorSet(0, *draw.atlas->set), "ARP-2D-ORDERED-ATLAS");
            core::requireSuccessfulResult(
                encoder.draw(draw.count, 1, draw.first, 0), "ARP-2D-ORDERED-DRAW");
        }
    });
    lastDrawCount_ = static_cast<std::uint32_t>(state.vertices.batches().size());
    return {true, {}};
}
} // namespace arp::renderer
