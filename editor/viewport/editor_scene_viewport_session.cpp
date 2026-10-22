#include "editor/viewport/editor_scene_viewport_session.h"

#include "2d/scene_sprite_extractor.h"
#include "2d/sprite_animation_component.h"
#include "2d/sprite_draw_plan.h"
#include "2d/tile_map_resource_component.h"
#include "editor/viewport/editor_scene_viewport_frame_builder.h"
#include "editor/viewport/editor_scene_viewport_material_payload.h"
#include "editor/viewport/editor_scene_viewport_resource_resolver.h"
#include "editor/viewport/editor_scene_viewport_sprite_pass_contract.h"
#include "editor/viewport/editor_scene_viewport_sprite_projection.h"
#include "editor/viewport/editor_scene_viewport_tilemap_visibility.h"

#include <algorithm>
#include <cstddef>

namespace arp::editor {

EditorSceneViewportSession::EditorSceneViewportSession(EditorSceneViewportSessionConfig config)
    : config_(config), meshCache_(config.meshCacheBudget), textureCache_(config.textureCacheBudget)
{}

bool EditorSceneViewportSession::initialize(EditorShell& shell,
    rhi::GpuDevice& device,
    const EditorSceneViewportShaderArtifacts& artifacts,
    const EditorSceneViewportShaderArtifacts& spriteArtifacts,
    const EditorSceneViewportShaderArtifacts& environmentArtifacts,
    rhi::Extent2D extent,
    core::pmr::String& diagnostics)
{
    if (device_ != nullptr && device_ != &device) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DEVICE-MISMATCH";
        return false;
    }
    if (config_.maxSnapshotObjects == 0u || config_.maxDrawCalls == 0u
        || config_.textureCacheBudget.maxResidentTextures > UINT32_MAX) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-BUDGET-INVALID";
        return false;
    }
    const auto package = loadEditorSceneViewportShaderPackage(
        artifacts, rhi::TextureFormat::Rgba16Float, rhi::TextureFormat::D32Float, 1u);
    if (!package.valid()) {
        diagnostics = editorSceneViewportShaderPackageDiagnosticCode(package.diagnostic);
        return false;
    }
    auto devicePipeline = createEditorSceneViewportDevicePipeline(device, package);
    if (!devicePipeline.valid()) {
        diagnostics = devicePipeline.backendDiagnostics;
        return false;
    }
    if (!environment_.initialize(device, environmentArtifacts, diagnostics))
        return false;
    shaderReflection_ = package.shaderReflection;
    spriteArtifacts_ = spriteArtifacts;
    devicePipeline_ = std::move(devicePipeline);
    device_ = &device;
    return renderIfDirty(shell, extent, diagnostics);
}

bool EditorSceneViewportSession::renderIfDirty(
    EditorShell& shell, rhi::Extent2D extent, core::pmr::String& diagnostics)
{
    if (device_ == nullptr || !devicePipeline_.valid()) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-NOT-INITIALIZED";
        return false;
    }
    if (extent.width == 0u || extent.height == 0u) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-EXTENT-INVALID";
        return false;
    }
    if (renderedResourceOwner_ && renderedResourceOwner_ != &shell.projectResources()) {
        device_->waitIdle();
        materialDescriptors_.clear();
        spriteRenderer_.reset();
        meshCache_.clearAfterDeviceIdle();
        textureCache_.clearAfterDeviceIdle();
        hasRendered_ = false;
    }
    renderedResourceOwner_ = &shell.projectResources();
    if (hasRendered_ && renderedStateVersion_ == shell.stateVersion()
        && composite_.extent().width == extent.width
        && composite_.extent().height == extent.height) {
        diagnostics = renderedSceneDiagnostics_;
        return true;
    }
    diagnostics.clear();
    return render(shell, extent, diagnostics);
}

