#include "3d/camera_parameters.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/gui/gui_draw_plan.h"
#include "renderer/gui/gui_rhi_draw_commands.h"
#include "renderer/particles/particle_draw_plan.h"
#include "renderer/particles/particle_rhi_draw_commands.h"
#include "renderer/postprocess/fxaa_draw_plan.h"
#include "renderer/postprocess/fxaa_rhi_pass.h"
#include "samples/stylized/stylized_postprocess.h"
#include "samples/stylized/stylized_scene_draw_plan.h"
#include "samples/stylized/stylized_scene_rhi_draw_commands.h"
#include "samples/stylized/stylized_shadows.h"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace {

struct FakeCommandList final : arp::rhi::CommandList {
    std::pmr::vector<std::pmr::string> events;
    std::pmr::vector<std::pmr::string> descriptorSetNames;
    std::pmr::vector<uint32_t> descriptorSetIndices;
    std::pmr::vector<uint64_t> pushSizes;
    std::pmr::vector<uint32_t> indexedDrawCounts;
    std::pmr::vector<uint32_t> drawCounts;
    bool renderPassHadDepth = false;
    arp::rhi::LoadOp lastColorLoadOp = arp::rhi::LoadOp::DontCare;

    void begin(const std::pmr::string& label) override { events.push_back("begin:" + label); }

    void transition(arp::rhi::Texture&, arp::rhi::ResourceState, arp::rhi::ResourceState) override
    {
        events.push_back("transition:texture");
    }

    void transition(arp::rhi::Buffer&, arp::rhi::ResourceState, arp::rhi::ResourceState) override
    {
        events.push_back("transition:buffer");
    }

    void clearColor(arp::rhi::Texture&, arp::rhi::Color) override
    {
        events.push_back("clearColor");
    }

    void beginRenderPass(const arp::rhi::RenderPassDesc& desc) override
    {
        events.push_back("beginRenderPass:" + desc.name);
        renderPassHadDepth = desc.depthAttachment != nullptr;
        lastColorLoadOp = desc.colorLoadOp;
    }

    void endRenderPass() override { events.push_back("endRenderPass"); }

    void bindGraphicsPipeline(arp::rhi::GraphicsPipeline& pipeline) override
    {
        events.push_back("bindGraphicsPipeline:" + pipeline.name());
    }

    void bindComputePipeline(arp::rhi::ComputePipeline&) override
    {
        events.push_back("bindComputePipeline");
    }

    void bindDescriptorSet(arp::rhi::DescriptorSet& descriptorSet) override
    {
        events.push_back("bindDescriptorSet");
        descriptorSetNames.push_back(descriptorSet.name());
        descriptorSetIndices.push_back(0);
    }

    void bindDescriptorSet(arp::rhi::DescriptorSet& descriptorSet, uint32_t setIndex) override
    {
        events.push_back("bindDescriptorSet");
        descriptorSetNames.push_back(descriptorSet.name());
        descriptorSetIndices.push_back(setIndex);
    }

    void bindVertexBuffer(arp::rhi::Buffer& buffer) override
    {
        events.push_back("bindVertexBuffer:" + buffer.name());
    }

    void bindIndexBuffer(arp::rhi::Buffer& buffer) override
    {
        events.push_back("bindIndexBuffer:" + buffer.name());
    }

    void pushConstants(const void*, uint64_t sizeBytes) override
    {
        events.push_back("pushConstants");
        pushSizes.push_back(sizeBytes);
    }

    void draw(uint32_t vertexCount, uint32_t, uint32_t, uint32_t) override
    {
        events.push_back("draw");
        drawCounts.push_back(vertexCount);
    }

    void drawIndexed(uint32_t indexCount, uint32_t, uint32_t, int32_t, uint32_t) override
    {
        events.push_back("drawIndexed");
        indexedDrawCounts.push_back(indexCount);
    }

    void dispatch(uint32_t, uint32_t, uint32_t) override { events.push_back("dispatch"); }

    void end() override { events.push_back("end"); }
};

arp::renderer::SceneConstants testSceneConstants()
{
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    auto scene = arp::renderer::SceneConstants::fromCamera(camera);
    scene.timeOfDay = 18.0f;
    scene.localLightPreviewIntensity = 0.25f;
    scene.contactShadowPreviewIntensity = 0.15f;
    return scene;
}

arp::renderer::MeshGpuBuffer testMeshGpuBuffer(const arp::renderer::MeshAsset& mesh)
{
    arp::renderer::MeshGpuBuffer gpuMesh;
    gpuMesh.reset(
        std::make_unique<arp::rhi::Buffer>("MeshVertices",
            static_cast<uint64_t>(mesh.vertices.size() * sizeof(arp::renderer::MeshVertex)),
            arp::rhi::ResourceState::ShaderRead,
            arp::rhi::BufferUsage::Vertex,
            arp::rhi::MemoryLocation::CpuToGpu,
            static_cast<uint64_t>(mesh.vertices.size() * sizeof(arp::renderer::MeshVertex))),
        std::make_unique<arp::rhi::Buffer>("MeshIndices",
            static_cast<uint64_t>(mesh.indices.size() * sizeof(uint32_t)),
            arp::rhi::ResourceState::ShaderRead,
            arp::rhi::BufferUsage::Index,
            arp::rhi::MemoryLocation::CpuToGpu,
            static_cast<uint64_t>(mesh.indices.size() * sizeof(uint32_t))),
        mesh.vertices.size(),
        mesh.indices.size(),
        mesh.submeshes);
    return gpuMesh;
}

arp::samples::stylized::CharacterMaterialSlot testCharacterMaterialSlot(uint32_t slot)
{
    auto material = arp::samples::stylized::CharacterMaterialSlot{};
    material.slot = slot;
    material.type = arp::samples::stylized::MaterialType::SceneStylizedPbr;
    material.npr = arp::samples::stylized::CharacterNprMaterial::defaults();
    return material;
}

arp::rhi::GraphicsPipeline testGraphicsPipeline(
    const std::pmr::string& name, uint64_t pushConstantSizeBytes, bool depthTestEnabled = false)
{
    return arp::rhi::GraphicsPipeline(name,
        arp::rhi::VertexLayout{},
        arp::rhi::TextureFormat::Bgra8Unorm,
        {640, 360},
        pushConstantSizeBytes,
        false,
        depthTestEnabled,
        depthTestEnabled ? arp::rhi::TextureFormat::D32Float : arp::rhi::TextureFormat::Unknown);
}

