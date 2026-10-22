#include "renderer/terrain_scene_binding.h"

#include <algorithm>

namespace arp::renderer {
namespace {

core::pmr::String diagnostic(const char* message)
{
    return core::pmr::String(message, &core::defaultCpuMemoryResource());
}

} // namespace

TerrainSceneBinding::Candidate TerrainSceneBinding::prepare(
    const scene3d::TerrainComponent& component,
    const resource::ResourceManager& resources,
    const Desc& desc) const
{
    Candidate candidate;
    auto& result = candidate.result;
    const auto& world = desc.worldTransform;
    if (world.scale.x != 1.0f || world.scale.y != 1.0f || world.scale.z != 1.0f
        || world.rotation.x != 0.0f || world.rotation.y != 0.0f || world.rotation.z != 0.0f
        || std::fabs(world.rotation.w) != 1.0f || !std::isfinite(world.translation.x)
        || !std::isfinite(world.translation.y) || !std::isfinite(world.translation.z)) {
        result.diagnostics = diagnostic(
            "TERRAIN-INSTANCE-TRANSFORM-UNSUPPORTED: only finite translation is supported");
        return candidate;
    }
    if (component.resourceId().empty()) {
        result.diagnostics =
            diagnostic("TerrainComponent requires an authored TerrainAsset resource ID");
        return candidate;
    }
    if (!desc.valid()) {
        result.diagnostics = diagnostic(
            "TERRAIN-SCENE-PREPARATION-CONFIG: explicit mode/material resolver required");
        return candidate;
    }

    const auto terrainHandle =
        resources.find<resource::TerrainAssetResource>(component.resourceId());
    if (!terrainHandle.ready()) {
        result.diagnostics = diagnostic("TerrainComponent TerrainAsset resource is not ready: ");
        result.diagnostics += terrainHandle.diagnostics();
        return candidate;
    }
    const auto* terrain = terrainHandle.get();
    if (terrain == nullptr || !terrain->heightRange().valid()) {
        result.diagnostics =
            diagnostic("TerrainComponent TerrainAsset has invalid height metadata");
        return candidate;
    }

    const auto localSurface = terrain->snapshotSurface();
    auto worldSurface =
        core::makeShared<terrain::TerrainSurfaceInstance>(localSurface, world.translation);
    if (!worldSurface->valid()) {
        result.diagnostics = diagnostic("TERRAIN-INSTANCE-BOUNDS-INVALID");
        return candidate;
    }
    if (desc.mode == PreparationMode::QueryOnly) {
        candidate.queryOnly = true;
        result.worldSurface = std::move(worldSurface);
        result.success = true;
        return candidate;
    }

    terrain::TerrainRuntime::Desc runtimeDesc = desc.runtime;
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
        return candidate;
    }

    auto runtime = core::makeShared<terrain::TerrainRuntime>();
    runtime->setTerrainSurface(localSurface);
    if (!runtime->initialize(runtimeDesc)) {
        result.diagnostics =
            diagnostic("TerrainRuntime rejected the resolved TerrainAsset surface");
        return candidate;
    }

    const auto bounds = terrain->bounds();
    const auto height = terrain->heightRange();
    scene3d::TerrainComponentDesc resolved;
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
    candidate.resolved = std::move(resolved);
    candidate.runtime = std::move(runtime);
    result.worldSurface = std::move(worldSurface);
    result.success = true;
    return candidate;
}

void TerrainSceneBinding::publish(scene3d::TerrainComponent& component,
    RuntimeScene& runtimeScene,
    Candidate&& candidate) noexcept
{
    if (candidate.queryOnly)
        return;
    const auto previous = component.desc().terrain;
    const TerrainHandle handle{previous.id};
    if (previous.valid() && runtimeScene.terrain(handle)) {
        runtimeScene.updateTerrain(handle, std::move(candidate.runtime));
        candidate.resolved.terrain = previous;
    } else {
        candidate.resolved.terrain =
            scene3d::TerrainHandle{runtimeScene.registerTerrain(std::move(candidate.runtime)).id};
    }
    component.bindResolvedTerrain(std::move(candidate.resolved));
}

TerrainSceneBinding::Result TerrainSceneBinding::bind(scene3d::TerrainComponent& component,
    const resource::ResourceManager& resources,
    RuntimeScene& runtimeScene,
    const Desc& desc) const
{
    auto candidate = prepare(component, resources, desc);
    if (!candidate.result.success)
        return std::move(candidate.result);
    if (candidate.queryOnly)
        return std::move(candidate.result);
    const auto previous = component.desc().terrain;
    const std::size_t registrations = runtimeScene.terrain(TerrainHandle{previous.id}) ? 0u : 1u;
    if (!runtimeScene.reserveTerrainRegistrations(registrations)) {
        candidate.result.success = false;
        candidate.result.diagnostics = "TERRAIN-SCENE-REGISTRATION-CAPACITY";
        return std::move(candidate.result);
    }
    publish(component, runtimeScene, std::move(candidate));
    return std::move(candidate.result);
}

} // namespace arp::renderer
