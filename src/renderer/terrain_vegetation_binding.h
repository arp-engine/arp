#pragma once

#include "resource/terrain_asset_resource.h"
#include "resource/vegetation_catalog_resource.h"

namespace arp::renderer {

// CPU world placements. The world-space upload retains this exact source.
struct TerrainVegetationSnapshot final {
    struct Tile final {
        terrain::TerrainTileCoord coord;
        core::pmr::Vector<vegetation::StaticVegetationInstance> instances{
            &core::defaultCpuMemoryResource()};
    };

    core::pmr::String terrainResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String catalogResourceId{&core::defaultCpuMemoryResource()};
    resource::ResourceGeneration terrainResourceGeneration = 0u, catalogResourceGeneration = 0u;
    std::uint64_t terrainGeneration = 0u;
    core::Vec3 translation;
    std::size_t instanceCount = 0u;
    std::shared_ptr<const resource::VegetationCatalogResourceData> catalog;
    core::pmr::Vector<Tile> tiles{&core::defaultCpuMemoryResource()};

    ARP_API bool equivalent(const TerrainVegetationSnapshot&) const noexcept;
};

class ARP_API TerrainVegetationBinding final {
public:
    struct Desc final {
        std::size_t maxTiles = 0u;
        std::size_t maxInstances = 0u;
    };

    struct Result final {
        bool success = false;
        bool unchanged = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    Result bind(const resource::ResourceManager& resources,
        resource::NameView terrainResourceId,
        resource::ResourceGeneration expectedGeneration,
        core::Vec3 translation,
        const Desc& desc);

    std::shared_ptr<const TerrainVegetationSnapshot> snapshot() const noexcept { return snapshot_; }

    void clear() noexcept { snapshot_.reset(); }

    // Normalize a validated candidate to the exact immutable GPU source.
    bool retainEquivalentSnapshot(std::shared_ptr<const TerrainVegetationSnapshot> source) noexcept;

private:
    std::shared_ptr<const TerrainVegetationSnapshot> snapshot_;
};
} // namespace arp::renderer