void scene_plan_maps_submeshes_to_draw_commands()
{
    const auto mesh = arp::renderer::MeshAsset::toonCharacter();
    const auto scene = testSceneConstants();
    auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    material.lightWrap = 0.61f;
    material.biomeTintStrength = 0.2f;
    const auto characterMaterials =
        arp::samples::stylized::CharacterMaterialSet::defaultToonCharacter();

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, characterMaterials);

    assert(plan.success);
    assert(plan.diagnostics.empty());
    assert(plan.commands.size() == mesh.submeshes.size());
    assert(
        plan.pushConstantSizeBytes() == sizeof(arp::samples::stylized::StylizedScenePushConstants));
    assert(plan.clearColor.r == 0.09f);
    assert(plan.clearDepth == 1.0f);
    assert(plan.commands[0].indexCount == mesh.submeshes[0].indexCount);
    assert(plan.commands[0].firstIndex == mesh.submeshes[0].firstIndex);
    assert(plan.commands[0].pushConstants.cameraPositionAndTime[3] == 18.0f);
    assert(plan.commands[0].pushConstants.lightDirectionAndWrap[3] == 0.61f);
    assert(plan.commands[0].pushConstants.rampParams[2] == 0.25f);
}

void scene_plan_reports_missing_character_material()
{
    const auto mesh = arp::renderer::MeshAsset::toonCharacter();
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    arp::samples::stylized::CharacterMaterialSet incomplete;
    incomplete.slots.push_back(
        arp::samples::stylized::CharacterMaterialSet::defaultToonCharacter().slots.front());

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, incomplete);

    assert(!plan.success);
    assert(plan.diagnostics
        == std::pmr::string("character material set does not cover mesh submeshes"));
    assert(plan.commands.empty());
}

void scene_plan_falls_back_to_single_indexed_draw_without_submeshes()
{
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    const arp::samples::stylized::CharacterMaterialSet materials;

    const auto plan =
        arp::samples::stylized::buildStylizedSceneDrawPlan(42, {}, scene, material, materials);

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].indexCount == 42);
    assert(plan.commands[0].firstIndex == 0);
    assert(plan.commands[0].pushConstants.materialParams[0] == 0.0f);
}

void scene_gi_params_default_to_disabled_and_pack_intensity()
{
    const auto disabled =
        arp::renderer::globalIlluminationDescForProfile(arp::renderer::QualityProfile::Low);
    const auto disabledParams = arp::samples::stylized::buildGiSceneParams(disabled, 0.75f);
    assert(disabledParams.enabled == 0.0f);
    assert(disabledParams.intensity == 0.0f);

    const auto enabled =
        arp::renderer::globalIlluminationDescForProfile(arp::renderer::QualityProfile::Medium);
    const auto enabledParams = arp::samples::stylized::buildGiSceneParams(enabled, 0.75f);
    assert(enabledParams.enabled == 1.0f);
    assert(enabledParams.intensity == 0.75f);

    const auto debugParams = arp::samples::stylized::buildGiSceneParams(enabled, 0.5f, true);
    assert(debugParams.enabled == 1.0f);
    assert(debugParams.intensity == 0.5f);
    assert(debugParams.debugMode == 1.0f);
}

void scene_plan_packs_gi_params_into_push_constants()
{
    const auto mesh = arp::renderer::MeshAsset::toonCharacter();
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    const auto characterMaterials =
        arp::samples::stylized::CharacterMaterialSet::defaultToonCharacter();
    const auto giDesc =
        arp::renderer::globalIlluminationDescForProfile(arp::renderer::QualityProfile::Medium);
    const auto giParams = arp::samples::stylized::buildGiSceneParams(giDesc, 0.6f, true);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, characterMaterials, giParams);

    assert(plan.success);
    assert(!plan.commands.empty());
    assert(plan.commands.front().pushConstants.fogParams[2] == 0.6f);
    assert(plan.commands.front().pushConstants.fogParams[3] == 1.0f);
}

void scene_plan_packs_shadow_params_into_push_constants()
{
    const auto mesh = arp::renderer::MeshAsset::toonCharacter();
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    const auto characterMaterials =
        arp::samples::stylized::CharacterMaterialSet::defaultToonCharacter();
    arp::core::Camera camera;
    camera.setPerspective(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    const auto cascades = arp::samples::stylized::ShadowCascadeSet::forCamera(
        camera, arp::samples::stylized::CsmSettings{4, 160.0f, 2048, 0.5f});
    const auto atlas = arp::samples::stylized::ShadowAtlasLayout::forCsmAndCharacter(
        cascades, arp::samples::stylized::ShadowAtlasSettings{});
    const auto shadowParams = arp::samples::stylized::buildShadowSceneParams(cascades, atlas, true);

    assert(shadowParams.enabled == 1.0f);
    assert(shadowParams.cascadeCount == 4.0f);
    assert(shadowParams.atlasResolution == 2048.0f);
    assert(shadowParams.strength > 0.0f);
    assert(shadowParams.firstCascadeFar > 0.0f);
    assert(shadowParams.lastCascadeFar > shadowParams.firstCascadeFar);
}

void scene_plan_packs_imported_pbr_params_into_push_constants()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 77;
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    auto slot = testCharacterMaterialSlot(77);
    slot.pbr.metallicFactor = 0.25f;
    slot.pbr.roughnessFactor = 0.82f;
    slot.pbr.emissiveFactor[0] = 0.12f;
    slot.pbr.emissiveFactor[1] = 0.24f;
    slot.pbr.emissiveFactor[2] = 0.36f;
    slot.pbr.alphaMode = arp::assets::ImportedAlphaMode::Mask;
    slot.pbr.alphaCutoff = 0.42f;
    slot.pbr.doubleSided = true;
    slot.pbr.occlusionStrength = 0.65f;
    slot.pbr.clearcoatFactor = 0.5f;
    slot.pbr.clearcoatRoughnessFactor = 0.2f;
    slot.pbr.specularFactor = 0.75f;
    slot.pbr.sheenColorFactor[0] = 0.2f;
    slot.pbr.sheenColorFactor[1] = 0.4f;
    slot.pbr.sheenColorFactor[2] = 0.6f;
    slot.pbr.sheenRoughnessFactor = 0.3f;
    slot.pbr.transmissionFactor = 0.35f;
    slot.pbr.thicknessFactor = 0.25f;
    slot.pbr.ior = 1.33f;
    slot.pbr.iridescenceFactor = 0.45f;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    assert(
        plan.pushConstantSizeBytes() == sizeof(arp::samples::stylized::StylizedScenePushConstants));
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].pushConstants.pbrParams[0] == 0.25f);
    assert(plan.commands[0].pushConstants.pbrParams[1] == 0.82f);
    assert(plan.commands[0].pushConstants.pbrParams[2] == 0.42f);
    assert(plan.commands[0].pushConstants.pbrParams[3] == 1.0f);
    assert(plan.commands[0].pushConstants.pbrExtraParams[0] == 0.65f);
    assert(plan.commands[0].pushConstants.pbrExtraParams[1] == 0.5f);
    assert(plan.commands[0].pushConstants.pbrExtraParams[2] == 0.2f);
    assert(plan.commands[0].pushConstants.pbrExtraParams[3] == 0.75f);
    assert(plan.commands[0].pushConstants.gltfExtensionParams[0] > 0.36f);
    assert(plan.commands[0].pushConstants.gltfExtensionParams[0] < 0.38f);
    assert(plan.commands[0].pushConstants.gltfExtensionParams[1] == 0.3f);
    assert(plan.commands[0].pushConstants.gltfExtensionParams[2] > 0.43f);
    assert(plan.commands[0].pushConstants.gltfExtensionParams[2] < 0.44f);
    assert(plan.commands[0].pushConstants.gltfExtensionParams[3] == 0.45f);
    assert(plan.commands[0].doubleSided);
    assert(!plan.commands[0].alphaBlend);
    assert(plan.commands[0].pushConstants.emissiveParams[0] == 0.12f);
    assert(plan.commands[0].pushConstants.emissiveParams[1] == 0.24f);
    assert(plan.commands[0].pushConstants.emissiveParams[2] == 0.36f);
    assert(plan.commands[0].pushConstants.emissiveParams[3] == 1.0f);
}

