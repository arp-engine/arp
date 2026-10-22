#include "2d/sprite_animation_component.h"
#include "2d/tile_map_resource_component.h"
#include "3d/authoring_components.h"
#include "3d/collider_component.h"
#include "3d/components.h"
#include "assets/glb_importer_common.h"
#include "assets/image_decoder.h"
#include "editor/viewport/editor_gltf_material_mapping.h"
#include "editor/viewport/editor_scene_component_gizmos.h"
#include "editor/viewport/editor_scene_viewport_contract.h"
#include "editor/viewport/editor_scene_viewport_frame_builder.h"
#include "editor/viewport/editor_scene_viewport_material_payload.h"
#include "editor/viewport/editor_scene_viewport_mesh_cache.h"
#include "editor/viewport/editor_scene_viewport_resource_resolver.h"
#include "editor/viewport/editor_scene_viewport_session.h"
#include "editor/viewport/editor_scene_viewport_texture_admission.h"
#include "editor/viewport/editor_scene_viewport_texture_cache.h"
#include "editor/viewport/editor_scene_viewport_texture_source.h"
#include "editor/workspace/editor_scene_document.h"
#include "physics/3d/physics_types.h"
#include "renderer/gui/gui_rhi_draw_commands.h"
#include "renderer/materials/texture_upload.h"
#include "resource/mesh_asset_resources.h"
#include "resource/prefab_asset_resources.h"
#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"
#include "rhi/backends/null/null_backend.h"
#include "shader/shader_reflection_cache.h"
#include "tests/fixtures/terrain_gizmo_fixture.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace arp;

constexpr const char* kMeshId = "7f4b9d5a-1f63-4f4c-9a45-3c1a9e7e92b1";
constexpr const char* kMaterialId = "c8591a23-0f99-4070-a4d0-d79ab87379eb";
constexpr const char* kSourceId = "b055987e-d04c-42ef-a3f1-f77721eac8f7";
constexpr const char* kShaderId = "9e3f0b60-93f7-42b8-88e7-32e0c9f0bc40";
constexpr const char* kTextureId = "86ccf943-c70e-4ddc-b7c4-f1a6c02ff115";

core::pmr::String text(const char* value)
{
    return core::pmr::String(value, &core::defaultCpuMemoryResource());
}

resource::ResourceReference reference(const char* id, const char* type)
{
    resource::ResourceReference value;
    value.id = text(id);
    value.type = text(type);
    value.status = text("Contract Complete");
    return value;
}

void setTerrainResourceId(scene3d::TerrainComponent& component, const char* resourceId)
{
    scene3d::TerrainComponentEditorProperties properties;
    properties.resourceId = text(resourceId);
    assert(component.applyEditorAuthoringProperties(reflection::object(std::move(properties))));
}

shader::ShaderReflection viewportMaterialReflection()
{
    static constexpr const char* yaml = R"yaml(
moduleName: EditorViewportPbrMaterial
reflectionContractVersion: "ShaderReflection/3"
entryPoints:
  - { name: fragmentMain, stage: Fragment }
bindings:
  - { name: BaseColorTexture, type: SampledTexture, set: 4, binding: 9, arrayCount: 1, stageMask: 2 }
  - { name: NormalTexture, type: SampledTexture, set: 4, binding: 10, arrayCount: 1, stageMask: 2 }
  - { name: MetallicRoughnessTexture, type: SampledTexture, set: 4, binding: 11, arrayCount: 1, stageMask: 2 }
  - { name: EmissiveTexture, type: SampledTexture, set: 4, binding: 12, arrayCount: 1, stageMask: 2 }
  - { name: OcclusionTexture, type: SampledTexture, set: 4, binding: 13, arrayCount: 1, stageMask: 2 }
  - { name: LinearSampler, type: Sampler, set: 4, binding: 14, arrayCount: 1, stageMask: 2 }
)yaml";
    const auto loaded = shader::ShaderReflectionCache::loadFromYaml(
        std::pmr::string(yaml, &core::defaultCpuMemoryResource()));
    assert(loaded.success);
    return loaded.module.reflection;
}

shader::ShaderBinding& bindingNamed(shader::ShaderReflection& reflection, const char* name)
{
    for (auto& binding : reflection.bindings) {
        if (binding.name == name)
            return binding;
    }
    assert(false);
    return reflection.bindings.front();
}

void testViewportShaderBindingContract()
{
    auto reflection = viewportMaterialReflection();
    const auto inspected = editor::inspectEditorSceneViewportShaderBindings(reflection);
    assert(inspected.valid());
    assert(inspected.bindings.textures[0].set == 4u);
    assert(inspected.bindings.textures[0].binding == 9u);
    assert(inspected.bindings.textures[3].binding == 13u);
    assert(inspected.bindings.textures[4].binding == 12u);
    assert(inspected.bindings.sampler.binding == 14u);

    auto invalid = reflection;
    invalid.reflectionContractVersion = text("ShaderReflection/2");
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::ReflectionContractUnsupported);

    invalid = reflection;
    invalid.bindings.erase(invalid.bindings.begin());
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::MissingBinding);

    invalid = reflection;
    invalid.bindings.push_back(invalid.bindings.front());
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::DuplicateBinding);

    invalid = reflection;
    bindingNamed(invalid, "NormalTexture").type = shader::BindingType::Sampler;
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::BindingTypeMismatch);

    invalid = reflection;
    bindingNamed(invalid, "NormalTexture").arrayCount = 2u;
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::ArrayCountMismatch);

    invalid = reflection;
    bindingNamed(invalid, "NormalTexture").stageMask = 1u;
    assert(editor::inspectEditorSceneViewportShaderBindings(invalid).diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::StageMaskMismatch);

    invalid = reflection;
    bindingNamed(invalid, "LinearSampler").binding = 9u;
    const auto collision = editor::inspectEditorSceneViewportShaderBindings(invalid);
    assert(collision.diagnostic
        == editor::EditorSceneViewportShaderBindingDiagnostic::DescriptorAddressCollision);
    assert(std::string_view(
               editor::editorSceneViewportShaderBindingDiagnosticCode(collision.diagnostic))
        == "ARP-EDITOR-VIEWPORT-SHADER-BINDING-ADDRESS-COLLISION");
}

void loadMesh(resource::ResourceManager& manager)
{
    auto mesh = core::makeUnique<resource::MeshAssetResource>(
        text(kMeshId), assets::MeshAsset::debugTriangle());
    assert(manager.loadResource(std::move(mesh)).success);
}

void loadSource(resource::ResourceManager& manager)
{
    auto source = core::makeUnique<resource::ExternalAssetSourceResource>();
    source->configureMetadata(reference(kSourceId, "ExternalAssetSourceResource"));
    assert(manager.loadResource(std::move(source)).success);
}

void loadTexture(resource::ResourceManager& manager)
{
    auto texture = core::makeUnique<resource::TextureResource>();
    texture->configureMetadata(reference(kTextureId, "TextureResource"));
    assert(manager.loadResource(std::move(texture)).success);
}

void loadDecodedTexture(resource::ResourceManager& manager)
{
    auto texture = core::makeUnique<resource::TextureResource>();
    texture->configureMetadata(reference(kTextureId, "TextureResource"));
    texture->usage = text("color");
    texture->srgb = true;
    texture->decodedSrgb = true;
    texture->decodedWidth = 2;
    texture->decodedHeight = 2;
    texture->decodedRgba8 = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    assert(manager.loadResource(std::move(texture)).success);
}

void loadShader(
    resource::ResourceManager& manager, bool withArtifacts = true, bool matchingInterface = true)
{
    auto shader = core::makeUnique<resource::ShaderPackageResource>();
    shader->configureMetadata(reference(kShaderId, "ShaderPackageResource"));
    if (withArtifacts) {
        shader->slangPath = text("shader.slang");
        shader->spirvPath = text("shader.spv");
        shader->mslPath = text("shader.metal");
        shader->reflectionPath = text("shader.reflection.yaml");
    }
    shader->materialInterfaceContract =
        text(matchingInterface ? resource::kPbrMetallicRoughnessContract : "arp/material/unknown");
    shader->materialInterfaceVersion = resource::kPbrMetallicRoughnessContractVersion;
    assert(manager.loadResource(std::move(shader)).success);
}

void loadMaterial(resource::ResourceManager& manager,
    bool validContract = true,
    bool validParameters = true,
    const char* baseColorTextureId = nullptr)
{
    auto material = core::makeUnique<resource::MaterialResource>();
    material->configureMetadata(reference(kMaterialId, "MaterialResource"));
    material->sourceResourceId = text(kSourceId);
    material->shaderPackageId = text(kShaderId);
    material->materialContractVersion =
        validContract ? resource::kPbrMetallicRoughnessContractVersion : 0u;
    material->surfaceModel = text(resource::kPbrMetallicRoughnessContract);
    material->baseColorR = validParameters ? 1.0f : 2.0f;
    material->baseColorG = 1.0f;
    material->baseColorB = 1.0f;
    material->baseColorA = 1.0f;
    material->alphaMode = text("OPAQUE");
    if (baseColorTextureId != nullptr)
        material->baseColorTextureId = text(baseColorTextureId);
    assert(manager.loadResource(std::move(material)).success);
}

} // namespace

