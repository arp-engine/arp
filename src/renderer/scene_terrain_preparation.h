#pragma once
#include "renderer/terrain_scene_binding.h"

#include <scene/scene.h>

namespace arp::renderer {
// Scene owner calls synchronize before extraction and clear before replacing
// its RuntimeScene. Entries contain stable IDs, never borrowed components.
class ARP_API SceneTerrainPreparation final {
public:
    struct Desc final {
        std::size_t maxObjects = 0u;
        std::size_t maxTerrains = 0u;
        TerrainSceneBinding::Desc binding;
    };

    struct Result final {
        bool success = false;
        std::size_t bound = 0u, reused = 0u, released = 0u, failed = 0u;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    Result synchronize(scene::Scene& scene,
        const resource::ResourceManager& resources,
        RuntimeScene& runtime,
        const Desc& desc);
    using BeforePublishFn = bool (*)(
        void* context, const SceneTerrainPreparation& candidate, core::pmr::String& diagnostics);
    // One serialized gate after complete CPU preparation/capacity reservation.
    // The callback may inspect candidate sources/surfaces, never its handles.
    Result synchronizeWithGate(scene::Scene& scene,
        const resource::ResourceManager& resources,
        RuntimeScene& runtime,
        const Desc& desc,
        BeforePublishFn gate,
        void* context);
    std::shared_ptr<const terrain::TerrainSurface> worldSurface(scene::ComponentId component) const;

    std::size_t terrainCount() const noexcept { return entries_.size(); }

    struct Source final {
        resource::NameView resourceId;
        resource::ResourceGeneration generation = 0u;
        core::Vec3 translation;
    };

    // Borrowed ID view is valid until the next synchronize/clear.
    Source singleSource() const noexcept
    {
        return entries_.size() == 1u ? Source{entries_.front().resourceId,
                                           entries_.front().resourceGeneration,
                                           entries_.front().transform.translation}
                                     : Source{};
    }

    std::shared_ptr<const terrain::TerrainSurface> singleWorldSurface() const
    {
        return entries_.size() == 1u ? entries_.front().worldSurface : nullptr;
    }

    void clear(scene::Scene& scene, RuntimeScene& runtime);

private:
    struct Entry final {
        scene::GameObjectId object;
        scene::ComponentId component;
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        resource::ResourceGeneration resourceGeneration = 0u;
        scene::Transform transform;
        TerrainHandle handle;
        std::shared_ptr<const terrain::TerrainSurface> worldSurface;
        bool queryOnly = false;
    };

    static void release(scene::Scene& scene, RuntimeScene& runtime, const Entry& entry) noexcept;

    scene::SceneId sceneId_;
    TerrainSceneBinding::PreparationMode mode_ =
        TerrainSceneBinding::PreparationMode::RenderAndQuery;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
};
} // namespace arp::renderer