void scene_plan_does_not_infer_stylized_params_from_demo_slot_ids()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 20;
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    auto slot = testCharacterMaterialSlot(20);
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].pushConstants.materialParams[0] == 0.0f);
    assert(plan.commands[0].pushConstants.materialParams[1] == 0.0f);
    assert(plan.commands[0].pushConstants.materialParams[2] == 0.0f);
    assert(plan.commands[0].pushConstants.materialParams[3] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[0] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[1] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[2] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[3] == 0.0f);
}

void scene_plan_packs_explicit_rock_stylized_params()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 20;
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    auto slot = testCharacterMaterialSlot(20);
    slot.stylized = arp::samples::stylized::StylizedMaterialParams::rock();
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].pushConstants.materialParams[1] == 1.0f);
    assert(plan.commands[0].pushConstants.materialParams[2] > 0.09f);
    assert(plan.commands[0].pushConstants.materialParams[2] < 0.11f);
    assert(plan.commands[0].pushConstants.stylizedParams[0] == 1.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[1] == 0.0f);
}

void scene_plan_packs_explicit_character_stylized_params()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 40;
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    auto slot = testCharacterMaterialSlot(40);
    slot.type = arp::samples::stylized::MaterialType::CharacterCloth;
    slot.stylized = arp::samples::stylized::StylizedMaterialParams::character();
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].pushConstants.materialParams[1] == 0.0f);
    assert(plan.commands[0].pushConstants.materialParams[3] < -0.09f);
    assert(plan.commands[0].pushConstants.materialParams[3] > -0.11f);
    assert(plan.commands[0].pushConstants.stylizedParams[2] > 0.51f);
    assert(plan.commands[0].pushConstants.stylizedParams[2] < 0.53f);
    assert(plan.commands[0].pushConstants.stylizedParams[3] > 0.41f);
    assert(plan.commands[0].pushConstants.stylizedParams[3] < 0.43f);
}

void scene_plan_packs_skybox_stylized_params()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 900;
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    auto slot = testCharacterMaterialSlot(900);
    slot.type = arp::samples::stylized::MaterialType::Skybox;
    slot.stylized = arp::samples::stylized::StylizedMaterialParams::skybox();
    slot.pbr.doubleSided = true;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].pushConstants.materialParams[0] == 1.0f);
    assert(plan.commands[0].doubleSided);
}

void scene_plan_packs_neutral_preview_stylized_params()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 77;
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    auto slot = testCharacterMaterialSlot(77);
    slot.type = arp::samples::stylized::MaterialType::GuiSurface;
    slot.stylized = arp::samples::stylized::StylizedMaterialParams::neutralPreview();
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].pushConstants.materialParams[0] == 0.0f);
    assert(plan.commands[0].pushConstants.materialParams[1] == 0.0f);
    assert(plan.commands[0].pushConstants.materialParams[2] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[0] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[1] == 0.0f);
    assert(plan.commands[0].pushConstants.stylizedParams[2] == 0.0f);
}

void scene_plan_packs_unity_pika_material_params()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.submeshes[0].materialSlot = 10;
    const auto scene = testSceneConstants();
    auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    material.weatherMaskStrength = 0.75f;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(
        arp::samples::stylized::CharacterMaterialSet::defaultPiKaNprCharacter().slots.front());

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        mesh.indices.size(), mesh.submeshes, scene, material, materials);

    assert(plan.success);
    const auto forward = std::find_if(plan.commands.begin(),
        plan.commands.end(),
        [](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
            return command.passKind == arp::samples::stylized::StylizedScenePassKind::Forward;
        });
    assert(forward != plan.commands.end());
    assert(forward->pushConstants.rampParams[0] > 0.238f);
    assert(forward->pushConstants.rampParams[0] < 0.240f);
    assert(forward->pushConstants.rampParams[1] == scene.exposure);
    assert(forward->pushConstants.rampParams[2] == 0.0f);
    assert(forward->pushConstants.rampParams[3] > 0.109f);
    assert(forward->pushConstants.rampParams[3] < 0.111f);
    assert(forward->pushConstants.pbrParams[2] == 0.5f);
    assert(forward->pushConstants.gltfExtensionParams[3] == 4073.0f);
}

void scene_plan_keeps_pika_standard_unified_model_for_imported_parts()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2, 0, 2, 1, 1, 0, 2};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 11},
        arp::renderer::Submesh{6, 3, 12},
    };

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(mesh.indices.size(),
        mesh.submeshes,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet::defaultPiKaNprCharacter());

    assert(plan.success);
    const auto findForward = [&](uint32_t materialSlot) {
        return std::find_if(plan.commands.begin(),
            plan.commands.end(),
            [materialSlot](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
                return command.materialSlot == materialSlot
                    && command.passKind == arp::samples::stylized::StylizedScenePassKind::Forward;
            });
    };
    const auto standard = findForward(10);
    const auto face = findForward(11);
    const auto hair = findForward(12);
    assert(standard != plan.commands.end());
    assert(face != plan.commands.end());
    assert(hair != plan.commands.end());
    assert(standard->pushConstants.gltfExtensionParams[2] == 0.0f);
    assert(face->pushConstants.gltfExtensionParams[2] == 0.0f);
    assert(hair->pushConstants.gltfExtensionParams[2] == 0.0f);
}

