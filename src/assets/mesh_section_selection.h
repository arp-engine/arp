#pragma once
#include "assets/asset_pipeline.h"
#include "core/cpu_allocator.h"

#include <limits>
#include <stdexcept>

namespace arp::assets {

// Select material sections for a raster representation. Source vertex values
// and index order are preserved exactly; unused vertices are removed. The
// original MeshAsset can remain the authoritative physics/GI representation.
inline MeshAsset selectMeshSectionsExceptMaterial(
    const MeshAsset& source, std::uint32_t excludedMaterial)
{
    if (!source.validForIndexedDraw())
        throw std::runtime_error("MESH-SECTION-SELECTION-INVALID-SOURCE");
    MeshAsset result;
    core::pmr::Vector<std::uint32_t> remap(source.vertices.size(),
        std::numeric_limits<std::uint32_t>::max(),
        &core::defaultCpuMemoryResource());
    for (const auto& section : source.submeshes) {
        if (section.materialSlot == excludedMaterial)
            continue;
        const auto first = static_cast<std::uint32_t>(result.indices.size());
        for (std::size_t i = section.firstIndex;
            i < std::size_t(section.firstIndex) + section.indexCount;
            ++i) {
            const auto old = source.indices[i];
            if (remap[old] == std::numeric_limits<std::uint32_t>::max()) {
                remap[old] = static_cast<std::uint32_t>(result.vertices.size());
                result.vertices.push_back(source.vertices[old]);
            }
            result.indices.push_back(remap[old]);
        }
        result.submeshes.push_back({first, section.indexCount, section.materialSlot});
    }
    if (!result.validForIndexedDraw())
        throw std::runtime_error("MESH-SECTION-SELECTION-EMPTY-RESULT");
    return result;
}
} // namespace arp::assets
