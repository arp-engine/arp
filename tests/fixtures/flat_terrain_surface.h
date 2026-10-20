#pragma once

#include "terrain/terrain_surface.h"

namespace arp::tests {

// Explicit flat test input. Production terrain never substitutes this surface.
class FlatTerrainSurface final : public terrain::TerrainSurface {
public:
    terrain::TerrainBounds bounds() const noexcept override
    {
        return {-128.0f, 128.0f, -128.0f, 128.0f};
    }

    std::uint64_t generation() const noexcept override { return 1u; }

    terrain::TerrainSurfaceQuery sample(float worldX, float worldZ) const override
    {
        terrain::TerrainSurfaceQuery query;
        query.status = bounds().contains(worldX, worldZ)
            ? terrain::TerrainSurfaceQueryStatus::Ready
            : terrain::TerrainSurfaceQueryStatus::OutOfBounds;
        return query;
    }
};

} // namespace arp::tests
