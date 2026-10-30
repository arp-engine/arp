#include "editor/viewport/editor_scene_viewport_sprite_renderer.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/math/quad_geometry.h"
#include "editor/viewport/editor_scene_viewport_sprite_contract.h"
#include "renderer/materials/texture_upload.h"
#include "renderer/shader_package_reflection.h"

#include <algorithm>
#include <array>
#include <limits>

namespace arp::editor {
namespace {

bool hasSuffix(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

bool makeSpritePipelinePackage(const EditorSceneViewportShaderArtifacts& artifacts,
    rhi::TextureFormat colorFormat,
    rhi::TextureFormat depthFormat,
    rhi::ShaderPackageDesc& package,
    rhi::PipelineLayoutDesc& layout,
    rhi::RenderPipelineDesc& pipeline,
    core::pmr::String& diagnostics)
{
    if (!hasSuffix(artifacts.sourcePath, ".slang") || !hasSuffix(artifacts.spirvPath, ".spv")
        || !hasSuffix(artifacts.mslPath, ".metal") || !hasSuffix(artifacts.wgslPath, ".wgsl")
        || !hasSuffix(artifacts.reflectionPath, ".reflection.yaml")) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ARTIFACT-PATH-INVALID";
        return false;
    }
    package.label = "ARP Editor Viewport Sprite";
    package.slangModulePath = artifacts.sourcePath;
    package.artifacts.spirvPath = artifacts.spirvPath;
    package.artifacts.mslPath = artifacts.mslPath;
    package.artifacts.wgslPath = artifacts.wgslPath;
    package.artifacts.reflectionPath = artifacts.reflectionPath;
    package.entrypoints.push_back(
        {package.slangModulePath, "vertexMain", rhi::ShaderStage::Vertex});
    package.entrypoints.push_back(
        {package.slangModulePath, "fragmentMain", rhi::ShaderStage::Fragment});
    if (!renderer::hydrateShaderPackageReflectionFromArtifact(package)) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PACKAGE-HYDRATION-FAILED";
        return false;
    }
    const auto& reflection = package.generatedArtifactSet->module().reflection;
    if (!validateEditorSceneViewportSpriteReflection(reflection)) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-REFLECTION-ABI-INVALID";
        return false;
    }
    for (const auto& reflected : reflection.bindings) {
        const auto type = rhi::descriptorTypeFromShaderBinding(reflected.type);
        if (!type) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-REFLECTION-BINDING-INVALID";
            return false;
        }
        package.reflection.descriptorBindings.push_back({reflected.set,
            reflected.binding,
            *type,
            static_cast<rhi::ShaderStage>(reflected.stageMask),
            reflected.arrayCount,
            reflected.subpassAttachmentIndex,
            reflected.depthTexture});
    }
    layout = rhi::buildPipelineLayoutDescFromShaderReflection(package.reflection);
    if (layout.descriptorSets.size() != 1u || layout.descriptorLayouts.size() != 1u
        || layout.descriptorSets[0].set != 0u || layout.pushConstantRanges.size() != 0u
        || !rhi::validatePipelineLayoutAgainstShaderReflection(layout, package.reflection)
            .success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PIPELINE-LAYOUT-INVALID";
        return false;
    }

    pipeline =
        buildEditorSceneViewportSpritePipeline(package.slangModulePath, {colorFormat, depthFormat});
    if (!rhi::validateRenderPipelineState(pipeline).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PIPELINE-STATE-INVALID";
        return false;
    }
    return true;
}

} // namespace

EditorSceneViewportSpriteRenderer::~EditorSceneViewportSpriteRenderer()
{
    if (resources_)
        resources_->removeLifecycleObserver(*this);
}

