#pragma once
#include "rhi/frontend/bindless_pbr_geometry.h"

namespace arp::rhi {
// TerrainGeometry/1. GeometryFrame400 and Draw16 are shared with mesh geometry;
// all additional material/weather/page inputs remain explicit ordinary bindings.
struct alignas(16) BindlessTerrainGeometryParameters64 {
    std::array<float, 4u> foamOriginExtent{}, foamParameters{}, weather{};
    std::array<std::uint32_t, 4u> pages{};
};

static_assert(std::is_trivially_copyable_v<BindlessTerrainGeometryParameters64>);
static_assert(sizeof(BindlessTerrainGeometryParameters64) == 64u
    && alignof(BindlessTerrainGeometryParameters64) == 16u
    && offsetof(BindlessTerrainGeometryParameters64, foamParameters) == 16u
    && offsetof(BindlessTerrainGeometryParameters64, weather) == 32u
    && offsetof(BindlessTerrainGeometryParameters64, pages) == 48u);

class BindlessTerrainGeometryContract final {
public:
    static constexpr std::uint32_t version = 1u;

    // Supply counts from the actual initialized page/foam ranges. Native cold
    // admission must authenticate those actors; these value checks issue no proof.
    static const char* validateParameters(const BindlessTerrainGeometryParameters64& parameters,
        std::uint32_t pageBuffers,
        std::uint32_t pageElements,
        std::uint32_t foamBuffers,
        std::uint64_t foamWords) noexcept
    {
        for (const auto* vector :
            {&parameters.foamOriginExtent, &parameters.foamParameters, &parameters.weather})
            for (const auto lane : *vector)
                if (!std::isfinite(lane))
                    return "RHI-BINDLESS-TERRAIN-GEOMETRY-NONFINITE";
        if (parameters.weather[0u] < 0.f || parameters.weather[0u] > 1.f
            || parameters.weather[1u] != 0.f || parameters.weather[2u] != 0.f
            || parameters.weather[3u] != 0.f)
            return "RHI-BINDLESS-TERRAIN-GEOMETRY-WEATHER";
        const auto& pages = parameters.pages;
        if (pages[0u] >= pageBuffers || pages[1u] > pageElements || pages[1u] > pages[2u]
            || pages[3u] >= foamBuffers)
            return "RHI-BINDLESS-TERRAIN-GEOMETRY-PAGE-RANGE";
        // Match the authored shore-wetness activation and rounded grid ABI.
        const auto& foam = parameters.foamParameters;
        if (foam[2u] > 0.5f) {
            if (foam[3u] < 0.f || foam[3u] >= 4294967040.f)
                return "RHI-BINDLESS-TERRAIN-GEOMETRY-FOAM-EXTENT";
            const auto rounded = static_cast<std::uint64_t>(foam[3u] + 0.5f);
            const auto resolution = rounded ? rounded : 1u;
            if (resolution > foamWords / resolution)
                return "RHI-BINDLESS-TERRAIN-GEOMETRY-FOAM-CAPACITY";
        }
        return nullptr;
    }
};
} // namespace arp::rhi
