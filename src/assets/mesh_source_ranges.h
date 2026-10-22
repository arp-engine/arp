#pragma once

#include "assets/asset_pipeline.h"
#include "core/diagnostics/diagnostic_error.h"

#include <cmath>
#include <limits>

namespace arp::assets {

inline MeshSourceRange buildMeshSourceRange(
    const MeshAsset& mesh, Submesh section, std::uint32_t parent, std::uint32_t sourceFirstIndex)
{
    if (mesh.indices.size() > std::numeric_limits<std::uint32_t>::max() || !section.indexCount
        || section.indexCount % 3u || section.firstIndex > mesh.indices.size()
        || section.indexCount > mesh.indices.size() - section.firstIndex)
        throw core::DiagnosticError("MESH-SOURCE-RANGE-INDICES-INVALID");
    MeshSourceRange range;
    range.indices = section;
    range.parentSubmesh = parent;
    range.sourceFirstIndex = sourceFirstIndex;
    for (unsigned a = 0; a < 3u; ++a) {
        range.bounds.min[a] = std::numeric_limits<float>::max();
        range.bounds.max[a] = std::numeric_limits<float>::lowest();
    }
    for (std::uint32_t i = 0; i < section.indexCount; ++i) {
        const auto index = mesh.indices[section.firstIndex + i];
        if (index >= mesh.vertices.size())
            throw core::DiagnosticError("MESH-SOURCE-RANGE-VERTEX-INVALID");
        const auto& vertex = mesh.vertices[index];
        for (unsigned a = 0; a < 3u; ++a) {
            if (!std::isfinite(vertex.position[a]) || !std::isfinite(vertex.normal[a]))
                throw core::DiagnosticError("MESH-SOURCE-RANGE-GEOMETRY-NONFINITE");
            range.bounds.min[a] = std::min(range.bounds.min[a], vertex.position[a]);
            range.bounds.max[a] = std::max(range.bounds.max[a], vertex.position[a]);
            range.maxAbsNormal[a] = std::max(range.maxAbsNormal[a], std::abs(vertex.normal[a]));
        }
    }
    range.bounds.valid = true;
    return range;
}

// Upload/reload validates coverage and actual containment, so a mutable mesh
// cannot keep using stale metadata after changing positions or normals.
inline void validateMeshSourceRanges(const MeshAsset& mesh)
{
    if (mesh.sourceRangesVersion == 0u && mesh.sourceRanges.empty())
        return;
    if (mesh.sourceRangesVersion != kMeshSourceRangesVersion || mesh.sourceRanges.empty())
        throw core::DiagnosticError("MESH-SOURCE-RANGES-VERSION-INVALID");
    std::size_t covered = 0u;
    for (const auto& range : mesh.sourceRanges) {
        if (range.indices.firstIndex != covered || range.parentSubmesh >= mesh.submeshes.size())
            throw core::DiagnosticError("MESH-SOURCE-RANGES-PARTITION-INVALID");
        const auto& parent = mesh.submeshes[range.parentSubmesh];
        const auto first = std::size_t(range.indices.firstIndex);
        if (parent.materialSlot != range.indices.materialSlot || first < parent.firstIndex
            || first > std::size_t(parent.firstIndex) + parent.indexCount
            || range.indices.indexCount
                > std::size_t(parent.firstIndex) + parent.indexCount - first)
            throw core::DiagnosticError("MESH-SOURCE-RANGES-PARENT-INVALID");
        const auto actual =
            buildMeshSourceRange(mesh, range.indices, range.parentSubmesh, range.sourceFirstIndex);
        if (!range.bounds.valid)
            throw core::DiagnosticError("MESH-SOURCE-RANGES-BOUNDS-INVALID");
        for (unsigned a = 0; a < 3u; ++a) {
            if (!std::isfinite(range.bounds.min[a]) || !std::isfinite(range.bounds.max[a])
                || !std::isfinite(range.maxAbsNormal[a]) || range.maxAbsNormal[a] < 0.f
                || actual.bounds.min[a] < range.bounds.min[a]
                || actual.bounds.max[a] > range.bounds.max[a]
                || actual.maxAbsNormal[a] > range.maxAbsNormal[a])
                throw core::DiagnosticError("MESH-SOURCE-RANGES-BOUNDS-NOT-CONSERVATIVE");
        }
        covered += range.indices.indexCount;
    }
    if (covered != mesh.indices.size())
        throw core::DiagnosticError("MESH-SOURCE-RANGES-COVERAGE-INVALID");
}
} // namespace arp::assets