void testTerrainGeometryPreview()
{
    constexpr const char* terrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";
    const auto path =
        std::filesystem::temp_directory_path() / "arp_editor_terrain_geometry_preview";
    resource::TerrainAssetBuildDesc desc;
    desc.version = resource::TerrainAssetVersion::V2;
    desc.resourceId = text(terrainId);
    desc.hasSurfaceWeights = false;
    desc.hasVegetation = false;
    desc.bounds = {-2.0f, 2.0f, -2.0f, 2.0f};
    desc.tileCountX = desc.tileCountZ = 1u;
    desc.samplesPerEdge = 3u;
    desc.tileWorldSize = 4.0f;
    resource::TerrainAssetBuildTile tile;
    tile.heights = {0, 0, 0, 0, 2, 0, 0, 0, 0};
    desc.tiles.push_back(std::move(tile));
    assert(resource::writeTerrainAsset(desc, path).success);
    resource::ResourceManager resources;
    assert(resource::loadTerrainAssetResource(
        resources, reference(terrainId, "TerrainAssetResource"), path / "terrain.asset.yaml")
            .success);
    loadSource(resources);
    loadShader(resources);
    loadMaterial(resources);
    editor::EditorSceneDocument document;
    auto& scene = document.scene();
    auto* mesh =
        scene.findObject(document.selectedObject())->getComponent<scene3d::MeshComponent>();
    assert(mesh);
    mesh->set_enabled(false);
    auto& object = scene.createObject(text("Terrain"));
    auto& component = object.addComponent<scene3d::TerrainComponent>();
    component.set_resource_id(text(terrainId));
    component.set_material_resource_id(text(kMaterialId));
    const auto snapshot = scene.extractTraversalSnapshot({});
    const auto frame = editor::buildEditorSceneViewportFrame(
        scene, snapshot, 1u, 640u, 480u, {0u, 0u, 640u, 480u}, &resources, 1u);
    assert(frame.valid() && frame.frame.drawItems.size() == 1u);
    assert(
        frame.frame.drawItems.front().geometry == editor::EditorSceneViewportGeometry::TerrainTile);
    const auto resolved = editor::resolveEditorSceneViewportResources(frame.frame, resources);
    assert(resolved.valid() && resolved.items.front().vertexCount == 9u
        && resolved.items.front().indexCount == 24u);
    auto device = rhi::createNullDevice();
    const auto bytes = 9u * sizeof(assets::MeshVertex) + 24u * sizeof(std::uint32_t);
    editor::EditorSceneViewportMeshCache cache({1u, bytes});
    const auto uploaded = cache.acquire(*device, resources, resolved.items.front());
    assert(uploaded.valid() && uploaded.mesh->vertexCount() == 9u);
    assert(cache.acquire(*device, resources, resolved.items.front()).reused);
    editor::EditorSceneViewportMeshCache tooSmall({1u, bytes - 1u});
    assert(tooSmall.acquire(*device, resources, resolved.items.front()).diagnostic
        == editor::EditorSceneViewportMeshCacheDiagnostic::BudgetExceeded);
    auto forged = resolved.items.front();
    forged.vertexCount = 1u;
    assert(tooSmall.acquire(*device, resources, forged).diagnostic
        == editor::EditorSceneViewportMeshCacheDiagnostic::ResolvedMetadataMismatch);
    auto scaled = frame.frame;
    scaled.drawItems.front().worldTransform.scale.x = 2.0f;
    assert(editor::validateEditorSceneViewportFrame(scaled).diagnostic
        == editor::EditorSceneViewportDiagnostic::TerrainTransformUnsupported);
    cache.clearAfterDeviceIdle();
    assert(cache.residentMeshCount() == 0u);
}

