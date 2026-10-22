#pragma once

#include "3d/components.h"
#include "3d/terrain/terrain_surface_instance.h"
#include "core/memory/cpu_allocator.h"
#include "renderer/runtime_scene.h"
#include "resource/resource_manager.h"
#include "resource/terrain_asset_resource.h"

namespace arp::renderer {

/// Scene preparation adapter for TerrainComponent. Authored components carry
/// only a TerrainAsset resource ID. RenderAndQuery derives renderer metadata;
/// QueryOnly retains a world surface without changing independent render owners.
class TerrainSceneBinding final {
public:
    enum class PreparationMode { RenderAndQuery, QueryOnly };

    using ResolveMaterialFn = bool (*)(void* context,
        const resource::TerrainAssetResource& terrain,
        MaterialHandle& outMaterial,
        core::pmr::String& diagnostics);

    struct Desc final {
        terrain::TerrainRuntime::Desc runtime;
        // A baked TerrainAsset owns its tile layout. Callers that stream its
        // mesh tiles directly select this instead of supplying duplicate
        // chunk dimensions in scene configuration.
        bool useAssetTileLayout = false;
        ResolveMaterialFn resolveMaterial = nullptr;
        void* context = nullptr;
        scene::Transform worldTransform;
        PreparationMode mode = PreparationMode::RenderAndQuery;

        bool valid() const noexcept
        {
            return mode == PreparationMode::QueryOnly
                || (mode == PreparationMode::RenderAndQuery && resolveMaterial != nullptr);
        }
    };

    struct Result final {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
        std::shared_ptr<const terrain::TerrainSurface> worldSurface;
    };

    /// Replaces the private renderer binding only after all resource and
    /// material checks succeed. A failure does not publish partial state.
    Result bind(scene3d::TerrainComponent& component,
        const resource::ResourceManager& resources,
        RuntimeScene& runtimeScene,
        const Desc& desc) const;

    // Explicit render-owner cleanup. QueryOnly bind has no component/registry
    // ownership to clear; discard its returned shared surface instead.
    void clear(scene3d::TerrainComponent& component) const { component.clearResolvedTerrain(); }

    void clear(scene3d::TerrainComponent& component, RuntimeScene& runtimeScene) const
    {
        runtimeScene.unloadTerrain(TerrainHandle{component.desc().terrain.id});
        component.clearResolvedTerrain();
    }

private:
    friend class SceneTerrainPreparation;

    struct Candidate final {
        Result result;
        std::shared_ptr<terrain::TerrainRuntime> runtime;
        scene3d::TerrainComponentDesc resolved;
        bool queryOnly = false;
    };

    Candidate prepare(const scene3d::TerrainComponent& component,
        const resource::ResourceManager& resources,
        const Desc& desc) const;
    // Caller reserves registration capacity and serializes all registry mutation.
    static void publish(scene3d::TerrainComponent& component,
        RuntimeScene& runtimeScene,
        Candidate&& candidate) noexcept;
};

} // namespace arp::renderer
