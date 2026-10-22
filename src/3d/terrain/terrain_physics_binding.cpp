#include "3d/terrain/terrain_physics_binding.h"

namespace arp::physics {
TerrainPhysicsBinding::~TerrainPhysicsBinding()
{
    if (ready())
        world_.clearTerrainHeightField(handle_);
}

TerrainPhysicsBinding::Result TerrainPhysicsBinding::bind(
    std::shared_ptr<const terrain::TerrainSurface> worldSurface)
{
    Result result;
    TerrainSurfacePhysicsSource source(worldSurface);
    const auto bounds = source.bounds();
    if (!source.valid() || !worldSurface->generation() || !std::isfinite(bounds.minX)
        || !std::isfinite(bounds.maxX) || !std::isfinite(bounds.minZ)
        || !std::isfinite(bounds.maxZ)) {
        result.diagnostics = "PHYSICS-TERRAIN-SURFACE-INVALID";
        return result;
    }
    if (world_.terrainHeightFieldHandle() != handle_) {
        result.diagnostics = "PHYSICS-TERRAIN-OWNERSHIP-CONFLICT";
        return result;
    }
    if (ready() && surface_->generation() == worldSurface->generation()) {
        const auto prior = surface_->bounds();
        if (prior.minX != bounds.minX || prior.maxX != bounds.maxX || prior.minZ != bounds.minZ
            || prior.maxZ != bounds.maxZ) {
            result.diagnostics = "PHYSICS-TERRAIN-GENERATION-BOUNDS-MISMATCH";
            return result;
        }
        result.success = result.reused = true;
        return result;
    }
    // The adapter retains the candidate throughout synchronous native creation.
    if (!world_.setTerrainHeightField(source.source(), bounds)) {
        result.diagnostics = world_.lastDiagnostic().message;
        return result;
    }
    handle_ = world_.terrainHeightFieldHandle();
    surface_ = std::move(worldSurface);
    result.success = true;
    return result;
}

TerrainPhysicsBinding::Result TerrainPhysicsBinding::clear()
{
    Result result;
    if (handle_.valid() && !world_.clearTerrainHeightField(handle_)) {
        result.diagnostics = world_.lastDiagnostic().message;
        return result;
    }
    handle_ = {};
    surface_.reset();
    result.success = true;
    return result;
}
} // namespace arp::physics