void scene_plan_schedules_unity_pika_npr_passes()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2, 3, 4, 5, 0, 2, 3, 1, 4, 5};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 11},
        arp::renderer::Submesh{6, 3, 12},
        arp::renderer::Submesh{9, 3, 13},
    };

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(mesh.indices.size(),
        mesh.submeshes,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        arp::samples::stylized::CharacterMaterialSet::defaultPiKaNprCharacter());

    assert(plan.success);
    assert(plan.commands.size() > mesh.submeshes.size());
    assert(plan.commands[0].passKind == arp::samples::stylized::StylizedScenePassKind::Forward);
    const auto hasPass = [&](arp::samples::stylized::StylizedScenePassKind kind) {
        return std::find_if(plan.commands.begin(),
                   plan.commands.end(),
                   [kind](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
                       return command.passKind == kind;
                   })
            != plan.commands.end();
    };
    assert(hasPass(arp::samples::stylized::StylizedScenePassKind::PlanarShadow));
    assert(hasPass(arp::samples::stylized::StylizedScenePassKind::FurShell));
    const auto hasPassForSlot = [&](arp::samples::stylized::StylizedScenePassKind kind,
                                    uint32_t materialSlot) {
        return std::find_if(plan.commands.begin(),
                   plan.commands.end(),
                   [kind, materialSlot](
                       const arp::samples::stylized::StylizedSceneDrawCommand& command) {
                       return command.passKind == kind && command.materialSlot == materialSlot;
                   })
            != plan.commands.end();
    };
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::XRayMask, 10));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::XRayMask, 11));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::XRayMask, 12));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::XRayMask, 13));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::Outline, 10));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::Outline, 11));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::Outline, 12));
    assert(hasPassForSlot(arp::samples::stylized::StylizedScenePassKind::Outline, 13));
    const auto commandForPass = [&](arp::samples::stylized::StylizedScenePassKind kind) {
        return std::find_if(plan.commands.begin(),
            plan.commands.end(),
            [kind](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
                return command.passKind == kind;
            });
    };
    assert(commandForPass(arp::samples::stylized::StylizedScenePassKind::Forward)
               ->pushConstants.emissiveParams[3]
        == 0.0f);
    const auto furShellCount = std::count_if(plan.commands.begin(),
        plan.commands.end(),
        [](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
            return command.passKind == arp::samples::stylized::StylizedScenePassKind::FurShell;
        });
    assert(furShellCount == 8);
    const auto furShell = commandForPass(arp::samples::stylized::StylizedScenePassKind::FurShell);
    assert(furShell != plan.commands.end());
    assert(furShell->pushConstants.emissiveParams[3] == 40.0f);
    assert(furShell->pushConstants.pbrParams[3]
        == static_cast<float>(furShell->furShellLayer) * 10.0f);
    assert(furShell->pushConstants.gltfExtensionParams[2] == 8.0f);
}

void scene_plan_schedules_outline_for_outline_only_pika_material()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 42},
    };

    auto slot = testCharacterMaterialSlot(42);
    slot.pika.outlineMode = arp::samples::stylized::OutlineMode::InvertedHull;
    slot.pika.outlineWidthPixels = 3.0f;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(mesh.indices.size(),
        mesh.submeshes,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);

    assert(plan.success);
    assert(plan.commands.size() == 2u);
    const auto outline = std::find_if(plan.commands.begin(),
        plan.commands.end(),
        [](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
            return command.passKind == arp::samples::stylized::StylizedScenePassKind::Outline;
        });
    assert(outline != plan.commands.end());
    assert(outline->pushConstants.gltfExtensionParams[3] == 3.0f);
}

void scene_plan_schedules_pika_aux_passes_for_alpha_mask_cutout_materials()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.4f, -0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.4f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 42},
    };

    auto slot = testCharacterMaterialSlot(42);
    slot.pbr.alphaMode = arp::assets::ImportedAlphaMode::Mask;
    slot.pbr.alphaCutoff = 0.5f;
    slot.pika = arp::samples::stylized::PiKaNprMaterial::standard();
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(slot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(mesh.indices.size(),
        mesh.submeshes,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);

    const auto hasPass = [&](arp::samples::stylized::StylizedScenePassKind kind) {
        return std::find_if(plan.commands.begin(),
                   plan.commands.end(),
                   [kind](const arp::samples::stylized::StylizedSceneDrawCommand& command) {
                       return command.passKind == kind;
                   })
            != plan.commands.end();
    };

    assert(plan.success);
    assert(hasPass(arp::samples::stylized::StylizedScenePassKind::Forward));
    assert(hasPass(arp::samples::stylized::StylizedScenePassKind::Outline));
    assert(hasPass(arp::samples::stylized::StylizedScenePassKind::PlanarShadow));
}

void scene_plan_draws_alpha_blend_materials_after_opaque_materials()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
    };
    auto blendSlot = testCharacterMaterialSlot(10);
    blendSlot.pbr.alphaMode = arp::assets::ImportedAlphaMode::Blend;
    auto opaqueSlot = testCharacterMaterialSlot(20);
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(blendSlot);
    materials.slots.push_back(opaqueSlot);

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(mesh.indices.size(),
        mesh.submeshes,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);

    assert(plan.success);
    assert(plan.commands.size() == 2);
    assert(plan.commands[0].materialSlot == 20);
    assert(!plan.commands[0].alphaBlend);
    assert(plan.commands[1].materialSlot == 10);
    assert(plan.commands[1].alphaBlend);
}

void scene_plan_sorts_alpha_blend_submeshes_back_to_front_when_bounds_available()
{
    const std::pmr::vector<arp::renderer::Submesh> submeshes{
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
        arp::renderer::Submesh{6, 3, 30},
    };
    auto opaqueSlot = testCharacterMaterialSlot(10);
    auto nearBlendSlot = testCharacterMaterialSlot(20);
    nearBlendSlot.pbr.alphaMode = arp::assets::ImportedAlphaMode::Blend;
    auto farBlendSlot = testCharacterMaterialSlot(30);
    farBlendSlot.pbr.alphaMode = arp::assets::ImportedAlphaMode::Blend;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots = {opaqueSlot, nearBlendSlot, farBlendSlot};
    const std::pmr::vector<arp::renderer::SubmeshDrawBounds> bounds{
        arp::renderer::SubmeshDrawBounds{{0.0f, 0.0f, 1.0f}, 1.0f, true},
        arp::renderer::SubmeshDrawBounds{{0.0f, 0.0f, 2.0f}, 1.0f, true},
        arp::renderer::SubmeshDrawBounds{{0.0f, 0.0f, 8.0f}, 1.0f, true},
    };

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlanWithSubmeshBounds(9,
        submeshes,
        bounds,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);

    assert(plan.success);
    assert(plan.commands.size() == 3);
    assert(plan.commands[0].materialSlot == 10);
    assert(!plan.commands[0].alphaBlend);
    assert(plan.commands[1].materialSlot == 30);
    assert(plan.commands[1].alphaBlend);
    assert(plan.commands[2].materialSlot == 20);
    assert(plan.commands[2].alphaBlend);
    assert(plan.commands[1].alphaSortDistanceSq > plan.commands[2].alphaSortDistanceSq);
}

