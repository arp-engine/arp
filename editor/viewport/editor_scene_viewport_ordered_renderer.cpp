#include "editor/viewport/editor_scene_viewport_ordered_renderer.h"

#include "core/diagnostics/diagnostic_error.h"
#include "core/math/quad_geometry.h"
#include "editor/viewport/editor_scene_viewport_composite.h"
#include "editor/viewport/editor_scene_viewport_material_payload.h"
#include "editor/viewport/editor_scene_viewport_mesh_cache.h"
#include "editor/viewport/editor_scene_viewport_sprite_extraction.h"
#include "editor/viewport/editor_scene_viewport_sprite_projection.h"
#include "editor/viewport/editor_scene_viewport_sprite_renderer.h"
#include "editor/workspace/editor_shell.h"
#include "rhi/frontend/descriptor_set_validation.h"

namespace arp::editor {

struct EditorSceneViewportOrderedRenderer::GeometryState {
    static constexpr std::uint32_t maxMaterials = 64, maxDraws = 1024;

    struct Material {
        EditorSceneViewportMaterialPayload payload;
        core::UniquePtr<rhi::DescriptorArena> arena;
        core::UniquePtr<rhi::DescriptorSet> set;
        std::uint32_t setIndex = 0;
    };

    struct Draw {
        const renderer::MeshGpuBuffer* mesh = nullptr;
        std::shared_ptr<Material> material;
        EditorSceneViewportPbrPush push;
    };

    explicit GeometryState(std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution)
        : textures({64, 32u << 20, 16u << 20}, std::move(execution))
    {
        materials.reserve(maxMaterials);
        draws.reserve(maxDraws);
        projectedSprites.reserve(maxDraws);
    }

