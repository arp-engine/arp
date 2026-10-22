#pragma once

#include "assets/asset_pipeline.h"
#include "core/algorithm/index_runs.h"
#include "core/math/conservative_clip_volume.h"
#include "core/math/conservative_float_interval.h"

#include <array>
#include <limits>

namespace arp::renderer {
struct StaticMeshRangePlan {
    std::array<core::IndexRun, 4u> views{};
    std::array<std::uint64_t, 4u> triangles{};
    std::array<std::uint32_t, 4u> selectedSources{};
};

class StaticMeshRangeSelection final {
public:
    // view0 is the actual jittered main clip matrix. views1..3 are the
    // independent light cascades. Source geometry is already world-baked.
    static StaticMeshRangePlan append(const core::pmr::Vector<assets::MeshSourceRange>& ranges,
        std::uint32_t parentSubmesh,
        const core::ConservativeClipVolume& camera,
        const core::ConservativeClipVolume* cascades,
        std::size_t cascadeCount,
        float normalBias,
        core::pmr::Vector<core::IndexRun>& output,
        std::size_t maxRanges)
    {
        if ((cascadeCount && !cascades) || cascadeCount > 3u || !std::isfinite(normalBias)
            || maxRanges > std::numeric_limits<std::uint32_t>::max())
            throw core::DiagnosticError("STATIC-MESH-RANGE-PLAN-BUDGET-INVALID");
        StaticMeshRangePlan plan;
        using I = core::ConservativeFloatInterval;
        const auto first =
            std::find_if(ranges.begin(), ranges.end(), [parentSubmesh](const auto& range) {
                return range.parentSubmesh == parentSubmesh;
            });
        const auto last = std::find_if(first, ranges.end(), [parentSubmesh](const auto& range) {
            return range.parentSubmesh != parentSubmesh;
        });
        if (first == last)
            throw core::DiagnosticError("STATIC-MESH-RANGE-PARENT-MISSING");
        const auto count = static_cast<std::size_t>(last - first);
        for (std::size_t v = 0; v <= cascadeCount; ++v) {
            const auto start = output.size();
            core::forEachSelectedIndexRun(
                count,
                [&](std::size_t ordinal) {
                    const auto& range = first[ordinal];
                    if (!range.bounds.valid)
                        throw core::DiagnosticError("STATIC-MESH-RANGE-BOUNDS-MISSING");
                    float minimum[3], maximum[3];
                    for (unsigned a = 0; a < 3u; ++a) {
                        const auto position = I::range(range.bounds.min[a], range.bounds.max[a]);
                        const auto bias = v
                            ? I::range(-range.maxAbsNormal[a], range.maxAbsNormal[a])
                                * I::point(normalBias)
                            : I::point(0.f);
                        const auto displaced = v ? position + bias : position;
                        minimum[a] = displaced.low;
                        maximum[a] = displaced.high;
                    }
                    const auto& volume = v ? cascades[v - 1u] : camera;
                    if (!volume.intersects(minimum, maximum))
                        return false;
                    ++plan.selectedSources[v];
                    plan.triangles[v] += range.indices.indexCount / 3u;
                    return true;
                },
                [&](core::IndexRun run) {
                    const auto begin = std::size_t(first[run.first].indices.firstIndex);
                    const auto& tail = first[run.first + run.count - 1u].indices;
                    const auto end = std::size_t(tail.firstIndex) + tail.indexCount;
                    if (output.size() >= maxRanges
                        || end > std::numeric_limits<std::uint32_t>::max() || begin >= end
                        || (end - begin) % 3u)
                        throw core::DiagnosticError("STATIC-MESH-RANGE-OUTPUT-BUDGET-INVALID");
                    output.push_back({begin, end - begin});
                });
            plan.views[v] = {start, output.size() - start};
        }
        return plan;
    }
};
} // namespace arp::renderer
