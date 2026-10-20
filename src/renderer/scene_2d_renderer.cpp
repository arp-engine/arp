#include "renderer/scene_2d_renderer.h"

#include "rhi/semantics/render_target_coordinates.h"

namespace arp::renderer {
namespace {
rhi::DescriptorLayoutDesc atlasLayout()
{
    return {"SpriteAtlasSet",
        {{0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment}}};
}

RendererResult failure(const char* id)
{
    return {false, id};
}
} // namespace

Scene2DRenderer::~Scene2DRenderer()
{
    resources_.removeLifecycleObserver(*this);
    for (auto& s : slots_)
        if (s.pending)
            s.completion->hostWait(s.submittedValue, UINT64_MAX);
}

bool Scene2DRenderer::beforeRetiredResourcesRelease() noexcept
{
    if (ordered_)
        return releaseOrderedReferences();
    for (auto& s : slots_)
        if (s.pending && s.completion->getValue() < s.submittedValue)
            return false;
    for (auto& s : slots_) {
        s.commands.reset();
        s.pending = false;
    }
    return true;
}

bool Scene2DRenderer::beforeResourceUnload(resource::NameView id) noexcept
{
    if (ordered_)
        return unloadOrderedAtlas(id);
    for (auto it = atlases_.begin(); it != atlases_.end(); ++it)
        if (it->source.id() == id) {
            if (!beforeRetiredResourcesRelease())
                return false;
            atlases_.erase(it);
            break;
        }
    return true;
}

RendererResult Scene2DRenderer::create(const Scene2DRendererDesc& d)
{
    if (pipeline_ || ordered_ || !d.maxInstances || !d.maxAtlases || !d.frameSlots
        || d.shaderPath.empty() || d.colorFormat == rhi::TextureFormat::Unknown)
        return failure("ARP-2D-RENDERER-CONFIG");
    if (device_.backend() == rhi::Backend::Null)
        return failure("ARP-2D-NULL-VALIDATOR-ONLY");
    const auto caps = device_.capabilities();
    if (!caps.core.graphics || !caps.features.timelineSync)
        return failure("ARP-2D-EXECUTION-CAPABILITY");
    desc_ = d;
    rhi::PipelineLayoutDesc layout;
    layout.descriptorLayouts.push_back(atlasLayout());
    layout.descriptorSets.push_back({0, "SpriteAtlasSet"});
    layout.pushConstantRanges.push_back(
        {rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0, 16});
    layout_ = device_.createPipelineLayout(layout);
    if (!layout_)
        return failure("ARP-2D-PIPELINE-LAYOUT");
    package_ = detail::makeGraphicsShaderPackage("Scene2D", d.shaderPath, 16);
    if (!package_.generatedReflectionHydrated)
        return failure("ARP-2D-SHADER-REFLECTION");
    package_.reflection.descriptorBindings.push_back(
        {0, 0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment});
    package_.reflection.descriptorBindings.push_back(
        {0, 1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment});
    auto pipeline =
        detail::makeRenderPipelineDesc("Scene2D", *layout_, package_, d.colorFormat, {1280, 720});
    pipeline.hasViewport = false;
    pipeline.hasScissor = false;
    pipeline.dynamicViewportScissor = true;
    pipeline.cullMode = rhi::CullMode::None;
    pipeline.frontFace = rhi::FrontFace::CounterClockwise;
    pipeline.topology = rhi::PrimitiveTopology::TriangleList;
    pipeline.depthFormat = rhi::TextureFormat::Unknown;
    rhi::ColorTargetState color;
    color.format = d.colorFormat;
    color.blendEnabled = true;
    color.writeMask = rhi::ColorWriteMask::All;
    color.colorBlend = {
        rhi::BlendFactor::One, rhi::BlendFactor::OneMinusSrcAlpha, rhi::BlendOp::Add};
    color.alphaBlend = color.colorBlend;
    pipeline.colorTargets.push_back(color);
    rhi::VertexBufferLayoutDesc stream;
    stream.slot = 0;
    stream.strideBytes = sizeof(scene2d::SpriteInstance);
    stream.inputRate = rhi::VertexInputRate::Instance;
    for (std::uint32_t i = 0; i < 6; ++i)
        stream.attributes.push_back({i, i * 16, rhi::VertexFormat::Float32x4});
    pipeline.vertexBuffers.push_back(std::move(stream));
    pipeline_ = device_.createRenderPipeline(pipeline);
    if (!pipeline_)
        return failure("ARP-2D-PIPELINE");
    sampler_ = device_.createSampler({"SpriteNearest",
        rhi::SamplerFilter::Nearest,
        rhi::SamplerFilter::Nearest,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::SamplerAddressMode::ClampToEdge,
        rhi::CompareOp::Never,
        rhi::SamplerMipFilter::None,
        0});
    if (!sampler_)
        return failure("ARP-2D-SAMPLER");
    atlases_.reserve(d.maxAtlases);
    slots_.reserve(d.frameSlots);
    for (std::uint32_t i = 0; i < d.frameSlots; ++i) {
        Slot slot;
        slot.instances = device_.createBuffer({"SpriteInstances",
            std::uint64_t(d.maxInstances) * sizeof(scene2d::SpriteInstance),
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead});
        slot.completion = device_.createTimelineSemaphore();
        if (!slot.instances || !slot.completion)
            return failure("ARP-2D-FRAME-SLOT");
        slots_.push_back(std::move(slot));
    }
    return {true, {}};
}

RendererResult Scene2DRenderer::prepareAtlas(
    resource::ResourceHandle<scene2d::SpriteAtlasResource> source)
{
    if (ordered_)
        return prepareOrderedAtlas(source);
    if (!source.ready() || !pipeline_)
        return failure("ARP-2D-ATLAS-NOT-READY");
    for (auto& a : atlases_)
        if (a.source.get() == source.get())
            return {true, {}};
    if (atlases_.size() >= desc_.maxAtlases)
        return failure("ARP-2D-ATLAS-CAPACITY");
    Atlas a;
    a.source = source;
    const auto& image = source.get()->image();
    a.texture = device_.createTexture({source.id(),
        {image.width, image.height, 1},
        image.srgb ? rhi::TextureFormat::Rgba8UnormSrgb : rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined});
    if (!a.texture)
        return failure("ARP-2D-ATLAS-TEXTURE");
    // RHI owns upload staging. Row pitch is explicit and backend neutral.
    constexpr auto alignment = rhi::kTextureUploadRowPitchAlignment;
    const auto unaligned = std::uint64_t(image.width) * 4;
    const auto aligned = (unaligned + alignment - 1) / alignment * alignment;
    if (aligned > UINT32_MAX || aligned * image.height > UINT32_MAX)
        return failure("ARP-2D-UPLOAD-BUDGET");
    const auto rowBytes = static_cast<std::uint32_t>(aligned);
    core::pmr::Vector<std::uint8_t> pixels(
        std::size_t(rowBytes) * image.height, 0, &core::defaultCpuMemoryResource());
    for (std::uint32_t y = 0; y < image.height; ++y)
        std::copy_n(image.rgba8.data() + std::size_t(y) * image.width * 4,
            image.width * 4,
            pixels.data() + std::size_t(y) * rowBytes);
    const auto uploaded = uploadTextureRgba8(
        device_, *a.texture, pixels.data(), image.width, image.height, rowBytes, image.height);
    if (!uploaded.success)
        return uploaded;
    a.view = device_.createTextureView(*a.texture,
        {"SpriteAtlasView",
            a.texture->desc().format,
            rhi::TextureUsage::Sampled,
            rhi::TextureViewDimension::Texture2D});
    a.arena = device_.createDescriptorArena({"SpriteAtlasArena", 1, 2});
    if (!a.arena)
        return failure("ARP-2D-DESCRIPTOR-ARENA");
    a.set = a.arena->allocateDescriptorSet({"SpriteAtlasSet", atlasLayout()});
    if (!a.view || !a.set)
        return failure("ARP-2D-ATLAS-BINDING");
    auto written = a.set->write({0,
        rhi::DescriptorType::SampledTexture,
        a.view->id(),
        rhi::DescriptorResourceKind::TextureView});
    if (!written.success)
        return {false, written.diagnostics};
    written = a.set->write(
        {1, rhi::DescriptorType::Sampler, sampler_->id(), rhi::DescriptorResourceKind::Sampler});
    if (!written.success)
        return {false, written.diagnostics};
    atlases_.push_back(std::move(a));
    return {true, {}};
}

RendererResult Scene2DRenderer::draw(rhi::GpuTexture& target,
    const scene2d::SpriteDrawPlan& plan,
    const scene2d::Viewport2DMapping& mapping,
    core::Vec2 camera,
    float ppu,
    std::uint64_t frameIndex)
{
    auto& slot = slots_[frameIndex % slots_.size()];
    if (slot.pending && slot.completion->getValue() < slot.submittedValue)
        return RendererResult::deferred("2D frame slot is still in flight", "ARP-2D-FRAME-PENDING");
    slot.commands.reset();
    slot.pending = false;
    auto encoder = device_.createCommandEncoder({"Scene2D"});
    if (!encoder)
        return failure("ARP-2D-ENCODER");
    auto transition =
        encoder->transition(target, {}, target.state(), rhi::ResourceState::ColorAttachment);
    if (!transition.success)
        return {false, transition.diagnostics};
    auto result = recordInto(*encoder, target, plan, mapping, camera, ppu, frameIndex);
    if (!result.success)
        return result;
    transition = encoder->transition(
        target, {}, rhi::ResourceState::ColorAttachment, rhi::ResourceState::Present);
    if (!transition.success)
        return {false, transition.diagnostics};
    slot.commands = encoder->finish();
    if (!slot.commands)
        return {false, encoder->lastValidation().diagnostics};
    const auto submitted = device_.queue().submit(
        *slot.commands, nullptr, 0, slot.completion.get(), ++slot.submittedValue);
    slot.pending = submitted.hasAcceptedWork();
    return {submitted.success, submitted.diagnostics};
}

RendererResult Scene2DRenderer::recordInto(rhi::CommandEncoder& encoder,
    rhi::GpuTexture& target,
    const scene2d::SpriteDrawPlan& plan,
    const scene2d::Viewport2DMapping& mapping,
    core::Vec2 camera,
    float ppu,
    std::uint64_t frameIndex)
{
    lastDrawCount_ = 0;
    if (ordered_)
        return failure("ARP-2D-ORDERED-GRAPH-ENTRY-REQUIRED");
    if (!pipeline_ || !mapping.valid() || !std::isfinite(ppu) || ppu <= 0 || plan.diagnostic()
        || plan.instances().size() > desc_.maxInstances
        || target.desc().format != desc_.colorFormat)
        return failure("ARP-2D-DRAW-CONFIG");
    auto& slot = slots_[frameIndex % slots_.size()];
    if (slot.pending && slot.completion->getValue() < slot.submittedValue)
        return RendererResult::deferred("2D frame slot is still in flight", "ARP-2D-FRAME-PENDING");
    for (const auto& batch : plan.batches()) {
        if (!batch.atlas.ready())
            return failure("ARP-2D-DRAW-ATLAS-STALE");
        if (std::none_of(atlases_.begin(), atlases_.end(), [&](const auto& a) {
                return a.source.get() == batch.atlas.get();
            }))
            return failure("ARP-2D-DRAW-ATLAS-UNPREPARED");
    }
    if (!plan.instances().empty()) {
        auto written = device_.writeBuffer(*slot.instances,
            plan.instances().data(),
            plan.instances().size() * sizeof(scene2d::SpriteInstance));
        if (!written.success)
            return {false, written.diagnostics};
    }
    rhi::RenderPassDesc passDesc;
    passDesc.label = "Scene2D";
    passDesc.colorTargetName = target.desc().label;
    passDesc.colorAttachments.push_back({"Scene2DColor",
        target.id(),
        target.desc().format,
        desc_.preserveColorAttachment ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
        rhi::StoreOp::Store,
        false,
        {},
        {0.012f, 0.018f, 0.025f, 1}});
    auto pass = encoder.beginRenderPass(passDesc);
    if (!pass)
        return {false, encoder.lastValidation().diagnostics};
    const auto w = target.desc().extent.width, h = target.desc().extent.height;
    const float sx = 2 * ppu * mapping.scale() / w;
    const float canonicalY = -2 * ppu * mapping.scale() / h;
    const float sy =
        rhi::semantics::projectionRequiresYFlip(device_.backend()) ? -canonicalY : canonicalY;
    core::Vec4 push{sx, sy, -camera.x * sx, -camera.y * sy};
    rhi::CommandValidationResult status = pass->setPipeline(*pipeline_);
    if (status.success)
        status = pass->setViewport({0, 0, float(w), float(h), 0, 1});
    const auto origin = mapping.origin();
    if (status.success)
        status = pass->setScissor({std::int32_t(origin.x),
            std::int32_t(origin.y),
            std::uint32_t(w - 2 * origin.x),
            std::uint32_t(h - 2 * origin.y)});
    if (status.success)
        status = pass->pushConstants(
            rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment, 0, 16, &push);
    for (const auto& batch : plan.batches()) {
        if (!status.success)
            break;
        for (auto& a : atlases_)
            if (a.source.get() == batch.atlas.get()) {
                status = pass->bindDescriptorSet(0, *a.set);
                break;
            }
        if (status.success)
            status = pass->bindVertexBuffer(
                0, *slot.instances, std::uint64_t(batch.first) * sizeof(scene2d::SpriteInstance));
        if (status.success)
            status = pass->draw(6, batch.count, 0, 0);
        if (status.success)
            ++lastDrawCount_;
    }
    auto ended = pass->end();
    if (!status.success)
        return {false, status.diagnostics};
    if (!ended.success)
        return {false, ended.diagnostics};
    return {true, {}};
}
} // namespace arp::renderer
