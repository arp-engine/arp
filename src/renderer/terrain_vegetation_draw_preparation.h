#pragma once

#include "3d/vegetation/instance_layout.h"
#include "renderer/terrain_vegetation_binding.h"

namespace arp::renderer {

// CPU preparation only; this type is not a submitted GPU page or upload token.
struct TerrainVegetationDrawSnapshot final {
    enum class PositionSpace { World };
    static constexpr PositionSpace positionSpace = PositionSpace::World;

    struct Prototype final {
        std::uint32_t prototypeId = 0u;
        core::pmr::Vector<vegetation::InstanceDataGpu> instances{&core::defaultCpuMemoryResource()};
    };

    struct Tile final {
        terrain::TerrainTileCoord coord;
        core::pmr::Vector<Prototype> prototypes{&core::defaultCpuMemoryResource()};
    };

    // Exact immutable provenance, including separate resource/content epochs.
    std::shared_ptr<const TerrainVegetationSnapshot> source;
    core::pmr::Vector<Tile> tiles{&core::defaultCpuMemoryResource()};
};

class ARP_API TerrainVegetationDrawPreparation final {
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

    // Consumes world records as-is. Failure retains the previous snapshot;
    // the caller must gate new work on the returned success flag.
    Result prepare(std::shared_ptr<const TerrainVegetationSnapshot> source, const Desc& desc);

    std::shared_ptr<const TerrainVegetationDrawSnapshot> snapshot() const noexcept
    {
        return snapshot_;
    }

    void clear() noexcept { snapshot_.reset(); }

private:
    std::shared_ptr<const TerrainVegetationDrawSnapshot> snapshot_;
};

} // namespace arp::renderer
