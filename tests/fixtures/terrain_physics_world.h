#pragma once

#include "physics/3d/physics_world.h"
#include "renderer/runtime_scene.h"

#include <cassert>
#include <cmath>

namespace arp::resource {
class TerrainAssetResource;
}

namespace arp::tests::terrain_physics {

inline core::UniquePtr<physics::PhysicsWorld> makeWorld(
    std::uint32_t resolution = 8u, std::size_t budget = 8u * 8u * sizeof(float))
{
    physics::PhysicsWorldDiagnostic diagnostic;
    auto world = physics::createPhysicsWorld(diagnostic);
    assert(world);
    physics::PhysicsWorldConfig config;
    config.maxBodies = 8u;
    config.maxBodyPairs = 32u;
    config.maxContactConstraints = 16u;
    config.terrainGridResolution = resolution;
    config.maxTerrainSampleBytes = budget;
    assert(world->init(config));
    return world;
}

inline physics::RaycastHit hit(physics::PhysicsWorld& world, float x, float z, float height)
{
    physics::RaycastQuery ray;
    ray.origin = {x, height + 10.0f, z};
    ray.distance = 20.0f;
    physics::RaycastHit result;
    assert(world.raycast(ray, result));
    assert(std::abs(result.position.y - height) < 0.002f);
    return result;
}

inline void misses(physics::PhysicsWorld& world, float x, float z)
{
    physics::RaycastQuery ray;
    ray.origin = {x, 20.0f, z};
    ray.distance = 40.0f;
    physics::RaycastHit result;
    assert(!world.raycast(ray, result));
    assert(world.lastDiagnostic().code == physics::PhysicsWorldDiagnostic::Code::None);
}

inline bool resolveMaterial(void* context,
    const resource::TerrainAssetResource&,
    renderer::MaterialHandle& material,
    core::pmr::String&)
{
    material = *static_cast<renderer::MaterialHandle*>(context);
    return true;
}
} // namespace arp::tests::terrain_physics