    // Explicit interactive viewport cache limits, independent of gameplay load.
    std::shared_ptr<resource::ResourceManager> resources;
    EditorSceneViewportMeshCache meshes{{64, 16u << 20}};
    EditorSceneViewportTextureCache textures;
    core::pmr::Vector<std::shared_ptr<Material>> materials;
    core::pmr::Vector<Draw> draws;
    scene2d::SpriteDrawPlan spritePlan{maxDraws};
    core::pmr::Vector<scene2d::SpriteInstance> projectedSprites;
    std::shared_ptr<EditorSceneViewportSpriteRenderer> sprites;
};

bool EditorSceneViewportOrderedRenderer::hasGeometry() const noexcept
{
    return geometry_ && !geometry_->draws.empty();
}

bool EditorSceneViewportOrderedRenderer::hasSprites() const noexcept
{
    return geometry_ && !geometry_->projectedSprites.empty();
}

void EditorSceneViewportOrderedRenderer::create(rhi::GpuDevice& device,
    std::shared_ptr<rhi::RenderPipeline> pipeline,
    std::shared_ptr<rhi::RenderPipeline> pbr,
    std::shared_ptr<rhi::OriginalOrderedGraphicsExecution> execution,
    const shader::ShaderReflection& reflection,
    std::shared_ptr<rhi::RenderPipeline> sprite)
{
    if (pipeline_ || !pipeline || !pbr || !execution
        || reflection.sourceSha256 != pbr->shaderReflection().sourceSha256)
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-PIPELINE");
    auto corners = core::shareUnique(device.createBuffer({"EditorEnvironmentCorners",
        sizeof(core::QuadTriangleTopology::corners),
        rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::CopyDst,
        rhi::MemoryUsage::CpuToGpu,
        rhi::ResourceState::ShaderRead}));
    if (!corners)
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-CORNERS");
    core::requireSuccessfulResult(device.writeBuffer(*corners,
                                      core::QuadTriangleTopology::corners.data(),
                                      sizeof(core::QuadTriangleTopology::corners)),
        "EDITOR-ORDERED-VIEWPORT-CORNERS-UPLOAD");
    pipeline_ = std::move(pipeline);
    pbr_ = std::move(pbr);
    sprite_ = std::move(sprite);
    execution_ = std::move(execution);
    pbrReflection_ = reflection;
    device_ = &device;
    corners_ = std::move(corners);
}

void EditorSceneViewportOrderedRenderer::prepareGeometry(
    EditorShell& shell, const EditorSceneViewportFrame& frame)
{
    if (geometry_ && geometry_.use_count() != 1)
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-GRAPH-RESERVED");
    if (!geometry_ || geometry_->resources.get() != &shell.projectResources()) {
        if (geometry_) {
            if (geometry_.use_count() != 1)
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-GRAPH-RESERVED");
            if (const auto* id = geometry_->meshes.clearAfterOrderedIssue(*execution_))
                throw core::DiagnosticError(id);
            if (const auto* id = geometry_->textures.clearAfterOrderedIssue())
                throw core::DiagnosticError(id);
        }
        auto candidate = core::makeShared<GeometryState>(execution_);
        candidate->resources = shell.retainProjectResourceManager();
        geometry_ = std::move(candidate);
    }
    auto& state = *geometry_;
    state.draws.clear();
    if (frame.drawItems.empty())
        return;
    const auto resolved = resolveEditorSceneViewportResources(frame, *state.resources);
    if (!resolved.valid()) {
        shell.reportViewportDiagnostic(editorSceneViewportDiagnosticCode(resolved.frameDiagnostic));
        for (const auto& item : resolved.items)
            if (item.diagnostic != EditorSceneViewportDiagnostic::None) {
                shell.reportViewportDiagnostic(editorSceneViewportDiagnosticCode(item.diagnostic));
                break;
            }
        return;
    }
    if (resolved.items.size() > GeometryState::maxDraws)
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-DRAW-BUDGET");
    const auto& camera = shell.sceneViewportCamera();
    const auto vp =
        camera.viewProjectionMatrix(float(frame.viewport.width) / frame.viewport.height);
    const auto eye = camera.position();
    for (std::size_t i = 0; i < resolved.items.size(); ++i) {
        const auto& item = resolved.items[i];
        const auto mesh = state.meshes.acquire(*device_, *state.resources, item);
        if (!mesh.valid())
            throw core::DiagnosticError(
                editorSceneViewportMeshCacheDiagnosticCode(mesh.diagnostic));
        auto payload = prepareEditorSceneViewportMaterialPayload(
            item, pbrReflection_, *device_, *state.resources, state.textures);
        if (!payload.valid())
            throw core::DiagnosticError(payload.detail);
        std::shared_ptr<GeometryState::Material> material;
        for (const auto& existing : state.materials) {
            const auto& a = existing->payload;
            const auto& b = payload.payload;
            bool same = a.materialResourceId == b.materialResourceId
                && a.materialGeneration == b.materialGeneration
                && a.sourceGeneration == b.sourceGeneration
                && a.shaderGeneration == b.shaderGeneration;
            for (std::size_t slot = 0; same && slot < a.textures.size(); ++slot)
                same = a.textures[slot].resourceId == b.textures[slot].resourceId
                    && a.textures[slot].generation == b.textures[slot].generation;
            if (same) {
                material = existing;
                break;
            }
        }
        if (!material) {
            if (state.materials.size() == GeometryState::maxMaterials)
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-MATERIAL-BUDGET");
            const auto writes = buildEditorSceneViewportDescriptorWrites(payload.payload);
            if (!writes.valid())
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-MATERIAL-TEXTURES");
            auto candidate = core::makeShared<GeometryState::Material>();
            candidate->payload = std::move(payload.payload);
            candidate->setIndex = writes.writes.front().set;
            const auto* slot =
                rhi::findDescriptorSetSlot(pbr_->layout()->desc(), candidate->setIndex);
            const auto* layout = slot
                ? rhi::findDescriptorSetLayout(pbr_->layout()->desc(), slot->layoutName)
                : nullptr;
            if (!layout)
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-MATERIAL-LAYOUT");
            candidate->arena = device_->createDescriptorArena({"EditorOrderedMaterial", 1, 6});
            if (!candidate->arena)
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-MATERIAL-ARENA");
            candidate->set =
                candidate->arena->allocateDescriptorSet({"EditorOrderedMaterial", *layout});
            if (!candidate->set)
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-MATERIAL-SET");
            for (const auto& write : writes.writes)
                core::requireSuccessfulResult(
                    candidate->set->write(write.write), "EDITOR-ORDERED-MATERIAL-WRITE");
            state.materials.push_back(candidate);
            material = std::move(candidate);
        }
        // Opaque is the desktop viewport's current admitted material path.
        // Alpha-mask/blend get explicit pipeline variants in the next slice.
        if (material->payload.alphaMode != "OPAQUE")
            throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-ALPHA-PIPELINE-REQUIRED");
        const auto& t = frame.drawItems[i].worldTransform;
        const auto model = core::multiply(core::Mat4::translation(t.translation),
            core::multiply(core::Mat4::rotation(t.rotation), core::Mat4::scale(t.scale)));
        const auto& m = material->payload;
        const EditorSceneViewportDrawItemSubmission submission{mesh.mesh,
            material->set.get(),
            material->setIndex,
            vp,
            model,
            core::Mat4::identity(),
            {eye.x, eye.y, eye.z, 1},
            m.baseColorFactor,
            {m.emissiveFactor[0], m.emissiveFactor[1], m.emissiveFactor[2], m.metallicFactor},
            {m.roughnessFactor, 1, m.alphaCutoff, 0}};
        state.draws.push_back(
            {mesh.mesh, std::move(material), buildEditorSceneViewportPbrPush(submission)});
    }
}

void EditorSceneViewportOrderedRenderer::prepareSprites(
    EditorShell& shell, const scene::SceneTraversalSnapshot& snapshot, rhi::Extent2D extent)
{
    auto& state = *geometry_;
    state.projectedSprites.clear();
    core::pmr::Vector<scene2d::SpriteDrawItem> items;
    core::pmr::String diagnostic;
    if (!extractEditorSceneViewportSprites(
            shell, snapshot, extent, GeometryState::maxDraws, items, diagnostic)
        || !state.spritePlan.build(*state.resources, items))
        throw core::DiagnosticError(
            diagnostic.empty() ? state.spritePlan.diagnostic() : diagnostic);
    if (state.spritePlan.instances().empty())
        return;
    if (!state.sprites) {
        auto candidate = core::makeShared<EditorSceneViewportSpriteRenderer>();
        if (!candidate->createOrdered(*device_,
                *state.resources,
                sprite_,
                execution_,
                GeometryState::maxDraws,
                64,
                16u << 20,
                diagnostic))
            throw core::DiagnosticError(diagnostic);
        state.sprites = std::move(candidate);
    }
    if (!state.sprites->prepare(state.spritePlan, diagnostic))
        throw core::DiagnosticError(diagnostic);
    const auto vp =
        shell.sceneViewportCamera().viewProjectionMatrix(float(extent.width) / extent.height);
    for (const auto& instance : state.spritePlan.instances()) {
        scene2d::SpriteInstance projected;
        if (!projectEditorSceneViewportSpriteInstance(instance, vp, projected))
            throw core::DiagnosticError("EDITOR-SPRITE-PROJECTION-NONFINITE");
        state.projectedSprites.push_back(projected);
    }
}

void EditorSceneViewportOrderedRenderer::addGeometryPass(render_graph::FrameGraph& graph,
    render_graph::TextureHandle color,
    render_graph::TextureHandle depth,
    rhi::Extent3D extent)
{
    if (!geometry_ || geometry_->draws.empty())
        return;
    auto pass = graph.addPass("EditorViewportPbr");
    pass.writeAttachment(
            color, rhi::ResourceState::ColorAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store)
        .writeAttachment(
            depth, rhi::ResourceState::DepthAttachment, rhi::LoadOp::Load, rhi::StoreOp::Store);
    for (const auto& draw : geometry_->draws) {
        // Aliasing owners retain the entire cache, including these original
        // resource wrappers. No no-op deleter or unowned graph borrow is used.
        auto& vertex = draw.mesh->vertexBuffer();
        auto& index = draw.mesh->indexBuffer();
        pass.access(graph.importBufferRef({geometry_, &vertex}),
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::VertexAttributeRead},
            0,
            vertex.desc().sizeBytes);
        pass.access(graph.importBufferRef({geometry_, &index}),
            {rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::VertexInput,
                rhi::ResourceAccess::IndexRead},
            0,
            index.desc().sizeBytes);
        for (const auto& binding : draw.material->payload.textures) {
            if (!binding.texture)
                throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-TEXTURE-OWNER");
            pass.read(graph.importTextureRef({geometry_, binding.texture}),
                rhi::ResourceState::ShaderRead);
        }
    }
    pass.executeRenderPass([state = geometry_, pipeline = pbr_, extent](
                               render_graph::RenderPassContext& context) {
        auto& encoder = context.renderPass();
        core::requireSuccessfulResult(
            encoder.setPipeline(*pipeline), "EDITOR-ORDERED-PBR-PIPELINE");
        core::requireSuccessfulResult(
            encoder.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
            "EDITOR-ORDERED-PBR-VIEWPORT");
        core::requireSuccessfulResult(
            encoder.setScissor({0, 0, extent.width, extent.height}), "EDITOR-ORDERED-PBR-SCISSOR");
        for (const auto& draw : state->draws) {
            core::requireSuccessfulResult(encoder.bindVertexBuffer(0, draw.mesh->vertexBuffer()),
                "EDITOR-ORDERED-PBR-VERTEX");
            core::requireSuccessfulResult(
                encoder.bindIndexBuffer(draw.mesh->indexBuffer(), rhi::IndexFormat::Uint32),
                "EDITOR-ORDERED-PBR-INDEX");
            core::requireSuccessfulResult(
                encoder.bindDescriptorSet(draw.material->setIndex, *draw.material->set),
                "EDITOR-ORDERED-PBR-MATERIAL");
            for (const auto& submesh : draw.mesh->submeshes()) {
                core::requireSuccessfulResult(
                    encoder.pushConstants(rhi::ShaderStage::Vertex | rhi::ShaderStage::Fragment,
                        0,
                        sizeof(draw.push),
                        &draw.push),
                    "EDITOR-ORDERED-PBR-PUSH");
                core::requireSuccessfulResult(
                    encoder.drawIndexed(submesh.indexCount, 1, submesh.firstIndex, 0, 0),
                    "EDITOR-ORDERED-PBR-DRAW");
            }
        }
    });
}

