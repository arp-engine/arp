#pragma once

#include "assets/asset_pipeline.h"
#include "core/cpu_allocator.h"

#include <algorithm>
#include <stdexcept>

namespace arp::assets {

// Opt-in for order-independent draws. Preserve every index and material slot.
inline void groupMeshSectionsByMaterial(MeshAsset& mesh)
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
    std::sort(sections.begin(), sections.end(), [](const auto& a, const auto& b) {
        return a.materialSlot < b.materialSlot
            || (a.materialSlot == b.materialSlot && a.firstIndex < b.firstIndex);
    });
    core::pmr::Vector<std::uint32_t> indices(&core::defaultCpuMemoryResource());
    core::pmr::Vector<Submesh> grouped(&core::defaultCpuMemoryResource());
    indices.reserve(mesh.indices.size());
    for (const auto& section : sections) {
        if (grouped.empty() || grouped.back().materialSlot != section.materialSlot) {
            grouped.push_back(
                {static_cast<std::uint32_t>(indices.size()), 0u, section.materialSlot});
        }
        indices.insert(indices.end(),
            mesh.indices.begin() + section.firstIndex,
            mesh.indices.begin() + section.firstIndex + section.indexCount);
        grouped.back().indexCount += section.indexCount;
    }
    mesh.indices.assign(indices.begin(), indices.end());
    mesh.submeshes.assign(grouped.begin(), grouped.end());
}

} // namespace arp::assets