bool EditorSceneViewportSpriteRenderer::create(rhi::GpuDevice& device,
    resource::ResourceManager& resources,
    const EditorSceneViewportShaderArtifacts& artifacts,
    const std::uint32_t maxInstances,
    const std::uint32_t maxAtlases,
    const std::uint64_t maxUploadBytes,
    core::pmr::String& diagnostics,
    EditorSceneViewportSurfaceProfile profile)
{
    if (device_ || !maxInstances || !maxAtlases || !maxUploadBytes) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-RENDERER-BUDGET-INVALID";
        return false;
    }
    const auto capabilities = device.capabilities();
    if (capabilities.backend != device.backend() || device.backend() != rhi::Backend::Vulkan
        || !capabilities.core.graphics) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-BACKEND-EXECUTION-UNAVAILABLE";
        return false;
    }
    rhi::ShaderPackageDesc package;
    rhi::PipelineLayoutDesc layoutDesc;
    rhi::RenderPipelineDesc pipelineDesc;
    if (!makeSpritePipelinePackage(artifacts,
            profile.color,
            profile.depth,
            package,
            layoutDesc,
            pipelineDesc,
            diagnostics))
        return false;
    EditorSceneViewportDevicePipelineResources candidate;
    candidate.package = std::move(package);
    candidate.layout = device.createPipelineLayout(layoutDesc);
    if (!candidate.layout) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PIPELINE-LAYOUT-CREATE-FAILED";
        return false;
    }
    pipelineDesc.layout = candidate.layout.get();
    pipelineDesc.shaderPackage = &candidate.package;
    const auto validation = device.validateRenderPipeline(pipelineDesc);
    if (!validation.success) {
        diagnostics = validation.diagnostics;
        return false;
    }
    candidate.pipeline = device.createRenderPipeline(pipelineDesc);
    if (!candidate.pipeline) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PIPELINE-CREATE-FAILED";
        return false;
    }
    if (!createStorage(device, resources, maxInstances, maxAtlases, maxUploadBytes, diagnostics))
        return false;
    pipeline_ = std::move(candidate);
    return true;
}

bool EditorSceneViewportSpriteRenderer::createStorage(rhi::GpuDevice& device,
    resource::ResourceManager& resources,
    std::uint32_t maxInstances,
    std::uint32_t maxAtlases,
    std::uint64_t maxUploadBytes,
    core::pmr::String& diagnostics)
{
    rhi::SamplerDesc samplerDesc;
    samplerDesc.label = "EditorViewportSpriteNearest";
    samplerDesc.minFilter = rhi::SamplerFilter::Nearest;
    samplerDesc.magFilter = rhi::SamplerFilter::Nearest;
    samplerDesc.addressU = rhi::SamplerAddressMode::ClampToEdge;
    samplerDesc.addressV = rhi::SamplerAddressMode::ClampToEdge;
    samplerDesc.addressW = rhi::SamplerAddressMode::ClampToEdge;
    samplerDesc.compareOp = rhi::CompareOp::Never;
    samplerDesc.mipFilter = rhi::SamplerMipFilter::None;
    auto sampler = device.createSampler(samplerDesc);
    if (!sampler) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-SAMPLER-CREATE-FAILED";
        return false;
    }
    const auto instanceBytes =
        static_cast<std::uint64_t>(maxInstances) * sizeof(scene2d::SpriteInstance);
    if (instanceBytes > std::numeric_limits<std::uint32_t>::max()) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-INSTANCE-BUFFER-BUDGET-EXCEEDED";
        return false;
    }
    auto instances = device.createBuffer({"EditorViewportSpriteInstances",
        instanceBytes,
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead});
    if (!instances) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-INSTANCE-BUFFER-CREATE-FAILED";
        return false;
    }
    auto corners = device.createBuffer({"EditorSpriteCorners",
        sizeof(core::QuadTriangleTopology::corners),
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead});
    if (!corners
        || !device
            .writeBuffer(*corners,
                core::QuadTriangleTopology::corners.data(),
                sizeof(core::QuadTriangleTopology::corners))
            .success) {
        diagnostics = "EDITOR-VIEWPORT-SPRITE-CORNERS";
        return false;
    }
    corners_ = std::move(corners);
    device_ = &device;
    resources_ = &resources;
    resources.addLifecycleObserver(*this);
    sampler_ = std::move(sampler);
    instances_ = std::move(instances);
    maxInstances_ = maxInstances;
    maxAtlases_ = maxAtlases;
    maxUploadBytes_ = maxUploadBytes;
    return true;
}