void EditorSceneViewportOrderedRenderer::resize(rhi::GpuDevice& device,
    renderer::GuiOrderedRenderer& gui,
    rhi::Extent2D extent,
    bool sampleFlipY)
{
    if (!pipeline_ || !extent.width || !extent.height)
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-EXTENT");
    if (color_ && color_->desc().extent.width == extent.width
        && color_->desc().extent.height == extent.height)
        return;
    const auto profile = EditorSceneViewportSurfaceProfile::baseline();
    auto color = core::shareUnique(device.createTexture({"EditorOrderedViewportColor",
        {extent.width, extent.height, 1},
        profile.color,
        rhi::TextureUsage::RenderAttachment | rhi::TextureUsage::Sampled,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined}));
    auto depth = core::shareUnique(device.createTexture({"EditorOrderedViewportDepth",
        {extent.width, extent.height, 1},
        profile.depth,
        rhi::TextureUsage::RenderAttachment,
        rhi::MemoryUsage::GpuOnly,
        rhi::ResourceState::Undefined}));
    if (!color || !depth)
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-TARGETS");
    if (color_)
        core::requireSuccessfulResult(
            gui.unregisterTexture(kEditorSceneViewportTextureId), "EDITOR-ORDERED-VIEWPORT-RETIRE");
    renderer::GuiOrderedTextureDesc registration;
    registration.resourceId = kEditorSceneViewportTextureId;
    registration.texture = color;
    registration.externalTexture = true;
    registration.flipUvY = sampleFlipY;
    core::requireSuccessfulResult(
        gui.registerTexture(registration), "EDITOR-ORDERED-VIEWPORT-REGISTER");
    color_ = std::move(color);
    depth_ = std::move(depth);
}

