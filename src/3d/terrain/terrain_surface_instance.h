#pragma once
#include "3d/terrain/terrain_surface.h"
#include "core/hash/stable_hash.h"
#include "core/memory/cpu_allocator.h"

#include <memory>

namespace arp::terrain {
// Translation-only world query boundary. Geometry and native renderer data
// stay in resource-local space; physics/gameplay/vegetation share this owner.
class TerrainSurfaceInstance final : public TerrainSurface {
public:
    TerrainSurfaceInstance(std::shared_ptr<const TerrainSurface> local, core::Vec3 translation)
        : local_(std::move(local)), translation_(translation)
    {
        if (local_) {
            const auto b = local_->bounds();
            bounds_ = {b.minX + translation.x,
                b.maxX + translation.x,
                b.minZ + translation.z,
                b.maxZ + translation.z};
            core::StableHash64 hash;
            hash.appendU64(local_->generation());
            hash.appendFloat(translation.x);
            hash.appendFloat(translation.y);
            hash.appendFloat(translation.z);
            generation_ = hash.value();
        }
    }

    bool valid() const noexcept
    {
        return local_ && local_->generation() && bounds_.valid() && std::isfinite(bounds_.minX)
            && std::isfinite(bounds_.maxX) && std::isfinite(bounds_.minZ)
            && std::isfinite(bounds_.maxZ) && std::isfinite(translation_.y);
    }

    TerrainBounds bounds() const noexcept override { return bounds_; }

    std::uint64_t generation() const noexcept override { return generation_; }

    TerrainSurfaceQuery sample(float worldX, float worldZ) const override
    {
        if (!valid())
            return {TerrainSurfaceQueryStatus::InvalidData, {}};
        auto query = local_->sample(worldX - translation_.x, worldZ - translation_.z);
        if (query.ready()) {
            query.sample.height += translation_.y;
            if (!std::isfinite(query.sample.height))
                query.status = TerrainSurfaceQueryStatus::InvalidData;
        }
        return query;
    }

private:
    std::shared_ptr<const TerrainSurface> local_;
    core::Vec3 translation_;
    TerrainBounds bounds_;
    std::uint64_t generation_ = 0u;
};
} // namespace arp::terrain
