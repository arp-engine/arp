#pragma once

#include "renderer/terrain_vegetation_draw_preparation.h"

namespace arp::renderer {

struct TerrainVegetationGeometrySnapshot final {
    struct Section final {
        std::uint32_t firstIndex = 0u, indexCount = 0u, materialSlot = 0u;
        std::uint32_t materialBindingIndex = 0u;
    };

    struct Prototype final {
        std::uint32_t prototypeId = 0u;
        std::shared_ptr<const vegetation::VegetationTemplate> geometry;
        std::shared_ptr<const resource::VegetationCatalogAssetEntry> asset;
        core::pmr::Vector<Section> sections{&core::defaultCpuMemoryResource()};
    };

    std::shared_ptr<const TerrainVegetationDrawSnapshot> instances;
    core::pmr::Vector<Prototype> prototypes{&core::defaultCpuMemoryResource()};
};

// TerrainVegetationGeometryPreparation/1: cold CPU mapping, no GPU authority.
class ARP_API TerrainVegetationGeometryPreparation final {
public:
    struct Desc final {
        std::uint32_t contractVersion = 1u;
        std::size_t maxTiles = 0u, maxInstances = 0u, maxCatalogEntries = 0u;
        std::size_t maxPrototypes = 0u, maxVertices = 0u, maxIndices = 0u, maxSections = 0u;
        std::uint64_t maxCpuBytes = 0u;
    };

    struct Result final {
        bool success = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    // Every call revalidates budgets. Failure retains the old complete mapping.
    Result prepare(std::shared_ptr<const TerrainVegetationSnapshot>, const Desc&);

    const auto& snapshot() const noexcept { return snapshot_; }

    void clear() noexcept { snapshot_.reset(); }

private:
    std::shared_ptr<const TerrainVegetationGeometrySnapshot> snapshot_;
};

} // namespace arp::renderer
