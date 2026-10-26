#pragma once

#include "editor/project/project_manifest.h"
#include "resource/asset_package_payload.h"
#include "scene/scene_asset_resource.h"

namespace arp::editor {

// Cold Editor authoring only. Projects own template selection; runtime receives
// a verified SceneAsset package and never parses the project manifest.
class ProjectSceneAuthoring final {
public:
    struct Snapshot {
        core::pmr::String resourceId;
        core::pmr::String document;
        // SHA-256 of the exact runtime document bytes, not the Editor document.
        core::pmr::String contentHash;
    };

    static constexpr std::size_t maxSceneBytes = 4u * 1024u * 1024u;
    static constexpr std::uint32_t maxObjects = 512;
    static core::SharedPtr<scene::SceneComponentFactory> factory(resource::ResourceManager&,
        core::SharedPtr<physics2d::PhysicsWorld2D>,
        core::pmr::String& diagnostic);
    // Instantiation-only provider composition; package loaders keep the cold
    // overload above, avoiding an owning callback back into their own manager.
    static core::SharedPtr<scene::SceneComponentFactory> factory(resource::ResourceManager&,
        core::SharedPtr<physics2d::PhysicsWorld2D>,
        core::pmr::String& diagnostic,
        core::SharedCallback<resource::ResourceManager*()> resourceProvider,
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> physicsProvider);
    static core::SharedPtr<const resource::AssetPackageCodecRegistry> packageCodecs(
        core::SharedPtr<const scene::SceneComponentFactory>, core::pmr::String& diagnostic);
    static bool prepareTemplate(const std::filesystem::path& projectRoot,
        ProjectManifest&,
        ProjectTemplateKind,
        core::pmr::String& diagnostic);
    // First export profile is a closed scene-only package. Referenced resources
    // require an explicit producer closure and fail instead of being omitted.
    static bool exportScene(const scene::Scene&,
        const scene::SceneComponentFactory&,
        const core::pmr::String& resourceId,
        const std::filesystem::path& output,
        core::pmr::String& diagnostic);
    // Encode only on the Scene owner thread. No live component escapes this call.
    static bool snapshot(const scene::Scene&,
        const scene::SceneComponentFactory&,
        const core::pmr::String& resourceId,
        Snapshot& destination,
        core::pmr::String& diagnostic);
    // Cold publication revalidates immutable bytes through the existing package
    // codec. The caller owns the factory and every owner captured by its callbacks.
    static bool exportSnapshot(const Snapshot&,
        const scene::SceneComponentFactory&,
        const std::filesystem::path& output,
        core::pmr::String& diagnostic);
};
} // namespace arp::editor
