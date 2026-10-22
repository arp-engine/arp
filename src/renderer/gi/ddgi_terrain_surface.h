#pragma once

#include "3d/terrain/terrain_surface.h"
#include "core/memory/cpu_allocator.h"

#include <cmath>
#include <cstdint>
#include <memory>

namespace arp::renderer {

/// Explicit DDGI input derived from the immutable baked TerrainSurface. DDGI
/// never receives a procedural generator: callers provide the exact grid
/// resolution intended for the R32_SFLOAT GPU copy.
struct DdgiTerrainSurfaceInput final {
    std::shared_ptr<const terrain::TerrainSurface> surface;
    std::uint32_t samplesX = 0u;
    std::uint32_t samplesZ = 0u;

    bool valid() const noexcept
    {
        return surface != nullptr && surface->bounds().valid() && samplesX >= 2u && samplesZ >= 2u;
    }
};

/// Fully materialized baked terrain payload.  This is the only DDGI terrain
/// representation permitted past the TerrainSurface boundary: it contains no
/// generator state and the row-major samples are the exact bytes uploaded to
/// the R32_SFLOAT height texture.
struct DdgiBakedTerrainGrid final {
    terrain::TerrainBounds bounds{};
    std::uint32_t samplesX = 0u;
    std::uint32_t samplesZ = 0u;
    core::pmr::Vector<float> heights{&core::defaultCpuMemoryResource()};

    bool valid() const noexcept
    {
        const auto expected = static_cast<std::size_t>(samplesX) * samplesZ;
        return bounds.valid() && samplesX >= 2u && samplesZ >= 2u && heights.size() == expected;
    }
};

/// Materializes the immutable surface into row-major R32_SFLOAT samples.
/// A missing or invalid sample fails the whole build; no height is clamped or
/// fabricated for DDGI's hierarchy or upload path.
inline bool buildDdgiTerrainSurfaceGrid(const DdgiTerrainSurfaceInput& input,
    core::pmr::Vector<float>& outHeights,
    core::pmr::String& diagnostics)
{
    outHeights.clear();
    diagnostics.clear();
    if (!input.valid()) {
        diagnostics = "DDGI-TERRAIN-SURFACE-INPUT-INVALID";
        return false;
    }
    const auto bounds = input.surface->bounds();
    const auto total = static_cast<std::size_t>(input.samplesX) * input.samplesZ;
    outHeights.resize(total);
    for (std::uint32_t z = 0u; z < input.samplesZ; ++z) {
        const float worldZ = bounds.minZ
            + (bounds.maxZ - bounds.minZ) * static_cast<float>(z)
                / static_cast<float>(input.samplesZ - 1u);
        for (std::uint32_t x = 0u; x < input.samplesX; ++x) {
            const float worldX = bounds.minX
                + (bounds.maxX - bounds.minX) * static_cast<float>(x)
                    / static_cast<float>(input.samplesX - 1u);
            const auto sampled = input.surface->sample(worldX, worldZ);
            if (!sampled.ready() || !std::isfinite(sampled.sample.height)) {
                diagnostics = "DDGI-TERRAIN-SURFACE-SAMPLE-UNAVAILABLE";
                outHeights.clear();
                return false;
            }
            outHeights[static_cast<std::size_t>(z) * input.samplesX + x] = sampled.sample.height;
        }
    }
    return true;
}

inline bool buildDdgiBakedTerrainGrid(const DdgiTerrainSurfaceInput& input,
    DdgiBakedTerrainGrid& outGrid,
    core::pmr::String& diagnostics)
{
    outGrid = {};
    if (!buildDdgiTerrainSurfaceGrid(input, outGrid.heights, diagnostics)) {
        return false;
    }
    outGrid.bounds = input.surface->bounds();
    outGrid.samplesX = input.samplesX;
    outGrid.samplesZ = input.samplesZ;
    if (!outGrid.valid()) {
        diagnostics = "DDGI-BAKED-TERRAIN-GRID-INVALID";
        outGrid = {};
        return false;
    }
    return true;
}

} // namespace arp::renderer