bool EditorSceneViewportSpriteRenderer::prepare(
    const scene2d::SpriteDrawPlan& plan, core::pmr::String& diagnostics)
{
    if (!device_ || !resources_ || !activePipeline() || plan.diagnostic()
        || plan.instances().size() > maxInstances_) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PLAN-INVALID";
        return false;
    }
    std::uint64_t stagingBytes = 0u;
    for (const auto& batch : plan.batches()) {
        if (!batch.atlas.ready()) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-NOT-READY";
            return false;
        }
        if (std::any_of(atlases_.begin(), atlases_.end(), [&](const auto& owner) {
                return owner.source.get() == batch.atlas.get();
            }))
            continue;
        if (atlases_.size() >= maxAtlases_) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-BUDGET-EXCEEDED";
            return false;
        }
        const auto& image = batch.atlas.get()->image();
        const auto packed = renderer::packRgba8TextureRows(image.rgba8.data(),
            image.rgba8.size(),
            image.width,
            image.height,
            maxUploadBytes_ - stagingBytes);
        if (!packed.success() || packed.pixels.size() > maxUploadBytes_ - stagingBytes) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-UPLOAD-BUDGET-EXCEEDED";
            return false;
        }
        stagingBytes += packed.pixels.size();
        const auto format =
            image.srgb ? rhi::TextureFormat::Rgba8UnormSrgb : rhi::TextureFormat::Rgba8Unorm;
        AtlasOwner owner;
        owner.source = batch.atlas;
        owner.texture = device_->createTexture({batch.atlas.id(),
            {image.width, image.height, 1u},
            format,
            rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDst,
            rhi::MemoryUsage::GpuOnly,
            execution_ ? rhi::ResourceState::ShaderRead : rhi::ResourceState::Undefined});
        if (!owner.texture) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-TEXTURE-CREATE-FAILED";
            return false;
        }
        renderer::RendererResult uploaded;
        if (execution_) {
            rhi::semantics::ClientTextureUpload upload;
            upload.contractVersion = 1;
            upload.extent = {image.width, image.height, 1};
            upload.bytesPerRow = packed.bytesPerRow;
            upload.bytes = packed.pixels;
            const auto result = execution_->uploadTexture(*owner.texture, upload);
            uploaded = {result.success, result.diagnostics};
        } else {
            uploaded = renderer::uploadTextureRgba8(*device_,
                *owner.texture,
                packed.pixels.data(),
                image.width,
                image.height,
                packed.bytesPerRow,
                image.height);
        }
        if (!uploaded.success) {
            diagnostics = uploaded.diagnostics;
            return false;
        }
        owner.view = device_->createTextureView(*owner.texture,
            {batch.atlas.id(),
                format,
                rhi::TextureUsage::Sampled,
                rhi::TextureViewDimension::Texture2D});
        owner.arena = device_->createDescriptorArena({"EditorViewportSpriteAtlasArena", 1u, 2u});
        if (!owner.view || !owner.arena) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-DESCRIPTOR-CREATE-FAILED";
            return false;
        }
        const auto& layoutDesc = activePipeline()->layout()->desc();
        if (layoutDesc.descriptorSets.size() != 1u || layoutDesc.descriptorLayouts.size() != 1u) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-LAYOUT-INVALID";
            return false;
        }
        owner.set = owner.arena->allocateDescriptorSet(
            {"EditorViewportSpriteAtlasSet", layoutDesc.descriptorLayouts.front()});
        if (!owner.set) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-SET-ALLOCATE-FAILED";
            return false;
        }
        auto written = owner.set->write({0u,
            rhi::DescriptorType::SampledTexture,
            owner.view->id(),
            rhi::DescriptorResourceKind::TextureView,
            rhi::ResourceState::ShaderRead});
        if (written.success)
            written = owner.set->write({1u,
                rhi::DescriptorType::Sampler,
                sampler_->id(),
                rhi::DescriptorResourceKind::Sampler});
        if (!written.success) {
            diagnostics = written.diagnostics;
            return false;
        }
        atlases_.push_back(std::move(owner));
    }
    return true;
}