void scene_plan_batches_opaque_materials_by_pipeline_variant_before_alpha()
{
    const std::pmr::vector<arp::renderer::Submesh> submeshes{
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
        arp::renderer::Submesh{6, 3, 30},
        arp::renderer::Submesh{9, 3, 40},
    };
    auto oneSided = testCharacterMaterialSlot(10);
    auto doubleSided = testCharacterMaterialSlot(20);
    doubleSided.pbr.doubleSided = true;
    auto maskedOneSided = testCharacterMaterialSlot(30);
    maskedOneSided.pbr.alphaMode = arp::assets::ImportedAlphaMode::Mask;
    auto alphaBlend = testCharacterMaterialSlot(40);
    alphaBlend.pbr.alphaMode = arp::assets::ImportedAlphaMode::Blend;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots = {oneSided, doubleSided, maskedOneSided, alphaBlend};

    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(12,
        submeshes,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);

    assert(plan.success);
    assert(plan.commands.size() == 4);
    assert(plan.commands[0].materialSlot == 10);
    assert(!plan.commands[0].doubleSided);
    assert(!plan.commands[0].alphaBlend);
    assert(plan.commands[1].materialSlot == 30);
    assert(!plan.commands[1].doubleSided);
    assert(!plan.commands[1].alphaBlend);
    assert(plan.commands[2].materialSlot == 20);
    assert(plan.commands[2].doubleSided);
    assert(!plan.commands[2].alphaBlend);
    assert(plan.commands[3].materialSlot == 40);
    assert(plan.commands[3].alphaBlend);
}

void scene_rhi_recording_uses_abstract_command_list()
{
    const auto mesh = arp::renderer::MeshAsset::toonCharacter();
    auto gpuMesh = testMeshGpuBuffer(mesh);
    const auto scene = testSceneConstants();
    const auto material = arp::samples::stylized::SceneStylizedMaterial::defaults();
    const auto characterMaterials =
        arp::samples::stylized::CharacterMaterialSet::defaultToonCharacter();
    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(
        gpuMesh, scene, material, characterMaterials);
    assert(plan.success);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    arp::rhi::Texture depthTarget("SceneDepth",
        {640, 360},
        arp::rhi::ResourceState::DepthAttachment,
        arp::rhi::TextureFormat::D32Float,
        arp::rhi::TextureUsage::DepthAttachment);
    auto pipeline =
        testGraphicsPipeline("StylizedScenePipeline", plan.pushConstantSizeBytes(), true);
    FakeCommandList commands;

    const auto recorded = arp::samples::stylized::recordStylizedSceneDrawPlan(commands,
        arp::samples::stylized::StylizedSceneRhiDrawDesc{
            colorTarget,
            &depthTarget,
            pipeline,
            nullptr,
            nullptr,
            gpuMesh,
            plan,
        });

    assert(recorded.success);
    assert(commands.events.front() == std::pmr::string("beginRenderPass:StylizedScene"));
    assert(commands.events[1] == std::pmr::string("bindGraphicsPipeline:StylizedScenePipeline"));
    assert(commands.events[2] == std::pmr::string("bindVertexBuffer:MeshVertices"));
    assert(commands.events[3] == std::pmr::string("bindIndexBuffer:MeshIndices"));
    assert(commands.events.back() == std::pmr::string("endRenderPass"));
    assert(commands.renderPassHadDepth);
    assert(commands.lastColorLoadOp == arp::rhi::LoadOp::Clear);
    assert(commands.indexedDrawCounts.size() == plan.commands.size());
    assert(commands.indexedDrawCounts.front() == plan.commands.front().indexCount);
    assert(commands.pushSizes.size() == plan.commands.size());
    assert(
        commands.pushSizes.front() == sizeof(arp::samples::stylized::StylizedScenePushConstants));
}

void scene_rhi_recording_switches_pipeline_for_alpha_blend_materials()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    for (uint32_t index = 0; index < 6; ++index) {
        mesh.vertices.push_back(arp::renderer::MeshVertex{
            {static_cast<float>(index) * 0.1f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f},
        });
    }
    mesh.indices = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
        arp::renderer::Submesh{6, 3, 30},
    };
    auto opaque = testCharacterMaterialSlot(10);
    auto alpha = testCharacterMaterialSlot(20);
    alpha.pbr.alphaMode = arp::assets::ImportedAlphaMode::Blend;
    auto doubleAlpha = testCharacterMaterialSlot(30);
    doubleAlpha.pbr.alphaMode = arp::assets::ImportedAlphaMode::Blend;
    doubleAlpha.pbr.doubleSided = true;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots = {opaque, alpha, doubleAlpha};
    auto gpuMesh = testMeshGpuBuffer(mesh);
    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(gpuMesh,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);
    assert(plan.success);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    auto primaryPipeline =
        testGraphicsPipeline("StylizedSceneCullBackPipeline", plan.pushConstantSizeBytes(), true);
    auto alphaPipeline =
        testGraphicsPipeline("StylizedSceneAlphaBlendPipeline", plan.pushConstantSizeBytes(), true);
    auto doubleAlphaPipeline = testGraphicsPipeline(
        "StylizedSceneDoubleSidedAlphaBlendPipeline", plan.pushConstantSizeBytes(), true);
    FakeCommandList commands;

    const auto recorded = arp::samples::stylized::recordStylizedSceneDrawPlan(commands,
        arp::samples::stylized::StylizedSceneRhiDrawDesc{
            colorTarget,
            nullptr,
            primaryPipeline,
            nullptr,
            nullptr,
            gpuMesh,
            plan,
            nullptr,
            nullptr,
            &alphaPipeline,
            &doubleAlphaPipeline,
        });

    assert(recorded.success);
    assert(commands.events[1]
        == std::pmr::string("bindGraphicsPipeline:StylizedSceneCullBackPipeline"));
    assert(commands.events[5] == std::pmr::string("drawIndexed"));
    assert(commands.events[6]
        == std::pmr::string("bindGraphicsPipeline:StylizedSceneAlphaBlendPipeline"));
    assert(commands.events[8] == std::pmr::string("drawIndexed"));
    assert(commands.events[9]
        == std::pmr::string("bindGraphicsPipeline:StylizedSceneDoubleSidedAlphaBlendPipeline"));
    assert(commands.events[11] == std::pmr::string("drawIndexed"));
}

