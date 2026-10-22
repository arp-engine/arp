#pragma once

#include "core/diagnostics/diagnostic_error.h"
#include "core/math/math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace arp::core {
// ProjectedDepthOrder/1. Reorder complete records without owned scratch.
// Projections must be pure; identity breaks equal-depth ties deterministically.
class ProjectedDepthOrder final {
public:
    template <class Record, class Position, class Identity>
    static void sort(
        Record* records, std::size_t count, Vec3 axis, Position position, Identity identity)
    {
        static_assert(
            std::is_trivially_copyable_v<Record>, "Depth ordering requires plain records");
        static_assert(std::is_integral_v<decltype(identity(std::declval<const Record&>()))>,
            "Depth ordering requires an integral stable identity");
        if (!std::isfinite(axis.x) || !std::isfinite(axis.y) || !std::isfinite(axis.z)
            || (axis.x == 0 && axis.y == 0 && axis.z == 0))
            throw DiagnosticError("PROJECTED-DEPTH-ORDER-AXIS-INVALID");
        if (count && !records)
            throw DiagnosticError("PROJECTED-DEPTH-ORDER-SPAN-INVALID");
        // Validate the entire span before sorting: failures never publish a
        // partially reordered instance array or invalid comparator relation.
        for (std::size_t i = 0; i < count; ++i) {
            const auto p = position(records[i]);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                throw DiagnosticError("PROJECTED-DEPTH-ORDER-POSITION-INVALID");
        }
        if (count < 2)
            return;
        const auto depth = [&](const Record& record) {
            const auto p = position(record);
            // Float inputs can overflow a float dot product at finite extrema.
            return double(p.x) * axis.x + double(p.y) * axis.y + double(p.z) * axis.z;
        };
        std::sort(records, records + count, [&](const Record& a, const Record& b) {
            const auto ad = depth(a), bd = depth(b);
            return ad < bd || (ad == bd && identity(a) < identity(b));
        });
    }
};
} // namespace arp::core