bool EditorSceneViewportSession::render(
    EditorShell& shell, rhi::Extent2D extent, core::pmr::String& diagnostics)
{
    if (hasRendered_)
        device_->waitIdle();
    if (!composite_.valid() || composite_.extent().width != extent.width
        || composite_.extent().height != extent.height) {
        if (!composite_.create(*device_, extent)) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-TARGET-CREATE-FAILED";
            return false;
        }
    }

    const auto snapshot = shell.sceneDocument().scene().extractTraversalSnapshot(
        scene::SceneTraversalOptions{config_.maxSnapshotObjects});
    const auto frame = buildEditorSceneViewportFrame(shell.sceneDocument().scene(),
        snapshot,
        shell.stateVersion(),
        extent.width,
        extent.height,
        {0u, 0u, extent.width, extent.height},
        &shell.projectResources(),
        config_.maxDrawCalls);
    if (!renderEnvironment(shell, false, frame.frame.sky, diagnostics))
        return false;
    if (!frame.valid()) {
        diagnostics = editorSceneViewportDiagnosticCode(frame.validation.diagnostic);
        if (frame.validation.diagnostic == EditorSceneViewportDiagnostic::GeometryBudgetExceeded)
            return false;
        return finishEnvironment(shell, frame.frame.sky, diagnostics);
    }

    scene2d::SceneSpriteExtractor spriteExtractor(
        shell.projectResources(), config_.maxDrawCalls, true);
    if (!spriteExtractor.extract(shell.sceneDocument().scene(), snapshot)) {
        diagnostics = spriteExtractor.diagnostic();
        return false;
    }
    core::pmr::Vector<scene2d::SpriteDrawItem> spriteItems{&core::defaultCpuMemoryResource()};
    spriteItems.reserve(spriteExtractor.items().size());
    spriteItems.insert(
        spriteItems.end(), spriteExtractor.items().begin(), spriteExtractor.items().end());
    const auto& viewportCamera = shell.sceneViewportCamera();
    const auto visibleWorld = editorSceneViewportTileMapVisibleWorld(
        viewportCamera.viewProjectionMatrix(
            static_cast<float>(extent.width) / static_cast<float>(extent.height)),
        extent.width,
        extent.height,
        0.0f);
    for (const auto& node : snapshot.nodes()) {
        if (!node.activeInHierarchy)
            continue;
        const auto* object = shell.sceneDocument().scene().findObject(node.objectId);
        if (object == nullptr) {
            diagnostics = "ARP-EDITOR-VIEWPORT-TILEMAP-SNAPSHOT-OBJECT-MISSING";
            return false;
        }
        for (const auto& component : object->components()) {
            if (!component->enabled())
                continue;
            auto* tileMap = dynamic_cast<scene2d::TileMapResourceComponent*>(component.get());
            if (tileMap == nullptr)
                continue;
            if (!visibleWorld.valid()) {
                diagnostics = "ARP-EDITOR-VIEWPORT-TILEMAP-VISIBLE-WORLD-INVALID";
                return false;
            }
            auto* runtime = tileMap->runtime();
            if (runtime == nullptr || !runtime->updateResidency(visibleWorld.worldBounds)
                || !runtime->extract(0.0)) {
                diagnostics = runtime != nullptr && runtime->diagnostic() != nullptr
                    ? runtime->diagnostic()
                    : "ARP-EDITOR-VIEWPORT-TILEMAP-RUNTIME-UNAVAILABLE";
                return false;
            }
            if (runtime->items().size() > config_.maxDrawCalls - spriteItems.size()) {
                diagnostics = "ARP-EDITOR-VIEWPORT-TILEMAP-DRAW-BUDGET-EXCEEDED";
                return false;
            }
            spriteItems.insert(spriteItems.end(), runtime->items().begin(), runtime->items().end());
        }
    }
    scene2d::SpriteDrawPlan spritePlan(config_.maxDrawCalls);
    if (!spritePlan.build(shell.projectResources(), spriteItems)) {
        diagnostics = spritePlan.diagnostic();
        return false;
    }
    core::pmr::Vector<scene2d::SpriteInstance> projectedSpriteInstances{
        &core::defaultCpuMemoryResource()};
    if (!spritePlan.instances().empty()) {
        std::uint64_t uploadBytes = static_cast<std::uint64_t>(spritePlan.instances().size())
            * sizeof(scene2d::SpriteInstance);
        std::uint32_t uniqueAtlasCount = 0u;
        for (std::size_t batchIndex = 0u; batchIndex < spritePlan.batches().size(); ++batchIndex) {
            const auto& batch = spritePlan.batches()[batchIndex];
            const bool firstUse = std::none_of(spritePlan.batches().begin(),
                spritePlan.batches().begin() + batchIndex,
                [&](const auto& earlier) {
                    return earlier.atlas.get() == batch.atlas.get();
                });
            if (!firstUse)
                continue;
            ++uniqueAtlasCount;
            const auto& image = batch.atlas.get()->image();
            const auto rowBytes = static_cast<std::uint64_t>(image.width) * 4u;
            const auto alignedRowBytes = (rowBytes + rhi::kTextureUploadRowPitchAlignment - 1u)
                / rhi::kTextureUploadRowPitchAlignment * rhi::kTextureUploadRowPitchAlignment;
            if (image.height == 0u || alignedRowBytes > UINT64_MAX / image.height
                || uploadBytes > UINT64_MAX - alignedRowBytes * image.height) {
                diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-UPLOAD-SIZE-OVERFLOW";
                return false;
            }
            uploadBytes += alignedRowBytes * image.height;
        }
        const EditorSceneViewportSpritePassDemand spriteDemand{
            kEditorSceneViewportSpritePassContractVersion,
            static_cast<std::uint32_t>(spritePlan.instances().size()),
            uniqueAtlasCount,
            uploadBytes};
        const EditorSceneViewportSpritePassBudget spriteBudget{config_.maxDrawCalls,
            static_cast<std::uint32_t>(config_.textureCacheBudget.maxResidentTextures),
            config_.textureCacheBudget.maxUploadStagingBytes};
        const auto spriteValidation =
            validateEditorSceneViewportSpritePass(spriteDemand, spriteBudget);
        if (!spriteValidation.valid()) {
            diagnostics = editorSceneViewportSpritePassDiagnosticCode(spriteValidation.diagnostic);
            return false;
        }
        if (!spriteRenderer_)
            spriteRenderer_ = core::makeUnique<EditorSceneViewportSpriteRenderer>();
        if (!spriteRenderer_->valid()
            && !spriteRenderer_->create(*device_,
                shell.projectResources(),
                spriteArtifacts_,
                config_.maxDrawCalls,
                static_cast<std::uint32_t>(config_.textureCacheBudget.maxResidentTextures),
                config_.textureCacheBudget.maxUploadStagingBytes,
                diagnostics))
            return false;
        if (!spriteRenderer_->prepare(spritePlan, diagnostics))
            return false;
        const auto spriteCapabilities = device_->capabilities();
        const auto spritePlanLowering =
            lowerEditorSceneViewportSpritePass(EditorSceneViewportSpritePassBackend::Vulkan);
        const auto spriteAdmission = admitEditorSceneViewportSpritePass(spritePlanLowering,
            EditorSceneViewportSpritePassBackend::Vulkan,
            spriteCapabilities.backend == rhi::Backend::Vulkan
                && device_->backend() == rhi::Backend::Vulkan && spriteCapabilities.core.graphics,
            spriteRenderer_->valid());
        if (!spriteAdmission.executable()) {
            diagnostics =
                editorSceneViewportSpritePassAdmissionDiagnosticCode(spriteAdmission.diagnostic);
            return false;
        }
        projectedSpriteInstances.reserve(spritePlan.instances().size());
        const auto& spriteCamera = shell.sceneViewportCamera();
        const auto spriteViewProjection = spriteCamera.viewProjectionMatrix(
            static_cast<float>(extent.width) / static_cast<float>(extent.height));
        for (const auto& instance : spritePlan.instances()) {
            scene2d::SpriteInstance projected;
            if (!projectEditorSceneViewportSpriteInstance(
                    instance, spriteViewProjection, projected)) {
                diagnostics = "ARP-EDITOR-VIEWPORT-SPRITE-PROJECTION-NONFINITE";
                return false;
            }
            projectedSpriteInstances.push_back(projected);
        }
    }

    const auto resolved =
        resolveEditorSceneViewportResources(frame.frame, shell.projectResources());
    const auto totalDrawItems = resolved.items.size() + spritePlan.instances().size();
    if (!resolved.valid()) {
        diagnostics = editorSceneViewportDiagnosticCode(resolved.frameDiagnostic);
        return finishEnvironment(shell, frame.frame.sky, diagnostics);
    }
    if (totalDrawItems > config_.maxDrawCalls) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DRAW-BUDGET-EXCEEDED";
        return false;
    }

    const auto& pipelineLayout = devicePipeline_.resources.layout->desc();
    materialDescriptors_.clear();
    core::pmr::Vector<EditorSceneViewportDrawItemSubmission> drawItems{
        &core::defaultCpuMemoryResource()};
    const auto& camera = shell.sceneViewportCamera();
    const auto viewProjection = camera.viewProjectionMatrix(
        static_cast<float>(extent.width) / static_cast<float>(extent.height));
    for (std::size_t index = 0u; index < resolved.items.size(); ++index) {
        const auto& item = resolved.items[index];
        const auto mesh = meshCache_.acquire(*device_, shell.projectResources(), item);
        if (!mesh.valid()) {
            diagnostics = editorSceneViewportMeshCacheDiagnosticCode(mesh.diagnostic);
            return false;
        }
        const auto material = prepareEditorSceneViewportMaterialPayload(
            item, shaderReflection_, *device_, shell.projectResources(), textureCache_);
        if (!material.valid()) {
            diagnostics = editorSceneViewportMaterialPayloadDiagnosticCode(material.diagnostic);
            return false;
        }
        const auto writes = buildEditorSceneViewportDescriptorWrites(material.payload);
        if (!writes.valid()) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DESCRIPTOR-WRITES-INVALID";
            return false;
        }
        const auto descriptorSetIndex = writes.writes.front().set;
        if (std::any_of(writes.writes.begin(), writes.writes.end(), [&](const auto& write) {
                return write.set != descriptorSetIndex;
            })) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DESCRIPTORS-SPAN-SETS";
            return false;
        }
        const auto setSlot = std::find_if(pipelineLayout.descriptorSets.begin(),
            pipelineLayout.descriptorSets.end(),
            [&](const auto& entry) {
                return entry.set == descriptorSetIndex;
            });
        if (setSlot == pipelineLayout.descriptorSets.end()) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DESCRIPTOR-SET-LAYOUT-MISSING";
            return false;
        }
        const auto descriptorLayout = std::find_if(pipelineLayout.descriptorLayouts.begin(),
            pipelineLayout.descriptorLayouts.end(),
            [&](const auto& entry) {
                return entry.name == setSlot->layoutName;
            });
        if (descriptorLayout == pipelineLayout.descriptorLayouts.end()) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DESCRIPTOR-LAYOUT-PAYLOAD-MISSING";
            return false;
        }

        rhi::DescriptorArenaDesc arenaDesc;
        arenaDesc.label = "EditorViewportSessionMaterialArena";
        arenaDesc.maxSets = 1u;
        arenaDesc.maxDescriptors = static_cast<std::uint32_t>(writes.writes.size());
        auto arena = device_->createDescriptorArena(arenaDesc);
        if (!arena) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DESCRIPTOR-ARENA-CREATE-FAILED";
            return false;
        }
        rhi::DescriptorSetDesc setDesc;
        setDesc.label = "EditorViewportSessionMaterialSet";
        setDesc.layout = *descriptorLayout;
        auto descriptorSet = arena->allocateDescriptorSet(setDesc);
        if (!descriptorSet) {
            diagnostics = "ARP-EDITOR-VIEWPORT-SESSION-DESCRIPTOR-SET-ALLOCATE-FAILED";
            return false;
        }
        for (const auto& write : writes.writes) {
            const auto result = descriptorSet->write(write.write);
            if (!result.success) {
                diagnostics = result.diagnostics;
                return false;
            }
        }
        materialDescriptors_.push_back({std::move(arena), std::move(descriptorSet)});

        const auto& draw = frame.frame.drawItems[index];
        const auto& world = draw.worldTransform;
        const auto model = core::multiply(core::Mat4::translation(world.translation),
            core::multiply(core::Mat4::rotation(world.rotation), core::Mat4::scale(world.scale)));
        drawItems.push_back({
            mesh.mesh,
            materialDescriptors_.back().set.get(),
            descriptorSetIndex,
            viewProjection,
            model,
            core::Mat4::identity(),
            {camera.position().x, camera.position().y, camera.position().z, 1.0f},
            material.payload.baseColorFactor,
            {material.payload.emissiveFactor[0],
                material.payload.emissiveFactor[1],
                material.payload.emissiveFactor[2],
                material.payload.metallicFactor},
            {material.payload.roughnessFactor, 1.0f, material.payload.alphaCutoff, 0.0f},
        });
    }
    if (!drawItems.empty()) {
        auto recorded = recordEditorSceneViewportPass(*device_,
            *devicePipeline_.resources.pipeline,
            *composite_.colorTarget(),
            *composite_.depthTarget(),
            {0u, 0u, extent.width, extent.height},
            drawItems,
            {config_.maxDrawCalls},
            true);
        if (!recorded.success()) {
            diagnostics = recorded.diagnostics;
            return false;
        }
        const auto submitted = device_->queue().submit(*recorded.commandBuffer);
        if (!submitted.success) {
            diagnostics = submitted.diagnostics;
            return false;
        }
        device_->waitIdle();
    }
    if (!spritePlan.instances().empty()) {
        auto recorded = spriteRenderer_->record(*composite_.colorTarget(),
            *composite_.depthTarget(),
            {0u, 0u, extent.width, extent.height},
            spritePlan,
            projectedSpriteInstances);
        if (!recorded.success()) {
            diagnostics = recorded.diagnostics;
            return false;
        }
        const auto submitted = device_->queue().submit(*recorded.commandBuffer);
        if (!submitted.success) {
            diagnostics = submitted.diagnostics;
            return false;
        }
        device_->waitIdle();
    }
    return finishEnvironment(shell, frame.frame.sky, diagnostics);
}

