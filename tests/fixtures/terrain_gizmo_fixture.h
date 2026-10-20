#pragma once

#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"

#include <filesystem>
#include <utility>

namespace arp::tests {

inline constexpr const char* terrainGizmoId = "8a1d6c41-2dd4-4dd5-a77e-03f1c02e7d10";

class TerrainGizmoCatalogFixture final : public resource::VegetationCatalogResource {
public:
    TerrainGizmoCatalogFixture()
        : resource::VegetationCatalogResource([] {
              resource::VegetationCatalogResourceLoadDesc desc;
              desc.resourceId = core::pmr::String(
                  "8a1d6c41-2dd4-4dd5-a77e-03f1c02e7d11", &core::defaultCpuMemoryResource());
              desc.alias = "editor/terrain-gizmo-catalog";
              desc.status = "Contract Complete";
              return desc;
          }())
    {}

    resource::ResourceLoadResult load(
        resource::ResourceManager& manager, const resource::ResourceLoadContext& context) override
    {
        data = core::makeShared<resource::VegetationCatalogResourceData>();
        return resource::ResourceBase::load(manager, context);
    }
};

inline bool loadTerrainGizmoFixture(
    resource::ResourceManager& manager, const std::filesystem::path& directory)
{
    constexpr const char* catalogId = "8a1d6c41-2dd4-4dd5-a77e-03f1c02e7d11";
    constexpr const char* materialId = "8a1d6c41-2dd4-4dd5-a77e-03f1c02e7d12";
    if (!manager.declareResource(materialId, "MaterialResource").success
        || !manager.load<TerrainGizmoCatalogFixture>(resource::ResourceLoadContext{}).success)
        return false;

    resource::TerrainAssetBuildDesc desc;
    desc.resourceId = terrainGizmoId;
    desc.alias = "editor/terrain-gizmo";
    desc.status = "Contract Complete";
    desc.bounds = {-2.0f, 2.0f, -1.0f, 3.0f};
    desc.tileCountX = 1u;
    desc.tileCountZ = 1u;
    desc.samplesPerEdge = 2u;
    desc.tileWorldSize = 4.0f;
    desc.vegetationCatalogResourceId = catalogId;
    constexpr const char* layerIds[] = {
        "base0", "base1", "base2", "base3", "base4", "base5", "overlay0", "overlay1"};
    for (std::uint32_t index = 0u; index < desc.layers.size(); ++index) {
        desc.layers[index].layerId = layerIds[index];
        desc.layers[index].semantic = layerIds[index];
        desc.layers[index].materialResourceId = materialId;
        desc.layers[index].page = index < 4u ? 0u : 1u;
        desc.layers[index].channel = index % 4u;
    }
    resource::TerrainAssetBuildTile tile;
    tile.coord = {0, 0};
    tile.heights = {0.0f, 0.25f, 0.5f, 1.0f};
    tile.weightPageA.resize(16u, 0u);
    tile.weightPageB.resize(16u, 0u);
    desc.tiles.push_back(std::move(tile));

    const auto written = resource::writeTerrainAsset(desc, directory);
    if (!written.success)
        return false;
    resource::ResourceReference reference;
    reference.id = terrainGizmoId;
    reference.type = "TerrainAssetResource";
    reference.status = "Contract Complete";
    return resource::loadTerrainAssetResource(
        manager, std::move(reference), directory / "terrain.asset.yaml")
        .success;
}

} // namespace arp::tests
