#pragma once

#include "renderer/terrain_vegetation_gpu_upload.h"
#include "tests/fixtures/terrain_vegetation_draw.h"

#include <cassert>

namespace arp::tests {

inline renderer::TerrainVegetationGpuUpload::Desc terrainVegetationUploadDescFixture()
{
    renderer::TerrainVegetationGpuUpload::Desc desc;
    desc.maxTiles = 3u;
    desc.maxInstances = 4u;
    desc.maxPrototypes = 3u;
    desc.maxCopyBytes = 64u;
    desc.maxCopyCommands = 4u;
    desc.maxCpuBytes = 16u * 1024u * 1024u;
    desc.liveGenerations = core::makeShared<core::AtomicCapacityBudget>(2u);
    desc.liveInstances = core::makeShared<core::AtomicCapacityBudget>(8u);
    return desc;
}

inline std::shared_ptr<const renderer::TerrainVegetationDrawSnapshot> terrainVegetationDrawFixture()
{
    renderer::TerrainVegetationDrawPreparation preparation;
    assert(preparation.prepare(terrainVegetationDrawSourceFixture(), {3u, 4u}).success);
    return preparation.snapshot();
}

inline void checkTerrainVegetationGpuSnapshot(const renderer::TerrainVegetationGpuSnapshot& gpu,
    const std::shared_ptr<const renderer::TerrainVegetationDrawSnapshot>& source)
{
    static_assert(renderer::TerrainVegetationGpuSnapshot::instanceStride == 64u);
    static_assert(renderer::TerrainVegetationGpuSnapshot::positionSpace
        == renderer::TerrainVegetationDrawSnapshot::PositionSpace::World);
    assert(gpu.source() == source);
    assert(gpu.tiles().size() == source->tiles.size());
    for (std::size_t i = 0u; i < gpu.tiles().size(); ++i) {
        const auto& tile = gpu.tiles()[i];
        const auto& original = source->tiles[i];
        assert(
            tile.coord == original.coord && tile.prototypes.size() == original.prototypes.size());
        for (std::size_t j = 0u; j < tile.prototypes.size(); ++j) {
            const auto& prototype = tile.prototypes[j];
            assert(prototype.prototypeId == original.prototypes[j].prototypeId);
            assert(prototype.instanceCount == original.prototypes[j].instances.size());
            assert(prototype.buffer && prototype.buffer->hostWritesSealed()
                && prototype.buffer->gpuWritesSealed());
            assert(prototype.buffer->desc().sizeBytes == prototype.instanceCount * 64u);
        }
    }
}

} // namespace arp::tests