void EditorSceneViewportOrderedRenderer::addPasses(render_graph::FrameGraph& graph,
    const EditorSceneViewportCamera& camera,
    const EditorSceneViewportSky& sky)
{
    if (!valid())
        throw core::DiagnosticError("EDITOR-ORDERED-VIEWPORT-NOT-CREATED");
    const auto color = graph.importTextureRef(color_);
    const auto depth = graph.importTextureRef(depth_);
    const auto corners = graph.importBufferRef(corners_);
    const auto extent = color_->desc().extent;
    for (const bool grid : {false, true}) {
        if (grid)
            addGeometryPass(graph, color, depth, extent);
        if (grid && hasSprites()) {
            core::pmr::String diagnostic;
            if (!geometry_->sprites->addPass(graph,
                    color,
                    depth,
                    geometry_->spritePlan,
                    geometry_->projectedSprites,
                    diagnostic))
                throw core::DiagnosticError(diagnostic);
        }
        const auto push = buildEditorSceneViewportEnvironmentPush(
            camera, float(extent.width) / float(extent.height), grid, sky);
        auto pass = graph.addPass(grid ? "EditorGroundGrid" : "EditorSkybox");
        pass.access(corners,
                {rhi::ResourceState::ShaderRead,
                    rhi::PipelineStage::VertexInput,
                    rhi::ResourceAccess::VertexAttributeRead},
                0,
                corners_->desc().sizeBytes)
            .writeAttachment(color,
                rhi::ResourceState::ColorAttachment,
                grid ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
                rhi::StoreOp::Store)
            .writeAttachment(depth,
                rhi::ResourceState::DepthAttachment,
                grid ? rhi::LoadOp::Load : rhi::LoadOp::Clear,
                rhi::StoreOp::Store);
        pass.executeRenderPass([pipeline = pipeline_, vertices = corners_, push, extent](
                                   render_graph::RenderPassContext& context) {
            auto& encoder = context.renderPass();
            core::requireSuccessfulResult(
                encoder.setPipeline(*pipeline), "EDITOR-ORDERED-ENV-PIPELINE");
            core::requireSuccessfulResult(
                encoder.bindVertexBuffer(0, *vertices), "EDITOR-ORDERED-ENV-VERTICES");
            core::requireSuccessfulResult(
                encoder.setViewport({0, 0, float(extent.width), float(extent.height), 0, 1}),
                "EDITOR-ORDERED-ENV-VIEWPORT");
            core::requireSuccessfulResult(encoder.setScissor({0, 0, extent.width, extent.height}),
                "EDITOR-ORDERED-ENV-SCISSOR");
            core::requireSuccessfulResult(
                encoder.pushConstants(rhi::ShaderStage::Fragment, 0, sizeof(push), &push),
                "EDITOR-ORDERED-ENV-PUSH");
            core::requireSuccessfulResult(
                encoder.draw(core::QuadTriangleTopology::vertexCount, 1, 0, 0),
                "EDITOR-ORDERED-ENV-DRAW");
        });
    }
}

} // namespace arp::editor
