#include "renderer/terrain_scene_binding.h"

#include <algorithm>

namespace arp::renderer {
namespace {

core::pmr::String diagnostic(const char* message)
{
    return core::pmr::String(message, &core::defaultCpuMemoryResource());
}

} // namespace

TerrainSceneBinding::Result TerrainSceneBinding::bind(scene3d::TerrainComponent& component,
    const resource::ResourceManager& resources,
    RuntimeScene& runtimeScene,
    const Desc& desc) const
{
    Result result;
    if (component.resourceId().empty()) {
        result.diagnostics =
            diagnostic("TerrainComponent requires an authored TerrainAsset resource ID");
        return result;
    }
    if (desc.resolveMaterial == nullptr) {
        result.diagnostics =
            diagnostic("Terrain scene preparation requires an explicit material resolver");
        return result;
    }

    const auto terrainHandle =
        resources.find<resource::TerrainAssetResource>(component.resourceId());
    if (!terrainHandle.ready()) {
        result.diagnostics = diagnostic("TerrainComponent TerrainAsset resource is not ready: ");
        result.diagnostics += terrainHandle.diagnostics();
        return result;
    }
    const auto* terrain = terrainHandle.get();
    if (terrain == nullptr || !terrain->heightRange().valid()) {
        result.diagnostics =
            diagnostic("TerrainComponent TerrainAsset has invalid height metadata");
        return result;
    }

    TerrainRuntime::Desc runtimeDesc = desc.runtime;
    runtimeDesc.worldBounds = terrain->bounds();
    if (desc.useAssetTileLayout) {
        runtimeDesc.chunkSize = terrain->tileWorldSize();
        runtimeDesc.samplesPerChunk = terrain->samplesPerEdge();
        runtimeDesc.activeRadius = std::max(terrain->tileCountX(), terrain->tileCountZ());
    }

    MaterialHandle material;
    if (!desc.resolveMaterial(desc.context, *terrain, material, result.diagnostics)
        || !material.valid()) {
        if (result.diagnostics.empty()) {
            result.diagnostics =
                diagnostic("Terrain material resolver did not return a valid renderer material");
        }
        return result;
    }

    auto runtime = core::makeShared<TerrainRuntime>();
    runtime->setTerrainSurface(terrain->snapshotSurface());
    if (!runtime->initialize(runtimeDesc)) {
        result.diagnostics =
            diagnostic("TerrainRuntime rejected the resolved TerrainAsset surface");
        return result;
    }

    const auto bounds = terrain->bounds();
    const auto height = terrain->heightRange();
    scene3d::TerrainComponentDesc resolved;
    const auto previous = component.desc().terrain;
    const TerrainHandle runtimeHandle{previous.id};
    if (previous.valid() && runtimeScene.terrain(runtimeHandle) != nullptr
        && runtimeScene.updateTerrain(runtimeHandle, std::move(runtime))) {
        resolved.terrain = previous;
    } else {
        resolved.terrain =
            scene3d::TerrainHandle{runtimeScene.registerTerrain(std::move(runtime)).id};
    }
    resolved.material = material;
    resolved.metadata = scene3d::TerrainMetadata::explicitGrid(
        terrain->tileCountX(), terrain->tileCountZ(), terrain->tileWorldSize());
    resolved.localBounds =
        scene3d::Bounds3D::explicitBounds({(bounds.minX + bounds.maxX) * 0.5f,
                                              (height.minimum + height.maximum) * 0.5f,
                                              (bounds.minZ + bounds.maxZ) * 0.5f},
            {(bounds.maxX - bounds.minX) * 0.5f,
                (height.maximum - height.minimum) * 0.5f,
                (bounds.maxZ - bounds.minZ) * 0.5f});
    component.bindResolvedTerrain(std::move(resolved));
    result.success = true;
    return result;
}

} // namespace arp::renderer
