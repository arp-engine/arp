#pragma once

#include "assets/asset_pipeline.h"

#include <array>

namespace arp::assets {
// Validated StaticMeshShadowLodAsset/1 metadata. Each range addresses its own
// level's independently packed bank; identity never implies equal offsets.
struct MeshShadowLodSource {
    std::uint32_t node = 0, mesh = 0, primitive = 0;
    bool approximable = false;
    std::array<float, 16> worldAffine{};
    std::array<double, 3> localAbsoluteSupport{};
    std::array<double, 3> surfaceError{};
    std::array<MeshSourceRange, 3> ranges{};
};
} // namespace arp::assets
