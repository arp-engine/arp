#pragma once

#include "core/cpu_allocator.h"
#include "core/diagnostic_error.h"
#include "core/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace arp::core {

// Spatially partition a complete oriented triangle list. Vertices and all
// attributes remain untouched; leaves retain the source triangle order.
// Intended for import/cook work, never for per-frame construction.
class TriangleClusterBuilder final {
public:
    struct Cluster {
        std::uint32_t firstIndex = 0, indexCount = 0;
        Vec3 minimum{}, maximum{};
    };

    struct Result {
        pmr::Vector<std::uint32_t> indices{&defaultCpuMemoryResource()};
        pmr::Vector<Cluster> clusters{&defaultCpuMemoryResource()};
    };

    template <class Positions, class Indices>
    Result build(const Positions& positions,
        const Indices& indices,
        std::uint32_t maxTriangles,
        std::uint32_t maxClusters) const
    {
        return build(positions.data(),
            positions.size(),
            indices.data(),
            indices.size(),
            maxTriangles,
            maxClusters);
    }

    Result build(const Vec3* positions,
        std::size_t positionCount,
        const std::uint32_t* indices,
        std::size_t indexCount,
        std::uint32_t maxTriangles,
        std::uint32_t maxClusters) const
    {
        if (!positions || !indices || !maxTriangles || !maxClusters || !indexCount || indexCount % 3
            || indexCount > UINT32_MAX)
            throw DiagnosticError("TRIANGLE-CLUSTER-INPUT-INVALID");
        if (indexCount / 3 > std::uint64_t(maxTriangles) * maxClusters)
            throw DiagnosticError("TRIANGLE-CLUSTER-CAPACITY-EXCEEDED");

        struct Face {
            std::uint32_t id;
            Vec3 center;
        };

        pmr::Vector<Face> faces{&defaultCpuMemoryResource()};
        faces.reserve(indexCount / 3);
        for (std::size_t i = 0; i < indexCount; i += 3) {
            Vec3 center{};
            for (unsigned lane = 0; lane < 3; ++lane) {
                if (indices[i + lane] >= positionCount)
                    throw DiagnosticError("TRIANGLE-CLUSTER-INDEX-INVALID");
                const auto p = positions[indices[i + lane]];
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                    throw DiagnosticError("TRIANGLE-CLUSTER-POSITION-INVALID");
                // Divide first to keep finite extreme positions bounded.
                center = center + scale(p, 1.f / 3.f);
            }
            if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z))
                throw DiagnosticError("TRIANGLE-CLUSTER-POSITION-INVALID");
            faces.push_back({static_cast<std::uint32_t>(i / 3), center});
        }
        Result result;
        result.indices.reserve(indexCount);
        result.clusters.reserve(std::min<std::size_t>(maxClusters, faces.size()));
        const auto coordinate = [](Vec3 p, unsigned axis) {
            return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        };
        const auto split = [&](auto&& self, std::size_t first, std::size_t count) -> void {
            if (count > maxTriangles) {
                Vec3 lo = faces[first].center, hi = lo;
                for (std::size_t i = first + 1; i < first + count; ++i) {
                    const auto p = faces[i].center;
                    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                }
                const auto e = hi - lo;
                const unsigned axis = e.x >= e.y && e.x >= e.z ? 0 : e.y >= e.z ? 1 : 2;
                const auto middle = first + count / 2;
                std::nth_element(faces.begin() + first,
                    faces.begin() + middle,
                    faces.begin() + first + count,
                    [&](const auto& a, const auto& b) {
                        const auto x = coordinate(a.center, axis), y = coordinate(b.center, axis);
                        return x < y || (x == y && a.id < b.id);
                    });
                self(self, first, count / 2);
                self(self, middle, count - count / 2);
                return;
            }
            if (result.clusters.size() == maxClusters)
                throw DiagnosticError("TRIANGLE-CLUSTER-CAPACITY-EXCEEDED");
            std::sort(faces.begin() + first,
                faces.begin() + first + count,
                [](const auto& a, const auto& b) {
                    return a.id < b.id;
                });
            Cluster cluster;
            cluster.firstIndex = static_cast<std::uint32_t>(result.indices.size());
            cluster.indexCount = static_cast<std::uint32_t>(count * 3);
            cluster.minimum = cluster.maximum = positions[indices[faces[first].id * 3]];
            for (std::size_t i = first; i < first + count; ++i) {
                for (unsigned lane = 0; lane < 3; ++lane) {
                    const auto index = indices[faces[i].id * 3 + lane];
                    const auto p = positions[index];
                    cluster.minimum = {std::min(cluster.minimum.x, p.x),
                        std::min(cluster.minimum.y, p.y),
                        std::min(cluster.minimum.z, p.z)};
                    cluster.maximum = {std::max(cluster.maximum.x, p.x),
                        std::max(cluster.maximum.y, p.y),
                        std::max(cluster.maximum.z, p.z)};
                    result.indices.push_back(index);
                }
            }
            result.clusters.push_back(cluster);
        };
        split(split, 0, faces.size());
        return result;
    }
};
} // namespace arp::core
