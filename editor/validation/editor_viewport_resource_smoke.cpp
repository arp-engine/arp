#include "editor/validation/editor_viewport_resource_smoke.h"

#include "2d/components.h"
#include "3d/components.h"
#include "resource/mesh_asset_resources.h"
#include "resource/prefab_asset_resources.h"
#include "tests/fixtures/scene_2d_fixture.h"

namespace arp::editor::validation {

resource::ResourceReference viewportSmokeReference(const char* id, const char* type)
{
    resource::ResourceReference reference;
    reference.id = id;
    reference.type = type;
    reference.status = "Contract Complete";
    return reference;
}

core::UniquePtr<resource::TextureResource> viewportSmokeTexture(const char* id, bool srgb)
{
    auto texture = core::makeUnique<resource::TextureResource>();
    texture->configureMetadata(viewportSmokeReference(id, "TextureResource"));
    texture->usage = srgb ? "color" : "data";
    texture->srgb = srgb;
    texture->decodedWidth = 1u;
    texture->decodedHeight = 1u;
    texture->decodedSrgb = srgb;
    texture->decodedRgba8 = {255u, 255u, 255u, 255u};
    return texture;
}

core::UniquePtr<resource::MaterialResource> viewportSmokeMaterial(
    const char* id, float red, float green, float blue, float metallic, float roughness)
{
    auto material = core::makeUnique<resource::MaterialResource>();
    material->configureMetadata(viewportSmokeReference(id, "MaterialResource"));
    material->sourceResourceId = kViewportSmokeSourceId;
    material->shaderPackageId = kViewportSmokeShaderId;
    material->materialContractVersion = resource::kPbrMetallicRoughnessContractVersion;
    material->surfaceModel = resource::kPbrMetallicRoughnessContract;
    material->baseColorR = red;
    material->baseColorG = green;
    material->baseColorB = blue;
    material->alphaMode = "OPAQUE";
    material->metallicFactor = metallic;
    material->roughnessFactor = roughness;
    material->baseColorTextureId = kViewportSmokeSrgbTextureId;
    material->normalTextureId = kViewportSmokeLinearTextureId;
    material->metallicRoughnessTextureId = kViewportSmokeLinearTextureId;
    material->occlusionTextureId = kViewportSmokeLinearTextureId;
    material->emissiveTextureId = kViewportSmokeSrgbTextureId;
    return material;
}

bool installViewportSmokeSceneResources(EditorShell& shell, core::pmr::String& diagnostics)
{
    auto mesh = core::makeUnique<resource::MeshAssetResource>(
        core::pmr::String(kViewportSmokeMeshId, &core::defaultCpuMemoryResource()),
        assets::MeshAsset::debugTriangle());
    if (!shell.projectResources().loadResource(std::move(mesh)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-MESH-RESOURCE";
        return false;
    }
    auto cubeMesh = core::makeUnique<resource::MeshAssetResource>(
        core::pmr::String(kViewportSmokeCubeMeshId, &core::defaultCpuMemoryResource()), 0.8f);
    if (!shell.projectResources().loadResource(std::move(cubeMesh)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-CUBE-MESH-RESOURCE";
        return false;
    }

    auto source = core::makeUnique<resource::ExternalAssetSourceResource>();
    source->configureMetadata(
        viewportSmokeReference(kViewportSmokeSourceId, "ExternalAssetSourceResource"));
    if (!shell.projectResources().loadResource(std::move(source)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-SOURCE-RESOURCE";
        return false;
    }

    auto shader = core::makeUnique<resource::ShaderPackageResource>();
    shader->configureMetadata(
        viewportSmokeReference(kViewportSmokeShaderId, "ShaderPackageResource"));
    shader->slangPath = "shaders/editor/viewport_pbr.slang";
    shader->spirvPath = "shaders/editor/viewport_pbr.spv";
    shader->mslPath = "shaders/editor/viewport_pbr.metal";
    shader->reflectionPath = "shaders/editor/viewport_pbr.reflection.yaml";
    shader->materialInterfaceContract = resource::kPbrMetallicRoughnessContract;
    shader->materialInterfaceVersion = resource::kPbrMetallicRoughnessContractVersion;
    if (!shell.projectResources().loadResource(std::move(shader)).success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-SHADER-RESOURCE";
        return false;
    }

    if (!shell.projectResources()
            .loadResource(
                viewportSmokeMaterial(kViewportSmokeMaterialId, 0.8f, 0.32f, 0.12f, 0.0f, 0.8f))
            .success
        || !shell.projectResources()
            .loadResource(viewportSmokeMaterial(
                kViewportSmokeCubeMaterialId, 0.12f, 0.42f, 0.82f, 0.15f, 0.35f))
            .success
        || !shell.projectResources()
            .loadResource(viewportSmokeTexture(kViewportSmokeSrgbTextureId, true))
            .success
        || !shell.projectResources()
            .loadResource(viewportSmokeTexture(kViewportSmokeLinearTextureId, false))
            .success) {
        diagnostics = "ARP-EDITOR-VIEWPORT-SMOKE-MATERIAL-RESOURCES";
        return false;
    }
    return true;
}

bool installViewportSmokeScene(
    EditorShell& shell, core::pmr::String& diagnostic, std::string_view fixtureRoot)
{
    if (!installViewportSmokeSceneResources(shell, diagnostic))
        return false;
    auto* object =
        shell.sceneDocument().scene().findObjectByName(core::pmr::String("MeshInstance"));
    auto* mesh = object ? object->getComponent<scene3d::MeshComponent>() : nullptr;
    if (!mesh) {
        diagnostic = "EDITOR-VALIDATION-MESH-COMPONENT";
        return false;
    }
    auto set = [&](const char* property, const char* id) {
        SetSceneComponentPropertyCommand command(property);
        command.objectId = object->id().value();
        command.componentId = mesh->id().value();
        command.valueKind = ScenePropertyValueKind::ResourceReference;
        command.stringValue = id;
        return shell.submitCommand(std::move(command)).status == EditorResponseStatus::Completed;
    };
    if (!set("resourceId", kViewportSmokeCubeMeshId)
        || !set("materialResourceId", kViewportSmokeMaterialId))
        return false;
    if (!tests::loadSpriteFixtureAtRoot(shell.projectResources(), fixtureRoot)) {
        diagnostic = "EDITOR-VALIDATION-SPRITE-RESOURCE";
        return false;
    }
    if (shell.submitCommand(CreateSceneObjectCommand("ViewportSprite")).status
            != EditorResponseStatus::Completed
        || shell.submitCommand(AddSceneComponentCommand("SpriteRendererComponent")).status
            != EditorResponseStatus::Completed)
        return false;
    auto* spriteObject =
        shell.sceneDocument().scene().findObject(shell.sceneDocument().selectedObject());
    auto* sprite =
        spriteObject ? spriteObject->getComponent<scene2d::SpriteRendererComponent>() : nullptr;
    if (!sprite)
        return false;
    sprite->setSpriteResource(core::pmr::String(tests::spriteA));
    SetSceneObjectTransformCommand transform;
    transform.objectId = spriteObject->id().value();
    transform.translation = {-1.5f, 0.5f, 0.0f};
    if (shell.submitCommand(transform).status != EditorResponseStatus::Completed)
        return false;
    return true;
}

} // namespace arp::editor::validation
