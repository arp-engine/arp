#pragma once

#include "core/math/math.h"

#include <stdexcept>

namespace arp::core {

// Translation-only placement preserves vertex frames, UVs and index winding.
// Apply to upload staging copies when the source asset remains object-local.
class MeshTranslation final {
public:
    template <class Mesh> static void apply(Mesh& mesh, Vec3 offset)
    {
        if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z))
            throw std::invalid_argument("MESH-TRANSLATION-NONFINITE");
        for (auto& vertex : mesh.vertices) {
            vertex.position[0] += offset.x;
            vertex.position[1] += offset.y;
            vertex.position[2] += offset.z;
        }
    }
};

} // namespace arp::core
