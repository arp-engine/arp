#include "pcg/terrain_asset_baker.h"
#include "tests/fixtures/terrain_domain_asset.h"

#include <cassert>
#include <cstdio>
#include <filesystem>

namespace {
bool height(void*, float x, float z, float& out)
{
    out = 0.01f * x + 0.02f * z;
    return true;
}

bool weights(void*, float, float, arp::pcg::TerrainBakeWeights& out)
{
    out.pageA = {255u, 0u, 0u, 0u};
    out.pageB = {0u, 0u, 0u, 0u};
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    arp::pcg::TerrainAssetBakeRequest request;
    request.asset = arp::tests::TerrainDomainAssetFixture::description();
    request.sampleHeight = height;
    request.sampleWeights = weights;
    const auto baked = arp::pcg::bakeTerrainAsset(request);
    const auto repeated = arp::pcg::bakeTerrainAsset(request);
    assert(baked.success && repeated.success);
    assert(baked.asset.tiles.size() == 4u);
    for (std::size_t index = 0; index < baked.asset.tiles.size(); ++index) {
        assert(baked.asset.tiles[index].heights == repeated.asset.tiles[index].heights);
        assert(baked.asset.tiles[index].weightPageA == repeated.asset.tiles[index].weightPageA);
    }
    const auto written =
        arp::resource::writeTerrainAsset(baked.asset, std::filesystem::path(argv[1]));
    if (!written.success)
        std::fprintf(stderr, "%s\n", written.diagnostics.c_str());
    assert(written.success);
}