void scene_rhi_recording_binds_material_descriptor_for_each_submesh_slot()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
    };
    auto gpuMesh = testMeshGpuBuffer(mesh);
    auto materials = arp::samples::stylized::CharacterMaterialSet{};
    materials.slots.push_back(testCharacterMaterialSlot(10));
    materials.slots.push_back(testCharacterMaterialSlot(20));
    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(gpuMesh,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);
    assert(plan.success);
    assert(plan.commands[0].materialSlot == 10);
    assert(plan.commands[1].materialSlot == 20);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    auto pipeline =
        testGraphicsPipeline("StylizedScenePipeline", plan.pushConstantSizeBytes(), true);
    arp::rhi::DescriptorSet fallbackDescriptorSet("StylizedSceneFallbackMaterialSet",
        {
            {0, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
            {1, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
        });
    arp::rhi::DescriptorSet sceneDescriptorSet(
        "StylizedSceneSceneSet", arp::samples::stylized::stylizedSceneSceneDescriptorBindings());
    arp::rhi::DescriptorSet slot10DescriptorSet(
        "MaterialSlot10Set", fallbackDescriptorSet.bindings());
    arp::rhi::DescriptorSet slot20DescriptorSet(
        "MaterialSlot20Set", fallbackDescriptorSet.bindings());
    std::pmr::vector<arp::samples::stylized::StylizedSceneMaterialDescriptorBinding>
        materialDescriptorSets{
            {10, &slot10DescriptorSet},
            {20, &slot20DescriptorSet},
        };
    FakeCommandList commands;

    const auto recorded = arp::samples::stylized::recordStylizedSceneDrawPlan(commands,
        arp::samples::stylized::StylizedSceneRhiDrawDesc{
            colorTarget,
            nullptr,
            pipeline,
            &fallbackDescriptorSet,
            &sceneDescriptorSet,
            gpuMesh,
            plan,
            &materialDescriptorSets,
        });

    assert(recorded.success);
    assert(commands.descriptorSetNames.size() == 3);
    assert(commands.descriptorSetNames[0] == std::pmr::string("StylizedSceneSceneSet"));
    assert(commands.descriptorSetIndices[0] == arp::samples::stylized::kStylizedSceneSceneSetIndex);
    assert(commands.descriptorSetNames[1] == std::pmr::string("MaterialSlot10Set"));
    assert(
        commands.descriptorSetIndices[1] == arp::samples::stylized::kStylizedSceneMaterialSetIndex);
    assert(commands.descriptorSetNames[2] == std::pmr::string("MaterialSlot20Set"));
    assert(
        commands.descriptorSetIndices[2] == arp::samples::stylized::kStylizedSceneMaterialSetIndex);
    assert(commands.indexedDrawCounts.size() == 2);
}

void scene_rhi_recording_switches_pipeline_for_double_sided_materials()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 20},
    };
    auto gpuMesh = testMeshGpuBuffer(mesh);
    auto oneSided = testCharacterMaterialSlot(10);
    auto doubleSided = testCharacterMaterialSlot(20);
    doubleSided.pbr.doubleSided = true;
    arp::samples::stylized::CharacterMaterialSet materials;
    materials.slots.push_back(oneSided);
    materials.slots.push_back(doubleSided);
    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(gpuMesh,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);
    assert(plan.success);
    assert(!plan.commands[0].doubleSided);
    assert(plan.commands[1].doubleSided);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    auto primaryPipeline =
        testGraphicsPipeline("StylizedSceneCullBackPipeline", plan.pushConstantSizeBytes(), true);
    auto doubleSidedPipeline = testGraphicsPipeline(
        "StylizedSceneDoubleSidedPipeline", plan.pushConstantSizeBytes(), true);
    FakeCommandList commands;

    const auto recorded = arp::samples::stylized::recordStylizedSceneDrawPlan(commands,
        arp::samples::stylized::StylizedSceneRhiDrawDesc{
            colorTarget,
            nullptr,
            primaryPipeline,
            nullptr,
            nullptr,
            gpuMesh,
            plan,
            nullptr,
            &doubleSidedPipeline,
        });

    assert(recorded.success);
    assert(commands.events[1]
        == std::pmr::string("bindGraphicsPipeline:StylizedSceneCullBackPipeline"));
    assert(commands.events[5] == std::pmr::string("drawIndexed"));
    assert(commands.events[6]
        == std::pmr::string("bindGraphicsPipeline:StylizedSceneDoubleSidedPipeline"));
    assert(commands.events[8] == std::pmr::string("drawIndexed"));
}

void scene_rhi_recording_switches_pipeline_for_pika_passes()
{
    auto mesh = arp::renderer::MeshAsset::debugTriangle();
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{-0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.2f, -0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}});
    mesh.vertices.push_back(
        arp::renderer::MeshVertex{{0.0f, 0.2f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}});
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.submeshes = {
        arp::renderer::Submesh{0, 3, 10},
        arp::renderer::Submesh{3, 3, 13},
    };
    auto gpuMesh = testMeshGpuBuffer(mesh);
    auto materials = arp::samples::stylized::CharacterMaterialSet::defaultPiKaNprCharacter();
    const auto plan = arp::samples::stylized::buildStylizedSceneDrawPlan(gpuMesh,
        testSceneConstants(),
        arp::samples::stylized::SceneStylizedMaterial::defaults(),
        materials);
    assert(plan.success);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    auto primaryPipeline =
        testGraphicsPipeline("StylizedSceneForwardPipeline", plan.pushConstantSizeBytes(), true);
    auto xrayPipeline =
        testGraphicsPipeline("PiKaXRayMaskPipeline", plan.pushConstantSizeBytes(), true);
    auto outlinePipeline =
        testGraphicsPipeline("PiKaOutlinePipeline", plan.pushConstantSizeBytes(), true);
    auto planarShadowPipeline =
        testGraphicsPipeline("PiKaPlanarShadowPipeline", plan.pushConstantSizeBytes(), true);
    auto furShellPipeline =
        testGraphicsPipeline("PiKaFurShellPipeline", plan.pushConstantSizeBytes(), true);
    FakeCommandList commands;

    const auto recorded = arp::samples::stylized::recordStylizedSceneDrawPlan(commands,
        arp::samples::stylized::StylizedSceneRhiDrawDesc{
            colorTarget,
            nullptr,
            primaryPipeline,
            nullptr,
            nullptr,
            gpuMesh,
            plan,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &xrayPipeline,
            &outlinePipeline,
            &planarShadowPipeline,
            &furShellPipeline,
        });

    assert(recorded.success);
    const auto hasEvent = [&](const std::pmr::string& event) {
        return std::find(commands.events.begin(), commands.events.end(), event)
            != commands.events.end();
    };
    assert(hasEvent("bindGraphicsPipeline:StylizedSceneForwardPipeline"));
    assert(hasEvent("bindGraphicsPipeline:PiKaXRayMaskPipeline"));
    assert(hasEvent("bindGraphicsPipeline:PiKaOutlinePipeline"));
    assert(hasEvent("bindGraphicsPipeline:PiKaPlanarShadowPipeline"));
    assert(hasEvent("bindGraphicsPipeline:PiKaFurShellPipeline"));
}

void gui_plan_keeps_text_only_draw_data_drawable()
{
    arp::gui::DrawData DrawData;
    DrawData.textQuads.push_back(arp::gui::DrawTextQuad{
        42,
        {12.0f, 16.0f, 84.0f, 18.0f},
        {0.8f, 0.9f, 1.0f, 1.0f},
    });

    const auto plan = arp::renderer::buildGuiDrawPlan(DrawData, {320, 180});

    assert(plan.success);
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].text);
    assert(plan.commands[0].vertexCount == 6);
    assert(plan.commands[0].pushConstants.screenSize[0] == 320.0f);
    assert(plan.commands[0].pushConstants.rectOrigin[0] == 12.0f);
    assert(plan.commands[0].pushConstants.color[2] == 1.0f);
}