bool EditorSceneViewportSession::renderEnvironment(EditorShell& shell,
    bool gridPass,
    const EditorSceneViewportSky& sky,
    core::pmr::String& diagnostics)
{
    auto recorded = environment_.record(*device_,
        *composite_.colorTarget(),
        *composite_.depthTarget(),
        shell.sceneViewportCamera(),
        gridPass,
        sky);
    if (!recorded.success()) {
        diagnostics = recorded.diagnostics;
        return false;
    }
    const auto submitted = device_->queue().submit(*recorded.commandBuffer);
    if (!submitted.success) {
        diagnostics = submitted.diagnostics;
        return false;
    }
    device_->waitIdle();
    return true;
}

bool EditorSceneViewportSession::finishEnvironment(
    EditorShell& shell, const EditorSceneViewportSky& sky, core::pmr::String& diagnostics)
{
    if (!renderEnvironment(shell, true, sky, diagnostics))
        return false;
    renderedSceneDiagnostics_ = diagnostics;
    renderedStateVersion_ = shell.stateVersion();
    hasRendered_ = true;
    return true;
}

bool EditorSceneViewportSession::valid() const noexcept
{
    return device_ != nullptr && devicePipeline_.valid() && composite_.valid()
        && environment_.valid() && hasRendered_;
}

} // namespace arp::editor
