// Physics Jolt backend smoke tests (Backend Validated).
//
// Validates the JoltPhysicsWorld implementation with real native payloads
// (not mocks/dummies): a dynamic box falls under gravity and comes to rest
// on a heightfield terrain. Contact events are also exercised. These tests
// establish the Backend Validated status for the physics layer (AGENTS.md §7).

#include "physics/physics_module.h"
#include "physics/physics_types.h"
#include "physics/physics_world.h"
#include "physics/terrain_surface_adapter.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>

namespace {

// Flat terrain at height 0 for the simplest resting case.
float flatTerrainSample(void* /*ctx*/, float /*x*/, float /*z*/)
{
    return 0.0f;
}

// A trivially wavy terrain: h = sin(x) so the box rests on a slope and we
// verify the body Y tracks the analytic height.
float waveTerrainSample(void* /*ctx*/, float x, float /*z*/)
{
    return std::sin(x) * 0.5f;
}

class BakedSurface final : public arp::terrain::TerrainSurface {
public:
    explicit BakedSurface(bool ready) : ready_(ready) {}

    arp::terrain::TerrainBounds bounds() const noexcept override
    {
        // A 600m extent with a 256-sample collider grid exercises the final
        // grid coordinate's float rounding path.
        return {-300.0f, 300.0f, -300.0f, 300.0f};
    }

    std::uint64_t generation() const noexcept override { return 7u; }

    arp::terrain::TerrainSurfaceQuery sample(float x, float z) const override
    {
        arp::terrain::TerrainSurfaceQuery query;
        if (!bounds().contains(x, z)) {
            query.status = arp::terrain::TerrainSurfaceQueryStatus::OutOfBounds;
            return query;
        }
        if (!ready_) {
            query.status = arp::terrain::TerrainSurfaceQueryStatus::TileUnavailable;
            return query;
        }
        query.status = arp::terrain::TerrainSurfaceQueryStatus::Ready;
        query.sample.height = 1.0f;
        return query;
    }

private:
    bool ready_ = false;
};

void world_init_succeeds()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);
    assert(diag.code == arp::physics::PhysicsWorldDiagnostic::Code::None);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 64;
    cfg.maxBodyPairs = 64;
    cfg.maxContactConstraints = 32;
    const bool ok = world->init(cfg);
    assert(ok);
    assert(world->lastDiagnostic().code == arp::physics::PhysicsWorldDiagnostic::Code::None);
}

