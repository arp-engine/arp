#pragma once

#include "3d/vegetation/template.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace arp::vegetation {

// Geometry semantics shared by cooked resources and renderer preparation.
// Material-resource bindings and legacy appearance fields are outside this domain.
class TemplateGeometryContract final {
public:
    static const char* validate(const VegetationTemplate& value) noexcept
    {
        if (value.vertices.empty() || value.indices.empty() || value.sections.empty()
            || value.indices.size() % 3u || value.vertices.size() > UINT32_MAX
            || value.indices.size() > UINT32_MAX || value.sections.size() > UINT32_MAX)
            return "VEGETATION-GEOMETRY-COUNTS";
        const auto finite = [](const auto& fields) {
            return std::all_of(std::begin(fields), std::end(fields), [](float f) {
                return std::isfinite(f);
            });
        };
        for (const auto& v : value.vertices) {
            if (!finite(v.position) || !finite(v.normal) || !finite(v.uv) || !finite(v.tangent)
                || !std::isfinite(v.primaryBend) || !std::isfinite(v.flutter)
                || !std::isfinite(v.stiffness))
                return "VEGETATION-GEOMETRY-VERTEX-NONFINITE";
            if (v.primaryBend < 0 || v.primaryBend > 1 || v.flutter < 0 || v.flutter > 1
                || v.stiffness < 0 || v.stiffness > 1)
                return "VEGETATION-GEOMETRY-WIND-RANGE";
        }
        for (const auto index : value.indices)
            if (index >= value.vertices.size())
                return "VEGETATION-GEOMETRY-INDEX-RANGE";
        std::size_t end = 0u;
        for (const auto& section : value.sections) {
            if (section.firstIndex != end || !section.indexCount || section.indexCount % 3u
                || section.indexCount > value.indices.size() - end)
                return "VEGETATION-GEOMETRY-SECTION-PARTITION";
            end += section.indexCount;
        }
        return end == value.indices.size() ? nullptr : "VEGETATION-GEOMETRY-SECTION-COVERAGE";
    }
};

} // namespace arp::vegetation
