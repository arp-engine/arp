#pragma once
#include "2d/components.h"
#include "2d/tile_map_runtime.h"
#include "core/concurrency/callback.h"
#include "scene/scene.h"

namespace arp::scene2d {
inline constexpr std::uint32_t kEditorTileMapMaxResidentChunks = 1024u;
inline constexpr std::uint32_t kEditorTileMapMaxVisibleCells = 65536u;

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct TileMapResourceComponentEditorProperties {
    APROP(Serialize,
        Editor,
        ResourceReference,
        ReferenceTarget = "TileMapResource",
        Doc = "resource:TileMapResource:required")
    core::pmr::String tileMapResourceId{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    std::uint32_t maxResidentChunks = 0;
    APROP(Serialize, Editor)
    std::uint32_t maxVisibleCells = 0;
    APROP(Serialize, Editor)
    std::uint64_t firstObjectId = 0;
};

// Resource-authoritative tile origins are authored in TileMapResource. Runtime
// ownership changes are prepared before replacing the active TileMapRuntime.
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class TileMapResourceComponent final : public scene::Component {
public:
    const char* scriptTypeName() const override { return "TileMapResourceComponent"; }

    TileMapResourceComponent(core::SharedCallback<resource::ResourceManager*()> resourcesProvider,
        core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> worldProvider)
        : resourcesProvider_(std::move(resourcesProvider)), worldProvider_(std::move(worldProvider))
    {
        synchronizeRuntimeOwner();
    }

    TileMapResourceComponent(resource::ResourceManager& resources,
        resource::ResourceHandle<TileMapResource> map,
        core::SharedPtr<physics2d::PhysicsWorld2D> world,
        TileMapRuntimeDesc desc)
        : resources_(&resources), map_(map), world_(std::move(world)), desc_(desc)
    {
        authoring_.tileMapResourceId = map_.id();
        authoring_.maxResidentChunks = desc_.maxResidentChunks;
        authoring_.maxVisibleCells = desc_.maxVisibleCells;
        authoring_.firstObjectId = desc_.firstObjectId;
        transformWasUnit_ = true;
        if (!map_.ready() || !world_ || !world_->valid())
            throw std::invalid_argument("ARP-2D-TILEMAP-BINDING-OR-TRS");
        resourceGeneration_ = map_.generation();
        const auto tileset = resources.find<TileSetResource>(map_.get()->tileSetId());
        tileSetGeneration_ = tileset.generation();
        runtime_ = core::makeUnique<TileMapRuntime>(resources, map_, *world_, desc_);
        editorDiagnostic_ = nullptr;
    }

    TileMapRuntime* runtime() { return runtime_.get(); }

    const TileMapRuntime* runtime() const { return runtime_.get(); }

    const auto& map() const { return map_; }

    const auto& desc() const { return desc_; }

    const char* editorDiagnostic() const { return editorDiagnostic_; }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(authoring_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (!value.is<TileMapResourceComponentEditorProperties>())
            return false;
        const auto& candidateProperties =
            value.cast<const TileMapResourceComponentEditorProperties&>();
        if (candidateProperties.tileMapResourceId.size() > 512u
            || candidateProperties.maxResidentChunks > kEditorTileMapMaxResidentChunks
            || candidateProperties.maxVisibleCells > kEditorTileMapMaxVisibleCells)
            return false;

        synchronizeRuntimeOwner();
        const bool configured = !candidateProperties.tileMapResourceId.empty()
            && candidateProperties.maxResidentChunks != 0u
            && candidateProperties.maxVisibleCells != 0u && candidateProperties.firstObjectId != 0u;
        if (!configured) {
            if (runtime_)
                return false;
            authoring_ = candidateProperties;
            editorDiagnostic_ = "EDITOR-TILEMAP-AUTHORING-INCOMPLETE";
            return true;
        }

        resource::ResourceManager* resources = currentResources();
        auto world = currentWorld();
        resource::ResourceHandle<TileMapResource> map;
        core::UniquePtr<TileMapRuntime> candidateRuntime;
        const auto candidateDesc = TileMapRuntimeDesc{candidateProperties.maxResidentChunks,
            candidateProperties.maxVisibleCells,
            candidateProperties.firstObjectId};
        if (!prepareRuntime(candidateProperties, resources, world, map, candidateRuntime))
            return false;

        auto previousWorld = world_;
        auto previousRuntime = std::move(runtime_);
        authoring_ = candidateProperties;
        resources_ = resources;
        resourceGeneration_ = map.generation();
        const auto tileset = resources->find<TileSetResource>(map.get()->tileSetId());
        tileSetGeneration_ = tileset.generation();
        map_ = map;
        world_ = std::move(world);
        desc_ = candidateDesc;
        transformWasUnit_ = true;
        runtime_ = std::move(candidateRuntime);
        editorDiagnostic_ = nullptr;
        previousRuntime.reset();
        return true;
    }

    void onUpdate(const scene::SceneUpdateContext&) override { synchronizeRuntimeOwner(); }

private:
    resource::ResourceManager* currentResources() const
    {
        return resourcesProvider_ ? resourcesProvider_() : resources_;
    }

    core::SharedPtr<physics2d::PhysicsWorld2D> currentWorld() const
    {
        return worldProvider_ ? worldProvider_() : world_;
    }

    bool ownerTransformIsUnit() const
    {
        const auto* owner = gameObject();
        if (!owner)
            return false;
        const auto transform = owner->world_transform();
        return transform.translation.x == 0.0f && transform.translation.y == 0.0f
            && transform.translation.z == 0.0f && transform.rotation.x == 0.0f
            && transform.rotation.y == 0.0f && transform.rotation.z == 0.0f
            && transform.rotation.w == 1.0f && transform.scale.x == 1.0f
            && transform.scale.y == 1.0f && transform.scale.z == 1.0f;
    }

    bool prepareRuntime(const TileMapResourceComponentEditorProperties& properties,
        resource::ResourceManager* resources,
        const core::SharedPtr<physics2d::PhysicsWorld2D>& world,
        resource::ResourceHandle<TileMapResource>& map,
        core::UniquePtr<TileMapRuntime>& runtime)
    {
        if (!resources || !world || !world->valid() || !ownerTransformIsUnit()) {
            editorDiagnostic_ = "EDITOR-TILEMAP-OWNER-OR-TRANSFORM";
            return false;
        }
        map = resources->find<TileMapResource>(properties.tileMapResourceId);
        if (!map.ready()) {
            editorDiagnostic_ = "EDITOR-TILEMAP-RESOURCE-NOT-READY";
            return false;
        }
        const auto tileset = resources->find<TileSetResource>(map.get()->tileSetId());
        if (!tileset.ready()) {
            editorDiagnostic_ = "EDITOR-TILEMAP-TILESET-NOT-READY";
            return false;
        }
        const TileMapRuntimeDesc candidateDesc{
            properties.maxResidentChunks, properties.maxVisibleCells, properties.firstObjectId};
        if (!candidateDesc.maxResidentChunks || !candidateDesc.maxVisibleCells
            || !candidateDesc.firstObjectId) {
            editorDiagnostic_ = "EDITOR-TILEMAP-RUNTIME-BUDGET-INVALID";
            return false;
        }
        runtime = core::makeUnique<TileMapRuntime>(*resources, map, *world, candidateDesc);
        return true;
    }

    void synchronizeRuntimeOwner()
    {
        auto* resources = currentResources();
        auto world = currentWorld();
        const auto generation = resources && !authoring_.tileMapResourceId.empty()
            ? resources->generationOf(authoring_.tileMapResourceId)
            : 0u;
        resource::ResourceGeneration tilesetGeneration = 0u;
        if (resources && !authoring_.tileMapResourceId.empty()) {
            const auto currentMap = resources->find<TileMapResource>(authoring_.tileMapResourceId);
            if (currentMap.ready())
                tilesetGeneration = resources->generationOf(currentMap.get()->tileSetId());
        }
        const bool ownerChanged = resources != resources_ || generation != resourceGeneration_
            || tilesetGeneration != tileSetGeneration_ || world.get() != world_.get();
        const bool transformIsUnit = ownerTransformIsUnit();
        const bool transformChanged = transformIsUnit != transformWasUnit_;
        if (!ownerChanged && !transformChanged)
            return;

        auto previousWorld = world_;
        auto previousRuntime = std::move(runtime_);
        resources_ = resources;
        resourceGeneration_ = generation;
        tileSetGeneration_ = tilesetGeneration;
        world_ = std::move(world);
        map_ = {};
        transformWasUnit_ = transformIsUnit;
        editorDiagnostic_ = nullptr;

        if (!transformIsUnit) {
            editorDiagnostic_ = "EDITOR-TILEMAP-OWNER-TRANSFORM";
        } else if (authoring_.tileMapResourceId.empty() || !authoring_.maxResidentChunks
            || !authoring_.maxVisibleCells || !authoring_.firstObjectId) {
            editorDiagnostic_ = "EDITOR-TILEMAP-AUTHORING-INCOMPLETE";
        } else {
            core::UniquePtr<TileMapRuntime> candidateRuntime;
            resource::ResourceHandle<TileMapResource> candidateMap;
            if (prepareRuntime(authoring_, resources_, world_, candidateMap, candidateRuntime)) {
                map_ = candidateMap;
                runtime_ = std::move(candidateRuntime);
                tileSetGeneration_ = resources_->generationOf(map_.get()->tileSetId());
                desc_ = {authoring_.maxResidentChunks,
                    authoring_.maxVisibleCells,
                    authoring_.firstObjectId};
            }
        }
        previousRuntime.reset();
    }

    core::SharedCallback<resource::ResourceManager*()> resourcesProvider_;
    core::SharedCallback<core::SharedPtr<physics2d::PhysicsWorld2D>()> worldProvider_;
    resource::ResourceManager* resources_ = nullptr;
    resource::ResourceGeneration resourceGeneration_ = 0;
    resource::ResourceGeneration tileSetGeneration_ = 0;
    resource::ResourceHandle<TileMapResource> map_;
    core::SharedPtr<physics2d::PhysicsWorld2D> world_;
    TileMapRuntimeDesc desc_{};
    TileMapResourceComponentEditorProperties authoring_;
    core::UniquePtr<TileMapRuntime> runtime_;
    const char* editorDiagnostic_ = "EDITOR-TILEMAP-AUTHORING-INCOMPLETE";
    bool transformWasUnit_ = false;
};

} // namespace arp::scene2d
