#pragma once

#include "renderer/terrain_vegetation_geometry_preparation.h"
#include "tests/fixtures/terrain_vegetation_draw.h"

namespace arp::tests {

inline constexpr const char* vegetationGeometryMaterialId = "33c5dcdf-4f06-482a-94eb-26baf7c937fb";
inline constexpr const char* vegetationGeometrySecondMaterialId =
    "33c5dcdf-4f06-482a-94eb-26baf7c937fc";

inline std::shared_ptr<renderer::TerrainVegetationSnapshot> terrainVegetationGeometrySourceFixture()
{
    auto source = terrainVegetationDrawSourceFixture();
    auto catalog = core::makeShared<resource::VegetationCatalogResourceData>(*source->catalog);
    catalog->contractVersion = 1u;
    for (std::size_t i = 0u; i < catalog->templates.size(); ++i) {
        auto& geometry = catalog->templates[i];
        geometry.vertices.resize(4u);
        geometry.vertices[1].position[0] = 1.25f;
        geometry.vertices[2].position[1] = 2.5f;
        geometry.vertices[3].position[2] = -0.75f;
        for (auto& vertex : geometry.vertices) {
            vertex.primaryBend = 0.25f;
            vertex.flutter = 0.5f;
            vertex.stiffness = 0.75f;
        }
        geometry.indices = {0, 1, 2, 2, 3, 0};
        auto& asset = catalog->assets[i];
        asset.materials.push_back({7u, vegetationGeometryMaterialId});
        if (i == 0u) {
            geometry.sections.push_back({0u, 6u, 7u});
        } else {
            geometry.sections = {{0u, 3u, 7u}, {3u, 3u, 23u}};
            asset.materials.push_back({23u, vegetationGeometrySecondMaterialId});
        }
    }
    source->catalog = std::move(catalog);
    return source;
}

inline renderer::TerrainVegetationGeometryPreparation::Desc terrainVegetationGeometryDescFixture()
{
    renderer::TerrainVegetationGeometryPreparation::Desc desc;
    desc.maxTiles = 3u;
    desc.maxInstances = 4u;
    desc.maxCatalogEntries = 2u;
    desc.maxPrototypes = 2u;
    desc.maxVertices = 8u;
    desc.maxIndices = 12u;
    desc.maxSections = 3u;
    desc.maxCpuBytes = 1024u * 1024u;
    return desc;
}

} // namespace arp::tests
