#include "pcg/terrain_asset_baker.h"

#include <cassert>
#include <string>

namespace {

bool sampleHeight(void*, float x, float z, float& out)
{
    out = x + z;
    return true;
}

bool sampleWeights(void*, float, float, arp::pcg::TerrainBakeWeights& out)
{
    out.pageA[0] = 255u;
    out.pageB[2] = 16u;
    return true;
}

bool sampleLatticeWeights(
    void*, std::uint64_t x, std::uint64_t z, arp::resource::TerrainBakeWeights& out)
{
    if (x >= 2u || z >= 2u)
        return false;
    out.pageA = {static_cast<std::uint8_t>(x + 2u * z), 0u, 0u, 0u};
    out.pageB = {0u, 0u, 0u, static_cast<std::uint8_t>(11u + x + 2u * z)};
    return true;
}

bool bakeVegetation(void*,
    arp::terrain::TerrainTileCoord coord,
    arp::core::pmr::Vector<arp::resource::TerrainStaticVegetationInstance>& out,
    arp::core::pmr::String&)
{
    arp::resource::TerrainStaticVegetationInstance instance;
    instance.prototypeId = 7u;
    instance.position = {
        static_cast<float>(coord.x) + 0.5f, 1.0f, static_cast<float>(coord.z) + 0.5f};
    out.push_back(instance);
    return true;
}

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

} // namespace

int main()
{
    arp::pcg::TerrainAssetBakeRequest request;
    request.asset.resourceId = text("a86e8edb-5b06-48aa-a5dc-c05305704f89");
    request.asset.alias = text("terrain/baked-test");
    request.asset.bounds = {0.0f, 2.0f, 0.0f, 2.0f};
    request.asset.tileCountX = 1u;
    request.asset.tileCountZ = 1u;
    request.asset.samplesPerEdge = 2u;
    request.asset.tileWorldSize = 2.0f;
    request.asset.vegetationCatalogResourceId = text("9d59cdea-e154-4fde-9c1a-ff6c88b26e9d");
    for (std::uint32_t index = 0u; index < request.asset.layers.size(); ++index) {
        request.asset.layers[index].layerId = text(("layer" + std::to_string(index)).c_str());
        request.asset.layers[index].semantic = text(("surface" + std::to_string(index)).c_str());
        request.asset.layers[index].materialResourceId =
            text("c34a60a8-1780-4aaa-bbe4-9c055d3907de");
        request.asset.layers[index].page = index < 4u ? 0u : 1u;
        request.asset.layers[index].channel = index < 4u ? index : index - 4u;
    }
    request.sampleHeight = sampleHeight;
    request.sampleWeights = sampleWeights;
    request.bakeVegetation = bakeVegetation;
    const auto baked = arp::pcg::bakeTerrainAsset(request);
    assert(baked.success);
    assert(baked.asset.tiles.size() == 1u);
    assert(baked.asset.tiles.front().heights[3] == 4.0f);
    assert(baked.asset.tiles.front().weightPageA[0] == 255u);
    assert(baked.asset.tiles.front().vegetation.size() == 1u);

    const auto bakedAgain = arp::pcg::bakeTerrainAsset(request);
    assert(bakedAgain.success);
    assert(bakedAgain.asset.tiles.size() == baked.asset.tiles.size());
    assert(bakedAgain.asset.tiles.front().heights == baked.asset.tiles.front().heights);
    assert(bakedAgain.asset.tiles.front().weightPageA == baked.asset.tiles.front().weightPageA);
    assert(bakedAgain.asset.tiles.front().weightPageB == baked.asset.tiles.front().weightPageB);
    assert(bakedAgain.asset.tiles.front().vegetation.front().prototypeId
        == baked.asset.tiles.front().vegetation.front().prototypeId);
    assert(bakedAgain.asset.tiles.front().vegetation.front().variationSeed
        == baked.asset.tiles.front().vegetation.front().variationSeed);

    request.sampleWeights = nullptr;
    assert(!arp::pcg::bakeTerrainAsset(request).success);
    request.sampleLatticeWeights = sampleLatticeWeights;
    request.asset.bounds = {100.0f, 102.0f, -200.0f, -198.0f};
    const auto lattice = arp::pcg::bakeTerrainAsset(request);
    assert(lattice.success && lattice.asset.tiles[0].weightPageA[12] == 3u
        && lattice.asset.tiles[0].weightPageB[15] == 14u);
    // Selecting both coordinate contracts is ambiguous and must fail.
    request.sampleWeights = sampleWeights;
    assert(!arp::pcg::bakeTerrainAsset(request).success);
    request.sampleWeights = nullptr;
    request.sampleLatticeWeights =
        [](void*, std::uint64_t, std::uint64_t, arp::resource::TerrainBakeWeights&) {
            return false;
        };
    assert(!arp::pcg::bakeTerrainAsset(request).success);
    return 0;
}
