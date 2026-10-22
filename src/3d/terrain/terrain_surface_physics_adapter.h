#pragma once

#include "3d/terrain/terrain_surface.h"
#include "physics/3d/physics_types.h"

#include <memory>
#include <utility>

namespace arp::physics {

/// Retains a terrain query generation while a physics heightfield is built.
/// A non-ready TerrainSurface query is deliberately exposed as failure so a
/// partially streamed TerrainAsset can never become a fabricated collider.
class TerrainSurfacePhysicsSource final {
public:
    explicit TerrainSurfacePhysicsSource(std::shared_ptr<const terrain::TerrainSurface> surface)
        : surface_(std::move(surface))
    {}

    bool valid() const noexcept { return surface_ != nullptr && surface_->bounds().valid(); }

    TerrainPhysicsBounds bounds() const noexcept
    {
        if (!valid()) {
            return {};
        }
        const auto sourceBounds = surface_->bounds();
        return {sourceBounds.minX, sourceBounds.maxX, sourceBounds.minZ, sourceBounds.maxZ};
    }

    TerrainPhysicsSource source() noexcept
    {
        return TerrainPhysicsSource{&trySample, nullptr, this};
    }

private:
    static bool trySample(void* context, float x, float z, float& outHeight)
    {
        const auto* self = static_cast<const TerrainSurfacePhysicsSource*>(context);
        if (self == nullptr || self->surface_ == nullptr) {
            return false;
        }
        const auto query = self->surface_->sample(x, z);
        if (!query.ready()) {
            return false;
        }
        outHeight = query.sample.height;
        return true;
    }

    std::shared_ptr<const terrain::TerrainSurface> surface_;
};

} // namespace arp::physics
