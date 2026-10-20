#include "portable_renderer.h"

#include "core/diagnostic_error.h"
#include "core/quad_geometry.h"
#include "renderer/gui/bitmap_text_resolver.h"

namespace arp::build_a_queen {
using namespace rhi;

namespace {
constexpr const char* kFontAtlas = "00000000-0000-4000-8000-000000000311";
constexpr const char* kFont = "00000000-0000-4000-8000-000000000312";

template <class Result> void require(const Result& r)
{
    core::requireSuccessfulResult(r, "QUEEN-ORDERED-DRAW");
}
} // namespace

PortableQueenRenderer::PortableQueenRenderer(GpuDevice& device, PortableQueenDesc desc)
    : device_(device), config_(std::move(desc)), gui_(device)
{}

std::shared_ptr<GpuTexture> PortableQueenRenderer::target(
    const char* name, Extent2D extent, bool depth)
{
    auto texture = core::shareUnique(device_.createTexture({name,
        {extent.width, extent.height, 1},
        depth ? TextureFormat::Depth24Stencil8 : TextureFormat::Rgba8Unorm,
        TextureUsage::RenderAttachment | TextureUsage::Sampled,
        MemoryUsage::GpuOnly,
        ResourceState::Undefined}));
    if (!texture)
        throw core::DiagnosticError("QUEEN-ORDERED-TARGET");
    return texture;
}

renderer::RendererResult PortableQueenRenderer::create(const core::pmr::String& directory)
try {
    auto result = content_.createContent(directory);
    if (!result.success)
        return result;
    result = hud_.createHud(PortableQueenDesc::kExtent);
    if (!result.success)
        return result;
    const auto buffer = [&](const char* name, std::size_t count, std::size_t stride) {
        auto resource = core::shareUnique(device_.createBuffer({name,
            count * stride,
            ResourceUsage::VertexBuffer | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead}));
        if (!resource)
            throw core::DiagnosticError("QUEEN-ORDERED-VERTEX-BUFFER");
        return resource;
    };
    world_ = buffer("QueenWorld", content_.vertexCapacity(), sizeof(SceneContent::Vertex));
    preview_ =
        buffer("QueenPreview", content_.previewVertexCapacity(), sizeof(SceneContent::Vertex));
    corners_ =
        buffer("QueenSkyCorners", core::QuadTriangleTopology::corners.size(), sizeof(core::Vec2));
    require(device_.writeBuffer(*corners_,
        core::QuadTriangleTopology::corners.data(),
        sizeof(core::QuadTriangleTopology::corners)));
    color_ = target("QueenColor", PortableQueenDesc::kExtent, false);
    depth_ = target("QueenDepth", PortableQueenDesc::kExtent, true);
    previewColor_ = target("QueenPreviewColor", {300, 184}, false);
    previewDepth_ = target("QueenPreviewDepth", {300, 184}, true);
    const Extent2D shadow{PortableQueenDesc::kShadowSize, PortableQueenDesc::kShadowSize};
    shadowColor_ = target("QueenShadowColor", shadow, false);
    shadowDepth_ = target("QueenShadowDepth", shadow, true);
    TextureViewDesc view;
    view.label = "QueenShadowSample";
    view.format = TextureFormat::Depth24Stencil8;
    view.usage = TextureUsage::Sampled;
    view.aspect = TextureAspect::Depth;
    shadowView_ = device_.createTextureView(*shadowDepth_, view);
    SamplerDesc sampler;
    sampler.minFilter = sampler.magFilter = SamplerFilter::Nearest;
    sampler.mipFilter = SamplerMipFilter::None;
    shadowSampler_ = device_.createSampler(sampler);
    shadowArena_ = device_.createDescriptorArena({"QueenShadowArena", 1, 2});
    if (!shadowView_ || !shadowSampler_ || !shadowArena_)
        return {false, "QUEEN-ORDERED-SHADOW-RESOURCES"};
    shadowSet_ = shadowArena_->allocateDescriptorSet(
        {"QueenShadowReceiverSet", config_.pipelines[2]->layout()->desc().descriptorLayouts[0]});
    if (!shadowSet_)
        return {false, "QUEEN-ORDERED-SHADOW-SET"};
    require(shadowSet_->write({0,
        DescriptorType::SampledTexture,
        shadowView_->id(),
        DescriptorResourceKind::TextureView}));
    require(shadowSet_->write(
        {1, DescriptorType::Sampler, shadowSampler_->id(), DescriptorResourceKind::Sampler}));

    auto quota = core::makeShared<core::CpuMemoryQuota>(4 * 1024 * 1024);
    result = gui_.create({1,
        PortableQueenDesc::kUiCommands,
        4,
        1,
        quota->limitBytes(),
        quota,
        config_.execution,
        config_.pipelines[3],
        config_.pipelines[3]->shaderReflection().sourceSha256,
        {1, false, false}});
    if (!result.success)
        return result;
    core::requireSuccessfulResult(resources_.load<scene2d::SpriteAtlasResource>({},
                                      core::pmr::String(kFontAtlas),
                                      core::pmr::String("/assets/web/font.atlas.yaml"),
                                      core::pmr::String("/assets/web/font.png"),
                                      1024 * 1024),
        "QUEEN-FONT-ATLAS");
    core::requireSuccessfulResult(resources_.load<ui::UiBitmapFontResource>({},
                                      core::pmr::String(kFont),
                                      core::pmr::String("/assets/web/font.yaml"),
                                      core::pmr::String(kFontAtlas),
                                      128 * 1024,
                                      95),
        "QUEEN-FONT");
    font_ = resources_.find<ui::UiBitmapFontResource>(kFont).get();
    result = gui_.registerBitmapFont(*font_);
    if (!result.success)
        return result;
    auto white = core::shareUnique(device_.createTexture({"QueenUiSolidWhite",
        {1, 1, 1},
        TextureFormat::Rgba8Unorm,
        TextureUsage::Sampled | TextureUsage::CopyDst,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead}));
    if (!white)
        return {false, "QUEEN-UI-SOLID-TEXTURE"};
    const std::array<std::uint8_t, 4> pixel{255, 255, 255, 255};
    semantics::ClientTextureUpload upload;
    upload.contractVersion = 1;
    upload.extent = {1, 1, 1};
    upload.bytesPerRow = 4;
    upload.bytes = pixel;
    require(config_.execution->uploadTexture(*white, upload));
    core::requireSuccessfulResult(gui_.registerTexture({"white", white}), "QUEEN-UI-WHITE");
    for (const auto& binding : std::array<std::pair<const char*, std::shared_ptr<GpuTexture>>, 2>{
             std::pair{"world", color_}, std::pair{"preview", previewColor_}})
        core::requireSuccessfulResult(gui_.registerTexture({binding.first,
                                          binding.second,
                                          renderer::GuiSampleAlpha::Straight,
                                          config_.attachmentSampleFlipY,
                                          false,
                                          true,
                                          {}}),
            "QUEEN-UI-3D-IMAGE");
    const auto focus = core::Vec3{0, 1, 3};
    const auto direction = core::normalize(core::Vec3{.42f, .78f, -.46f});
    light_ = core::multiply(core::orthographic(8, 12, .1f, 40),
        core::lookAt(focus + core::scale(direction, 18), focus));
    resolved_.reserve(PortableQueenDesc::kUiCommands);
    composed_.reserve(PortableQueenDesc::kUiCommands);
    return {true, {}};
} catch (const std::exception& error) {
    return {false, error.what()};
}

renderer::RendererResult PortableQueenRenderer::update(const Game& game, float seconds)
{
    if (!content_.prepareFrame(game.snapshot(),
            game.levelDesc(),
            game.pickups(),
            game.resolvedPickups(),
            game.pickupCount(),
            seconds,
            frame_))
        return {false, "QUEEN-PORTABLE-FRAME-CONTENT"};
    if (const auto* id = config_.execution->resourceReuseDiagnostic(*world_))
        return {false, id};
    if (const auto* id = config_.execution->resourceReuseDiagnostic(*preview_))
        return {false, id};
    const auto world = device_.writeBuffer(
        *world_, frame_.world.data(), frame_.world.size() * sizeof(SceneContent::Vertex));
    if (!world.success)
        return {false, world.diagnostics};
    const auto preview = device_.writeBuffer(
        *preview_, frame_.preview.data(), frame_.preview.size() * sizeof(SceneContent::Vertex));
    if (!preview.success)
        return {false, preview.diagnostics};
    const auto hud = hud_.prepareHud(game.snapshot(), game.levelDesc());
    if (!hud.success)
        return hud;
    return renderer::BitmapTextResolver::resolve(
        hud_.commands(), *font_, PortableQueenDesc::kUiCommands - 2, resolved_);
}

core::ArrayView<std::shared_ptr<RenderPipeline>> PortableQueenRenderer::pipelines() const noexcept
{
    return config_.pipelines;
}

void PortableQueenRenderer::draw(render_graph::RenderPassContext& context,
    std::uint32_t pipeline,
    const GpuBuffer& vertices,
    std::uint32_t count,
    const core::Mat4& camera,
    bool shadows,
    Extent2D extent)
{
    auto& pass = context.renderPass();
    require(pass.setPipeline(*config_.pipelines[pipeline]));
    require(pass.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}));
    require(pass.setScissor({0, 0, extent.width, extent.height}));
    require(pass.bindVertexBuffer(0, vertices, 0));
    if (pipeline == 1)
        require(pass.pushConstants(ShaderStage::Vertex, 0, sizeof(camera), &camera));
    if (pipeline == 2) {
        struct Push {
            core::Mat4 camera, light;
            float shadow[4];
        };

        static_assert(sizeof(Push) == 144);
        const Push push{camera,
            light_,
            {1.0f / PortableQueenDesc::kShadowSize,
                1.0f / PortableQueenDesc::kShadowSize,
                shadows ? 1.0f : 0.0f,
                config_.attachmentSampleFlipY ? 1.0f : 0.0f}};
        require(pass.bindDescriptorSet(0, *shadowSet_));
        require(pass.pushConstants(ShaderStage::Vertex, 0, sizeof(push), &push));
    }
    require(pass.draw(count, 1, 0, 0));
}

