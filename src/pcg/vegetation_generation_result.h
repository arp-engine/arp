#pragma once
#include "3d/vegetation/instance.h"
#include "assets/asset_pipeline.h"

namespace arp::pcg {
/// Result of per-chunk vegetation generation. Carries either instance batches
/// (P6 path) or a fallback merged mesh (prototype/A-B path).
struct ChunkVegetationResult {
    // Optional publisher-issued content identity. Zero means unpublished.
    // Consumers that cache generated content must compare this revision.
    std::uint64_t revision = 0u;
    arp::core::pmr::Vector<vegetation::InstanceBatch> batches{
        &arp::core::defaultCpuMemoryResource()};
    bool useInstances = false;

    // Fallback merged mesh (prototype mode when useInstances = false).
    assets::MeshAsset fallbackMesh;

    std::size_t totalInstanceCount() const
    {
        std::size_t n = 0;
        for (const auto& b : batches)
            n += b.instances.size();
        return n;
    }
};

} // namespace arp::pcg
