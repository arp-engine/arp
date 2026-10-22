#pragma once

#include "3d/vegetation/mesh_adapter.h"
#include "3d/vegetation/template.h"

namespace arp::resource::detail {
// Shared asset-to-domain conversion. Units, wind and appearance belong to the
// caller's explicit authoring contract (or the versioned legacy loader).
class VegetationTemplateGeometryBuilder final {
public:
    static vegetation::VegetationTemplate build(const assets::MeshAsset& mesh)
    {
        vegetation::VegetationTemplate result;
        result.vertices = vegetation::convertToInstanceVertices(mesh, nullptr, false);
        result.indices = mesh.indices;
        result.sections.reserve(mesh.submeshes.size());
        for (const auto& section : mesh.submeshes)
            result.sections.push_back(
                {section.firstIndex, section.indexCount, section.materialSlot});
        if (!mesh.submeshes.empty())
            result.materialIndex = mesh.submeshes.front().materialSlot;
        return result;
    }
};
} // namespace arp::resource::detail