void PortableQueenRenderer::build(
    render_graph::FrameGraph& graph, render_graph::TextureHandle canvas)
{
    const auto color = graph.importTextureRef(color_), depth = graph.importTextureRef(depth_);
    const auto shadowColor = graph.importTextureRef(shadowColor_),
               shadowDepth = graph.importTextureRef(shadowDepth_);
    const auto previewColor = graph.importTextureRef(previewColor_),
               previewDepth = graph.importTextureRef(previewDepth_);
    const auto world = graph.importBufferRef(world_), preview = graph.importBufferRef(preview_);
    const auto corners = graph.importBufferRef(corners_);
    const semantics::ResourceAccessScope vertexAccess{
        ResourceState::ShaderRead, PipelineStage::VertexInput, ResourceAccess::VertexAttributeRead};
    const auto extent = PortableQueenDesc::kExtent;
    graph.addPass("QueenRealShapeShadow")
        .writeAttachment(shadowColor, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store)
        .writeAttachment(shadowDepth, ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store)
        .access(world, vertexAccess, 0, frame_.world.size() * sizeof(SceneContent::Vertex))
        .executeRenderPass([this](render_graph::RenderPassContext& context) {
            draw(context,
                1,
                *world_,
                worldVertices(),
                light_,
                false,
                {PortableQueenDesc::kShadowSize, PortableQueenDesc::kShadowSize});
        });
    graph.addPass("QueenSky")
        .access(corners, vertexAccess, 0, sizeof(core::QuadTriangleTopology::corners))
        .writeAttachment(color, ResourceState::ColorAttachment, LoadOp::Clear, StoreOp::Store)
        .writeAttachment(depth, ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store)
        .executeRenderPass([this, extent](render_graph::RenderPassContext& context) {
            draw(context, 0, *corners_, 6, {}, false, extent);
        });
    const auto camera =
        core::multiply(core::perspective(50, float(extent.width) / extent.height, .1f, 60),
            core::lookAt({0, 4.8f, -7.5f}, {0, 1, 3}));
    graph.addPass("QueenAnimatedWorld")
        .writeAttachment(color, ResourceState::ColorAttachment, LoadOp::Load, StoreOp::Store)
        .writeAttachment(depth, ResourceState::DepthAttachment, LoadOp::Load, StoreOp::Store)
        .read(shadowDepth, ResourceState::ShaderRead)
        .access(world, vertexAccess, 0, frame_.world.size() * sizeof(SceneContent::Vertex))
        .executeRenderPass([this, camera, extent](render_graph::RenderPassContext& context) {
            draw(context, 2, *world_, worldVertices(), camera, true, extent);
        });
    const auto previewCamera = core::multiply(
        core::perspective(38, 300.0f / 184, .1f, 12), core::lookAt({0, 1.4f, -3.4f}, {0, .9f, 0}));
    graph.addPass("QueenFrontFacing3DLook")
        .writeAttachment(previewColor,
            ResourceState::ColorAttachment,
            LoadOp::Clear,
            StoreOp::Store,
            {},
            {.20f, .08f, .15f, 1})
        .writeAttachment(
            previewDepth, ResourceState::DepthAttachment, LoadOp::Clear, StoreOp::Store)
        .read(shadowDepth, ResourceState::ShaderRead)
        .access(preview, vertexAccess, 0, frame_.preview.size() * sizeof(SceneContent::Vertex))
        .executeRenderPass([this, previewCamera](render_graph::RenderPassContext& context) {
            draw(context, 2, *preview_, previewVertices(), previewCamera, false, {300, 184});
        });
    composed_.clear();
    const auto image = [&](const char* id, ui::LayoutRect rect) {
        renderer::ui::UiDrawCommand command;
        command.kind = renderer::ui::UiDrawCommandKind::Image;
        command.rect = rect;
        command.clipRect = {0, 0, float(extent.width), float(extent.height)};
        command.textureResourceId = id;
        command.textureSource = renderer::ui::UiTextureSource::External;
        command.hasTextureSourceRect = true;
        command.textureSourceRect = {0, 0, 1, 1};
        std::fill_n(command.color, 4, 1);
        composed_.push_back(std::move(command));
    };
    image("world", {0, 0, float(extent.width), float(extent.height)});
    image("preview", {956, 68, 300, 184});
    composed_.insert(composed_.end(), resolved_.begin(), resolved_.end());
    core::requireSuccessfulResult(
        gui_.addPass(graph, canvas, composed_, "white", 0, LoadOp::Clear), "QUEEN-UI-GRAPH");
}
} // namespace arp::build_a_queen
