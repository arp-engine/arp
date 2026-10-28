#include "renderer/gui/gui_ordered_renderer.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/math/quad_geometry.h"
#include "renderer/gui/gui_ordered_unified_contract.h"
#include "renderer/ui/ui_atlas_runtime.h"
#include "rhi/frontend/descriptor_set_validation.h"

namespace arp::renderer {
struct GuiOrderedRenderer::State {
    struct Texture {
        GuiOrderedTextureDesc source;
        core::UniquePtr<rhi::GpuTextureView> view;
        core::UniquePtr<rhi::GpuSampler> sampler;
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
    };

    struct Draw {
        std::shared_ptr<Texture> texture;
        GuiPushConstants push;
        rhi::ScissorState clip;
    };

    struct Slot {
        core::pmr::Vector<Draw> draws{&core::defaultCpuMemoryResource()};
    };

    GuiOrderedRendererDesc desc;
    std::shared_ptr<rhi::GpuBuffer> corners;
    core::pmr::Vector<std::shared_ptr<Texture>> textures{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<Slot>> slots{&core::defaultCpuMemoryResource()};
};

RendererResult GuiOrderedRenderer::create(const GuiOrderedRendererDesc& d)
try {
    if (state_ || (d.contractVersion != 1 && d.contractVersion != 2) || !d.execution || !d.pipeline
        || !d.maxCommands || !d.maxTextures || !d.frameSlots || !d.maxCpuMetadataBytes || !d.quota
        || d.quota->limitBytes() != d.maxCpuMetadataBytes || d.coordinates.contractVersion != 1
        || d.coordinates.flipRectangleY || d.coordinates.flipTextureUvY)
        return {false, "GUI-ORDERED-CONFIG"};
    const char* id = d.execution->diagnostic(device_);
    if (!id)
        id = d.execution->metadataQuotaDiagnostic(d.quota);
    const auto* p = id ? nullptr : d.execution->pipelineDefinition(*d.pipeline, id);
    if (!p)
        return {false, id ? id : "GUI-ORDERED-PIPELINE-OWNER"};
    if (d.shaderSourceSha256.size() != 64
        || d.pipeline->shaderReflection().sourceSha256 != d.shaderSourceSha256)
        return {false, "GUI-ORDERED-SOURCE-IDENTITY"};
    const auto* layout = d.pipeline->layout();
    const rhi::DescriptorLayoutDesc atlas{"GuiBitmapSet",
        {{0, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {1, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment}}};
    if (!layout || p->colorFormat != rhi::TextureFormat::Rgba8Unorm || p->colorTargets.size() != 1
        || p->depthFormat != rhi::TextureFormat::Unknown
        || p->depthStencil.format != rhi::TextureFormat::Unknown || p->depthStencil.depthTestEnabled
        || p->depthStencil.depthWriteEnabled || p->depthStencil.stencilEnabled
        || p->topology != rhi::PrimitiveTopology::TriangleList || p->cullMode != rhi::CullMode::None
        || !p->dynamicViewportScissor || p->hasViewport || p->hasScissor
        || p->multisample.sampleCount != 1 || p->vertexShader.entryPoint != "vertexMain"
        || p->fragmentShader.entryPoint != "fragmentMain" || p->vertexBuffers.size() != 1
        || p->vertexBuffers[0].slot != 0 || p->vertexBuffers[0].strideBytes != sizeof(core::Vec2)
        || p->vertexBuffers[0].inputRate != rhi::VertexInputRate::Vertex
        || p->vertexBuffers[0].attributes.size() != 1
        || p->vertexBuffers[0].attributes[0].shaderLocation != 0
        || p->vertexBuffers[0].attributes[0].offsetBytes != 0
        || p->vertexBuffers[0].attributes[0].format != rhi::VertexFormat::Float32x2
        || layout->desc().descriptorLayouts.size() != 1 || layout->desc().descriptorSets.size() != 1
        || !layout->desc().descriptorTables.empty() || layout->desc().descriptorSets[0].set != 0
        || layout->desc().descriptorSets[0].layoutName != atlas.name
        || !rhi::descriptorLayoutDefinitionMatches(atlas, layout->desc().descriptorLayouts[0])
        || layout->desc().pushConstantRanges.size() != 1
        || layout->desc().pushConstantRanges[0].stages != rhi::ShaderStage::Vertex
        || layout->desc().pushConstantRanges[0].offsetBytes != 0
        || layout->pushConstantByteCount() != sizeof(GuiPushConstants))
        return {false, "GUI-ORDERED-PIPELINE-CONTRACT"};
    const auto& color = p->colorTargets[0];
    if (color.format != p->colorFormat || !color.blendEnabled
        || color.writeMask != rhi::ColorWriteMask::All || color.resolveTargetRequired)
        return {false, "GUI-ORDERED-BLEND-CONTRACT"};
    for (const auto blend : {color.colorBlend, color.alphaBlend})
        if (blend.sourceFactor != rhi::BlendFactor::One
            || blend.destinationFactor != rhi::BlendFactor::OneMinusSrcAlpha
            || blend.operation != rhi::BlendOp::Add)
            return {false, "GUI-ORDERED-BLEND-CONTRACT"};
    core::CpuMemoryQuotaScope scope(d.quota);
    auto state = core::makeShared<State>();
    state->desc = d;
    state->textures.reserve(d.maxTextures);
    state->slots.reserve(d.frameSlots);
    state->corners = core::shareUnique(device_.createBuffer({"GuiOrderedCorners",
        sizeof(core::QuadTriangleTopology::corners),
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead}));
    if (!state->corners)
        return {false, "GUI-ORDERED-CORNER-BUFFER"};
    const auto written = device_.writeBuffer(*state->corners,
        core::QuadTriangleTopology::corners.data(),
        sizeof(core::QuadTriangleTopology::corners));
    if (!written.success)
        return {false, written.diagnostics};
    for (std::uint32_t i = 0; i < d.frameSlots; ++i) {
        auto slot = core::makeShared<State::Slot>();
        slot->draws.reserve(d.maxCommands);
        state->slots.push_back(std::move(slot));
    }
    state_ = std::move(state);
    return {true, {}};
} catch (const std::bad_alloc&) {
    return {false, "GUI-ORDERED-CPU-ALLOCATION"};
}

RendererResult GuiOrderedRenderer::registerTexture(const GuiOrderedTextureDesc& d)
try {
    if (!state_ || d.resourceId.empty() || !d.texture
        || d.texture->desc().format != rhi::TextureFormat::Rgba8Unorm
        || (d.texture->desc().usage & rhi::TextureUsage::Sampled) == rhi::TextureUsage::None
        || !d.texture->desc().extent.width || !d.texture->desc().extent.height
        || (d.alpha != GuiSampleAlpha::Straight && d.alpha != GuiSampleAlpha::Premultiplied)
        || (d.bitmapGlyphPage && (d.fontToken.empty() || d.alpha != GuiSampleAlpha::Straight)))
        return {false, "GUI-ORDERED-TEXTURE-CONTRACT"};
    auto& state = *state_;
    if (!d.unifiedFonts.empty()) {
        if (state.desc.contractVersion != 2 || d.bitmapGlyphPage || d.externalTexture
            || d.alpha != GuiSampleAlpha::Straight || d.flipUvY)
            return {false, "GUI-ORDERED-UNIFIED-ATLAS-CONTRACT"};
        for (std::size_t i = 0; i < d.unifiedFonts.size(); ++i) {
            const auto& face = d.unifiedFonts[i];
            if (face.tokenId.empty() || (face.renderMode != "sdf" && face.renderMode != "alpha"))
                return {false, "GUI-ORDERED-UNIFIED-FONT-CONTRACT"};
            for (std::size_t j = 0; j < i; ++j)
                if (d.unifiedFonts[j].tokenId == face.tokenId)
                    return {false, "GUI-ORDERED-UNIFIED-FONT-DUPLICATE"};
        }
    }
    if (const auto* id = state.desc.execution->diagnostic(device_))
        return {false, id};
    for (const auto& texture : state.textures)
        if (texture->source.resourceId == d.resourceId)
            return {false, "GUI-ORDERED-TEXTURE-DUPLICATE"};
    if (state.textures.size() == state.desc.maxTextures)
        return {false, "GUI-ORDERED-TEXTURE-CAPACITY"};
    core::CpuMemoryQuotaScope scope(state.desc.quota);
    auto texture = core::makeShared<State::Texture>();
    texture->source = d;
    texture->view = device_.createTextureView(*d.texture,
        {d.resourceId,
            rhi::TextureFormat::Rgba8Unorm,
            rhi::TextureUsage::Sampled,
            rhi::TextureViewDimension::Texture2D});
    rhi::SamplerDesc sampler;
    sampler.label = d.resourceId;
    sampler.minFilter = sampler.magFilter = rhi::SamplerFilter::Nearest;
    if (!d.unifiedFonts.empty())
        sampler.minFilter = sampler.magFilter = rhi::SamplerFilter::Linear;
    sampler.mipFilter = rhi::SamplerMipFilter::None;
    texture->sampler = device_.createSampler(sampler);
    texture->arena = device_.createDescriptorArena({d.resourceId, 1, 2});
    if (!texture->view || !texture->sampler || !texture->arena)
        return {false, "GUI-ORDERED-TEXTURE-BINDING"};
    texture->set = texture->arena->allocateDescriptorSet(
        {"GuiBitmapSet", state.desc.pipeline->layout()->desc().descriptorLayouts[0]});
    if (!texture->set)
        return {false, "GUI-ORDERED-TEXTURE-BINDING"};
    auto result = texture->set->write({0,
        rhi::DescriptorType::SampledTexture,
        texture->view->id(),
        rhi::DescriptorResourceKind::TextureView});
    if (result.success)
        result = texture->set->write({1,
            rhi::DescriptorType::Sampler,
            texture->sampler->id(),
            rhi::DescriptorResourceKind::Sampler});
    if (!result.success)
        return {false, result.diagnostics};
    state.textures.push_back(std::move(texture));
    return {true, {}};
} catch (const std::bad_alloc&) {
    return {false, "GUI-ORDERED-CPU-ALLOCATION"};
}

bool GuiOrderedRenderer::readyForFrame(std::uint64_t frame) const
{
    return state_ && !state_->desc.execution->diagnostic(device_)
        && state_->slots[frame % state_->slots.size()].use_count() == 1;
}

RendererResult GuiOrderedRenderer::registerUnifiedAtlas(const ui::UnifiedUiAtlasRuntime& atlas)
try {
    if (!state_ || state_->desc.contractVersion != 2 || !atlas.ok() || atlas.fontFaces().empty()
        || atlas.atlas().srgb)
        return {false, "GUI-ORDERED-UNIFIED-ATLAS-CONTRACT"};
    const auto& image = atlas.atlas();
    core::CpuMemoryQuotaScope scope(state_->desc.quota);
    auto texture = core::shareUnique(device_.createTexture({image.resourceId,
        {image.extent.width, image.extent.height, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead}));
    if (!texture)
        return {false, "GUI-ORDERED-UNIFIED-TEXTURE"};
    rhi::semantics::ClientTextureUpload upload;
    upload.contractVersion = 1;
    upload.extent = {image.extent.width, image.extent.height, 1};
    upload.bytesPerRow = image.extent.width * 4;
    upload.bytes = image.rgba8;
    auto result = state_->desc.execution->uploadTexture(*texture, upload);
    if (!result.success)
        return {false, result.diagnostics};
    GuiOrderedTextureDesc desc;
    desc.resourceId = image.resourceId;
    desc.texture = std::move(texture);
    desc.unifiedFonts = atlas.fontFaces();
    return registerTexture(desc);
} catch (const std::bad_alloc&) {
    return {false, "GUI-ORDERED-CPU-ALLOCATION"};
}

RendererResult GuiOrderedRenderer::updateUnifiedAtlas(const ui::UnifiedUiAtlasRuntime& atlas)
try {
    if (!state_ || state_->desc.contractVersion != 2 || !atlas.ok())
        return {false, "GUI-ORDERED-UNIFIED-ATLAS-CONTRACT"};
    for (const auto& slot : state_->slots)
        if (slot.use_count() != 1)
            return {false, "GUI-ORDERED-TEXTURE-GRAPH-RESERVED"};
    for (const auto& entry : state_->textures) {
        if (entry->source.resourceId != atlas.atlas().resourceId)
            continue;
        const auto& source = entry->source;
        const auto& image = atlas.atlas();
        if (source.unifiedFonts.size() != atlas.fontFaces().size()
            || source.texture->desc().extent.width != image.extent.width
            || source.texture->desc().extent.height != image.extent.height)
            return {false, "GUI-ORDERED-UNIFIED-ATLAS-CHANGED"};
        for (std::size_t i = 0; i < source.unifiedFonts.size(); ++i)
            if (source.unifiedFonts[i].tokenId != atlas.fontFaces()[i].tokenId
                || source.unifiedFonts[i].renderMode != atlas.fontFaces()[i].renderMode)
                return {false, "GUI-ORDERED-UNIFIED-ATLAS-CHANGED"};
        if (const auto* id = state_->desc.execution->resourceReuseDiagnostic(*source.texture))
            return {false, id};
        rhi::semantics::ClientTextureUpload upload;
        upload.contractVersion = 1;
        upload.extent = {image.extent.width, image.extent.height, 1};
        upload.bytesPerRow = image.extent.width * 4;
        upload.bytes = image.rgba8;
        const auto result = state_->desc.execution->uploadTexture(*source.texture, upload);
        return {result.success, result.diagnostics};
    }
    return {false, "GUI-ORDERED-TEXTURE-MISSING"};
} catch (const std::bad_alloc&) {
    return {false, "GUI-ORDERED-CPU-ALLOCATION"};
}

RendererResult GuiOrderedRenderer::registerBitmapFont(const arp::ui::UiBitmapFontResource& font)
try {
    if (!state_ || !font.valid())
        return {false, "GUI-ORDERED-BITMAP-FONT-NOT-READY"};
    auto& state = *state_;
    if (const auto* id = state.desc.execution->diagnostic(device_))
        return {false, id};
    core::CpuMemoryQuotaScope scope(state.desc.quota);
    const auto& image = font.atlas().get()->image();
    auto texture = core::shareUnique(device_.createTexture({font.atlas().id(),
        {image.width, image.height, 1},
        rhi::TextureFormat::Rgba8Unorm,
        rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::ShaderRead}));
    if (!texture)
        return {false, "GUI-ORDERED-BITMAP-FONT-TEXTURE"};
    rhi::semantics::ClientTextureUpload upload;
    upload.contractVersion = 1;
    upload.extent = {image.width, image.height, 1};
    upload.bytesPerRow = image.width * 4;
    upload.bytes = image.rgba8;
    const auto result = state.desc.execution->uploadTexture(*texture, upload);
    if (!result.success)
        return {false, result.diagnostics};
    return registerTexture({font.atlas().id(),
        std::move(texture),
        GuiSampleAlpha::Straight,
        false,
        true,
        false,
        font.fontToken(),
        font.page()});
} catch (const std::bad_alloc&) {
    return {false, "GUI-ORDERED-CPU-ALLOCATION"};
}

RendererResult GuiOrderedRenderer::unregisterTexture(core::StringView id)
{
    if (!state_)
        return {false, "GUI-ORDERED-NOT-CREATED"};
    auto& state = *state_;
    for (const auto& slot : state.slots)
        if (slot.use_count() != 1)
            return {false, "GUI-ORDERED-TEXTURE-GRAPH-RESERVED"};
    const auto found =
        std::find_if(state.textures.begin(), state.textures.end(), [&](const auto& texture) {
            return texture->source.resourceId == id;
        });
    if (found == state.textures.end())
        return {false, "GUI-ORDERED-TEXTURE-MISSING"};
    if (const auto* diagnostic =
            state.desc.execution->resourceReuseDiagnostic(*(*found)->source.texture))
        return {false, diagnostic};
    for (auto& slot : state.slots)
        slot->draws.clear();
    state.textures.erase(found);
    return {true, {}};
}

RendererResult GuiOrderedRenderer::addPass(render_graph::FrameGraph& graph,
    render_graph::TextureHandle target,
    const core::pmr::Vector<ui::UiDrawCommand>& commands,
    core::StringView boxTextureId,
    std::uint64_t frame,
    rhi::LoadOp load,
    rhi::Color clear)
try {
    if (!readyForFrame(frame))
        return {false, "GUI-ORDERED-FRAME-RESERVED"};
    auto& state = *state_;
    if (commands.empty() || commands.size() > state.desc.maxCommands)
        return {false, "GUI-ORDERED-COMMAND-CAPACITY"};
    const auto& targetDesc = graph.texture(target).desc();
    const rhi::Extent2D extent{targetDesc.extent.width, targetDesc.extent.height};
    if (targetDesc.format != rhi::TextureFormat::Rgba8Unorm || !extent.width || !extent.height)
        return {false, "GUI-ORDERED-TARGET"};
    core::CpuMemoryQuotaScope scope(state.desc.quota);
    // The original lowerer is authoritative for text/image binding and push ABI.
    const auto plan = buildGuiDrawPlan(commands, extent, state.desc.coordinates);
    if (!plan.success)
        return {false, core::pmr::String(plan.diagnostics, &core::defaultCpuMemoryResource())};
    auto slot = state.slots[frame % state.slots.size()];
    slot->draws.clear();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto& command = commands[i];
        if (command.kind != ui::UiDrawCommandKind::Box
            && command.kind != ui::UiDrawCommandKind::Text
            && command.kind != ui::UiDrawCommandKind::Image
            && command.kind != ui::UiDrawCommandKind::GlyphQuad)
            return {false, "GUI-ORDERED-COMMAND-KIND"};
        if (command.textureSource != ui::UiTextureSource::Atlas
            && command.textureSource != ui::UiTextureSource::External)
            return {false, "GUI-ORDERED-TEXTURE-SOURCE"};
        if (command.sdfMode && state.desc.contractVersion == 1)
            return {false, "GUI-ORDERED-SDF-PROFILE-REQUIRED"};
        const auto& clip = command.clipRect;
        const auto& rect = command.rect;
        const auto integral = [](float x) {
            return std::isfinite(x) && x == std::floor(x);
        };
        rhi::ScissorState quantized;
        if (state.desc.contractVersion == 2) {
            if (const auto* id = GuiOrderedUnifiedContract::clip(clip, extent, quantized))
                return {false, id};
        }
        if (!rect.valid()
            || (state.desc.contractVersion == 1
                && (!clip.valid() || clip.width <= 0 || clip.height <= 0 || !integral(clip.x)
                    || !integral(clip.y) || !integral(clip.width) || !integral(clip.height)
                    || clip.x < 0 || clip.y < 0 || double(clip.x) + clip.width > extent.width
                    || double(clip.y) + clip.height > extent.height || double(clip.x) > INT32_MAX
                    || double(clip.y) > INT32_MAX)))
            return {false, "GUI-ORDERED-EXPLICIT-INTEGER-CLIP-REQUIRED"};
        if (state.desc.contractVersion == 1)
            quantized = {static_cast<std::int32_t>(clip.x),
                static_cast<std::int32_t>(clip.y),
                static_cast<std::uint32_t>(clip.width),
                static_cast<std::uint32_t>(clip.height)};
        auto push = plan.commands[i].pushConstants;
        for (float c : push.color)
            if (!std::isfinite(c) || c < 0 || c > 1)
                return {false, "GUI-ORDERED-COLOR-CONTRACT"};
        if (command.kind != ui::UiDrawCommandKind::Box && !command.hasTextureSourceRect)
            return {false, "GUI-ORDERED-SOURCE-RECT-REQUIRED"};
        const core::StringView id =
            plan.commands[i].textured ? core::StringView(command.textureResourceId) : boxTextureId;
        const auto found =
            std::find_if(state.textures.begin(), state.textures.end(), [&](const auto& texture) {
                return texture->source.resourceId == id;
            });
        if (found == state.textures.end())
            return {false, "GUI-ORDERED-TEXTURE-MISSING"};
        const auto& source = (*found)->source;
        if (plan.commands[i].externalTexture != source.externalTexture)
            return {false, "GUI-ORDERED-TEXTURE-SOURCE-MISMATCH"};
        for (const auto* pair : {push.rectOrigin, push.rectSize, push.uvOrigin, push.uvSize})
            if (!std::isfinite(pair[0]) || !std::isfinite(pair[1]))
                return {false, "GUI-ORDERED-NONFINITE-GEOMETRY"};
        if (plan.commands[i].textured
            && (push.uvOrigin[0] < 0 || push.uvOrigin[1] < 0 || push.uvSize[0] <= 0
                || push.uvSize[1] <= 0 || double(push.uvOrigin[0]) + push.uvSize[0] > 1
                || double(push.uvOrigin[1]) + push.uvSize[1] > 1))
            return {false, "GUI-ORDERED-SOURCE-RECT-RANGE"};
        if (plan.commands[i].text && !source.unifiedFonts.empty()) {
            const auto face = std::find_if(
                source.unifiedFonts.begin(), source.unifiedFonts.end(), [&](const auto& value) {
                    return value.tokenId == command.fontToken;
                });
            if (!command.hasAtlasPageIndex || command.atlasPageIndex != 0
                || face == source.unifiedFonts.end()
                || command.sdfMode != (face->renderMode == "sdf"))
                return {false, "GUI-ORDERED-UNIFIED-GLYPH-MISMATCH"};
        } else if (plan.commands[i].text
            && (!source.bitmapGlyphPage || !command.hasAtlasPageIndex
                || command.atlasPageIndex != source.atlasPage || command.fontToken.empty()
                || command.fontToken != source.fontToken))
            return {false, "GUI-ORDERED-GLYPH-PAGE-FONT-MISMATCH"};
        if (command.sdfMode && (source.unifiedFonts.empty() || !plan.commands[i].text))
            return {false, "GUI-ORDERED-UNIFIED-GLYPH-MISMATCH"};
        if (source.flipUvY) {
            push.uvOrigin[1] += push.uvSize[1];
            push.uvSize[1] = -push.uvSize[1];
        }
        push.flags[1] = command.sdfMode                     ? 2.f
            : source.alpha == GuiSampleAlpha::Premultiplied ? 1.f
                                                            : 0.f;
        slot->draws.push_back({*found, push, quantized});
    }
    const auto vertex = graph.importBufferRef(state.corners);
    auto pass = graph.addPass("GuiOrderedBitmap");
    pass.access(vertex,
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::VertexAttributeRead},
            0,
            state.corners->desc().sizeBytes)
        .writeAttachment(
            target, rhi::ResourceState::ColorAttachment, load, rhi::StoreOp::Store, {}, clear);
    // One graph resource/access per actual atlas owner. Importing the same
    // atlas for every glyph creates thousands of duplicate graph identities.
    for (const auto& texture : state.textures)
        if (std::any_of(slot->draws.begin(), slot->draws.end(), [&](const auto& draw) {
                return draw.texture == texture;
            }))
            pass.read(
                graph.importTextureRef(texture->source.texture), rhi::ResourceState::ShaderRead);
    auto pipeline = state.desc.pipeline;
    auto corners = state.corners;
    pass.executeRenderPass([slot, pipeline, corners, extent](
                               render_graph::RenderPassContext& context) {
        auto& encoder = context.renderPass();
        core::requireSuccessfulResult(encoder.setPipeline(*pipeline), "GUI-ORDERED-PIPELINE");
        core::requireSuccessfulResult(
            encoder.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
            "GUI-ORDERED-VIEWPORT");
        core::requireSuccessfulResult(encoder.bindVertexBuffer(0, *corners), "GUI-ORDERED-CORNERS");
        for (const auto& draw : slot->draws) {
            core::requireSuccessfulResult(encoder.setScissor(draw.clip), "GUI-ORDERED-CLIP");
            core::requireSuccessfulResult(
                encoder.bindDescriptorSet(0, *draw.texture->set), "GUI-ORDERED-DESCRIPTORS");
            core::requireSuccessfulResult(
                encoder.pushConstants(rhi::ShaderStage::Vertex, 0, sizeof(draw.push), &draw.push),
                "GUI-ORDERED-PUSH");
            core::requireSuccessfulResult(
                encoder.draw(core::QuadTriangleTopology::vertexCount, 1, 0, 0), "GUI-ORDERED-DRAW");
        }
    });
    return {true, {}};
} catch (const std::bad_alloc&) {
    return {false, "GUI-ORDERED-CPU-ALLOCATION"};
}
} // namespace arp::renderer
