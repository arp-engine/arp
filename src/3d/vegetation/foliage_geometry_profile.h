#pragma once
#include "3d/vegetation/wind_field_system.h"

#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <type_traits>

namespace arp::vegetation {
// Family intent is fixed before compiling/publishing any material generation.
// Heights are the existing authored WOCC values, not backend defaults.
enum class FoliageGeometryFamily { PbrGrass, DedicatedCanopy };

struct FoliageGeometryProfile {
    std::uint32_t contractVersion = 0u;
    FoliageGeometryFamily family = FoliageGeometryFamily::PbrGrass;
    float heightReference = 0.f;
};

class FoliageGeometryContract final {
public:
    static constexpr FoliageGeometryProfile grass() noexcept
    {
        return {1u, FoliageGeometryFamily::PbrGrass, 0.5f};
    }

    static constexpr FoliageGeometryProfile canopy() noexcept
    {
        return {1u, FoliageGeometryFamily::DedicatedCanopy, 2.f};
    }

    static const char* validate(const FoliageGeometryProfile& p) noexcept
    {
        if (p.contractVersion != 1u)
            return "PCG-FOLIAGE-GEOMETRY-VERSION";
        if (!std::isfinite(p.heightReference) || p.heightReference <= 0.f)
            return "PCG-FOLIAGE-GEOMETRY-HEIGHT";
        switch (p.family) {
        case FoliageGeometryFamily::PbrGrass:
            return p.heightReference == grass().heightReference ? nullptr
                                                                : "PCG-FOLIAGE-GEOMETRY-PROFILE";
        case FoliageGeometryFamily::DedicatedCanopy:
            return p.heightReference == canopy().heightReference ? nullptr
                                                                 : "PCG-FOLIAGE-GEOMETRY-PROFILE";
        default:
            return "PCG-FOLIAGE-GEOMETRY-FAMILY";
        }
    }

    static const char* validatePassFamily(const FoliageGeometryProfile& color,
        const FoliageGeometryProfile& depth,
        const FoliageGeometryProfile& shadow) noexcept
    {
        for (const auto* profile : {&color, &depth, &shadow})
            if (const auto* id = validate(*profile))
                return id;
        return color.family == depth.family && color.family == shadow.family
            ? nullptr
            : "PCG-FOLIAGE-GEOMETRY-PASS-MISMATCH";
    }
};

static_assert(std::is_standard_layout_v<WindFieldParameters> && sizeof(WindFieldParameters) == 64u);
static_assert(offsetof(WindFieldParameters, directionSpeed) == 0u
    && offsetof(WindFieldParameters, timeGustTurb) == 16u
    && offsetof(WindFieldParameters, flutterParams) == 32u
    && offsetof(WindFieldParameters, interactionCenter) == 48u);
} // namespace arp::vegetation
