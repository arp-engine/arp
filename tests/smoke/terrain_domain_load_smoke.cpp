#include "3d/terrain/terrain_runtime.h"
#include "3d/vegetation/instance.h"
#include "resource/mesh_asset_resources.h"
#include "tests/fixtures/terrain_domain_asset.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

int main(int argc, char** argv)
{
    assert(argc == 2);
    arp::resource::ResourceManager resources;
    assert(
        resources.load<arp::tests::EmptyTerrainCatalogFixture>(arp::resource::ResourceLoadContext{})
            .success);
    assert(resources.declareResource("33c5dcdf-4f06-482a-94eb-26baf7c937fb", "MaterialResource")
            .success);
    const auto loaded = arp::resource::loadTerrainAssetResource(resources,
        arp::tests::TerrainDomainAssetFixture::reference(),
        std::filesystem::path(argv[1]) / "terrain.asset.yaml");
    if (!loaded.success)
        std::fprintf(stderr, "%s\n", loaded.diagnostics.c_str());
    assert(loaded.success);
    const auto resource = resources.find<arp::resource::TerrainAssetResource>(
        arp::tests::TerrainDomainAssetFixture::resourceId);
    assert(resource.ready());
    const auto surface = resource.get()->snapshotSurface();
    assert(surface && surface->sample(0.0f, 0.0f).ready());
    assert(std::fabs(surface->sample(16.0f, 16.0f).sample.height - 0.48f) < 0.00001f);
    arp::terrain::TerrainRuntime runtime;
    runtime.setTerrainSurface(surface);
    arp::terrain::TerrainRuntime::Desc desc;
    desc.worldBounds = surface->bounds();
    desc.chunkSize = 64.0f;
    desc.samplesPerChunk = 9u;
    desc.activeRadius = 2u;
    desc.maxChunkGenerationsPerUpdate = 4u;
    assert(runtime.initialize(desc));
    assert(runtime.update(0.0f, 0.0f, 0.0f, 1.0f, 1.0f / 60.0f));
    std::fprintf(stderr,
        "Baked runtime visible=%zu active=%zu requests=%zu\n",
        runtime.visibleChunkCount(),
        runtime.streamer().activeChunks().size(),
        runtime.streamer().genRequests().size());
    assert(runtime.visibleChunkCount() == 4u);
    arp::vegetation::InstanceData instance;
    instance.x = 16.0f;
    instance.z = 16.0f;
    assert(instance.anchorToSurface(*surface) == arp::terrain::TerrainSurfaceQueryStatus::Ready);
    assert(std::fabs(instance.y - 0.48f) < 0.00001f);
    runtime.shutdown();
    std::puts("Baked terrain loaded, queried, streamed and anchored without PCG");
}
