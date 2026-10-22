#pragma once

#include "assets/mesh_source_ranges.h"

namespace arp::assets {
struct MeshSourceIndexRepack {
    core::pmr::Vector<std::uint32_t> indices{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<MeshSourceRange> ranges{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint32_t> oldToNew{&core::defaultCpuMemoryResource()};
};

// MeshSourceIndexOrder/1. Cold preparation only. Reorder complete retained
// primitive intervals within their existing parents, preserving all index
// values/corner order and sourceFirstIndex bindings. Vertices are never copied.
inline MeshSourceIndexRepack repackMeshSourceIndices(const MeshAsset& mesh,
    const core::pmr::Vector<std::uint32_t>& order,
    std::size_t maxDestinationBytes)
{
    validateMeshSourceRanges(mesh);
    const auto count = mesh.sourceRanges.size();
    if (!count || order.size() != count || count > UINT32_MAX)
        throw core::DiagnosticError("MESH-SOURCE-REPACK-ORDER-COUNT");
    const auto bytes = std::uint64_t(mesh.indices.size()) * sizeof(std::uint32_t)
        + std::uint64_t(count) * (sizeof(MeshSourceRange) + 2u * sizeof(std::uint32_t));
    if (bytes > maxDestinationBytes)
        throw core::DiagnosticError("MESH-SOURCE-REPACK-BUDGET");
    // UINT32_MAX is both the unvisited marker and excluded as a valid ordinal.
    MeshSourceIndexRepack result;
    result.oldToNew.assign(count, UINT32_MAX);
    for (const auto ordinal : order) {
        if (ordinal >= count || result.oldToNew[ordinal] != UINT32_MAX)
            throw core::DiagnosticError("MESH-SOURCE-REPACK-ORDER-BIJECTION");
        result.oldToNew[ordinal] = 0;
    }
    core::pmr::Vector<std::uint32_t> sorted(order, &core::defaultCpuMemoryResource());
    // Stable caller ordering within each parent; no source can move into
    // another material or change the parent's interval used by the renderer.
    for (std::uint32_t i = 0; i < count; ++i)
        result.oldToNew[order[i]] = i;
    std::sort(sorted.begin(), sorted.end(), [&](auto a, auto b) {
        const auto pa = mesh.sourceRanges[a].parentSubmesh;
        const auto pb = mesh.sourceRanges[b].parentSubmesh;
        return pa < pb || (pa == pb && result.oldToNew[a] < result.oldToNew[b]);
    });
    result.indices.reserve(mesh.indices.size());
    result.ranges.reserve(count);
    for (const auto ordinal : sorted) {
        auto range = mesh.sourceRanges[ordinal];
        const auto& parent = mesh.submeshes[range.parentSubmesh];
        const auto first = result.indices.size();
        if (first < parent.firstIndex || first > std::size_t(parent.firstIndex) + parent.indexCount
            || range.indices.indexCount
                > std::size_t(parent.firstIndex) + parent.indexCount - first)
            throw core::DiagnosticError("MESH-SOURCE-REPACK-PARENT");
        result.indices.insert(result.indices.end(),
            mesh.indices.begin() + range.indices.firstIndex,
            mesh.indices.begin() + range.indices.firstIndex + range.indices.indexCount);
        range.indices.firstIndex = static_cast<std::uint32_t>(first);
        result.oldToNew[ordinal] = static_cast<std::uint32_t>(result.ranges.size());
        result.ranges.push_back(range);
    }
    if (result.indices.size() != mesh.indices.size())
        throw core::DiagnosticError("MESH-SOURCE-REPACK-COVERAGE");
    return result;
}
} // namespace arp::assets