void gui_plan_draws_panels_before_text()
{
    arp::gui::DrawData DrawData;
    DrawData.quads.push_back(arp::gui::DrawQuad{
        7,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 100.0f, 40.0f},
    });
    DrawData.textQuads.push_back(arp::gui::DrawTextQuad{
        8,
        {8.0f, 8.0f, 30.0f, 16.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    });

    const auto plan = arp::renderer::buildGuiDrawPlan(DrawData, {640, 360});

    assert(plan.success);
    assert(plan.commands.size() == 2);
    assert(!plan.commands[0].text);
    assert(plan.commands[1].text);
    assert(plan.commands[0].pushConstants.color[3] > 0.0f);
}

void gui_plan_uses_dear_imgui_dark_control_colors()
{
    arp::gui::DrawData DrawData;
    DrawData.quads.push_back(arp::gui::DrawQuad{
        20,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 100.0f, 40.0f},
    });
    DrawData.quads.push_back(arp::gui::DrawQuad{
        21,
        arp::gui::NodeType::Button,
        {0.0f, 44.0f, 80.0f, 20.0f},
        arp::gui::VisualState::Hovered,
    });
    DrawData.quads.push_back(arp::gui::DrawQuad{
        22,
        arp::gui::NodeType::Button,
        {0.0f, 68.0f, 80.0f, 20.0f},
        arp::gui::VisualState::Active,
    });
    DrawData.quads.push_back(arp::gui::DrawQuad{
        23,
        arp::gui::NodeType::Slider,
        {0.0f, 92.0f, 160.0f, 20.0f},
        arp::gui::VisualState::Disabled,
    });

    const auto plan = arp::renderer::buildGuiDrawPlan(DrawData, {640, 360});

    assert(plan.success);
    assert(plan.commands[0].pushConstants.color[0] >= 0.10f);
    assert(plan.commands[0].pushConstants.color[0] <= 0.16f);
    assert(plan.commands[0].pushConstants.color[0] == plan.commands[0].pushConstants.color[1]);
    assert(plan.commands[0].pushConstants.color[0] == plan.commands[0].pushConstants.color[2]);
    assert(plan.commands[0].pushConstants.color[3] > 0.95f);
    assert(plan.commands[1].pushConstants.color[0] > 0.37f);
    assert(plan.commands[1].pushConstants.color[0] < 0.39f);
    assert(plan.commands[1].pushConstants.color[0] == plan.commands[1].pushConstants.color[1]);
    assert(plan.commands[1].pushConstants.color[0] == plan.commands[1].pushConstants.color[2]);
    assert(plan.commands[1].pushConstants.color[3] > 0.90f);
    assert(plan.commands[2].pushConstants.color[3] > plan.commands[1].pushConstants.color[3]);
    assert(plan.commands[3].pushConstants.color[3] < 0.30f);
}

void gui_plan_honors_custom_decoration_quad_colors()
{
    arp::gui::DrawData DrawData;
    arp::gui::DrawQuad custom{
        30,
        arp::gui::NodeType::Button,
        {0.0f, 0.0f, 80.0f, 20.0f},
    };
    custom.customColor = true;
    custom.color = {1.0f, 0.0f, 0.0f, 0.50f};
    DrawData.quads.push_back(custom);

    const auto plan = arp::renderer::buildGuiDrawPlan(DrawData, {640, 360});

    assert(plan.success);
    assert(plan.commands[0].pushConstants.color[0] == 1.0f);
    assert(plan.commands[0].pushConstants.color[1] == 0.0f);
    assert(plan.commands[0].pushConstants.color[3] == 0.50f);
}

