#pragma once
// Offline PCG producer bridge. Canonical sampled-source baking belongs to the
// resource tooling layer and remains usable with ARP_ENABLE_PCG=OFF.
#include "resource/terrain_asset_baker.h"

namespace arp::pcg {
using TerrainBakeWeights = resource::TerrainBakeWeights;
using TerrainBakeHeightFn = resource::TerrainBakeHeightFn;
using TerrainBakeWeightFn = resource::TerrainBakeWeightFn;
using TerrainBakeVegetationFn = resource::TerrainBakeVegetationFn;
using TerrainAssetBakeRequest = resource::TerrainAssetBakeRequest;
using TerrainAssetBakeResult = resource::TerrainAssetBakeResult;

inline TerrainAssetBakeResult bakeTerrainAsset(const TerrainAssetBakeRequest& request)
{
    return resource::TerrainAssetBaker().bake(request);
}
} // namespace arp::pcg