bool EditorSceneViewportSpriteRenderer::createOrdered(rhi::GpuDevice& device,
    resource::ResourceManager& resources,
    std::shared_ptr<rhi::RenderPipeline> pipeline,
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution,
    std::uint32_t maxInstances,
    std::uint32_t maxAtlases,
    std::uint64_t maxUploadBytes,
    core::pmr::String& diagnostics)
{
    if (device_ || !pipeline || !execution || weak_from_this().expired() || !maxInstances
        || !maxAtlases || !maxUploadBytes) {
        diagnostics = "EDITOR-SPRITE-ORDERED-CONFIG";
        return false;
    }
    if (const auto* id = execution->diagnostic(device)) {
        diagnostics = id;
        return false;
    }
    const char* diagnostic = nullptr;
    if (!execution->pipelineDefinition(*pipeline, diagnostic)) {
        diagnostics = diagnostic ? diagnostic : "EDITOR-SPRITE-ORDERED-PIPELINE";
        return false;
    }
    if (!createStorage(device, resources, maxInstances, maxAtlases, maxUploadBytes, diagnostics))
        return false;
    orderedPipeline_ = std::move(pipeline);
    execution_ = std::move(execution);
    atlases_.reserve(maxAtlases);
    return true;
}

bool EditorSceneViewportSpriteRenderer::addPass(render_graph::FrameGraph& graph,
    render_graph::TextureHandle color,
    render_graph::TextureHandle depth,
    const scene2d::SpriteDrawPlan& plan,
    const core::pmr::Vector<scene2d::SpriteInstance>& projected,
    core::pmr::String& diagnostics)
{
    if (!execution_ || !valid() || projected.size() != plan.instances().size()
        || projected.size() > maxInstances_) {
        diagnostics = "EDITOR-SPRITE-ORDERED-PLAN";
        return false;
    }
    if (projected.empty())
        return true;
    if (const auto* id = execution_->resourceReuseDiagnostic(*instances_)) {
        diagnostics = id;
        return false;
    }
    const auto uploaded = device_->writeBuffer(
        *instances_, projected.data(), projected.size() * sizeof(projected[0]));
    if (!uploaded.success) {
        diagnostics = uploaded.diagnostics;
        return false;
    }

    struct Batch {
        const rhi::DescriptorSet* set;
        std::uint32_t first, count;
    };

    core::pmr::Vector<Batch> batches;
    batches.reserve(plan.batches().size());
    auto owner = shared_from_this();
    auto pass = graph.addPass("EditorViewportSprite");
    pass.writeAttachment(
            color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store)
        .writeAttachment(
            depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store);
    for (auto* buffer : {instances_.get(), corners_.get()})
        pass.access(graph.importBufferRef({owner, buffer}),
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::VertexAttributeRead},
            0,
            buffer->desc().sizeBytes);
    for (const auto& batch : plan.batches()) {
        const auto atlas = std::find_if(atlases_.begin(), atlases_.end(), [&](const auto& value) {
            return value.source.get() == batch.atlas.get();
        });
        if (atlas == atlases_.end()) {
            diagnostics = "EDITOR-SPRITE-ORDERED-ATLAS";
            return false;
        }
        pass.read(
            graph.importTextureRef({owner, atlas->texture.get()}), rhi::ResourceState::ShaderRead);
        batches.push_back({atlas->set.get(), batch.first, batch.count});
    }
    const auto extent = graph.texture(color).desc().extent;
    pass.executeRenderPass(
        [owner, batches = std::move(batches), extent](render_graph::RenderPassContext& context) {
            auto& encoder = context.renderPass();
            core::requireSuccessfulResult(
                encoder.setPipeline(*owner->orderedPipeline_), "EDITOR-SPRITE-PIPELINE");
            core::requireSuccessfulResult(
                encoder.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
                "EDITOR-SPRITE-VIEWPORT");
            core::requireSuccessfulResult(
                encoder.setScissor({0, 0, extent.width, extent.height}), "EDITOR-SPRITE-SCISSOR");
            core::requireSuccessfulResult(
                encoder.bindVertexBuffer(1, *owner->corners_), "EDITOR-SPRITE-CORNERS");
            for (const auto& batch : batches) {
                core::requireSuccessfulResult(
                    encoder.bindDescriptorSet(0, *batch.set), "EDITOR-SPRITE-ATLAS");
                core::requireSuccessfulResult(
                    encoder.bindVertexBuffer(0,
                        *owner->instances_,
                        std::uint64_t(batch.first) * sizeof(scene2d::SpriteInstance)),
                    "EDITOR-SPRITE-INSTANCES");
                core::requireSuccessfulResult(
                    encoder.draw(core::QuadTriangleTopology::vertexCount, batch.count, 0, 0),
                    "EDITOR-SPRITE-DRAW");
            }
        });
    return true;
}

