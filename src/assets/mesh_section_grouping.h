#pragma once

#include "assets/asset_pipeline.h"
#include "assets/mesh/triangle_cluster_builder.h"
#include "assets/mesh_source_ranges.h"
#include "core/memory/cpu_allocator.h"

#include <algorithm>
#include <stdexcept>

namespace arp::assets {

inline core::pmr::Vector<Submesh> validatedMeshSectionPartition(const MeshAsset& mesh)
{
    if (!mesh.validForIndexedDraw()) {
        throw std::runtime_error("MESH-SECTION-GROUPING-INVALID-MESH");
    }
    core::pmr::Vector<Submesh> sections(
        mesh.submeshes.begin(), mesh.submeshes.end(), &core::defaultCpuMemoryResource());
    std::sort(sections.begin(), sections.end(), [](const auto& a, const auto& b) {
        return a.firstIndex < b.firstIndex;
    });
    std::size_t covered = 0;
    for (const auto& section : sections) {
        if (section.firstIndex != covered || section.indexCount % 3u != 0u) {
            throw std::runtime_error("MESH-SECTION-GROUPING-NONPARTITIONED-INDICES");
        }
        covered += section.indexCount;
    }
    if (covered != mesh.indices.size()) {
        throw std::runtime_error("MESH-SECTION-GROUPING-INCOMPLETE-INDICES");
    }
    return sections;
}

// Opt-in for order-independent draws. Preserve every index and material slot.
inline void groupMeshSectionsByMaterial(MeshAsset& mesh, bool retainSourceRanges = false)
{
    if (retainSourceRanges && (mesh.sourceRangesVersion || !mesh.sourceRanges.empty()))
        throw core::DiagnosticError("MESH-SOURCE-RANGES-ALREADY-GROUPED");
    auto sections = validatedMeshSectionPartition(mesh);
    std::sort(sections.begin(), sections.end(), [](const auto& a, const auto& b) {
        return a.materialSlot < b.materialSlot
            || (a.materialSlot == b.materialSlot && a.firstIndex < b.firstIndex);
    });
    core::pmr::Vector<std::uint32_t> indices(&core::defaultCpuMemoryResource());
    core::pmr::Vector<Submesh> grouped(&core::defaultCpuMemoryResource());
    core::pmr::Vector<MeshSourceRange> ranges(&core::defaultCpuMemoryResource());
    if (retainSourceRanges)
        ranges.reserve(sections.size());
    indices.reserve(mesh.indices.size());
    for (const auto& section : sections) {
        if (grouped.empty() || grouped.back().materialSlot != section.materialSlot) {
            grouped.push_back(
                {static_cast<std::uint32_t>(indices.size()), 0u, section.materialSlot});
        }
        if (retainSourceRanges) {
            auto range = buildMeshSourceRange(
                mesh, section, static_cast<std::uint32_t>(grouped.size() - 1u), section.firstIndex);
            range.indices.firstIndex = static_cast<std::uint32_t>(indices.size());
            ranges.push_back(range);
        }
        indices.insert(indices.end(),
            mesh.indices.begin() + section.firstIndex,
            mesh.indices.begin() + section.firstIndex + section.indexCount);
        grouped.back().indexCount += section.indexCount;
    }
    mesh.indices.assign(indices.begin(), indices.end());
    mesh.submeshes.assign(grouped.begin(), grouped.end());
    mesh.sourceRanges.swap(ranges);
    mesh.sourceRangesVersion = retainSourceRanges ? kMeshSourceRangesVersion : 0u;
}

// MeshSectionSpatialPartition/1. Zero/zero means disabled on an import policy;
// calling the transformation itself requires both explicit positive limits.
struct MeshSectionSpatialPartition {
    std::uint32_t maxTrianglesPerSection = 0;
    std::uint32_t maxSections = 0;

    bool enabled() const noexcept { return maxTrianglesPerSection || maxSections; }
};

inline void partitionMeshSectionsSpatially(MeshAsset& mesh, MeshSectionSpatialPartition policy)
{
    if (mesh.sourceRangesVersion || !mesh.sourceRanges.empty())
        throw core::DiagnosticError("MESH-SOURCE-RANGES-SPATIAL-PARTITION-INCOMPATIBLE");
    if (!policy.maxTrianglesPerSection || !policy.maxSections)
        throw std::runtime_error("MESH-SECTION-SPATIAL-POLICY-INVALID");
    const auto sections = validatedMeshSectionPartition(mesh);
    core::pmr::Vector<core::Vec3> positions(&core::defaultCpuMemoryResource());
    positions.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices)
        positions.push_back({vertex.position[0], vertex.position[1], vertex.position[2]});
    // Inherit the destination allocators so the final swaps are allocation-free
    // and failures cannot leave a partially transformed mesh.
    core::pmr::Vector<std::uint32_t> indices(mesh.indices.get_allocator().resource());
    core::pmr::Vector<Submesh> partitioned(mesh.submeshes.get_allocator().resource());
    indices.reserve(mesh.indices.size());
    for (const auto& section : sections) {
        if (partitioned.size() >= policy.maxSections)
            throw core::DiagnosticError("TRIANGLE-CLUSTER-CAPACITY-EXCEEDED");
        const auto remaining = policy.maxSections - static_cast<std::uint32_t>(partitioned.size());
        const auto result = core::TriangleClusterBuilder{}.build(positions.data(),
            positions.size(),
            mesh.indices.data() + section.firstIndex,
            section.indexCount,
            policy.maxTrianglesPerSection,
            remaining);
        const auto first = static_cast<std::uint32_t>(indices.size());
        indices.insert(indices.end(), result.indices.begin(), result.indices.end());
        for (const auto& cluster : result.clusters)
            partitioned.push_back(
                {first + cluster.firstIndex, cluster.indexCount, section.materialSlot});
    }
    mesh.indices.swap(indices);
    mesh.submeshes.swap(partitioned);
}

} // namespace arp::assets