int main()
{
    testTerrainGeometryPreview();
    {
        resource::ResourceManager gizmoResources;
        scene::Scene terrainAuthoringScene;
        auto& terrainAuthoringObject = terrainAuthoringScene.createObject(
            core::pmr::String("TerrainAuthoring", &core::defaultCpuMemoryResource()));
        scene::Transform terrainAuthoringTransform;
        terrainAuthoringTransform.translation = {3.0f, 1.0f, -2.0f};
        terrainAuthoringTransform.scale = {2.0f, 1.0f, 0.5f};
        terrainAuthoringObject.transform().setLocalTransform(terrainAuthoringTransform);
        auto& terrainAuthoring =
            terrainAuthoringObject.addComponent<scene3d::TerrainAuthoringComponent>();
        terrainAuthoring.set_half_extent(2.0f);
        const auto terrainAuthoringPreview = editor::buildEditorSceneComponentGizmos(
            terrainAuthoringScene, gizmoResources, 1.0f, 4u);
        assert(terrainAuthoringPreview.valid);
        assert(terrainAuthoringPreview.segments.size() == 4u);
        assert(terrainAuthoringPreview.segments.front().componentId == terrainAuthoring.id());
        assert(terrainAuthoringPreview.segments.front().kind
            == editor::EditorSceneComponentGizmoKind::TerrainAuthoring);
        assert(std::abs(terrainAuthoringPreview.segments.front().from.x - (-1.0f)) < 1e-5f);
        assert(std::abs(terrainAuthoringPreview.segments.front().from.y - 1.0f) < 1e-5f);
        assert(std::abs(terrainAuthoringPreview.segments.front().from.z - (-3.0f)) < 1e-5f);
        assert(std::abs(terrainAuthoringPreview.segments.front().to.x - 7.0f) < 1e-5f);
        const auto terrainAuthoringBudget = editor::buildEditorSceneComponentGizmos(
            terrainAuthoringScene, gizmoResources, 1.0f, 3u);
        assert(!terrainAuthoringBudget.valid && terrainAuthoringBudget.segments.empty());
        assert(terrainAuthoringBudget.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED"));
        terrainAuthoring.set_half_extent(-1.0f);
        const auto invalidTerrainAuthoring = editor::buildEditorSceneComponentGizmos(
            terrainAuthoringScene, gizmoResources, 1.0f, 4u);
        assert(!invalidTerrainAuthoring.valid && invalidTerrainAuthoring.segments.empty());
        assert(invalidTerrainAuthoring.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-AUTHORING-DESC-INVALID"));

        scene::Scene critterFieldScene;
        auto& critterFieldObject = critterFieldScene.createObject(
            core::pmr::String("CritterField", &core::defaultCpuMemoryResource()));
        scene::Transform critterOwnerTransform;
        critterOwnerTransform.translation = {70.0f, -12.0f, 40.0f};
        critterOwnerTransform.scale = {2.0f, 3.0f, 4.0f};
        critterFieldObject.transform().setLocalTransform(critterOwnerTransform);
        auto& critterField = critterFieldObject.addComponent<scene3d::CritterFieldComponent>();
        critterField.set_bounds(5.0f, -7.0f, 11.0f, 2.5f);
        const auto critterFieldPreview =
            editor::buildEditorSceneComponentGizmos(critterFieldScene, gizmoResources, 1.0f, 4u);
        assert(critterFieldPreview.valid && critterFieldPreview.segments.size() == 4u);
        assert(critterFieldPreview.segments.front().componentId == critterField.id());
        assert(critterFieldPreview.segments.front().kind
            == editor::EditorSceneComponentGizmoKind::CritterField);
        assert(std::abs(critterFieldPreview.segments.front().from.x + 5.0f) < 1e-5f);
        assert(std::abs(critterFieldPreview.segments.front().from.y - 2.5f) < 1e-5f);
        assert(std::abs(critterFieldPreview.segments.front().from.z + 7.0f) < 1e-5f);
        assert(std::abs(critterFieldPreview.segments.front().to.x - 5.0f) < 1e-5f);
        const auto critterFieldBudget =
            editor::buildEditorSceneComponentGizmos(critterFieldScene, gizmoResources, 1.0f, 3u);
        assert(!critterFieldBudget.valid && critterFieldBudget.segments.empty());
        assert(critterFieldBudget.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED"));
        scene3d::CritterFieldEditorProperties invalidCritterField;
        invalidCritterField.worldMaxX = 0.0f;
        assert(
            !critterField.applyEditorAuthoringProperties(reflection::object(invalidCritterField)));
        critterField.set_bounds(5.0f, 11.0f, -7.0f, 2.5f);
        const auto invalidCritterPreview =
            editor::buildEditorSceneComponentGizmos(critterFieldScene, gizmoResources, 1.0f, 4u);
        assert(!invalidCritterPreview.valid && invalidCritterPreview.segments.empty());
        assert(invalidCritterPreview.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-CRITTER-FIELD-DESC-INVALID"));

        scene::Scene gizmoScene;
        auto& cameraObject =
            gizmoScene.createObject(core::pmr::String("Camera", &core::defaultCpuMemoryResource()));
        auto& camera = cameraObject.addComponent<scene::CameraComponent>();
        auto& lightObject = gizmoScene.createObject(
            core::pmr::String("PointLight", &core::defaultCpuMemoryResource()));
        auto& light = lightObject.addComponent<scene3d::LightComponent>();
        light.setRange(3.0f);
        auto& colliderObject = gizmoScene.createObject(
            core::pmr::String("Collider", &core::defaultCpuMemoryResource()));
        auto& collider = colliderObject.addComponent<scene3d::ColliderComponent>();

        const auto preview =
            editor::buildEditorSceneComponentGizmos(gizmoScene, gizmoResources, 1.5f, 128u);
        assert(preview.valid);
        assert(preview.segments.size() == 72u);
        assert(preview.segments.front().componentId == camera.id());
        assert(preview.segments.front().kind == editor::EditorSceneComponentGizmoKind::Camera);
        assert(preview.segments[59].componentId == light.id());
        assert(preview.segments[59].kind == editor::EditorSceneComponentGizmoKind::Light);
        assert(preview.segments.back().componentId == collider.id());
        assert(preview.segments.back().kind == editor::EditorSceneComponentGizmoKind::Collider);

        const auto exhausted =
            editor::buildEditorSceneComponentGizmos(gizmoScene, gizmoResources, 1.5f, 71u);
        assert(!exhausted.valid);
        assert(exhausted.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED"));
        assert(exhausted.segments.empty());

        scene::Scene shapeScene;
        auto& shapeObject =
            shapeScene.createObject(core::pmr::String("Shape", &core::defaultCpuMemoryResource()));
        auto& shapeCollider = shapeObject.addComponent<scene3d::ColliderComponent>();
        auto shapeDesc = shapeCollider.desc();
        const std::array<std::pair<physics::ColliderShapeKind, std::size_t>, 4> shapeCases{{
            {physics::ColliderShapeKind::Box, 12u},
            {physics::ColliderShapeKind::Sphere, 48u},
            {physics::ColliderShapeKind::Capsule, 100u},
            {physics::ColliderShapeKind::Cylinder, 36u},
        }};
        for (const auto& [kind, expectedSegments] : shapeCases) {
            shapeDesc.body.shape.kind = kind;
            shapeCollider.setDesc(shapeDesc);
            const auto shapePreview =
                editor::buildEditorSceneComponentGizmos(shapeScene, gizmoResources, 1.0f, 128u);
            assert(shapePreview.valid);
            assert(shapePreview.segments.size() == expectedSegments);
            assert(shapePreview.segments.front().componentId == shapeCollider.id());
        }
        shapeDesc.body.shape.kind = physics::ColliderShapeKind::HeightField;
        shapeCollider.setDesc(shapeDesc);
        const auto missingTopology =
            editor::buildEditorSceneComponentGizmos(shapeScene, gizmoResources, 1.0f, 128u);
        assert(!missingTopology.valid);
        assert(missingTopology.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-COLLIDER-SHAPE-TOPOLOGY-UNAVAILABLE"));
    }
    {
        resource::ResourceManager terrainResources;
        scene::Scene unresolvedTerrainScene;
        auto& unresolvedTerrainObject = unresolvedTerrainScene.createObject(
            core::pmr::String("UnresolvedTerrain", &core::defaultCpuMemoryResource()));
        unresolvedTerrainObject.addComponent<scene3d::TerrainComponent>();
        const auto unresolved = editor::buildEditorSceneComponentGizmos(
            unresolvedTerrainScene, terrainResources, 1.0f, 12u);
        assert(unresolved.valid);
        assert(unresolved.segments.empty());

        scene::Scene missingTerrainScene;
        auto& missingTerrainObject = missingTerrainScene.createObject(
            core::pmr::String("MissingTerrain", &core::defaultCpuMemoryResource()));
        auto& missingTerrain = missingTerrainObject.addComponent<scene3d::TerrainComponent>();
        setTerrainResourceId(missingTerrain, tests::terrainGizmoId);
        const auto missing = editor::buildEditorSceneComponentGizmos(
            missingTerrainScene, terrainResources, 1.0f, 12u);
        assert(!missing.valid);
        assert(missing.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-RESOURCE-MISSING"));

        resource::ResourceManager notReadyResources;
        assert(notReadyResources.declareResource(tests::terrainGizmoId, "TerrainAssetResource")
                .success);
        const auto notReady = editor::buildEditorSceneComponentGizmos(
            missingTerrainScene, notReadyResources, 1.0f, 12u);
        assert(!notReady.valid);
        assert(notReady.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-RESOURCE-NOT-READY"));

        resource::ResourceManager wrongTypeResources;
        assert(
            wrongTypeResources.declareResource(tests::terrainGizmoId, "MaterialResource").success);
        const auto wrongType = editor::buildEditorSceneComponentGizmos(
            missingTerrainScene, wrongTypeResources, 1.0f, 12u);
        assert(!wrongType.valid);
        assert(wrongType.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-RESOURCE-TYPE-MISMATCH"));

        const auto terrainFixtureDirectory = std::filesystem::temp_directory_path()
            / ("arp-editor-terrain-gizmo-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(terrainFixtureDirectory);
        assert(tests::loadTerrainGizmoFixture(terrainResources, terrainFixtureDirectory));

        scene::Scene terrainScene;
        auto& terrainObject = terrainScene.createObject(
            core::pmr::String("Terrain", &core::defaultCpuMemoryResource()));
        scene::Transform terrainTransform;
        terrainTransform.translation = {3.0f, 2.0f, -1.0f};
        terrainTransform.scale = {2.0f, 1.0f, 0.5f};
        terrainObject.transform().setLocalTransform(terrainTransform);
        auto& terrain = terrainObject.addComponent<scene3d::TerrainComponent>();
        setTerrainResourceId(terrain, tests::terrainGizmoId);

        const auto preview =
            editor::buildEditorSceneComponentGizmos(terrainScene, terrainResources, 1.0f, 12u);
        assert(preview.valid);
        assert(preview.segments.size() == 12u);
        assert(
            preview.segments.front().kind == editor::EditorSceneComponentGizmoKind::TerrainBounds);
        assert(preview.segments.front().componentId == terrain.id());
        assert(std::abs(preview.segments.front().from.x - (-1.0f)) < 1e-5f);
        assert(std::abs(preview.segments.front().from.y - 2.0f) < 1e-5f);
        assert(std::abs(preview.segments.front().from.z - (-1.5f)) < 1e-5f);

        const auto exhausted =
            editor::buildEditorSceneComponentGizmos(terrainScene, terrainResources, 1.0f, 11u);
        assert(!exhausted.valid);
        assert(exhausted.diagnostic
            == std::string_view("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED"));
        assert(exhausted.segments.empty());

        std::filesystem::remove_all(terrainFixtureDirectory);
    }
    const auto& interactiveBudget = editor::kEditorSceneViewportInteractiveSessionConfig;
    assert(editor::kEditorSceneViewportInteractiveBudgetVersion == 1u);
    assert(interactiveBudget.budgetVersion == editor::kEditorSceneViewportInteractiveBudgetVersion);
    assert(interactiveBudget.maxSnapshotObjects == editor::EditorSceneAuthoringCodec::maxObjects);
    assert(interactiveBudget.maxDrawCalls == editor::EditorSceneAuthoringCodec::maxComponents);
    assert(interactiveBudget.meshCacheBudget.maxResidentMeshes
        == editor::EditorSceneAuthoringCodec::maxComponents);
    assert(interactiveBudget.textureCacheBudget.maxResidentTextures
        == editor::EditorSceneAuthoringCodec::maxComponents
            * editor::kEditorSceneViewportMaterialTextureSlotCount);
    assert(interactiveBudget.maxDrawCalls
        > editor::kEditorSceneViewportSmokeSessionConfig.maxDrawCalls);
    assert(interactiveBudget.meshCacheBudget.maxResidentGeometryBytes
        > editor::kEditorSceneViewportSmokeSessionConfig.meshCacheBudget.maxResidentGeometryBytes);
    assert(interactiveBudget.textureCacheBudget.maxResidentDecodedBytes
        > editor::kEditorSceneViewportSmokeSessionConfig.textureCacheBudget
            .maxResidentDecodedBytes);

    using namespace arp;

    {
        auto compatible = renderer::detail::guiAtlasDescriptorLayout();
        assert(renderer::guiAtlasLayoutCompatible(compatible));
        compatible.bindings[0].textureViewDimension = rhi::TextureViewDimension::TextureCube;
        assert(!renderer::guiAtlasLayoutCompatible(compatible));
    }

    testViewportShaderBindingContract();

    {
        const std::array<std::uint8_t, 16u> tightPixels{
            1u,
            2u,
            3u,
            4u,
            5u,
            6u,
            7u,
            8u,
            9u,
            10u,
            11u,
            12u,
            13u,
            14u,
            15u,
            16u,
        };
        const auto packed =
            renderer::packRgba8TextureRows(tightPixels.data(), tightPixels.size(), 2u, 2u, 512u);
        assert(packed.success());
        assert(packed.bytesPerRow == rhi::kTextureUploadRowPitchAlignment);
        assert(packed.pixels.size() == 2u * rhi::kTextureUploadRowPitchAlignment);
        assert(std::equal(tightPixels.begin(), tightPixels.begin() + 8, packed.pixels.begin()));
        assert(std::equal(tightPixels.begin() + 8,
            tightPixels.end(),
            packed.pixels.begin() + rhi::kTextureUploadRowPitchAlignment));
        assert(std::all_of(packed.pixels.begin() + 8,
            packed.pixels.begin() + rhi::kTextureUploadRowPitchAlignment,
            [](std::uint8_t value) {
                return value == 0u;
            }));
        const auto overBudget =
            renderer::packRgba8TextureRows(tightPixels.data(), tightPixels.size(), 2u, 2u, 511u);
        assert(!overBudget.success());
    }

    {
        const char* textureCoordinateJson = R"json({
            "materials": [{
                "alphaMode": "MASK",
                "normalTexture": {"index": 0, "texCoord": 1},
                "pbrMetallicRoughness": {
                    "baseColorTexture": {"index": 1, "texCoord": 2}
                }
            }]
        })json";
        assets::detail::JsonValue root;
        assert(assets::detail::parseJson(textureCoordinateJson, root));
        const auto parsed = assets::detail::parseImportedGlbMaterialAssets(root, "", {});
        assert(parsed.success && parsed.materials.size() == 1);
        assert(parsed.materials[0].normalTexture == 0);
        assert(parsed.materials[0].normalTextureCoordinateSet == 1);
        assert(parsed.materials[0].baseColorTexture == 1);
        assert(parsed.materials[0].baseColorTextureCoordinateSet == 2);

        const char* invalidTexCoordJson =
            R"({"materials":[{"normalTexture":{"index":0,"texCoord":-1}}]})";
        assets::detail::JsonValue invalidTexCoordRoot;
        assert(assets::detail::parseJson(invalidTexCoordJson, invalidTexCoordRoot));
        const auto invalidTexCoord =
            assets::detail::parseImportedGlbMaterialAssets(invalidTexCoordRoot, "", {});
        assert(!invalidTexCoord.success);
        assert(invalidTexCoord.diagnostics == "ASSET-GLB-MATERIAL-TEXTURE-INFO-INVALID");

        const char* unknownAlphaJson = R"({"materials":[{"alphaMode":"ADDITIVE"}]})";
        assets::detail::JsonValue unknownAlphaRoot;
        assert(assets::detail::parseJson(unknownAlphaJson, unknownAlphaRoot));
        const auto unknownAlpha =
            assets::detail::parseImportedGlbMaterialAssets(unknownAlphaRoot, "", {});
        assert(!unknownAlpha.success);
        assert(unknownAlpha.diagnostics == "ASSET-GLB-MATERIAL-ALPHA-MODE-UNSUPPORTED");

        const char* vendorExtensionJson =
            R"({"materials":[{"extensions":{"VENDOR_material_semantics":{}}}]})";
        assets::detail::JsonValue vendorExtensionRoot;
        assert(assets::detail::parseJson(vendorExtensionJson, vendorExtensionRoot));
        const auto vendorExtension =
            assets::detail::parseImportedGlbMaterialAssets(vendorExtensionRoot, "", {});
        assert(vendorExtension.success);
        assert(vendorExtension.materials[0].hasUnsupportedMaterialExtensions);
    }

    {
        assets::ImportedMaterialAsset imported;
        imported.name = "Mapped GLB material";
        imported.hasCompleteMaterialImportMetadata = true;
        imported.baseColorFactor[0] = 0.2f;
        imported.baseColorFactor[1] = 0.4f;
        imported.baseColorFactor[2] = 0.6f;
        imported.baseColorFactor[3] = 0.8f;
        imported.emissiveFactor[1] = 0.1f;
        imported.metallicFactor = 0.35f;
        imported.roughnessFactor = 0.7f;
        imported.alphaMode = assets::ImportedAlphaMode::Mask;
        imported.alphaCutoff = 0.4f;
        imported.doubleSided = true;
        imported.baseColorTexture = 0;

        std::pmr::vector<assets::ImportedTextureAsset> textures;
        textures.emplace_back();
        core::pmr::Vector<core::pmr::String> textureIds{&core::defaultCpuMemoryResource()};
        textureIds.emplace_back(kTextureId);

        resource::ShaderPackageResource shader;
        shader.materialInterfaceContract = resource::kPbrMetallicRoughnessContract;
        shader.materialInterfaceVersion = resource::kPbrMetallicRoughnessContractVersion;
        const auto mapped = editor::mapGltfPbrMaterial(
            imported, textures, textureIds, kSourceId, kShaderId, shader);
        assert(mapped.success);
        assert(mapped.diagnostic == editor::EditorGltfMaterialMappingDiagnostic::None);
        assert(mapped.material.sourceResourceId == kSourceId);
        assert(mapped.material.shaderPackageId == kShaderId);
        assert(mapped.material.baseColorR == 0.2f);
        assert(mapped.material.emissiveG == 0.1f);
        assert(mapped.material.metallicFactor == 0.35f);
        assert(mapped.material.roughnessFactor == 0.7f);
        assert(mapped.material.alphaMode == "MASK");
        assert(mapped.material.alphaCutoff == 0.4f);
        assert(mapped.material.doubleSided);
        assert(mapped.material.baseColorTextureId == kTextureId);

        auto incompleteMetadata = imported;
        incompleteMetadata.hasCompleteMaterialImportMetadata = false;
        auto rejected = editor::mapGltfPbrMaterial(
            incompleteMetadata, textures, textureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::MaterialMetadataIncomplete);

        auto invalidReference = imported;
        invalidReference.baseColorTexture = 1;
        rejected = editor::mapGltfPbrMaterial(
            invalidReference, textures, textureIds, kSourceId, kShaderId, shader);
        assert(!rejected.success);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::InvalidTextureReference);

        auto unsupportedUv = imported;
        unsupportedUv.baseColorTextureCoordinateSet = 1;
        rejected = editor::mapGltfPbrMaterial(
            unsupportedUv, textures, textureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::UnsupportedTextureCoordinateSet);

        auto explicitSampler = textures;
        explicitSampler[0].sampler = 0;
        rejected = editor::mapGltfPbrMaterial(
            imported, explicitSampler, textureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::UnsupportedTextureSampler);

        auto unsupportedExtension = imported;
        unsupportedExtension.clearcoatFactor = 0.5f;
        rejected = editor::mapGltfPbrMaterial(
            unsupportedExtension, textures, textureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::UnsupportedMaterialSemantics);

        unsupportedExtension = imported;
        unsupportedExtension.hasUnsupportedMaterialExtensions = true;
        rejected = editor::mapGltfPbrMaterial(
            unsupportedExtension, textures, textureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::UnsupportedMaterialSemantics);

        core::pmr::Vector<core::pmr::String> incompleteTextureIds{
            &core::defaultCpuMemoryResource()};
        rejected = editor::mapGltfPbrMaterial(
            imported, textures, incompleteTextureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::TextureTableSizeMismatch);

        rejected = editor::mapGltfPbrMaterial(
            imported, textures, textureIds, "not-a-uuid", kShaderId, shader);
        assert(rejected.diagnostic == editor::EditorGltfMaterialMappingDiagnostic::MissingIdentity);

        shader.materialInterfaceVersion++;
        rejected = editor::mapGltfPbrMaterial(
            imported, textures, textureIds, kSourceId, kShaderId, shader);
        assert(rejected.diagnostic
            == editor::EditorGltfMaterialMappingDiagnostic::InvalidShaderInterface);
    }

    {
        const auto imagePath =
            std::filesystem::path(ARP_SOURCE_DIR) / "editor" / "resources" / "arp_editor.png";
        std::ifstream file(imagePath, std::ios::binary);
        assert(file.good());
        std::pmr::vector<std::uint8_t> sourceBytes;
        sourceBytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        assert(!sourceBytes.empty());
        const std::pmr::string pngMime("image/png");
        const auto sourcePixels = assets::decodeImageToRgba8(sourceBytes, pngMime, true);
        assert(sourcePixels.success);

        assets::ImportedTextureAsset importedTexture;
        importedTexture.sourceImage = 0u;
        std::pmr::vector<assets::ImportedTextureAsset> textureTable;
        textureTable.push_back(importedTexture);
        assets::ImportedImageAsset importedImage;
        importedImage.mimeType = "image/png";
        importedImage.encodedBytes.assign(sourceBytes.begin(), sourceBytes.end());
        std::pmr::vector<assets::ImportedImageAsset> imageTable;
        imageTable.push_back(std::move(importedImage));
        core::pmr::Vector<core::pmr::String> textureIds{&core::defaultCpuMemoryResource()};
        textureIds.emplace_back(kTextureId);

        resource::TextureResource texture;
        texture.configureMetadata(reference(kTextureId, "TextureResource"));
        texture.usage = "color";
        texture.srgb = true;
        texture.alpha = sourcePixels.image.alpha;
        core::StableHash64 emptyTextureFingerprint;
        texture.appendContentFingerprint(emptyTextureFingerprint);
        const auto decoded = editor::decodeEditorViewportTextureSource(
            texture, 0u, textureTable, imageTable, textureIds, 16u * 1024u * 1024u);
        assert(decoded.success);
        assert(decoded.diagnostic == editor::EditorViewportTextureSourceDiagnostic::None);
        assert(texture.decodedWidth == sourcePixels.image.width);
        assert(texture.decodedHeight == sourcePixels.image.height);
        assert(texture.decodedHasAlpha == sourcePixels.image.alpha);
        assert(texture.decodedSrgb);
        assert(texture.decodedRgba8.size() == texture.decodedWidth * texture.decodedHeight * 4u);
        assert(std::equal(texture.decodedRgba8.begin(),
            texture.decodedRgba8.end(),
            sourcePixels.image.rgba8.begin(),
            sourcePixels.image.rgba8.end()));
        assert(
            texture.decodedRgba8.get_allocator().resource() == &core::defaultCpuMemoryResource());
        assert(texture.processedByteCount() == texture.decodedRgba8.size());
        core::StableHash64 decodedTextureFingerprint;
        texture.appendContentFingerprint(decodedTextureFingerprint);
        assert(decodedTextureFingerprint.value() != emptyTextureFingerprint.value());

        const auto pixelCount = texture.decodedRgba8.size();
        const auto overBudget = editor::decodeEditorViewportTextureSource(
            texture, 0u, textureTable, imageTable, textureIds, 4u);
        assert(overBudget.diagnostic
            == editor::EditorViewportTextureSourceDiagnostic::ImageDecodeFailed);
        assert(texture.decodedRgba8.size() == pixelCount);

        auto mismatchedMime = imageTable;
        mismatchedMime[0].mimeType = "image/jpeg";
        const auto mimeFailure = editor::decodeEditorViewportTextureSource(
            texture, 0u, textureTable, mismatchedMime, textureIds, 16u * 1024u * 1024u);
        assert(mimeFailure.diagnostic
            == editor::EditorViewportTextureSourceDiagnostic::ImageDecodeFailed);
        assert(mimeFailure.detail == "image MIME type does not match encoded payload");

        auto invalidBinding = textureTable;
        invalidBinding[0].sourceImage = assets::invalidImportedImageIndex;
        const auto missingImage = editor::decodeEditorViewportTextureSource(
            texture, 0u, invalidBinding, imageTable, textureIds, 16u * 1024u * 1024u);
        assert(missingImage.diagnostic
            == editor::EditorViewportTextureSourceDiagnostic::InvalidImageBinding);
        assert(texture.decodedRgba8.size() == pixelCount);

        auto mismatchedIds = textureIds;
        mismatchedIds[0] = kMaterialId;
        const auto wrongId = editor::decodeEditorViewportTextureSource(
            texture, 0u, textureTable, imageTable, mismatchedIds, 16u * 1024u * 1024u);
        assert(wrongId.diagnostic
            == editor::EditorViewportTextureSourceDiagnostic::InvalidTextureBinding);

        texture.srgb = false;
        const auto wrongColorSpace = editor::decodeEditorViewportTextureSource(
            texture, 0u, textureTable, imageTable, textureIds, 16u * 1024u * 1024u);
        assert(wrongColorSpace.diagnostic
            == editor::EditorViewportTextureSourceDiagnostic::ColorSpaceMismatch);
        texture.srgb = true;
        texture.alpha = !sourcePixels.image.alpha;
        const auto wrongAlpha = editor::decodeEditorViewportTextureSource(
            texture, 0u, textureTable, imageTable, textureIds, 16u * 1024u * 1024u);
        assert(wrongAlpha.diagnostic
            == editor::EditorViewportTextureSourceDiagnostic::AlphaMetadataMismatch);
    }

    {
        const auto meshPath = std::filesystem::path(ARP_SOURCE_DIR) / "3rd" / "assimp-6.0.5"
            / "test" / "models" / "glTF2" / "BoxTextured-glTF-Binary" / "BoxTextured.glb";
        resource::ResourceManager importedResources;
        auto importedMesh = core::makeUnique<resource::MeshAssetResource>(text(kMeshId), meshPath);
        assert(importedResources.loadResource(std::move(importedMesh)).success);
        const auto mesh = importedResources.find<resource::MeshAssetResource>(kMeshId);
        assert(mesh.ready());
        assert(mesh.get()->textures().size() == 1u);
        assert(mesh.get()->images().size() == 1u);
        assert(mesh.get()->textureResourceIds().size() == mesh.get()->textures().size());
        for (std::size_t index = 0u; index < mesh.get()->textureResourceIds().size(); ++index) {
            const auto& textureId = mesh.get()->textureResourceIds()[index];
            assert(resource::isResourceUuid(textureId));
            assert(textureId
                == resource::deriveResourceUuid(
                    kMeshId, "TextureResource/glb", static_cast<std::uint64_t>(index)));
        }
        assert(mesh.get()->textureResourceIds()[0]
            != resource::deriveResourceUuid(kMeshId, "TextureResource/glb", std::uint64_t{1u}));
        const auto sourceGeneration = importedResources.generationOf(kMeshId);
        const auto stale = editor::admitEditorViewportGlbTextures(
            importedResources, *mesh.get(), sourceGeneration + 1u, 16u * 1024u * 1024u);
        assert(!stale.success);
        assert(
            stale.diagnostic == editor::EditorViewportTextureAdmissionDiagnostic::SourceNotReady);
        const auto admitted = editor::admitEditorViewportGlbTextures(
            importedResources, *mesh.get(), sourceGeneration, 16u * 1024u * 1024u);
        assert(admitted.success);
        assert(admitted.admittedCount == 1u);
        const auto texture =
            importedResources.find<resource::TextureResource>(mesh.get()->textureResourceIds()[0]);
        assert(texture.ready());
        assert(texture.get()->sourceResourceId == kMeshId);
        assert(texture.get()->sourceGeneration == sourceGeneration);
        assert(texture.get()->decodedRgba8.size()
            == texture.get()->decodedWidth * texture.get()->decodedHeight * 4u);
        const auto repeated = editor::admitEditorViewportGlbTextures(
            importedResources, *mesh.get(), sourceGeneration, 16u * 1024u * 1024u);
        assert(repeated.success && repeated.reusedCount == 1u);
    }

    {
        const auto meshPath = std::filesystem::path(ARP_SOURCE_DIR) / "editor" / "tests"
            / "fixtures" / "editor_viewport_two_texture.glb";
        resource::ResourceManager validResources;
        auto validMeshResource =
            core::makeUnique<resource::MeshAssetResource>(text(kMeshId), meshPath);
        assert(validResources.loadResource(std::move(validMeshResource)).success);
        const auto validMesh = validResources.find<resource::MeshAssetResource>(kMeshId);
        assert(validMesh.ready());

        std::vector<std::size_t> usedTextureIndices;
        for (const auto& material : validMesh.get()->materials()) {
            const std::uint32_t indices[] = {
                material.baseColorTexture,
                material.emissiveTexture,
                material.normalTexture,
                material.metallicRoughnessTexture,
                material.occlusionTexture,
            };
            for (const auto index : indices) {
                if (index != assets::invalidImportedTextureIndex
                    && std::find(usedTextureIndices.begin(), usedTextureIndices.end(), index)
                        == usedTextureIndices.end())
                    usedTextureIndices.push_back(index);
            }
        }
        assert(usedTextureIndices.size() > 1u);
        const auto validAdmission = editor::admitEditorViewportGlbTextures(validResources,
            *validMesh.get(),
            validResources.generationOf(kMeshId),
            64u * 1024u * 1024u);
        assert(validAdmission.success);
        assert(validAdmission.admittedCount == usedTextureIndices.size());

        resource::ResourceManager failingResources;
        auto failingMeshResource =
            core::makeUnique<resource::MeshAssetResource>(text(kMeshId), meshPath);
        assert(failingResources.loadResource(std::move(failingMeshResource)).success);
        const auto failingMesh = failingResources.find<resource::MeshAssetResource>(kMeshId);
        assert(failingMesh.ready());
        const auto lastTextureIndex = usedTextureIndices.back();
        const auto imageIndex = failingMesh.get()->textures()[lastTextureIndex].sourceImage;
        assert(imageIndex < failingMesh.get()->images().size());
        auto& mutableImages =
            const_cast<std::pmr::vector<assets::ImportedImageAsset>&>(failingMesh.get()->images());
        mutableImages[imageIndex].encodedBytes.clear();

        const auto rejectedAdmission = editor::admitEditorViewportGlbTextures(failingResources,
            *failingMesh.get(),
            failingResources.generationOf(kMeshId),
            64u * 1024u * 1024u);
        assert(!rejectedAdmission.success);
        assert(rejectedAdmission.diagnostic
            == editor::EditorViewportTextureAdmissionDiagnostic::DecodeFailed);
        for (const auto index : usedTextureIndices)
            assert(failingResources.stateOf(failingMesh.get()->textureResourceIds()[index])
                == resource::ResourceState::Unloaded);
    }

    editor::EditorSceneViewportFrame frame;
    frame.sceneId = scene::SceneId::fromValue(1);
    frame.stateVersion = 1;
    frame.cameraObjectId = scene::GameObjectId::fromValue(2);
    frame.targetWidth = 1280;
    frame.targetHeight = 720;
    frame.viewport = {16, 32, 800, 600};

    editor::EditorSceneViewportDrawItem item;
    item.objectId = scene::GameObjectId::fromValue(3);
    item.componentId = scene::ComponentId::fromValue(4);
    item.worldTransform.translation = {2.0f, 3.0f, 4.0f};
    item.worldTransform.scale = {2.0f, 1.0f, 0.5f};
    item.meshResourceId = "mesh://editor/cube";
    item.materialResourceId = "material://editor/default";
    frame.drawItems.push_back(item);
    assert(editor::validateEditorSceneViewportFrame(frame).valid());

    auto viewportCamera = frame;
    viewportCamera.cameraAspectPolicy = scene::CameraAspectPolicyKind::MatchViewport;
    viewportCamera.cameraProjectionAspectRatio = 4.0f / 3.0f;
    assert(editor::validateEditorSceneViewportFrame(viewportCamera).valid());

    auto invalid = frame;
    invalid.contractVersion++;
    assert(editor::validateEditorSceneViewportFrame(invalid).diagnostic
        == editor::EditorSceneViewportDiagnostic::UnsupportedContractVersion);

    invalid = viewportCamera;
    invalid.cameraProjectionAspectRatio = 16.0f / 9.0f;
    assert(editor::validateEditorSceneViewportFrame(invalid).diagnostic
        == editor::EditorSceneViewportDiagnostic::CameraViewportAspectMismatch);

    invalid = frame;
    invalid.cameraViewMatrix.m[6] = std::numeric_limits<float>::quiet_NaN();
    assert(editor::validateEditorSceneViewportFrame(invalid).diagnostic
        == editor::EditorSceneViewportDiagnostic::InvalidCameraMatrix);

    invalid = frame;
    invalid.drawItems[0].worldTransform.translation.x = std::numeric_limits<float>::infinity();
    assert(editor::validateEditorSceneViewportFrame(invalid).diagnostic
        == editor::EditorSceneViewportDiagnostic::InvalidWorldTransform);

    editor::EditorSceneDocument document;
    auto& sceneDocument = document.scene();
    auto* cameraObject = sceneDocument.findObjectByName(text("Camera3D"));
    assert(cameraObject != nullptr);
    auto* cameraComponent = cameraObject->getComponent<scene::CameraComponent>();
    assert(cameraComponent != nullptr);
    cameraComponent->desc().aspectPolicy = scene::CameraAspectPolicy::matchViewport();
    auto* meshObject = sceneDocument.findObject(document.selectedObject());
    assert(meshObject != nullptr);
    auto* meshComponent = meshObject->getComponent<scene3d::MeshComponent>();
    assert(meshComponent != nullptr);
    meshComponent->set_resource_id(text(kMeshId));
    meshComponent->set_material_resource_id(text(kMaterialId));
    auto& skyComponent = meshObject->addComponent<scene3d::SkyComponent>();
    skyComponent.set_zenith_color(0.04f, 0.12f, 0.3f);
    skyComponent.set_horizon_color(0.8f, 0.2f, 0.1f);
    skyComponent.set_sun_direction(1.0f, 2.0f, 3.0f);
    skyComponent.set_sun_color(0.3f, 0.5f, 0.7f);
    const auto sceneSnapshot = sceneDocument.extractTraversalSnapshot({});
    const auto invalidExtentFrame = editor::buildEditorSceneViewportFrame(
        sceneDocument, sceneSnapshot, 7, 1280, 720, {16, 32, 800, 0});
    assert(invalidExtentFrame.validation.diagnostic
        == editor::EditorSceneViewportDiagnostic::InvalidViewportRect);
    const auto builtFrame = editor::buildEditorSceneViewportFrame(
        sceneDocument, sceneSnapshot, 7, 1280, 720, {16, 32, 800, 600});
    assert(builtFrame.valid());
    assert(builtFrame.frame.contractVersion == 4);
    assert(builtFrame.frame.sceneId == sceneDocument.sceneId());
    assert(builtFrame.frame.stateVersion == 7);
    assert(builtFrame.frame.cameraObjectId == cameraObject->id());
    assert(builtFrame.frame.cameraProjectionAspectRatio == 4.0f / 3.0f);
    assert(builtFrame.frame.drawItems.size() == 1);
    assert(builtFrame.frame.drawItems[0].objectId == meshObject->id());
    assert(builtFrame.frame.drawItems[0].componentId == meshComponent->id());
    assert(builtFrame.frame.drawItems[0].worldTransform.translation.x
        == sceneSnapshot.requireNode(meshObject->id()).worldTransform.translation.x);
    assert(builtFrame.frame.drawItems[0].meshResourceId == kMeshId);
    assert(builtFrame.frame.drawItems[0].materialResourceId == kMaterialId);
    assert(builtFrame.frame.cameraProjectionMatrix.m[0]
        == core::perspective(60.0f, 4.0f / 3.0f, 0.1f, 1000.0f).m[0]);
    assert(builtFrame.frame.sky.present);
    assert(builtFrame.frame.sky.zenith.x == 0.04f && builtFrame.frame.sky.zenith.y == 0.12f
        && builtFrame.frame.sky.zenith.z == 0.3f);
    assert(builtFrame.frame.sky.horizon.x == 0.8f && builtFrame.frame.sky.horizon.y == 0.2f
        && builtFrame.frame.sky.horizon.z == 0.1f);
    assert(builtFrame.frame.sky.sunDirection.x == 1.0f
        && builtFrame.frame.sky.sunDirection.y == 2.0f
        && builtFrame.frame.sky.sunDirection.z == 3.0f);
    assert(builtFrame.frame.sky.sunColor.x == 0.3f && builtFrame.frame.sky.sunColor.y == 0.5f
        && builtFrame.frame.sky.sunColor.z == 0.7f);
    auto invalidSkyFrame = builtFrame.frame;
    invalidSkyFrame.sky.domeRadius = 0.0f;
    assert(editor::validateEditorSceneViewportFrame(invalidSkyFrame).diagnostic
        == editor::EditorSceneViewportDiagnostic::InvalidSkyParameters);

    meshObject->addComponent<scene2d::TileMapResourceComponent>(
        core::SharedCallback<resource::ResourceManager*()>{},
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()>{});
    const auto tileMapPreviewFrame = editor::buildEditorSceneViewportFrame(
        sceneDocument, sceneSnapshot, 8, 1280, 720, {16, 32, 800, 600});
    assert(tileMapPreviewFrame.valid());
    assert(tileMapPreviewFrame.frame.drawItems.size() == 1u);

    invalid = frame;
    invalid.viewport.width = invalid.targetWidth;
    assert(editor::validateEditorSceneViewportFrame(invalid).diagnostic
        == editor::EditorSceneViewportDiagnostic::InvalidViewportRect);

    invalid = frame;
    invalid.drawItems[0].meshResourceId.clear();
    const auto missingMesh = editor::validateEditorSceneViewportFrame(invalid);
    assert(missingMesh.diagnostic == editor::EditorSceneViewportDiagnostic::MissingMeshResource);
    assert(missingMesh.drawItemIndex == 0);
    assert(std::string_view(editor::editorSceneViewportDiagnosticCode(missingMesh.diagnostic))
        == "ARP-EDITOR-VIEWPORT-MESH-RESOURCE-MISSING");

    frame.drawItems[0].meshResourceId = kMeshId;
    frame.drawItems[0].materialResourceId = kMaterialId;
    resource::ResourceManager resources;
    loadMesh(resources);
    loadMaterial(resources);
    loadSource(resources);
    loadShader(resources);
    const auto resolved = editor::resolveEditorSceneViewportResources(frame, resources);
    assert(resolved.valid());
    assert(resolved.items.size() == 1);
    assert(resolved.items[0].meshGeneration == 1);
    assert(resolved.items[0].materialGeneration == 1);
    assert(resolved.items[0].sourceGeneration == 1);
    assert(resolved.items[0].shaderGeneration == 1);
    assert(resolved.items[0].vertexCount == 3);
    assert(resolved.items[0].indexCount == 3);
    assert(resolved.items[0].submeshCount == 1);
    assert(resolved.items[0].baseColorFactor.size() == 4);
    assert(resolved.items[0].emissiveFactor.size() == 3);
    assert(resolved.items[0].alphaMode == "OPAQUE");
    assert(resolved.items[0].baseColorTextureGeneration == 0);

    loadDecodedTexture(resources);
    auto materialWithBaseTexture = resources.find<resource::MaterialResource>(kMaterialId);
    assert(materialWithBaseTexture.ready());
    const_cast<resource::MaterialResource*>(materialWithBaseTexture.get())->baseColorTextureId =
        text(kTextureId);
    const auto texturedResolved = editor::resolveEditorSceneViewportResources(frame, resources);
    assert(texturedResolved.valid());
    assert(texturedResolved.items[0].baseColorTextureGeneration != 0u);

    auto device = rhi::createNullDevice();
    assert(device);
    const auto meshByteBudget =
        static_cast<std::uint64_t>(resolved.items[0].vertexCount * sizeof(assets::MeshVertex)
            + resolved.items[0].indexCount * sizeof(std::uint32_t));
    editor::EditorSceneViewportMeshCache meshCache({1, meshByteBudget});
    const auto cachedMesh = meshCache.acquire(*device, resources, resolved.items[0]);
    assert(cachedMesh.valid());
    assert(!cachedMesh.reused);
    assert(cachedMesh.mesh->validForIndexedDraw());
    assert(cachedMesh.mesh->vertexCount() == resolved.items[0].vertexCount);
    assert(cachedMesh.mesh->indexCount() == resolved.items[0].indexCount);
    assert(meshCache.residentMeshCount() == 1);
    assert(meshCache.residentGeometryBytes() == meshByteBudget);
    const auto reusedMesh = meshCache.acquire(*device, resources, resolved.items[0]);
    assert(reusedMesh.valid() && reusedMesh.reused);
    assert(reusedMesh.mesh == cachedMesh.mesh);
    auto otherDevice = rhi::createNullDevice();
    assert(otherDevice);
    const auto wrongOwner = meshCache.acquire(*otherDevice, resources, resolved.items[0]);
    assert(!wrongOwner.valid());
    assert(wrongOwner.diagnostic == editor::EditorSceneViewportMeshCacheDiagnostic::OwnerMismatch);

    auto staleMesh = resolved.items[0];
    ++staleMesh.meshGeneration;
    const auto staleResult = meshCache.acquire(*device, resources, staleMesh);
    assert(!staleResult.valid());
    assert(
        staleResult.diagnostic == editor::EditorSceneViewportMeshCacheDiagnostic::ResourceChanged);

    editor::EditorSceneViewportMeshCache noBudgetCache({0, 0});
    const auto noBudgetResult = noBudgetCache.acquire(*device, resources, resolved.items[0]);
    assert(!noBudgetResult.valid());
    assert(
        noBudgetResult.diagnostic == editor::EditorSceneViewportMeshCacheDiagnostic::InvalidBudget);
    editor::EditorSceneViewportMeshCache exhaustedCache({1, meshByteBudget - 1});
    const auto exhaustedResult = exhaustedCache.acquire(*device, resources, resolved.items[0]);
    assert(!exhaustedResult.valid());
    assert(exhaustedResult.diagnostic
        == editor::EditorSceneViewportMeshCacheDiagnostic::BudgetExceeded);
    assert(exhaustedCache.residentMeshCount() == 0);

    const auto textureGeneration = resources.generationOf(kTextureId);
    editor::EditorSceneViewportTextureCache textureCache({1, 16, 512});
    const auto uploadedTexture = textureCache.acquire(*device,
        resources,
        kTextureId,
        textureGeneration,
        editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(uploadedTexture.valid() && !uploadedTexture.reused);
    assert(uploadedTexture.texture->desc().format == rhi::TextureFormat::Rgba8UnormSrgb);
    assert(uploadedTexture.view->desc().usage == rhi::TextureUsage::Sampled);
    assert(textureCache.residentTextureCount() == 1);
    assert(textureCache.residentDecodedBytes() == 16);
    const auto reusedTexture = textureCache.acquire(*device,
        resources,
        kTextureId,
        textureGeneration,
        editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(reusedTexture.valid() && reusedTexture.reused);
    assert(reusedTexture.texture == uploadedTexture.texture);
    const auto wrongColorSpace = textureCache.acquire(*device,
        resources,
        kTextureId,
        textureGeneration,
        editor::EditorSceneViewportTextureColorSpace::Linear);
    assert(!wrongColorSpace.valid());
    assert(wrongColorSpace.diagnostic
        == editor::EditorSceneViewportTextureCacheDiagnostic::ColorSpaceMismatch);
    const auto staleTexture = textureCache.acquire(*device,
        resources,
        kTextureId,
        textureGeneration + 1,
        editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(!staleTexture.valid());
    assert(staleTexture.diagnostic
        == editor::EditorSceneViewportTextureCacheDiagnostic::ResourceChanged);
    const auto wrongTextureOwner = textureCache.acquire(*otherDevice,
        resources,
        kTextureId,
        textureGeneration,
        editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(!wrongTextureOwner.valid());
    assert(wrongTextureOwner.diagnostic
        == editor::EditorSceneViewportTextureCacheDiagnostic::OwnerMismatch);
    resource::ResourceManager otherTextureResources;
    loadDecodedTexture(otherTextureResources);
    const auto wrongResourceManagerOwner = textureCache.acquire(*device,
        otherTextureResources,
        kTextureId,
        otherTextureResources.generationOf(kTextureId),
        editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(!wrongResourceManagerOwner.valid());
    assert(wrongResourceManagerOwner.diagnostic
        == editor::EditorSceneViewportTextureCacheDiagnostic::OwnerMismatch);
    editor::EditorSceneViewportTextureCache exhaustedTextureCache({1, 15, 512});
    const auto overBudgetTexture = exhaustedTextureCache.acquire(*device,
        resources,
        kTextureId,
        textureGeneration,
        editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(!overBudgetTexture.valid());
    assert(overBudgetTexture.diagnostic
        == editor::EditorSceneViewportTextureCacheDiagnostic::BudgetExceeded);

    editor::EditorSceneViewportTextureCache materialTextureCache({1u, 16u, 512u});
    const auto viewportShaderReflection = viewportMaterialReflection();
    const auto materialPayload =
        editor::prepareEditorSceneViewportMaterialPayload(texturedResolved.items[0],
            viewportShaderReflection,
            *device,
            resources,
            materialTextureCache);
    assert(materialPayload.valid());
    assert(materialPayload.payload.materialResourceId == kMaterialId);
    assert(
        materialPayload.payload.materialGeneration == texturedResolved.items[0].materialGeneration);
    assert(materialPayload.payload.baseColorFactor[0] == 1.0f);
    assert(materialPayload.payload.textures[0].semantic
        == editor::EditorSceneViewportMaterialTextureSemantic::BaseColor);
    assert(materialPayload.payload.textures[0].colorSpace
        == editor::EditorSceneViewportTextureColorSpace::Srgb);
    assert(materialPayload.payload.textures[0].present());
    assert(!materialPayload.payload.textures[1].present());
    const auto descriptorReadiness =
        editor::validateEditorSceneViewportDescriptorReadiness(materialPayload.payload);
    assert(!descriptorReadiness.ready());
    assert(descriptorReadiness.readiness
        == editor::EditorSceneViewportDescriptorReadiness::MissingTexture);
    assert(descriptorReadiness.failedSlot == 1u);

    auto malformedMaterialPayload = materialPayload.payload;
    malformedMaterialPayload.textures[0].sampler = nullptr;
    const auto malformedDescriptorReadiness =
        editor::validateEditorSceneViewportDescriptorReadiness(malformedMaterialPayload);
    assert(!malformedDescriptorReadiness.ready());
    assert(malformedDescriptorReadiness.readiness
        == editor::EditorSceneViewportDescriptorReadiness::InvalidPayload);

    auto invalidMaterialPayloadItem = texturedResolved.items[0];
    invalidMaterialPayloadItem.normalTextureId = text(kTextureId);
    invalidMaterialPayloadItem.normalTextureGeneration = textureGeneration;
    editor::EditorSceneViewportTextureCache wrongSemanticCache({1u, 16u, 512u});
    const auto wrongSemanticPayload =
        editor::prepareEditorSceneViewportMaterialPayload(invalidMaterialPayloadItem,
            viewportShaderReflection,
            *device,
            resources,
            wrongSemanticCache);
    assert(!wrongSemanticPayload.valid());
    assert(wrongSemanticPayload.diagnostic
        == editor::EditorSceneViewportMaterialPayloadDiagnostic::TextureCacheFailure);
    assert(wrongSemanticPayload.textureDiagnostic
        == editor::EditorSceneViewportTextureCacheDiagnostic::ColorSpaceMismatch);

    auto staleMaterialPayloadItem = texturedResolved.items[0];
    ++staleMaterialPayloadItem.materialGeneration;
    editor::EditorSceneViewportTextureCache staleMaterialTextureCache({1u, 16u, 512u});
    const auto staleMaterialPayload =
        editor::prepareEditorSceneViewportMaterialPayload(staleMaterialPayloadItem,
            viewportShaderReflection,
            *device,
            resources,
            staleMaterialTextureCache);
    assert(!staleMaterialPayload.valid());
    assert(staleMaterialPayload.diagnostic
        == editor::EditorSceneViewportMaterialPayloadDiagnostic::ResourceChanged);

    resource::ResourceManager missingMeshManager;
    const auto absent = editor::resolveEditorSceneViewportResources(frame, missingMeshManager);
    assert(!absent.valid());
    assert(
        absent.items[0].diagnostic == editor::EditorSceneViewportDiagnostic::MissingMeshResource);

    resource::ResourceManager pendingMeshManager;
    assert(pendingMeshManager.declareResource(kMeshId, "MeshAssetResource").success);
    const auto pending = editor::resolveEditorSceneViewportResources(frame, pendingMeshManager);
    assert(
        pending.items[0].diagnostic == editor::EditorSceneViewportDiagnostic::MeshResourceNotReady);

    resource::ResourceManager wrongTypeManager;
    auto wrongType = core::makeUnique<resource::MaterialResource>();
    wrongType->configureMetadata(reference(kMeshId, "MaterialResource"));
    assert(wrongTypeManager.loadResource(std::move(wrongType)).success);
    const auto wrongMeshType = editor::resolveEditorSceneViewportResources(frame, wrongTypeManager);
    assert(wrongMeshType.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MeshResourceWrongType);
    assert(std::string_view(
               editor::editorSceneViewportDiagnosticCode(wrongMeshType.items[0].diagnostic))
        == "ARP-EDITOR-VIEWPORT-MESH-RESOURCE-WRONG-TYPE");

    resource::ResourceManager invalidMeshManager;
    loadMesh(invalidMeshManager);
    auto mutableMesh = invalidMeshManager.find<resource::MeshAssetResource>(kMeshId);
    const_cast<resource::MeshAssetResource*>(mutableMesh.get())->mutableMesh()->indices.clear();
    const auto invalidMesh = editor::resolveEditorSceneViewportResources(frame, invalidMeshManager);
    assert(
        invalidMesh.items[0].diagnostic == editor::EditorSceneViewportDiagnostic::InvalidMeshData);

    resource::ResourceManager mutableMeshManager;
    loadMesh(mutableMeshManager);
    auto mutableMeshHandle = mutableMeshManager.find<resource::MeshAssetResource>(kMeshId);
    assert(const_cast<resource::MeshAssetResource*>(mutableMeshHandle.get())->mutableMesh()
        != nullptr);
    const auto mutableGeometry =
        editor::resolveEditorSceneViewportResources(frame, mutableMeshManager);
    assert(mutableGeometry.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MutableMeshGeometryUnsupported);
    assert(std::string_view(
               editor::editorSceneViewportDiagnosticCode(mutableGeometry.items[0].diagnostic))
        == "ARP-EDITOR-VIEWPORT-MESH-GEOMETRY-MUTABLE-UNSUPPORTED");

    resource::ResourceManager missingSourceManager;
    loadMesh(missingSourceManager);
    loadMaterial(missingSourceManager);
    const auto missingSource =
        editor::resolveEditorSceneViewportResources(frame, missingSourceManager);
    assert(missingSource.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MaterialSourceMissing);

    resource::ResourceManager missingShaderManager;
    loadMesh(missingShaderManager);
    loadMaterial(missingShaderManager);
    loadSource(missingShaderManager);
    const auto missingShader =
        editor::resolveEditorSceneViewportResources(frame, missingShaderManager);
    assert(missingShader.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MaterialShaderPackageMissing);

    resource::ResourceManager incompleteShaderManager;
    loadMesh(incompleteShaderManager);
    loadMaterial(incompleteShaderManager);
    loadSource(incompleteShaderManager);
    loadShader(incompleteShaderManager, false);
    const auto incompleteShader =
        editor::resolveEditorSceneViewportResources(frame, incompleteShaderManager);
    assert(incompleteShader.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::ShaderArtifactMetadataMissing);

    resource::ResourceManager legacyMaterialManager;
    loadMesh(legacyMaterialManager);
    loadMaterial(legacyMaterialManager, false);
    loadSource(legacyMaterialManager);
    loadShader(legacyMaterialManager);
    const auto legacyMaterial =
        editor::resolveEditorSceneViewportResources(frame, legacyMaterialManager);
    assert(legacyMaterial.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MaterialContractUnsupported);

    resource::ResourceManager invalidMaterialManager;
    loadMesh(invalidMaterialManager);
    loadMaterial(invalidMaterialManager, true, false);
    loadSource(invalidMaterialManager);
    loadShader(invalidMaterialManager);
    const auto invalidMaterial =
        editor::resolveEditorSceneViewportResources(frame, invalidMaterialManager);
    assert(invalidMaterial.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MaterialContractInvalid);

    resource::ResourceManager mismatchedShaderManager;
    loadMesh(mismatchedShaderManager);
    loadMaterial(mismatchedShaderManager);
    loadSource(mismatchedShaderManager);
    loadShader(mismatchedShaderManager, true, false);
    const auto mismatchedShader =
        editor::resolveEditorSceneViewportResources(frame, mismatchedShaderManager);
    assert(mismatchedShader.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MaterialShaderInterfaceMismatch);

    resource::ResourceManager missingTextureManager;
    loadMesh(missingTextureManager);
    loadMaterial(missingTextureManager, true, true, kTextureId);
    loadSource(missingTextureManager);
    loadShader(missingTextureManager);
    const auto missingTexture =
        editor::resolveEditorSceneViewportResources(frame, missingTextureManager);
    assert(missingTexture.items[0].diagnostic
        == editor::EditorSceneViewportDiagnostic::MaterialTextureNotReady);

    resource::ResourceManager readyTextureManager;
    loadMesh(readyTextureManager);
    loadMaterial(readyTextureManager, true, true, kTextureId);
    loadSource(readyTextureManager);
    loadShader(readyTextureManager);
    loadTexture(readyTextureManager);
    const auto readyTexture =
        editor::resolveEditorSceneViewportResources(frame, readyTextureManager);
    assert(readyTexture.valid());
    assert(readyTexture.items[0].baseColorTextureId == kTextureId);
    assert(readyTexture.items[0].baseColorTextureGeneration == 1);
    assert(readyTexture.items[0].normalTextureGeneration == 0);
    assert(readyTexture.items[0].metallicRoughnessTextureGeneration == 0);
    assert(readyTexture.items[0].occlusionTextureGeneration == 0);
    assert(readyTexture.items[0].emissiveTextureGeneration == 0);

    const editor::EditorSceneViewportBackend backends[] = {
        editor::EditorSceneViewportBackend::Null,
        editor::EditorSceneViewportBackend::Vulkan,
        editor::EditorSceneViewportBackend::Metal,
        editor::EditorSceneViewportBackend::WebGPU,
    };
    for (std::size_t index = 0; index < 4; ++index) {
        const auto disposition = editor::editorSceneViewportBackendDisposition(backends[index]);
        assert(disposition
            == (index == 0 ? editor::EditorSceneViewportBackendDisposition::ContractValidationOnly
                           : editor::EditorSceneViewportBackendDisposition::DiagnosticFailure));
        assert(editor::editorSceneViewportBackendDiagnostic(backends[index])[0] != '\0');
    }
    return 0;
}