EditorSceneViewportDrawExecutionResult EditorSceneViewportSpriteRenderer::record(
    rhi::GpuTexture& colorTarget,
    rhi::GpuTexture& depthTarget,
    const EditorSceneViewportRect& viewport,
    const scene2d::SpriteDrawPlan& plan,
    const core::pmr::Vector<scene2d::SpriteInstance>& projectedInstances)
{
    if (!device_ || !pipeline_.pipeline || projectedInstances.empty()
        || projectedInstances.size() != plan.instances().size()
        || projectedInstances.size() > maxInstances_ || viewport.width == 0u
        || viewport.height == 0u)
        return {{}, "ARP-EDITOR-VIEWPORT-SPRITE-RECORD-INPUT-INVALID", 0u};
    const auto uploadBytes = projectedInstances.size() * sizeof(scene2d::SpriteInstance);
    if (uploadBytes > maxUploadBytes_)
        return {{}, "ARP-EDITOR-VIEWPORT-SPRITE-UPLOAD-BUDGET-EXCEEDED", 0u};
    const auto write = device_->writeBuffer(*instances_, projectedInstances.data(), uploadBytes);
    if (!write.success)
        return {{}, write.diagnostics, 0u};
    auto encoder = device_->createCommandEncoder({"EditorSceneViewportSprite"});
    if (!encoder)
        return {{}, "ARP-EDITOR-VIEWPORT-SPRITE-ENCODER-CREATE-FAILED", 0u};
    if (colorTarget.state() != rhi::ResourceState::ColorAttachment) {
        const auto transition = encoder->transition(
            colorTarget, colorTarget.state(), rhi::ResourceState::ColorAttachment);
        if (!transition.success)
            return {{}, transition.diagnostics, 0u};
    }
    if (depthTarget.state() != rhi::ResourceState::DepthAttachment) {
        const auto transition = encoder->transition(
            depthTarget, depthTarget.state(), rhi::ResourceState::DepthAttachment);
        if (!transition.success)
            return {{}, transition.diagnostics, 0u};
    }
    rhi::RenderPassDesc passDesc;
    passDesc.label = "EditorSceneViewportSprite";
    passDesc.colorTargetName = colorTarget.desc().label;
    passDesc.colorAttachments.push_back({"EditorSceneViewportSpriteColor",
        colorTarget.id(),
        colorTarget.desc().format,
        rhi::LoadOp::Load,
        rhi::StoreOp::Store});
    passDesc.hasDepthStencilAttachment = true;
    passDesc.depthStencilAttachment = {"EditorSceneViewportSpriteDepth",
        depthTarget.id(),
        depthTarget.desc().format,
        rhi::LoadOp::Load,
        rhi::StoreOp::Store,
        rhi::LoadOp::DontCare,
        rhi::StoreOp::DontCare,
        1.0f,
        0u,
        false,
        false};
    auto pass = encoder->beginRenderPass(passDesc);
    if (!pass)
        return {{}, encoder->lastValidation().diagnostics, 0u};
    auto status = pass->setPipeline(*pipeline_.pipeline);
    if (status.success)
        status = pass->setViewport({static_cast<float>(viewport.x),
            static_cast<float>(viewport.y),
            static_cast<float>(viewport.width),
            static_cast<float>(viewport.height),
            0.0f,
            1.0f});
    if (status.success)
        status = pass->setScissor({static_cast<std::int32_t>(viewport.x),
            static_cast<std::int32_t>(viewport.y),
            viewport.width,
            viewport.height});
    if (status.success)
        status = pass->bindVertexBuffer(1u, *corners_);
    std::uint32_t drawCount = 0u;
    for (const auto& batch : plan.batches()) {
        const auto atlas = std::find_if(atlases_.begin(), atlases_.end(), [&](const auto& owner) {
            return owner.source.get() == batch.atlas.get();
        });
        if (!status.success || atlas == atlases_.end()) {
            if (status.success)
                status = {false, "ARP-EDITOR-VIEWPORT-SPRITE-ATLAS-OWNER-MISSING"};
            break;
        }
        status = pass->bindDescriptorSet(0u, *atlas->set);
        if (status.success)
            status = pass->bindVertexBuffer(0u,
                *instances_,
                static_cast<std::uint64_t>(batch.first) * sizeof(scene2d::SpriteInstance));
        if (status.success)
            status = pass->draw(6u, batch.count, 0u, 0u);
        if (status.success)
            ++drawCount;
    }
    const auto ended = pass->end();
    if (!status.success)
        return {{}, status.diagnostics, 0u};
    if (!ended.success)
        return {{}, ended.diagnostics, 0u};
    if (colorTarget.state() != rhi::ResourceState::ShaderRead) {
        const auto transition =
            encoder->transition(colorTarget, colorTarget.state(), rhi::ResourceState::ShaderRead);
        if (!transition.success)
            return {{}, transition.diagnostics, 0u};
    }
    auto commands = encoder->finish();
    if (!commands)
        return {{}, "ARP-EDITOR-VIEWPORT-SPRITE-COMMAND-FINISH-FAILED", 0u};
    return {std::move(commands), {}, drawCount};
}

bool EditorSceneViewportSpriteRenderer::beforeResourceUnload(resource::NameView id) noexcept
{
    if (execution_) {
        if (execution_->diagnostic(*device_))
            return false;
        for (const auto& atlas : atlases_)
            if (execution_->resourceReuseDiagnostic(*atlas.texture))
                return false;
    } else if (device_) {
        device_->waitIdle();
    }
    for (auto it = atlases_.begin(); it != atlases_.end(); ++it) {
        if (it->source.id() == id) {
            atlases_.erase(it);
            break;
        }
    }
    return true;
}

bool EditorSceneViewportSpriteRenderer::beforeRetiredResourcesRelease() noexcept
{
    if (execution_) {
        if (execution_->diagnostic(*device_))
            return false;
        for (const auto& atlas : atlases_)
            if (execution_->resourceReuseDiagnostic(*atlas.texture))
                return false;
    } else if (device_) {
        device_->waitIdle();
    }
    return true;
}

} // namespace arp::editor
