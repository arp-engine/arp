#pragma once

#include "3d/components.h"
#include "core/cpu_allocator.h"
#include "renderer/runtime_scene.h"
#include "resource/resource_manager.h"
#include "resource/terrain_asset_resource.h"

namespace arp::renderer {

/// Scene preparation adapter for TerrainComponent. Authored components carry
/// only a TerrainAsset resource ID; this adapter resolves all native handles
/// and derives render metadata from the immutable asset.
class TerrainSceneBinding final {
public:
    using ResolveMaterialFn = bool (*)(void* context,
        const resource::TerrainAssetResource& terrain,
        MaterialHandle& outMaterial,
        core::pmr::String& diagnostics);

    struct Desc final {
        TerrainRuntime::Desc runtime;
        // A baked TerrainAsset owns its tile layout. Callers that stream its
        // mesh tiles directly select this instead of supplying duplicate
        // chunk dimensions in scene configuration.
        bool useAssetTileLayout = false;
        ResolveMaterialFn resolveMaterial = nullptr;
        void* context = nullptr;
    };

    struct Result final {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    /// Replaces the private renderer binding only after all resource and
    /// material checks succeed. A failure does not publish partial state.
    Result bind(scene3d::TerrainComponent& component,
        const resource::ResourceManager& resources,
        RuntimeScene& runtimeScene,
        const Desc& desc) const;

    void clear(scene3d::TerrainComponent& component) const { component.clearResolvedTerrain(); }
};

} // namespace arp::renderer
