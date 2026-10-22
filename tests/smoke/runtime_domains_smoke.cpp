#include "3d/terrain/terrain_runtime.h"
#include "3d/vegetation/template.h"
#include "3d/vegetation/wind_field_system.h"
#include "tests/fixtures/flat_terrain_surface.h"

#include <cassert>
#include <chrono>
#include <cstdio>

int main()
{
    arp::terrain::TerrainRuntime runtime;
    runtime.setTerrainSurface(arp::core::makeShared<arp::tests::FlatTerrainSurface>());
    arp::terrain::TerrainRuntime::Desc desc;
    desc.worldBounds = {-128.0f, 128.0f, -128.0f, 128.0f};
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 9u;
    desc.activeRadius = 1u;
    desc.maxChunkGenerationsPerUpdate = 16u;
    assert(runtime.initialize(desc));
    for (float camera : {-96.0f, 0.0f, 96.0f, 0.0f, -96.0f}) {
        assert(runtime.update(camera, 0.0f, 0.0f, 1.0f, 1.0f / 60.0f));
        assert(runtime.visibleChunkCount() > 0u);
        for (const auto& [coord, chunk] : runtime.streamer().activeChunks()) {
            if (!chunk.genPending)
                assert(chunk.mesh.validForIndexedDraw());
        }
    }
    arp::vegetation::InstanceData instance;
    instance.x = -96.0f;
    instance.z = 0.0f;
    instance.y = 9.0f;
    assert(instance.anchorToSurface(*runtime.terrainSurface())
        == arp::terrain::TerrainSurfaceQueryStatus::Ready);
    assert(instance.y == 0.0f);
    instance.speciesId = 7u;
    instance.geometryLod = 2u;
    const auto gpu = instance.toGpu();
    assert(arp::vegetation::instanceSpeciesId(gpu) == 7u);
    assert(arp::vegetation::instanceGeometryLod(gpu) == 2u);
    arp::vegetation::WindFieldSystem wind;
    wind.update(1.0f / 60.0f);
    assert(wind.buildUbo().timeGustTurb[0] > 0.0f);
    runtime.shutdown();
    assert(!runtime.initialized() && runtime.streamer().activeChunks().empty());
    assert(runtime.initialize(desc));
    assert(runtime.update(0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 60.0f));
    assert(runtime.visibleChunkCount() > 0u);
    // Finish the generation-to-resident transition before measuring residency.
    assert(runtime.update(0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 60.0f));
    assert(runtime.streamer().genRequests().empty());
    // Measure a fixed resident scene separately from chunk churn. These are
    // CPU-domain diagnostics, not GPU or platform performance claims.
    constexpr unsigned measuredFrames = 600u;
    const auto memoryBefore = arp::core::defaultCpuAllocator().stats();
    const auto begin = std::chrono::steady_clock::now();
    for (unsigned frame = 0u; frame < measuredFrames; ++frame) {
        assert(runtime.update(0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 60.0f));
        wind.update(1.0f / 60.0f);
        assert(instance.anchorToSurface(*runtime.terrainSurface())
            == arp::terrain::TerrainSurfaceQueryStatus::Ready);
    }
    const auto elapsed =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    const auto memoryAfter = arp::core::defaultCpuAllocator().stats();
    std::printf(
        "resident_frames=%u cpu_us_per_frame=%.3f allocations=%llu live_before=%llu live_after=%llu peak_bytes=%llu\n",
        measuredFrames,
        elapsed / measuredFrames,
        static_cast<unsigned long long>(memoryAfter.allocationCount - memoryBefore.allocationCount),
        static_cast<unsigned long long>(memoryBefore.liveBytes),
        static_cast<unsigned long long>(memoryAfter.liveBytes),
        static_cast<unsigned long long>(memoryAfter.peakBytes));
    std::fflush(stdout);
    assert(memoryAfter.liveBytes == memoryBefore.liveBytes);
    assert(memoryAfter.allocationCount == memoryBefore.allocationCount);
    runtime.shutdown();
    std::puts(
        "Terrain/vegetation/LOD runtime: camera, residency, instances, wind, reload passed without PCG");
}