void gui_rhi_recording_uses_abstract_command_list()
{
    arp::gui::DrawData DrawData;
    DrawData.quads.push_back(arp::gui::DrawQuad{
        9,
        arp::gui::NodeType::Panel,
        {0.0f, 0.0f, 128.0f, 40.0f},
    });
    DrawData.textQuads.push_back(arp::gui::DrawTextQuad{
        10,
        {8.0f, 8.0f, 64.0f, 16.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    });
    const auto plan = arp::renderer::buildGuiDrawPlan(DrawData, {640, 360});
    assert(plan.success);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    auto pipeline = testGraphicsPipeline("GuiPipeline", plan.pushConstantSizeBytes());
    FakeCommandList commands;

    const auto recorded = arp::renderer::recordGuiRhiDrawPlan(commands,
        arp::renderer::GuiRhiDrawDesc{
            colorTarget,
            pipeline,
            plan,
        });

    assert(recorded.success);
    assert(commands.events.front() == std::pmr::string("beginRenderPass:Gui"));
    assert(commands.events[1] == std::pmr::string("bindGraphicsPipeline:GuiPipeline"));
    assert(commands.events.back() == std::pmr::string("endRenderPass"));
    assert(!commands.renderPassHadDepth);
    assert(commands.lastColorLoadOp == arp::rhi::LoadOp::Load);
    assert(commands.drawCounts.size() == plan.commands.size());
    assert(commands.drawCounts.front() == 6);
    assert(commands.pushSizes.front() == sizeof(arp::renderer::GuiPushConstants));
}

std::pmr::vector<arp::renderer::ParticleInstance> testParticles()
{
    arp::renderer::ParticleInstance nearParticle;
    nearParticle.position = {0.0f, 0.0f, 1.0f};
    nearParticle.color = {1.0f, 0.5f, 0.2f, 0.75f};
    nearParticle.size = 0.4f;
    nearParticle.lifetimeSeconds = 1.0f;

    arp::renderer::ParticleInstance farParticle;
    farParticle.position = {0.0f, 0.0f, 4.0f};
    farParticle.color = {0.2f, 0.4f, 1.0f, 0.5f};
    farParticle.size = 0.8f;
    farParticle.lifetimeSeconds = 2.0f;

    return {nearParticle, farParticle};
}

void particle_plan_builds_sorted_billboard_vertices()
{
    const auto scene = testSceneConstants();
    const auto plan = arp::renderer::buildParticleDrawPlan(testParticles(), scene, 16, true);

    assert(plan.success);
    assert(plan.diagnostics.empty());
    assert(plan.commands.size() == 1);
    assert(plan.commands[0].vertexCount == 12);
    assert(plan.vertices.size() == 12);
    assert(plan.pushConstantSizeBytes() == sizeof(arp::renderer::ParticlePushConstants));
    assert(plan.vertices[0].worldPosition[2] == 4.0f);
    assert(plan.vertices[0].color[2] == 1.0f);
    assert(plan.vertices[6].worldPosition[2] == 1.0f);
    assert(plan.vertices[6].color[0] == 1.0f);
}

void particle_plan_rejects_empty_or_oversized_inputs()
{
    const auto scene = testSceneConstants();
    const auto empty = arp::renderer::buildParticleDrawPlan({}, scene, 16, true);
    assert(!empty.success);
    assert(empty.diagnostics == std::pmr::string("particle draw data is empty"));

    const auto oversized = arp::renderer::buildParticleDrawPlan(testParticles(), scene, 1, true);
    assert(!oversized.success);
    assert(oversized.diagnostics == std::pmr::string("particle count exceeds draw plan budget"));
}

void particle_rhi_recording_uses_abstract_command_list()
{
    const auto scene = testSceneConstants();
    const auto plan = arp::renderer::buildParticleDrawPlan(testParticles(), scene, 16, true);
    assert(plan.success);

    arp::rhi::Texture colorTarget("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    arp::rhi::Buffer vertexBuffer("ParticleVertices",
        static_cast<uint64_t>(plan.verticesSizeBytes()),
        arp::rhi::ResourceState::ShaderRead,
        arp::rhi::BufferUsage::Vertex,
        arp::rhi::MemoryLocation::CpuToGpu,
        static_cast<uint64_t>(plan.verticesSizeBytes()));
    auto pipeline = testGraphicsPipeline("ParticlePipeline", plan.pushConstantSizeBytes());
    FakeCommandList commands;

    const auto recorded = arp::renderer::recordParticleDrawPlan(commands,
        arp::renderer::ParticleRhiDrawDesc{
            colorTarget,
            pipeline,
            vertexBuffer,
            plan,
        });

    assert(recorded.success);
    assert(commands.events.front() == std::pmr::string("beginRenderPass:Particles"));
    assert(commands.events[1] == std::pmr::string("bindGraphicsPipeline:ParticlePipeline"));
    assert(commands.events[2] == std::pmr::string("bindVertexBuffer:ParticleVertices"));
    assert(commands.events.back() == std::pmr::string("endRenderPass"));
    assert(commands.lastColorLoadOp == arp::rhi::LoadOp::Load);
    assert(commands.drawCounts.size() == 1);
    assert(commands.drawCounts.front() == 12);
    assert(commands.pushSizes.front() == sizeof(arp::renderer::ParticlePushConstants));
}

void fxaa_rhi_recording_samples_scene_color_before_gui()
{
    arp::rhi::Texture sceneColor("FxaaSceneColor",
        {640, 360},
        arp::rhi::ResourceState::ColorAttachment,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment | arp::rhi::TextureUsage::Sampled);
    arp::rhi::Texture backbuffer("Backbuffer",
        {640, 360},
        arp::rhi::ResourceState::Present,
        arp::rhi::TextureFormat::Bgra8Unorm,
        arp::rhi::TextureUsage::ColorAttachment);
    arp::rhi::DescriptorSet descriptorSet("FxaaInputSet",
        {
            {0, arp::rhi::DescriptorType::SampledTexture, arp::rhi::ShaderStage::Fragment},
            {1, arp::rhi::DescriptorType::Sampler, arp::rhi::ShaderStage::Fragment},
        });
    auto pipeline = testGraphicsPipeline("FxaaPipeline", sizeof(arp::renderer::FxaaPushConstants));
    const auto pushConstants = arp::renderer::buildFxaaPushConstants(
        sceneColor.extent(), arp::samples::stylized::PostProcessSettings::animeDefaults().fxaa);
    assert(pushConstants.inputUvScaleBias[0] == 1.0f);
    assert(pushConstants.inputUvScaleBias[1] == 0.0f);
    auto invertedInputSettings = arp::samples::stylized::PostProcessSettings::animeDefaults().fxaa;
    invertedInputSettings.inputUvYTransform = arp::rhi::semantics::TextureUvYTransform::Inverted;
    const auto invertedInputPushConstants =
        arp::renderer::buildFxaaPushConstants(sceneColor.extent(), invertedInputSettings);
    assert(invertedInputPushConstants.inputUvScaleBias[0] == -1.0f);
    assert(invertedInputPushConstants.inputUvScaleBias[1] == 1.0f);
    FakeCommandList commands;

    const auto recorded = arp::renderer::recordFxaaPass(commands,
        arp::renderer::FxaaRhiDrawDesc{
            sceneColor,
            backbuffer,
            pipeline,
            descriptorSet,
            pushConstants,
        });

    assert(recorded.success);
    assert(commands.events[0] == std::pmr::string("transition:texture"));
    assert(commands.events[1] == std::pmr::string("beginRenderPass:Fxaa"));
    assert(commands.events[2] == std::pmr::string("bindGraphicsPipeline:FxaaPipeline"));
    assert(commands.events[3] == std::pmr::string("bindDescriptorSet"));
    assert(commands.descriptorSetIndices.front() == arp::renderer::kFxaaInputSetIndex);
    assert(commands.events[4] == std::pmr::string("pushConstants"));
    assert(commands.events[5] == std::pmr::string("draw"));
    assert(commands.events.back() == std::pmr::string("endRenderPass"));
    assert(!commands.renderPassHadDepth);
    assert(commands.lastColorLoadOp == arp::rhi::LoadOp::DontCare);
    assert(commands.drawCounts.size() == 1);
    assert(commands.drawCounts.front() == 3);
    assert(commands.pushSizes.front() == sizeof(arp::renderer::FxaaPushConstants));
}

} // namespace

int main()
{
    scene_plan_maps_submeshes_to_draw_commands();
    scene_plan_reports_missing_character_material();
    scene_plan_falls_back_to_single_indexed_draw_without_submeshes();
    scene_gi_params_default_to_disabled_and_pack_intensity();
    scene_plan_packs_gi_params_into_push_constants();
    scene_plan_packs_shadow_params_into_push_constants();
    scene_plan_packs_imported_pbr_params_into_push_constants();
    scene_plan_does_not_infer_stylized_params_from_demo_slot_ids();
    scene_plan_packs_explicit_rock_stylized_params();
    scene_plan_packs_explicit_character_stylized_params();
    scene_plan_packs_skybox_stylized_params();
    scene_plan_packs_neutral_preview_stylized_params();
    scene_plan_packs_unity_pika_material_params();
    scene_plan_keeps_pika_standard_unified_model_for_imported_parts();
    scene_plan_schedules_unity_pika_npr_passes();
    scene_plan_schedules_outline_for_outline_only_pika_material();
    scene_plan_schedules_pika_aux_passes_for_alpha_mask_cutout_materials();
    scene_plan_draws_alpha_blend_materials_after_opaque_materials();
    scene_plan_sorts_alpha_blend_submeshes_back_to_front_when_bounds_available();
    scene_plan_batches_opaque_materials_by_pipeline_variant_before_alpha();
    scene_rhi_recording_uses_abstract_command_list();
    scene_rhi_recording_switches_pipeline_for_alpha_blend_materials();
    scene_rhi_recording_binds_material_descriptor_for_each_submesh_slot();
    scene_rhi_recording_switches_pipeline_for_double_sided_materials();
    scene_rhi_recording_switches_pipeline_for_pika_passes();
    gui_plan_keeps_text_only_draw_data_drawable();
    gui_plan_draws_panels_before_text();
    gui_plan_uses_dear_imgui_dark_control_colors();
    gui_plan_honors_custom_decoration_quad_colors();
    gui_rhi_recording_uses_abstract_command_list();
    particle_plan_builds_sorted_billboard_vertices();
    particle_plan_rejects_empty_or_oversized_inputs();
    particle_rhi_recording_uses_abstract_command_list();
    fxaa_rhi_recording_samples_scene_color_before_gui();
    return 0;
}
