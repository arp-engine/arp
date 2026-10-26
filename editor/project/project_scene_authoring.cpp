#include "editor/project/project_scene_authoring.h"

#include "2d/scene_component_persistence.h"
#include "2d/sprite_animation_component.h"
#include "2d/tile_map_resource_component.h"
#include "3d/authoring_components.h"
#include "3d/components.h"
#include "3d/scene_component_persistence.h"
#include "core/hash/sha256.h"
#include "core/io/atomic_file_service.h"
#include "core/io/filesystem_path.h"
#include "core/io/staged_directory.h"
#include "resource/asset_package_closure.h"
#include "scene/scene_asset_package.h"
#include "scene/scene_component_persistence.h"

namespace arp::editor {
core::SharedPtr<scene::SceneComponentFactory> ProjectSceneAuthoring::factory(
    resource::ResourceManager& resources,
    core::SharedPtr<physics2d::PhysicsWorld2D> world,
    core::pmr::String& diagnostic)
{
    return factory(resources, std::move(world), diagnostic, {}, {});
}

core::SharedPtr<scene::SceneComponentFactory> ProjectSceneAuthoring::factory(
    resource::ResourceManager& resources,
    core::SharedPtr<physics2d::PhysicsWorld2D> world,
    core::pmr::String& diagnostic,
    core::SharedCallback<resource::ResourceManager*()> resourceProvider,
    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> physicsProvider)
{
    if (bool(resourceProvider) != bool(physicsProvider)
        || (resourceProvider && (resourceProvider() != &resources || physicsProvider() != world))) {
        diagnostic = "EDITOR-SCENE-PERSISTENCE-SERVICE-BINDING";
        return {};
    }
    auto result = core::makeShared<scene::SceneComponentFactory>();
    // Reuse the native persistence schema/validator, supplying only the two
    // constructors whose resource providers must follow this live workspace.
    if (world && resourceProvider && (!result->add("SpriteAnimationComponent", [resourceProvider] {
            return core::makeUnique<scene2d::SpriteAnimationComponent>(
                resourceProvider, scene2d::kEditorSpriteAnimationEventBudget);
        }) || !result->add("TileMapResourceComponent", [resourceProvider, physicsProvider] {
            return core::makeUnique<scene2d::TileMapResourceComponent>(
                resourceProvider, physicsProvider, physics2d::ComponentUsage::Authoring);
        }))) {
        diagnostic = "EDITOR-SCENE-PERSISTENCE-SERVICE-REGISTRATION";
        return {};
    }
    if (!scene::registerScenePersistence(*result) || !scene3d::registerScene3DPersistence(*result)
        || (world
            && !scene2d::registerScene2DPersistence(
                *result, resources, world, physics2d::ComponentUsage::Authoring))) {
        diagnostic = "EDITOR-SCENE-PERSISTENCE-REGISTRATION";
        return {};
    }
    return result;
}

core::SharedPtr<const resource::AssetPackageCodecRegistry> ProjectSceneAuthoring::packageCodecs(
    core::SharedPtr<const scene::SceneComponentFactory> factory, core::pmr::String& diagnostic)
{
    auto result = core::makeShared<resource::AssetPackageCodecRegistry>(2);
    if (!scene::registerSceneAssetPackageCodecs(*result, std::move(factory), diagnostic)
        || !result->freeze(diagnostic))
        return {};
    return result;
}

bool ProjectSceneAuthoring::exportScene(const scene::Scene& scene,
    const scene::SceneComponentFactory& factory,
    const core::pmr::String& id,
    const std::filesystem::path& output,
    core::pmr::String& diagnostic)
{
    Snapshot value;
    return snapshot(scene, factory, id, value, diagnostic)
        && exportSnapshot(value, factory, output, diagnostic);
}

bool ProjectSceneAuthoring::snapshot(const scene::Scene& scene,
    const scene::SceneComponentFactory& factory,
    const core::pmr::String& id,
    Snapshot& destination,
    core::pmr::String& diagnostic)
{
    Snapshot candidate;
    if (!resource::isResourceUuid(id)) {
        diagnostic = "EDITOR-SCENE-EXPORT-IDENTITY-REQUIRED";
        return false;
    }
    if (scene.objects().size() > maxObjects) {
        diagnostic = "EDITOR-SCENE-EXPORT-BUDGET";
        return false;
    }
    if (!scene::SceneDocumentResource::encode(
            scene, factory, "arp.scene-asset", id, candidate.document, diagnostic))
        return false;
    if (candidate.document.size() > maxSceneBytes) {
        diagnostic = "EDITOR-SCENE-EXPORT-BUDGET";
        return false;
    }
    candidate.resourceId = id;
    candidate.contentHash = core::Sha256::hexDigest(candidate.document);
    destination = std::move(candidate);
    diagnostic.clear();
    return true;
}

bool ProjectSceneAuthoring::exportSnapshot(const Snapshot& value,
    const scene::SceneComponentFactory& factory,
    const std::filesystem::path& output,
    core::pmr::String& diagnostic)
{
    using Y = core::YamlContract;
    if (value.document.size() > maxSceneBytes || !resource::isResourceUuid(value.resourceId)
        || core::Sha256::hexDigest(value.document) != value.contentHash) {
        diagnostic = "EDITOR-SCENE-EXPORT-SNAPSHOT-INVALID";
        return false;
    }
    try {
        if (!scene::SceneDocumentResource::validateDocument(Y::parseSingleDocument(value.document),
                "arp.scene-asset",
                value.resourceId,
                maxObjects)) {
            diagnostic = "EDITOR-SCENE-EXPORT-DOCUMENT-INVALID";
            return false;
        }
    } catch (...) {
        diagnostic = "EDITOR-SCENE-EXPORT-DOCUMENT-INVALID";
        return false;
    }
    // Stage into a sibling directory; closure.publish owns the final exclusive
    // publication. Temporary preparation never becomes a valid user project.
    core::io::StagedDirectory staging;
    auto preparation = output;
    preparation += ".preparation";
    if (!staging.begin(preparation)) {
        diagnostic = staging.diagnostic();
        return false;
    }
    const auto written =
        core::io::replaceFileAtomically(core::FilePath::utf8(staging.path() / "scene.yaml"),
            {value.document.begin(), value.document.end()});
    if (!written.success) {
        diagnostic = written.diagnostic;
        return false;
    }
    auto payload = Y::Node::mapping();
    payload["type"] = "arp.scene-package";
    payload["contractVersion"] = 1;
    payload["documentPath"] = "scene.yaml";
    payload["maxDocumentBytes"] = static_cast<std::int64_t>(maxSceneBytes);
    payload["maxObjects"] = maxObjects;
    resource::ResourceEnvelopeBuildDesc desc;
    desc.resourceId = value.resourceId;
    desc.resourceType = "SceneAssetResource";
    desc.status = "Contract Complete";
    const auto envelope = resource::buildResourceEnvelopeFromYaml(desc, Y::serialize(payload));
    if (!envelope.success) {
        diagnostic = envelope.diagnostics;
        return false;
    }
    resource::InternalAssetPackageDesc package;
    package.name = "EditorScene/1";
    package.status = "Contract Complete";
    package.closurePath = "closure.yaml";
    resource::InternalAssetPackageEntry entry;
    entry.resourceId = value.resourceId;
    entry.resourceType = desc.resourceType;
    entry.status = desc.status;
    entry.contentHash = envelope.contentHash;
    entry.envelopeYaml = envelope.envelopeYaml;
    entry.envelopePath = "resources/scene.arpres.yaml";
    package.entries.push_back(std::move(entry));
    const auto codecs =
        packageCodecs(core::makeShared<scene::SceneComponentFactory>(factory), diagnostic);
    if (!codecs)
        return false;
    resource::AssetPackageClosureService closure(codecs);
    const auto indexed = closure.prepareIndex(
        package, staging.path(), {1, 1, maxSceneBytes, maxSceneBytes, maxSceneBytes});
    if (!indexed.success) {
        diagnostic = indexed.diagnostics;
        return false;
    }
    const auto published = closure.publish(staging.path(), output);
    diagnostic = published.diagnostics;
    return published.success;
}

bool ProjectSceneAuthoring::prepareTemplate(const std::filesystem::path& root,
    ProjectManifest& manifest,
    ProjectTemplateKind kind,
    core::pmr::String& diagnostic)
{
    if (kind == ProjectTemplateKind::Empty)
        return manifest.entryScene.empty();
    if ((kind != ProjectTemplateKind::Game2D && kind != ProjectTemplateKind::Game3D)
        || !resource::isResourceUuid(manifest.entryScene) || !manifest.assetPackages.empty()) {
        diagnostic = "EDITOR-PROJECT-TEMPLATE-CONFIG";
        return false;
    }
    resource::ResourceManager resources;
    const auto codecs = factory(resources, {}, diagnostic);
    if (!codecs)
        return false;
    scene::Scene authored(
        manifest.name, scene::SceneId::fromValue(1), scene::SceneExecutionMode::Passive);
    auto& camera =
        authored.createObject(kind == ProjectTemplateKind::Game2D ? "Camera2D" : "Camera3D");
    auto d = kind == ProjectTemplateKind::Game2D
        ? scene::CameraComponentDesc::orthographic(16, .1f, 1000)
        : scene::CameraComponentDesc::perspective(60, 16.0f / 9.0f, .1f, 1000);
    d.aspectPolicy = scene::CameraAspectPolicy::matchViewport();
    d.visibleQueues = kind == ProjectTemplateKind::Game2D ? scene::RenderQueueMask::queue2D()
                                                          : scene::RenderQueueMask::queue3D();
    camera.addComponent<scene::CameraComponent>(d);
    if (kind == ProjectTemplateKind::Game3D) {
        scene3d::LightComponentDesc light;
        light.type = scene3d::LightType::Directional;
        authored.createObject("DirectionalLight").addComponent<scene3d::LightComponent>(light);
        authored.createObject("Sky").addComponent<scene3d::SkyComponent>();
    }
    std::error_code error;
    std::filesystem::create_directories(root / "assets", error);
    if (error) {
        diagnostic = "EDITOR-PROJECT-TEMPLATE-DIRECTORY";
        return false;
    }
    if (!exportScene(authored, *codecs, manifest.entryScene, root / "assets/main", diagnostic))
        return false;
    manifest.assetPackages.push_back("assets/main");
    return true;
}
} // namespace arp::editor
