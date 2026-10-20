#pragma once

#include "assets/asset_pipeline.h"
#include "core/api.h"

#include <array>
#include <cstdint>

namespace arp::assets {

struct IndexedMeshTangentRange {
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

/// Stores an authored tangent frame after orthonormalizing tangent.xyz against
/// the vertex normal. Returns false for non-finite or degenerate source data.
/// The handedness is recovered from the authored bitangent instead of assumed.
ARP_API bool applyImportedTangentFrame(
    MeshVertex& vertex, const std::array<float, 3>& tangent, const std::array<float, 3>& bitangent);

/// Rebuilds tangent.xyz from mesh UV derivatives. When UV V coordinates were
/// flipped without vertically transforming the sampled normal map, preserve
/// the authored normal-map Y orientation by compensating tangent.w.
ARP_API bool generateTangentsForIndexedMesh(
    MeshAsset& mesh, bool compensateNormalMapForUvYFlip = false);

/// Rebuilds tangents only for one explicitly owned vertex/index range. Every
/// referenced index must stay within the range, preventing fallback generation
/// for one imported submesh from overwriting authored tangents in another.
ARP_API bool generateTangentsForIndexedMeshRange(MeshAsset& mesh,
    const IndexedMeshTangentRange& range,
    bool compensateNormalMapForUvYFlip = false);

} // namespace arp::assets
