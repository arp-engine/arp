#pragma once

#include "renderer/terrain_vegetation_draw_preparation.h"

#include <limits>

namespace arp::tests {
using namespace arp;
inline constexpr const char* vegetationDrawTerrainId = "3f6fbcca-17d2-46a6-9817-2af357399301";
inline constexpr const char* vegetationDrawCatalogId = "3ea4c197-d012-4e64-8f5f-6a415ec2c109";

inline std::shared_ptr<renderer::TerrainVegetationSnapshot> terrainVegetationDrawSourceFixture()
{
    auto source = core::makeShared<renderer::TerrainVegetationSnapshot>();
    source->terrainResourceId = vegetationDrawTerrainId;
    source->catalogResourceId = vegetationDrawCatalogId;
    source->terrainResourceGeneration = 11u;
    source->catalogResourceGeneration = 17u;
    source->terrainGeneration = 23u;
    source->translation = {100, 5, -200};
    auto catalog = core::makeShared<resource::VegetationCatalogResourceData>();
    for (const auto id : {1u, 2u}) {
        resource::VegetationCatalogAssetEntry asset;
        asset.id = id == 1u ? "grass-a" : "tree-b";
        asset.templateId = static_cast<std::uint16_t>(id);
        catalog->templateIdsByAssetId.emplace(asset.id, asset.templateId);
        catalog->assets.push_back(std::move(asset));
        vegetation::VegetationTemplate geometry;
        geometry.id = static_cast<std::uint16_t>(id);
        catalog->templates.push_back(std::move(geometry));
    }
    source->catalog = std::move(catalog);
    renderer::TerrainVegetationSnapshot::Tile tile;
    tile.coord = {0, 0};
    vegetation::StaticVegetationInstance instance;
    instance.prototypeId = 2u;
    instance.position = {-17, 8, 31};
    instance.scale = {2, 3, 2};
    const auto q = core::quatMul(core::quatMul(core::quatFromAxisAngle(0, 1, 0, 0.73f),
                                     core::quatFromAxisAngle(1, 0, 0, -0.31f)),
        core::quatFromAxisAngle(0, 0, 1, 0.17f));
    instance.rotation = {q.x, q.y, q.z, q.w};
    instance.variationSeed = 91u;
    tile.instances.push_back(instance);
    instance.prototypeId = 1u;
    instance.position = {103, -4, -205};
    instance.variationSeed = 0u;
    tile.instances.push_back(instance);
    instance.prototypeId = 2u;
    instance.position = {-11, 9, 36};
    instance.variationSeed = std::numeric_limits<std::uint32_t>::max();
    tile.instances.push_back(instance);
    source->tiles.push_back(std::move(tile));
    tile = {};
    tile.coord = {1, 0};
    source->tiles.push_back(std::move(tile));
    tile = {};
    tile.coord = {0, 1};
    instance.position = {111, 7, -198};
    tile.instances.push_back(instance);
    source->tiles.push_back(std::move(tile));
    source->instanceCount = 4u;
    return source;
}

} // namespace arp::tests
