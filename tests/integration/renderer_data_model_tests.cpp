#include "assets/asset_pipeline.h"
#include "renderer/renderer_result.h"
#include "renderer/runtime_scene.h"
#include "renderer/terrain_gpu_runtime.h"
#include "renderer/terrain_runtime.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <cassert>
#include <memory>
#include <vector>

namespace {

using FlatTerrainSurface = arp::tests::FlatTerrainSurface;

} // namespace

int main()
{
    using namespace arp::renderer;

    const auto deferredResult = RendererResult::deferred(
        "upload staging remains in flight", "ARP-RHI-STAGING-READINESS-DEFERRED");
    assert(!deferredResult.success);
    assert(deferredResult.isDeferred());
    assert(deferredResult.diagnosticId == "ARP-RHI-STAGING-READINESS-DEFERRED");
    const RendererResult terminalResult{false, "descriptor allocation failed"};
    assert(!terminalResult.isDeferred());

    RuntimeScene scene;
    const auto mesh = scene.addMesh("tree_mesh");
    const auto material = scene.addMaterial("leaf_material");
    const auto instance = scene.addInstance(RuntimeRenderableDesc{mesh, material, 0});
    assert(mesh.valid());
    assert(material.valid());
    assert(instance.valid());

    arp::assets::MeshAsset first = arp::assets::MeshAsset::debugTriangle();
    const auto registered = scene.registerMesh("runtime_mesh", &first);
    assert(scene.meshAsset(registered) == &first);
    assert(scene.mesh(registered) != nullptr);

    arp::assets::MeshAsset replacement = arp::assets::MeshAsset::debugTriangle();
    assert(scene.updateMesh(registered, &replacement));
    assert(scene.meshAsset(registered) == &replacement);
    assert(scene.unloadMesh(registered));
    assert(scene.meshAsset(registered) == nullptr);
    assert(!scene.updateMesh(registered, &first));

    const auto next = scene.registerMesh("next_runtime_mesh", &first);
    assert(next.id != registered.id);
    assert(scene.meshAsset(registered) == nullptr);

    auto terrainRuntime = std::make_shared<TerrainRuntime>();
    terrainRuntime->setTerrainSurface(std::make_shared<FlatTerrainSurface>());
    TerrainRuntime::Desc terrainDesc;
    terrainDesc.worldHalfExtent = 64.0f;
    terrainDesc.activeRadius = 1u;
    terrainDesc.maxChunkGenerationsPerUpdate = 1u;
    assert(terrainRuntime->initialize(terrainDesc));
    std::size_t generatedChunks = 0u;
    std::size_t unloadedChunks = 0u;
    terrainRuntime->setChunkGeneratedCallback(
        [&generatedChunks](const arp::pcg::ChunkGenRequest&, const arp::assets::MeshAsset& mesh) {
            assert(mesh.validForIndexedDraw());
            ++generatedChunks;
        });
    terrainRuntime->setChunkUnloadedCallback([&unloadedChunks](arp::pcg::ChunkCoord) {
        ++unloadedChunks;
    });
    const auto terrain = scene.registerTerrain(terrainRuntime);
    assert(terrain.valid());
    assert(scene.terrain(terrain) != nullptr);
    assert(terrainRuntime->update(0.0f, 0.0f, 0.0f, 1.0f, 0.016f));
    assert(terrainRuntime->visibleChunkCount() > 0u);
    assert(generatedChunks == 1u);
    assert(terrainRuntime->update(64.0f, 0.0f, 0.0f, 1.0f, 0.016f));
    assert(unloadedChunks > 0u);
    assert(scene.unloadTerrain(terrain));
    assert(scene.terrain(terrain) == nullptr);
    assert(!scene.updateTerrain(terrain, terrainRuntime));

    auto failedTerrainRuntime = std::make_shared<TerrainRuntime>();
    failedTerrainRuntime->setTerrainSurface(std::make_shared<FlatTerrainSurface>());
    assert(failedTerrainRuntime->initialize(terrainDesc, [](const arp::pcg::ChunkGenRequest&) {
        return arp::assets::MeshAsset{};
    }));
    assert(failedTerrainRuntime->update(0.0f, 0.0f, 0.0f, 1.0f, 0.016f));
    assert(failedTerrainRuntime->visibleChunkCount() == 0u);

    TerrainGpuRuntime gpuRuntime;
    const auto gpuMesh = arp::assets::MeshAsset::debugTriangle();
    std::size_t uploads = 0u;
    std::size_t updates = 0u;
    const auto upload = [&uploads](const arp::assets::MeshAsset&, MeshGpuBuffer&) {
        ++uploads;
        return true;
    };
    const auto update = [&updates](const arp::assets::MeshAsset&, MeshGpuBuffer&) {
        ++updates;
        return true;
    };
    const auto replace = [&uploads](const arp::assets::MeshAsset&, MeshGpuBuffer&) {
        ++uploads;
        return true;
    };
    const arp::pcg::ChunkCoord firstCoord{0, 0};
    assert(gpuRuntime.syncChunk(firstCoord, gpuMesh, 0, 1u, 7u, upload, update, replace));
    assert(gpuRuntime.activeSlotCount() == 1u);
    assert(uploads == 1u);
    assert(!gpuRuntime.slotMayRequireReplacementRetirement(firstCoord, 0));
    assert(gpuRuntime.slotMayRequireReplacementRetirement(firstCoord, 1));
    // A distinct mesh revision must update even when the LOD is unchanged.
    assert(gpuRuntime.syncChunk(firstCoord, gpuMesh, 0, 2u, 7u, upload, update, replace));
    assert(updates == 1u);
    assert(gpuRuntime.slot(firstCoord)->meshRevision == 2u);
    assert(gpuRuntime.syncChunk(firstCoord, gpuMesh, 1, 3u, 7u, upload, update, replace));
    assert(updates == 2u);
    gpuRuntime.retireInactive({}, 7u);
    assert(gpuRuntime.activeSlotCount() == 0u);
    assert(gpuRuntime.retiredSlotCount() == 1u);
    assert(gpuRuntime.reapCompleted(7u) == 0u);
    assert(gpuRuntime.reapCompleted(8u) == 1u);
    assert(!gpuRuntime.syncChunk(
        {1, 1}, arp::assets::MeshAsset{}, 0, 1u, 7u, upload, update, replace));
    return 0;
}
