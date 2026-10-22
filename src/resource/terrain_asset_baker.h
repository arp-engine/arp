#pragma once

// Offline sampled-source terrain builder, shared by PCG and authoring tools.
// No generator is retained by the resource runtime.

#include "core/algorithm/row_major_index_grid.h"
#include "resource/terrain_asset_resource.h"

#include <cmath>

namespace arp::resource {

struct TerrainBakeWeights final {
    std::array<std::uint8_t, 4> pageA{};
    std::array<std::uint8_t, 4> pageB{};
};

using TerrainBakeHeightFn = bool (*)(void* context, float worldX, float worldZ, float& outHeight);
using TerrainBakeLatticeHeightFn = bool (*)(
    void* context, std::uint64_t x, std::uint64_t z, float& outHeight);
using TerrainBakeWeightFn = bool (*)(
    void* context, float worldX, float worldZ, TerrainBakeWeights& outWeights);
using TerrainBakeLatticeWeightFn = bool (*)(
    void* context, std::uint64_t x, std::uint64_t z, TerrainBakeWeights& outWeights);
using TerrainBakeVegetationFn = bool (*)(void* context,
    terrain::TerrainTileCoord tile,
    core::pmr::Vector<vegetation::StaticVegetationInstance>& outInstances,
    core::pmr::String& diagnostics);

struct TerrainAssetBakeRequest final {
    resource::TerrainAssetBuildDesc asset;
    void* sourceContext = nullptr;
    TerrainBakeHeightFn sampleHeight = nullptr;
    TerrainBakeLatticeHeightFn sampleLatticeHeight = nullptr;
    std::size_t maxDecodedBytes = 0u;
    TerrainBakeWeightFn sampleWeights = nullptr;
    TerrainBakeVegetationFn bakeVegetation = nullptr;
    // Appended to retain existing aggregate initialization and world callbacks.
    TerrainBakeLatticeWeightFn sampleLatticeWeights = nullptr;
};

struct TerrainAssetBakeResult final {
    bool success = false;
    resource::TerrainAssetBuildDesc asset;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

/// Bakes explicit sampled inputs into the selected v1/v2 contract. Height is
/// mandatory; weight and vegetation callbacks follow the declared presence.
/// The runtime retains no generator or implicit source fallback.
class TerrainAssetBaker final {
public:
    TerrainAssetBakeResult bake(const TerrainAssetBakeRequest& request) const
    {
        TerrainAssetBakeResult result;
        if ((request.sampleHeight == nullptr && request.sampleLatticeHeight == nullptr)
            || (request.sampleHeight != nullptr && request.sampleLatticeHeight != nullptr)
            || (request.asset.hasSurfaceWeights && request.sampleWeights == nullptr
                && request.sampleLatticeWeights == nullptr)
            || (request.sampleWeights != nullptr && request.sampleLatticeWeights != nullptr)
            || request.asset.resourceId.empty() || !request.asset.bounds.valid()
            || request.asset.tileCountX == 0u || request.asset.tileCountZ == 0u
            || request.asset.samplesPerEdge < 2u || request.asset.tileWorldSize <= 0.0f
            || !std::isfinite(request.asset.tileWorldSize)
            || !std::isfinite(request.asset.bounds.minX)
            || !std::isfinite(request.asset.bounds.maxX)
            || !std::isfinite(request.asset.bounds.minZ)
            || !std::isfinite(request.asset.bounds.maxZ)) {
            result.diagnostics = core::pmr::String(
                "TerrainAsset bake requires explicit geometry, height, and weight contracts",
                &core::defaultCpuMemoryResource());
            return result;
        }
        const core::RowMajorIndexGrid tiles(request.asset.tileCountZ, request.asset.tileCountX);
        const core::RowMajorIndexGrid samples(
            request.asset.samplesPerEdge, request.asset.samplesPerEdge);
        const auto bytesPerSample = request.asset.hasSurfaceWeights ? 12u : 4u;
        if (!tiles.valid() || !samples.valid() || request.asset.tileCountX > INT32_MAX
            || request.asset.tileCountZ > INT32_MAX
            || tiles.size()
                > std::numeric_limits<std::size_t>::max() / bytesPerSample / samples.size()
            || (request.maxDecodedBytes
                && tiles.size() * samples.size() * bytesPerSample > request.maxDecodedBytes)
            || (!request.asset.hasVegetation && request.bakeVegetation != nullptr)) {
            result.diagnostics = "TERRAIN-BAKE-BUDGET-OR-PRESENCE";
            return result;
        }
        const float expectedWidth =
            static_cast<float>(request.asset.tileCountX) * request.asset.tileWorldSize;
        const float expectedDepth =
            static_cast<float>(request.asset.tileCountZ) * request.asset.tileWorldSize;
        if (!std::isfinite(expectedWidth) || !std::isfinite(expectedDepth)
            || request.asset.bounds.maxX != request.asset.bounds.minX + expectedWidth
            || request.asset.bounds.maxZ != request.asset.bounds.minZ + expectedDepth) {
            result.diagnostics = core::pmr::String(
                "TerrainAsset bake bounds must exactly cover the declared tile grid",
                &core::defaultCpuMemoryResource());
            return result;
        }

        result.asset.version = request.asset.version;
        result.asset.hasSurfaceWeights = request.asset.hasSurfaceWeights;
        result.asset.hasVegetation = request.asset.hasVegetation;
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
                if (result.asset.hasSurfaceWeights) {
                    tile.weightPageA.reserve(sampleCount * 4u);
                    tile.weightPageB.reserve(sampleCount * 4u);
                }
                for (std::uint32_t sampleZ = 0u; sampleZ < samples; ++sampleZ) {
                    for (std::uint32_t sampleX = 0u; sampleX < samples; ++sampleX) {
                        const auto latticeX =
                            static_cast<std::uint64_t>(tileX) * (samples - 1u) + sampleX;
                        const auto latticeZ =
                            static_cast<std::uint64_t>(tileZ) * (samples - 1u) + sampleZ;
                        const float worldX = result.asset.bounds.minX
                            + static_cast<float>(latticeX) * result.asset.tileWorldSize
                                / static_cast<float>(samples - 1u);
                        const float worldZ = result.asset.bounds.minZ
                            + static_cast<float>(latticeZ) * result.asset.tileWorldSize
                                / static_cast<float>(samples - 1u);
                        float height = 0.0f;
                        TerrainBakeWeights weights;
                        const bool sampled = request.sampleLatticeHeight
                            ? request.sampleLatticeHeight(
                                  request.sourceContext, latticeX, latticeZ, height)
                            : request.sampleHeight(request.sourceContext, worldX, worldZ, height);
                        const bool weightsSampled = !result.asset.hasSurfaceWeights
                            || (request.sampleLatticeWeights
                                    ? request.sampleLatticeWeights(
                                          request.sourceContext, latticeX, latticeZ, weights)
                                    : request.sampleWeights(
                                          request.sourceContext, worldX, worldZ, weights));
                        if (!sampled || !std::isfinite(height) || !weightsSampled) {
                            result.diagnostics = core::pmr::String(
                                "TerrainAsset bake source failed to provide a finite height or explicit weights",
                                &core::defaultCpuMemoryResource());
                            return result;
                        }
                        tile.heights.push_back(height);
                        if (result.asset.hasSurfaceWeights) {
                            tile.weightPageA.insert(
                                tile.weightPageA.end(), weights.pageA.begin(), weights.pageA.end());
                            tile.weightPageB.insert(
                                tile.weightPageB.end(), weights.pageB.begin(), weights.pageB.end());
                        }
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
};

} // namespace arp::resource