void dynamic_box_falls_and_rests_on_flat_terrain()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 64;
    cfg.maxBodyPairs = 64;
    cfg.maxContactConstraints = 32;
    cfg.collisionSteps = 2;
    cfg.terrainGridResolution = 16;
    assert(world->init(cfg));

    // Build a flat terrain at y=0 over a small half-extent.
    arp::physics::TerrainPhysicsSource terrain{};
    terrain.sampleFn = &flatTerrainSample;
    terrain.ctx = nullptr;
    assert(world->setTerrainHeightField(terrain, 10.0f));

    // Create a 1x1x1 dynamic box 5 units above the terrain.
    arp::physics::RigidBodyDesc bodyDesc;
    bodyDesc.motionType = arp::physics::BodyMotionType::Dynamic;
    bodyDesc.layer = arp::physics::CollisionLayer::Dynamic;
    bodyDesc.shape.kind = arp::physics::ColliderShapeKind::Box;
    bodyDesc.shape.halfExtents = arp::core::Vec3{0.5f, 0.5f, 0.5f};
    bodyDesc.mass = 1.0f;
    bodyDesc.friction = 0.5f;

    const auto handle = world->createBody(
        bodyDesc, arp::core::Vec3{0.0f, 5.0f, 0.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    assert(handle.valid());

    // Step the world until the box settles. A 0.5 half-extent box resting on
    // y=0 terrain has its center at y=0.5. Allow a tolerance for the solver.
    constexpr float kStep = 1.0f / 60.0f;
    constexpr int kMaxSteps = 240;
    float finalY = 5.0f;
    for (int i = 0; i < kMaxSteps; ++i) {
        assert(world->step(kStep));
        arp::core::Vec3 pos;
        arp::core::Quat rot;
        world->getBodyTransform(handle, pos, rot);
        finalY = pos.y;
        // Stop early if the box is near rest and barely moving.
        if (i > 30 && std::fabs(world->getBodyVelocity(handle).y) < 0.05f) {
            break;
        }
    }
    // The box must have fallen from y=5 down to near y=0.5 (resting on flat
    // terrain at y=0 with half-extent 0.5).
    assert(finalY < 1.5f);
    assert(finalY > 0.3f);
    // And it must be essentially at rest vertically.
    const auto v = world->getBodyVelocity(handle);
    assert(std::fabs(v.y) < 0.2f);

    world->destroyBody(handle);
}

void static_body_does_not_fall()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 32;
    cfg.maxBodyPairs = 32;
    cfg.maxContactConstraints = 16;
    assert(world->init(cfg));

    arp::physics::RigidBodyDesc bodyDesc;
    bodyDesc.motionType = arp::physics::BodyMotionType::Static;
    bodyDesc.layer = arp::physics::CollisionLayer::Static;
    bodyDesc.shape.kind = arp::physics::ColliderShapeKind::Box;
    bodyDesc.shape.halfExtents = arp::core::Vec3{1.0f, 1.0f, 1.0f};

    const auto handle = world->createBody(
        bodyDesc, arp::core::Vec3{0.0f, 10.0f, 0.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    assert(handle.valid());

    for (int i = 0; i < 30; ++i) {
        world->step(1.0f / 60.0f);
    }
    arp::core::Vec3 pos;
    arp::core::Quat rot;
    world->getBodyTransform(handle, pos, rot);
    // Static body must not move under gravity.
    assert(std::fabs(pos.y - 10.0f) < 0.001f);

    world->destroyBody(handle);
}

void kinematic_body_moved_explicitly()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 32;
    cfg.maxBodyPairs = 32;
    cfg.maxContactConstraints = 16;
    assert(world->init(cfg));

    arp::physics::RigidBodyDesc bodyDesc;
    bodyDesc.motionType = arp::physics::BodyMotionType::Kinematic;
    bodyDesc.layer = arp::physics::CollisionLayer::Character;
    bodyDesc.shape.kind = arp::physics::ColliderShapeKind::Box;
    bodyDesc.shape.halfExtents = arp::core::Vec3{0.5f, 0.5f, 0.5f};

    const auto handle = world->createBody(
        bodyDesc, arp::core::Vec3{0.0f, 0.0f, 0.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    assert(handle.valid());

    // Teleport the kinematic body via setBodyTransform.
    world->setBodyTransform(
        handle, arp::core::Vec3{5.0f, 3.0f, -2.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    world->step(1.0f / 60.0f);

    arp::core::Vec3 pos;
    arp::core::Quat rot;
    world->getBodyTransform(handle, pos, rot);
    assert(std::fabs(pos.x - 5.0f) < 0.01f);
    assert(std::fabs(pos.y - 3.0f) < 0.01f);
    assert(std::fabs(pos.z + 2.0f) < 0.01f);

    world->destroyBody(handle);
}

// A simple event counter listener to verify the contact bridge delivers
// L2 events from native Jolt callbacks.
struct CountingListener : arp::physics::PhysicsEventListener {
    int beginCount = 0;
    int persistCount = 0;
    int endCount = 0;

    void onPhysicsEvent(const arp::physics::PhysicsContactEvent& event) override
    {
        switch (event.kind) {
        case arp::physics::PhysicsEventKind::ContactBegin:
            ++beginCount;
            break;
        case arp::physics::PhysicsEventKind::ContactPersist:
            ++persistCount;
            break;
        case arp::physics::PhysicsEventKind::ContactEnd:
            ++endCount;
            break;
        default:
            break;
        }
    }
};

void contact_listener_receives_events()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 32;
    cfg.maxBodyPairs = 32;
    cfg.maxContactConstraints = 16;
    cfg.collisionSteps = 2;
    cfg.terrainGridResolution = 16;
    assert(world->init(cfg));

    arp::physics::TerrainPhysicsSource terrain{};
    terrain.sampleFn = &flatTerrainSample;
    assert(world->setTerrainHeightField(terrain, 10.0f));

    arp::physics::RigidBodyDesc bodyDesc;
    bodyDesc.motionType = arp::physics::BodyMotionType::Dynamic;
    bodyDesc.layer = arp::physics::CollisionLayer::Dynamic;
    bodyDesc.shape.kind = arp::physics::ColliderShapeKind::Box;
    bodyDesc.shape.halfExtents = arp::core::Vec3{0.5f, 0.5f, 0.5f};
    bodyDesc.mass = 1.0f;

    const auto handle = world->createBody(
        bodyDesc, arp::core::Vec3{0.0f, 3.0f, 0.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    assert(handle.valid());

    CountingListener listener;
    world->registerEventListener(handle, &listener);

    // Step enough for the box to fall and contact the terrain.
    for (int i = 0; i < 120; ++i) {
        world->step(1.0f / 60.0f);
    }
    // The box must have contacted the terrain at least once.
    assert(listener.beginCount >= 1);
    // Once resting, persistent contacts should accrue.
    assert(listener.persistCount >= 1);
}

void wave_terrain_body_rests_near_analytic_height()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 32;
    cfg.maxBodyPairs = 32;
    cfg.maxContactConstraints = 16;
    cfg.collisionSteps = 2;
    cfg.terrainGridResolution = 32;
    assert(world->init(cfg));

    arp::physics::TerrainPhysicsSource terrain{};
    terrain.sampleFn = &waveTerrainSample;
    // Use a small half-extent so the wave (period 2*pi) is well sampled.
    assert(world->setTerrainHeightField(terrain, 4.0f));

    arp::physics::RigidBodyDesc bodyDesc;
    bodyDesc.motionType = arp::physics::BodyMotionType::Dynamic;
    bodyDesc.layer = arp::physics::CollisionLayer::Dynamic;
    bodyDesc.shape.kind = arp::physics::ColliderShapeKind::Sphere;
    bodyDesc.shape.radius = 0.3f;
    bodyDesc.mass = 1.0f;
    bodyDesc.friction = 0.9f;

    // Drop a sphere at x=0 where the analytic height is sin(0)*0.5 = 0.
    const auto handle = world->createBody(
        bodyDesc, arp::core::Vec3{0.0f, 4.0f, 0.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    assert(handle.valid());

    constexpr float kStep = 1.0f / 60.0f;
    for (int i = 0; i < 300; ++i) {
        world->step(kStep);
        if (i > 60 && std::fabs(world->getBodyVelocity(handle).y) < 0.05f) {
            break;
        }
    }
    arp::core::Vec3 pos;
    arp::core::Quat rot;
    world->getBodyTransform(handle, pos, rot);
    // The sphere must have descended from y=4 and now be near the terrain
    // surface (within ~1.5m of the analytic height at its landing XZ). A
    // sphere on a sloped wave terrain rolls toward a local trough, so we do
    // not assert an exact rest height; we assert it is no longer airborne and
    // is close to the terrain envelope.
    const float terrainHeightAtBall = waveTerrainSample(nullptr, pos.x, pos.z);
    const float heightAboveTerrain = pos.y - terrainHeightAtBall - 0.3f; // 0.3 = radius
    assert(heightAboveTerrain < 1.0f);
    assert(pos.y < 3.5f); // must have fallen significantly from y=4

    world->destroyBody(handle);
}

void baked_surface_uses_explicit_bounds_and_rejects_incomplete_reload()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world);

    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 32;
    cfg.maxBodyPairs = 32;
    cfg.maxContactConstraints = 16;
    // 256 exposes endpoint rounding for bounds such as [-300, 300]. The
    // physics sampler must query the declared endpoint exactly, not a value
    // one ULP beyond it.
    cfg.terrainGridResolution = 256;
    assert(world->init(cfg));

    auto completeSurface = arp::core::makeShared<BakedSurface>(true);
    arp::physics::TerrainSurfacePhysicsSource completeSource(completeSurface);
    assert(completeSource.valid());
    const auto explicitBounds = completeSource.bounds();
    assert(explicitBounds.minX == -300.0f && explicitBounds.maxZ == 300.0f);
    assert(world->setTerrainHeightField(completeSource.source(), explicitBounds));

    auto incompleteSurface = arp::core::makeShared<BakedSurface>(false);
    arp::physics::TerrainSurfacePhysicsSource incompleteSource(incompleteSurface);
    assert(!world->setTerrainHeightField(incompleteSource.source(), incompleteSource.bounds()));
    assert(world->lastDiagnostic().code
        == arp::physics::PhysicsWorldDiagnostic::Code::TerrainBuildFailed);

    // The failed generation cannot remove the already-published collider.
    arp::physics::RigidBodyDesc bodyDesc;
    bodyDesc.motionType = arp::physics::BodyMotionType::Dynamic;
    bodyDesc.layer = arp::physics::CollisionLayer::Dynamic;
    bodyDesc.shape.kind = arp::physics::ColliderShapeKind::Box;
    bodyDesc.shape.halfExtents = arp::core::Vec3{0.5f, 0.5f, 0.5f};
    bodyDesc.mass = 1.0f;
    const auto body = world->createBody(
        bodyDesc, arp::core::Vec3{12.0f, 4.0f, -1.0f}, arp::core::Quat{0.0f, 0.0f, 0.0f, 1.0f});
    assert(body.valid());
    for (int i = 0; i < 240; ++i) {
        assert(world->step(1.0f / 60.0f));
    }
    arp::core::Vec3 position;
    arp::core::Quat rotation;
    world->getBodyTransform(body, position, rotation);
    assert(position.y > 1.2f && position.y < 2.0f);
    world->destroyBody(body);
}

void scene_queries_and_shape_replacement_use_native_payloads()
{
    using namespace arp::physics;
    PhysicsWorldDiagnostic diag;
    auto world = createPhysicsWorld(diag);
    assert(world);
    PhysicsWorldConfig cfg;
    cfg.maxBodies = 32;
    cfg.maxBodyPairs = 32;
    cfg.maxContactConstraints = 16;
    assert(world->init(cfg));

    RigidBodyDesc obstacle;
    obstacle.motionType = BodyMotionType::Static;
    obstacle.layer = CollisionLayer::Static;
    obstacle.shape.kind = ColliderShapeKind::Box;
    obstacle.shape.halfExtents = {0.5f, 0.5f, 0.5f};
    const auto obstacleHandle =
        world->createBody(obstacle, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
    assert(obstacleHandle.valid());

    RaycastQuery ray;
    ray.origin = {0.0f, 3.0f, 0.0f};
    ray.direction = {0.0f, -1.0f, 0.0f};
    ray.distance = 5.0f;
    RaycastHit rayHit;
    assert(world->raycast(ray, rayHit));
    assert(rayHit.body == obstacleHandle);
    assert(rayHit.fraction > 0.0f && rayHit.fraction < 1.0f);
    assert(rayHit.normal.y > 0.9f);

    ray.filter.ignoredBody = obstacleHandle;
    assert(!world->raycast(ray, rayHit));
    assert(world->lastDiagnostic().code == PhysicsWorldDiagnostic::Code::None);

    CapsuleOverlapQuery overlap;
    overlap.center = {0.0f, 1.0f, 0.0f};
    overlap.radius = 0.4f;
    overlap.halfHeight = 0.55f;
    assert(world->overlapsCapsule(overlap));
    overlap.center.x = 4.0f;
    assert(!world->overlapsCapsule(overlap));

    CapsuleSweepQuery sweep;
    sweep.center = {-3.0f, 1.0f, 0.0f};
    sweep.radius = 0.4f;
    sweep.halfHeight = 0.55f;
    sweep.displacement = {6.0f, 0.0f, 0.0f};
    CapsuleSweepHit sweepHit;
    assert(world->sweepCapsule(sweep, sweepHit));
    assert(sweepHit.body == obstacleHandle);
    assert(sweepHit.fraction > 0.0f && sweepHit.fraction < 1.0f);

    RigidBodyDesc character;
    character.motionType = BodyMotionType::Kinematic;
    character.layer = CollisionLayer::Character;
    character.shape.kind = ColliderShapeKind::Capsule;
    character.shape.radius = 0.4f;
    character.shape.height = 0.9f;
    const auto characterHandle =
        world->createBody(character, {4.0f, 1.3f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
    assert(characterHandle.valid());
    auto crouchShape = character.shape;
    crouchShape.height = 0.55f;
    assert(world->setBodyShape(characterHandle, crouchShape));
    auto invalidShape = crouchShape;
    invalidShape.radius = 0.0f;
    assert(!world->setBodyShape(characterHandle, invalidShape));
    assert(world->lastDiagnostic().code == PhysicsWorldDiagnostic::Code::ShapeLoweringFailed);

    world->destroyBody(characterHandle);
    world->destroyBody(obstacleHandle);
}

} // namespace

int main()
{
    world_init_succeeds();
    dynamic_box_falls_and_rests_on_flat_terrain();
    static_body_does_not_fall();
    kinematic_body_moved_explicitly();
    contact_listener_receives_events();
    wave_terrain_body_rests_near_analytic_height();
    baked_surface_uses_explicit_bounds_and_rejects_incomplete_reload();
    scene_queries_and_shape_replacement_use_native_payloads();

    std::fprintf(stderr, "physics_jolt_backend_tests: all assertions passed\n");
    return 0;
}
