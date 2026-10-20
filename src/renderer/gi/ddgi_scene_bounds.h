#pragma once
#include "core/array_view.h"
#include "renderer/gi/ddgi_renderer.h"
#include "renderer/global_illumination.h"

namespace arp::renderer {
// Cold adapter from immutable world-space scene geometry to the existing grid
// placement policy. Recompute on geometry revision, never on camera movement.
inline DdgiVolumePlacement fitDdgiProbeGridToSceneGeometry(const ProbeGridDesc& grid,
    core::ArrayView<DdgiSceneBvhTriangle> triangles,
    const std::array<float, 3>& minimumSpacing,
    float receiverPadding)
{
    if (!triangles.data() || triangles.empty() || !std::isfinite(receiverPadding)
        || receiverPadding < 0.f)
        return {};
    scene3d::Aabb bounds{};
    bool first = true;
    for (const auto& triangle : triangles) {
        if (triangle.materialIdx >= 0x80000000u)
            return {};
        for (const auto* v : {triangle.v0, triangle.v1, triangle.v2}) {
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]))
                return {};
            const core::Vec3 point{v[0], v[1], v[2]};
            const scene3d::Aabb vertexBounds{point, point};
            bounds = first ? vertexBounds : scene3d::Aabb::merge(bounds, vertexBounds);
            first = false;
        }
    }
    return fitDdgiProbeGridToBounds(grid,
        {{bounds.min.x - receiverPadding,
             bounds.min.y - receiverPadding,
             bounds.min.z - receiverPadding},
            {bounds.max.x + receiverPadding,
                bounds.max.y + receiverPadding,
                bounds.max.z + receiverPadding}},
        minimumSpacing);
}
} // namespace arp::renderer
