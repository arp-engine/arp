#include "renderer/pipeline/basic_forward_pipeline.h"

#include "core/diagnostics/diagnostic_error.h"
#include "rhi/frontend/descriptor_set_validation.h"

namespace arp::renderer {
struct BasicForwardRenderPipeline::State {
    struct Material {
        BasicForwardMaterialConstants constants;
        std::uint32_t variant = 0;
        std::shared_ptr<rhi::GpuTexture> texture;
        core::UniquePtr<rhi::GpuTextureView> view;
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::pmr::Vector<core::UniquePtr<rhi::DescriptorSet>> sets{
            &core::defaultCpuMemoryResource()};
    };

    struct Mesh {
        std::shared_ptr<rhi::GpuBuffer> vertices, indices;
        rhi::IndexFormat indexFormat = rhi::IndexFormat::Uint16;
        core::pmr::Vector<assets::Submesh> sections{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<Material> materials{&core::defaultCpuMemoryResource()};
    };

    struct Draw {
        std::shared_ptr<Mesh> mesh;
        std::uint32_t section = 0;
    };

    struct Slot {
        std::shared_ptr<rhi::GpuBuffer> camera;
        core::pmr::Vector<Draw> draws{&core::defaultCpuMemoryResource()};
    };

    BasicForwardPipelineDesc desc;
    std::shared_ptr<rhi::GpuSampler> sampler;
    core::pmr::Vector<std::shared_ptr<Mesh>> meshes{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::shared_ptr<Slot>> slots{&core::defaultCpuMemoryResource()};
};

RendererResult BasicForwardRenderPipeline::create(const RenderPipelineCreateContext& context)
try {
    const auto& d = config_;
    if (state_ || d.contractVersion != 1 || !d.execution || !d.maxMeshes || !d.maxMaterialsPerMesh
        || !d.maxSectionsPerFrame || !d.frameSlots || d.frameSlots > UINT32_MAX / 3
        || !resource::isResourceUuid(d.shaderPackageId) || !d.maxCpuMetadataBytes || !d.quota
        || d.quota->limitBytes() != d.maxCpuMetadataBytes || !context.extent.width
        || !context.extent.height || context.colorFormat != rhi::TextureFormat::Rgba8Unorm
        || context.depthFormat != rhi::TextureFormat::Depth24Stencil8)
        return {false, "BASIC-FORWARD-CONFIG"};
    if (const auto* id = d.execution->diagnostic(context.device))
        return {false, id};
    if (const auto* id = d.execution->metadataQuotaDiagnostic(d.quota))
        return {false, id};
    const rhi::DescriptorLayoutDesc set{"BasicForwardSet",
        {{0, rhi::DescriptorType::UniformBuffer, rhi::ShaderStage::Vertex},
            {1, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment},
            {2, rhi::DescriptorType::Sampler, rhi::ShaderStage::Fragment}}};
    for (std::uint32_t i = 0; i < 4; ++i) {
        const char* id = nullptr;
        const auto& owner = d.pipelines[i];
        const auto* p = owner ? d.execution->pipelineDefinition(*owner, id) : nullptr;
        if (!p)
            return {false, id ? id : "BASIC-FORWARD-PIPELINE-OWNER"};
        const auto* layout = owner->layout();
        if (d.shaderSourceSha256.size() != 64
            || owner->shaderReflection().sourceSha256 != d.shaderSourceSha256)
            return {false, "BASIC-FORWARD-SOURCE-IDENTITY"};
        if (!layout || p->colorFormat != context.colorFormat || p->colorTargets.size() != 1
            || p->depthFormat != context.depthFormat
            || p->depthStencil.format != context.depthFormat || !p->depthStencil.depthTestEnabled
            || p->depthStencil.depthWriteEnabled != (i < 2)
            || p->depthStencil.depthCompare != rhi::CompareOp::LessEqual
            || p->depthStencil.stencilEnabled || p->depthStencil.depthBiasEnabled
            || p->topology != rhi::PrimitiveTopology::TriangleList
            || p->cullMode != (i % 2 ? rhi::CullMode::None : rhi::CullMode::Back)
            || p->frontFace != rhi::FrontFace::CounterClockwise || !p->dynamicViewportScissor
            || p->hasViewport || p->hasScissor || p->multisample.sampleCount != 1
            || p->multisample.alphaToCoverageEnabled || p->vertexShader.entryPoint != "vertexMain"
            || p->fragmentShader.entryPoint != "fragmentMain" || p->vertexBuffers.size() != 1
            || layout->desc().descriptorLayouts.size() != 1
            || layout->desc().descriptorSets.size() != 1 || !layout->desc().descriptorTables.empty()
            || layout->desc().descriptorSets[0].set != 0
            || layout->desc().descriptorSets[0].layoutName != set.name
            || !rhi::descriptorLayoutDefinitionMatches(set, layout->desc().descriptorLayouts[0])
            || layout->desc().pushConstantRanges.size() != 1
            || layout->desc().pushConstantRanges[0].stages != rhi::ShaderStage::Fragment
            || layout->desc().pushConstantRanges[0].offsetBytes != 0
            || layout->pushConstantByteCount() != sizeof(BasicForwardMaterialConstants))
            return {false, "BASIC-FORWARD-PIPELINE-CONTRACT"};
        const auto& stream = p->vertexBuffers[0];
        if (stream.slot != 0 || stream.strideBytes != sizeof(assets::MeshVertex)
            || stream.inputRate != rhi::VertexInputRate::Vertex || stream.attributes.size() != 3)
            return {false, "BASIC-FORWARD-VERTEX-CONTRACT"};
        const std::array formats{rhi::VertexFormat::Float32x3,
            rhi::VertexFormat::Float32x3,
            rhi::VertexFormat::Float32x2};
        const std::array offsets{std::uint32_t(offsetof(assets::MeshVertex, position)),
            std::uint32_t(offsetof(assets::MeshVertex, normal)),
            std::uint32_t(offsetof(assets::MeshVertex, uv))};
        for (std::uint32_t n = 0; n < 3; ++n)
            if (stream.attributes[n].shaderLocation != n
                || stream.attributes[n].offsetBytes != offsets[n]
                || stream.attributes[n].format != formats[n])
                return {false, "BASIC-FORWARD-VERTEX-CONTRACT"};
        const auto& color = p->colorTargets[0];
        if (color.format != p->colorFormat || color.blendEnabled != (i >= 2)
            || color.writeMask != rhi::ColorWriteMask::All || color.resolveTargetRequired)
            return {false, "BASIC-FORWARD-BLEND-CONTRACT"};
        for (auto blend : {color.colorBlend, color.alphaBlend})
            if (blend.sourceFactor != rhi::BlendFactor::One
                || blend.destinationFactor != rhi::BlendFactor::OneMinusSrcAlpha
                || blend.operation != rhi::BlendOp::Add)
                return {false, "BASIC-FORWARD-BLEND-CONTRACT"};
    }
    core::CpuMemoryQuotaScope scope(d.quota);
    auto state = core::makeShared<State>();
    state->desc = d;
    state->meshes.reserve(d.maxMeshes);
    state->slots.reserve(d.frameSlots);
    rhi::SamplerDesc sampler;
    sampler.label = "BasicForwardNearest";
    sampler.minFilter = sampler.magFilter = rhi::SamplerFilter::Nearest;
    sampler.mipFilter = rhi::SamplerMipFilter::None;
    state->sampler = core::shareUnique(context.device.createSampler(sampler));
    if (!state->sampler)
        return {false, "BASIC-FORWARD-SAMPLER"};
    for (std::uint32_t i = 0; i < d.frameSlots; ++i) {
        auto slot = core::makeShared<State::Slot>();
        slot->draws.reserve(d.maxSectionsPerFrame);
        slot->camera = core::shareUnique(context.device.createBuffer({"BasicForwardCamera",
            sizeof(core::Mat4),
            rhi::ResourceUsage::UniformBuffer | rhi::ResourceUsage::CopyDst,
            rhi::MemoryUsage::CpuToGpu,
            rhi::ResourceState::ShaderRead}));
        if (!slot->camera)
            return {false, "BASIC-FORWARD-CAMERA-BUFFER"};
        state->slots.push_back(std::move(slot));
    }
    desc_ = {
        "BasicForwardLambert/1", context.colorFormat, context.depthFormat, context.extent, true};
    device_ = &context.device;
    state_ = std::move(state);
    return {true, {}};
} catch (const std::bad_alloc&) {
    return {false, "BASIC-FORWARD-CPU-ALLOCATION"};
}

RendererResult BasicForwardRenderPipeline::resize(rhi::Extent2D extent)
{
    if (!state_ || !extent.width || !extent.height)
        return {false, "BASIC-FORWARD-EXTENT"};
    desc_.extent = extent;
    return {true, {}};
}

bool BasicForwardRenderPipeline::frameInputsReady(std::uint32_t frame) const
{
    if (!state_ || state_->desc.execution->diagnostic(*device_))
        return false;
    const auto& slot = state_->slots[frame % state_->slots.size()];
    return slot.use_count() == 1 && !state_->desc.execution->resourceReuseDiagnostic(*slot->camera);
}

RendererResult BasicForwardRenderPipeline::registerMesh(const assets::MeshAsset& source,
    const BasicWorldMeshProfile& profile,
    core::ArrayView<BasicForwardMaterialBinding> bindings,
    const resource::ShaderPackageResource& shader,
    std::string_view shaderId,
    const BasicForwardLighting& light,
    std::uint32_t& meshId)
try {
    meshId = UINT32_MAX;
    if (!state_)
        return {false, "BASIC-FORWARD-NOT-CREATED"};
    auto& s = *state_;
    if (const auto* id = s.desc.execution->diagnostic(*device_))
        return {false, id};
    if (shaderId != std::string_view(s.desc.shaderPackageId))
        return {false, "BASIC-FORWARD-SHADER-PACKAGE-IDENTITY"};
    if (s.meshes.size() >= s.desc.maxMeshes || bindings.size() != profile.materialCount
        || bindings.size() > s.desc.maxMaterialsPerMesh)
        return {false, "BASIC-FORWARD-MESH-CAPACITY"};
    if (const auto* id = BasicForwardContract::mesh(source, profile))
        return {false, id};
    core::CpuMemoryQuotaScope scope(s.desc.quota);
    auto mesh = core::makeShared<State::Mesh>();
    mesh->materials.reserve(bindings.size());
    for (const auto& b : bindings) {
        State::Material material;
        if (!b.material || !b.linearStraightAlphaTexture || b.textureId.empty())
            return {false, "BASIC-FORWARD-MATERIAL-SOURCE"};
        if (const auto* id = BasicForwardContract::material(
                *b.material, shader, shaderId, light, material.constants))
            return {false, id};
        if (!b.material->baseColorTextureId.empty()
            && b.material->baseColorTextureId != b.textureId)
            return {false, "BASIC-FORWARD-TEXTURE-IDENTITY"};
        material.texture = b.linearStraightAlphaTexture;
        const auto& td = material.texture->desc();
        if (td.format != rhi::TextureFormat::Rgba8Unorm || !td.extent.width || !td.extent.height
            || td.extent.depth != 1 || td.mipLevelCount != 1)
            return {false, "BASIC-FORWARD-TEXTURE-PROFILE"};
        material.variant =
            (b.material->alphaMode == "Blend" ? 2u : 0u) + (b.material->doubleSided ? 1u : 0u);
        material.view = device_->createTextureView(*material.texture,
            {"BasicForwardColor",
                rhi::TextureFormat::Rgba8Unorm,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D});
        material.arena = device_->createDescriptorArena(
            {"BasicForwardMaterials", s.desc.frameSlots, s.desc.frameSlots * 3});
        if (!material.view || !material.arena)
            return {false, "BASIC-FORWARD-MATERIAL-BINDING"};
        material.sets.reserve(s.desc.frameSlots);
        for (const auto& slot : s.slots) {
            auto set = material.arena->allocateDescriptorSet({"BasicForwardSet",
                s.desc.pipelines[material.variant]->layout()->desc().descriptorLayouts[0]});
            if (!set)
                return {false, "BASIC-FORWARD-MATERIAL-SET"};
            rhi::DescriptorBindingWrite buffer{0,
                rhi::DescriptorType::UniformBuffer,
                slot->camera->id(),
                rhi::DescriptorResourceKind::Buffer};
            buffer.bufferBindingRange = {1, 0, 0};
            for (const auto& write : {buffer,
                     rhi::DescriptorBindingWrite{1,
                         rhi::DescriptorType::SampledTexture,
                         material.view->id(),
                         rhi::DescriptorResourceKind::TextureView},
                     rhi::DescriptorBindingWrite{2,
                         rhi::DescriptorType::Sampler,
                         s.sampler->id(),
                         rhi::DescriptorResourceKind::Sampler}}) {
                const auto status = set->write(write);
                if (!status.success)
                    return {false, status.diagnostics};
            }
            material.sets.push_back(std::move(set));
        }
        mesh->materials.push_back(std::move(material));
    }
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
    const auto width = profile.indexPolicy == BasicWorldMeshIndexPolicy::Uint16Exact
        ? core::IndexElementWidth::Uint16
        : core::IndexElementWidth::Uint32;
    if (const auto* id = core::IndexBufferEncoding::encode(
            source.indices, source.vertices.size(), width, profile.maxIndexBytes, bytes))
        return {false, id};
    mesh->indexFormat = width == core::IndexElementWidth::Uint16 ? rhi::IndexFormat::Uint16
                                                                 : rhi::IndexFormat::Uint32;
    mesh->vertices = core::shareUnique(device_->createBuffer({"BasicForwardVertices",
        source.vertices.size() * sizeof(assets::MeshVertex),
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead}));
    mesh->indices = core::shareUnique(device_->createBuffer({"BasicForwardIndices",
        bytes.size(),
        rhi::ResourceUsage::IndexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead}));
    if (!mesh->vertices || !mesh->indices)
        return {false, "BASIC-FORWARD-MESH-BUFFERS"};
    const auto vertexWrite = device_->writeBuffer(*mesh->vertices,
        source.vertices.data(),
        source.vertices.size() * sizeof(assets::MeshVertex));
    if (!vertexWrite.success)
        return {false, vertexWrite.diagnostics};
    const auto indexWrite = device_->writeBuffer(*mesh->indices, bytes.data(), bytes.size());
    if (!indexWrite.success)
        return {false, indexWrite.diagnostics};
    mesh->sections.assign(source.submeshes.begin(), source.submeshes.end());
    meshId = static_cast<std::uint32_t>(s.meshes.size());
    s.meshes.push_back(std::move(mesh));
    return {true, {}};
} catch (const std::bad_alloc&) {
    return {false, "BASIC-FORWARD-CPU-ALLOCATION"};
}

RendererResult BasicForwardRenderPipeline::addPass(render_graph::FrameGraph& graph,
    render_graph::TextureHandle target,
    const BasicForwardFrame& frame)
{
    if (!state_ || frame.frameIndex > UINT32_MAX
        || !frameInputsReady(static_cast<std::uint32_t>(frame.frameIndex)))
        return {false, "BASIC-FORWARD-FRAME-RESERVED"};
    if (const auto* id = BasicForwardContract::camera(frame.viewProjection))
        return {false, id};
    const auto& color = graph.texture(target).desc();
    if (!frame.depth || frame.meshes.empty() || color.format != desc_.colorFormat
        || color.extent.width != desc_.extent.width || color.extent.height != desc_.extent.height
        || frame.depth->desc().format != desc_.depthFormat
        || frame.depth->desc().extent.width != color.extent.width
        || frame.depth->desc().extent.height != color.extent.height
        || (frame.colorLoad != rhi::LoadOp::Clear && frame.colorLoad != rhi::LoadOp::Load))
        return {false, "BASIC-FORWARD-FRAME-TARGET"};
    auto& s = *state_;
    std::uint64_t count = 0;
    for (const auto id : frame.meshes) {
        if (id >= s.meshes.size())
            return {false, "BASIC-FORWARD-MESH-ID"};
        count += s.meshes[id]->sections.size();
        if (count > s.desc.maxSectionsPerFrame)
            return {false, "BASIC-FORWARD-DRAW-BUDGET"};
    }
    const auto slotIndex = frame.frameIndex % s.slots.size();
    auto slot = s.slots[slotIndex];
    slot->draws.clear();
    for (const auto id : frame.meshes)
        for (std::uint32_t n = 0; n < s.meshes[id]->sections.size(); ++n)
            slot->draws.push_back({s.meshes[id], n});
    const auto status =
        device_->writeBuffer(*slot->camera, &frame.viewProjection, sizeof(core::Mat4));
    if (!status.success)
        return {false, status.diagnostics};
    auto pass = graph.addPass("BasicForwardLambert");
    pass.writeAttachment(target,
            rhi::ResourceState::ColorAttachment,
            frame.colorLoad,
            rhi::StoreOp::Store,
            {},
            frame.clear)
        .writeAttachment(graph.importTextureRef(frame.depth),
            rhi::ResourceState::DepthAttachment,
            rhi::LoadOp::Clear,
            rhi::StoreOp::Store);
    pass.access(graph.importBufferRef(slot->camera),
        {rhi::ResourceState::ShaderRead,
            rhi::PipelineStage::VertexShader,
            rhi::ResourceAccess::ShaderRead},
        0,
        sizeof(core::Mat4));
    for (const auto& draw : slot->draws) {
        pass.access(graph.importBufferRef(draw.mesh->vertices),
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::VertexAttributeRead},
            0,
            draw.mesh->vertices->desc().sizeBytes);
        pass.access(graph.importBufferRef(draw.mesh->indices),
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::IndexRead},
            0,
            draw.mesh->indices->desc().sizeBytes);
        pass.read(graph.importTextureRef(
                      draw.mesh->materials[draw.mesh->sections[draw.section].materialSlot].texture),
            rhi::ResourceState::ShaderRead);
    }
    auto pipelines = s.desc.pipelines;
    auto sampler = s.sampler;
    pass.executeRenderPass([slot = std::move(slot),
                               pipelines,
                               sampler,
                               slotIndex,
                               extent = desc_.extent](render_graph::RenderPassContext& context) {
        auto& encoder = context.renderPass();
        for (const auto& draw : slot->draws) {
            const auto& section = draw.mesh->sections[draw.section];
            const auto& material = draw.mesh->materials[section.materialSlot];
            core::requireSuccessfulResult(
                encoder.setPipeline(*pipelines[material.variant]), "BASIC-FORWARD-PIPELINE");
            core::requireSuccessfulResult(
                encoder.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
                "BASIC-FORWARD-VIEWPORT");
            core::requireSuccessfulResult(
                encoder.setScissor({0, 0, extent.width, extent.height}), "BASIC-FORWARD-SCISSOR");
            core::requireSuccessfulResult(
                encoder.bindVertexBuffer(0, *draw.mesh->vertices), "BASIC-FORWARD-VERTICES");
            core::requireSuccessfulResult(
                encoder.bindIndexBuffer(*draw.mesh->indices, draw.mesh->indexFormat),
                "BASIC-FORWARD-INDICES");
            core::requireSuccessfulResult(encoder.bindDescriptorSet(0, *material.sets[slotIndex]),
                "BASIC-FORWARD-DESCRIPTORS");
            core::requireSuccessfulResult(
                encoder.pushConstants(
                    rhi::ShaderStage::Fragment, 0, sizeof(material.constants), &material.constants),
                "BASIC-FORWARD-MATERIAL");
            core::requireSuccessfulResult(
                encoder.drawIndexed(section.indexCount, 1, section.firstIndex, 0, 0),
                "BASIC-FORWARD-DRAW");
        }
    });
    return {true, {}};
}

RendererResult BasicForwardRenderPipeline::render(RenderPipelineContext&)
{
    return {false, "BASIC-FORWARD-GRAPH-REQUIRED"};
}

RendererResult BasicForwardRenderPipeline::composeGraph(render_graph::FrameGraph& graph,
    render_graph::TextureHandle color,
    const RenderPipelineGraphCompositionContext& context,
    RenderPipelineFrameFeatures features)
{
    if (!context.sceneEnabled || !context.request.pipelinePayload
        || !context.request.pipelinePayloadContractName
        || std::string_view(context.request.pipelinePayloadContractName) != "BasicForwardFrame"
        || context.request.pipelinePayloadContractVersion != 1 || features.gui
        || features.debugOverlay || features.particlesWithColor)
        return {false, "BASIC-FORWARD-FRAME-CONTRACT"};
    return addPass(
        graph, color, *static_cast<const BasicForwardFrame*>(context.request.pipelinePayload));
}
} // namespace arp::renderer
