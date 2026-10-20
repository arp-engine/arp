#pragma once

// Offline PCG-to-terrain bridge. This API intentionally produces only the
// asset build value; resource registration and runtime residency remain owned
// by the terrain/resource systems.

#include "resource/terrain_asset_resource.h"

#include <cmath>

namespace arp::pcg {

struct TerrainBakeWeights final {
    std::array<std::uint8_t, 4> pageA{};
    std::array<std::uint8_t, 4> pageB{};
};

using TerrainBakeHeightFn = bool (*)(void* context, float worldX, float worldZ, float& outHeight);
using TerrainBakeWeightFn = bool (*)(
    void* context, float worldX, float worldZ, TerrainBakeWeights& outWeights);
using TerrainBakeVegetationFn = bool (*)(void* context,
    terrain::TerrainTileCoord tile,
    core::pmr::Vector<resource::TerrainStaticVegetationInstance>& outInstances,
    core::pmr::String& diagnostics);

struct TerrainAssetBakeRequest final {
    resource::TerrainAssetBuildDesc asset;
    void* sourceContext = nullptr;
    TerrainBakeHeightFn sampleHeight = nullptr;
    TerrainBakeWeightFn sampleWeights = nullptr;
    TerrainBakeVegetationFn bakeVegetation = nullptr;
};

struct TerrainAssetBakeResult final {
    bool success = false;
    resource::TerrainAssetBuildDesc asset;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Bakes PCG output into the fixed TerrainAsset v1 data layout. No generator
/// fallback is permitted: height and weight functions are mandatory, while an
/// absent vegetation callback means the caller explicitly authored no static
/// vegetation for the asset.
inline TerrainAssetBakeResult bakeTerrainAsset(const TerrainAssetBakeRequest& request)
{
    TerrainAssetBakeResult result;
    if (request.sampleHeight == nullptr || request.sampleWeights == nullptr
        || request.asset.resourceId.empty() || !request.asset.bounds.valid()
        || request.asset.tileCountX == 0u || request.asset.tileCountZ == 0u
        || request.asset.samplesPerEdge < 2u || request.asset.tileWorldSize <= 0.0f) {
        result.diagnostics = core::pmr::String(
            "TerrainAsset bake requires explicit geometry, height, and weight contracts",
            &core::defaultCpuMemoryResource());
        return result;
    }
    const float expectedWidth =
        static_cast<float>(request.asset.tileCountX) * request.asset.tileWorldSize;
    const float expectedDepth =
        static_cast<float>(request.asset.tileCountZ) * request.asset.tileWorldSize;
    if (std::fabs((request.asset.bounds.maxX - request.asset.bounds.minX) - expectedWidth) > 1e-4f
        || std::fabs((request.asset.bounds.maxZ - request.asset.bounds.minZ) - expectedDepth)
            > 1e-4f) {
        result.diagnostics =
            core::pmr::String("TerrainAsset bake bounds must exactly cover the declared tile grid",
                &core::defaultCpuMemoryResource());
        return result;
    }

    result.asset.resourceId = request.asset.resourceId;
    result.asset.alias = request.asset.alias;
    result.asset.status = request.asset.status;
    result.asset.bounds = request.asset.bounds;
    result.asset.tileCountX = request.asset.tileCountX;
    result.asset.tileCountZ = request.asset.tileCountZ;
    result.asset.samplesPerEdge = request.asset.samplesPerEdge;
    result.asset.tileWorldSize = request.asset.tileWorldSize;
    result.asset.vegetationCatalogResourceId = request.asset.vegetationCatalogResourceId;
    result.asset.layers = request.asset.layers;
    result.asset.tiles.reserve(
        static_cast<std::size_t>(result.asset.tileCountX) * result.asset.tileCountZ);

    for (std::uint32_t tileZ = 0u; tileZ < result.asset.tileCountZ; ++tileZ) {
        for (std::uint32_t tileX = 0u; tileX < result.asset.tileCountX; ++tileX) {
            resource::TerrainAssetBuildTile tile;
            tile.coord = {static_cast<std::int32_t>(tileX), static_cast<std::int32_t>(tileZ)};
            const auto samples = result.asset.samplesPerEdge;
            const auto sampleCount = static_cast<std::size_t>(samples) * samples;
            tile.heights.reserve(sampleCount);
            tile.weightPageA.reserve(sampleCount * 4u);
            tile.weightPageB.reserve(sampleCount * 4u);
            for (std::uint32_t sampleZ = 0u; sampleZ < samples; ++sampleZ) {
                for (std::uint32_t sampleX = 0u; sampleX < samples; ++sampleX) {
                    const float worldX = result.asset.bounds.minX
                        + static_cast<float>(tileX) * result.asset.tileWorldSize
                        + static_cast<float>(sampleX) * result.asset.tileWorldSize
                            / static_cast<float>(samples - 1u);
                    const float worldZ = result.asset.bounds.minZ
                        + static_cast<float>(tileZ) * result.asset.tileWorldSize
                        + static_cast<float>(sampleZ) * result.asset.tileWorldSize
                            / static_cast<float>(samples - 1u);
                    float height = 0.0f;
                    TerrainBakeWeights weights;
                    if (!request.sampleHeight(request.sourceContext, worldX, worldZ, height)
                        || !std::isfinite(height)
                        || !request.sampleWeights(request.sourceContext, worldX, worldZ, weights)) {
                        result.diagnostics = core::pmr::String(
                            "TerrainAsset bake source failed to provide a finite height or explicit weights",
                            &core::defaultCpuMemoryResource());
                        return result;
                    }
                    tile.heights.push_back(height);
                    tile.weightPageA.insert(
                        tile.weightPageA.end(), weights.pageA.begin(), weights.pageA.end());
                    tile.weightPageB.insert(
                        tile.weightPageB.end(), weights.pageB.begin(), weights.pageB.end());
                }
            }
            if (request.bakeVegetation != nullptr
                && !request.bakeVegetation(
                    request.sourceContext, tile.coord, tile.vegetation, result.diagnostics)) {
                if (result.diagnostics.empty()) {
                    result.diagnostics =
                        core::pmr::String("TerrainAsset vegetation bake callback failed",
                            &core::defaultCpuMemoryResource());
                }
                return result;
            }
            result.asset.tiles.push_back(std::move(tile));
        }
    }
    result.success = true;
    return result;
}

} // namespace arp::pcg
