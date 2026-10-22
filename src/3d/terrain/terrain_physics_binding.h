#pragma once

#include "3d/terrain/terrain_surface_physics_adapter.h"
#include "physics/3d/physics_world.h"

namespace arp::physics {
// Cold single-heightfield owner. The world must outlive this binding; calls
// are serialized before simulation. No resource, scene or renderer dependency.
class ARP_API TerrainPhysicsBinding final {
public:
    struct Result final {
        bool success = false, reused = false;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    explicit TerrainPhysicsBinding(PhysicsWorld& world) : world_(world) {}

    ~TerrainPhysicsBinding();
    TerrainPhysicsBinding(const TerrainPhysicsBinding&) = delete;
    TerrainPhysicsBinding& operator=(const TerrainPhysicsBinding&) = delete;

    Result bind(std::shared_ptr<const terrain::TerrainSurface> worldSurface);
    Result clear();

    bool ready() const noexcept
    {
        return handle_.valid() && world_.terrainHeightFieldHandle() == handle_;
    }

    PhysicsBodyHandle handle() const noexcept { return ready() ? handle_ : PhysicsBodyHandle{}; }

    std::uint64_t generation() const noexcept { return ready() ? surface_->generation() : 0u; }

    std::shared_ptr<const terrain::TerrainSurface> surface() const
    {
        return ready() ? surface_ : nullptr;
    }

private:
    PhysicsWorld& world_;
    PhysicsBodyHandle handle_;
    std::shared_ptr<const terrain::TerrainSurface> surface_;
};
} // namespace arp::physics
