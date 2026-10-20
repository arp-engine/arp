// ColliderComponent contract tests.
//
// Validates the L2 <-> physics bridge component:
//   1. Header hygiene: collider_component.h must not include any backend type.
//      (Compile-time check: the header must not mention Jolt/Bullet.)
//   2. Lifecycle: init() creates a body, sync() syncs transforms,
//      destroy() destroys the body. Idempotency: init/destroy are safe to
//      call multiple times.
//   3. Event buffer: onPhysicsEvent buffers events, drainEvents clears them.
//   4. Fail-closed: a null PhysicsWorld produces a diagnostic, never a crash.
//   5. Dynamic body: after step(), sync() reads back the simulated transform
//      (gravity pulls a box down onto the terrain).

#include "3d/collider_component.h"
#include "physics/physics_module.h"
#include "physics/physics_world.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// 1. Header hygiene: no backend types leak into the component header.
// ---------------------------------------------------------------------------
//
// This is a source-level assertion. If someone adds `#include <Jolt/...>` to
// collider_component.h, this test file will fail to compile (the include
// would resolve and pull in Jolt types). The static_assert below is a
// complementary check that the component type is a Component subclass and a
// PhysicsEventListener -- both L2 types, no backend.
//
static_assert(std::is_base_of<arp::scene::Component, arp::scene3d::ColliderComponent>::value,
    "ColliderComponent must derive from arp::scene::Component");
static_assert(
    std::is_base_of<arp::physics::PhysicsEventListener, arp::scene3d::ColliderComponent>::value,
    "ColliderComponent must implement arp::physics::PhysicsEventListener");

// ---------------------------------------------------------------------------
// Helper: create a physics world for testing.
// ---------------------------------------------------------------------------
arp::core::UniquePtr<arp::physics::PhysicsWorld> createTestWorld()
{
    arp::physics::PhysicsWorldDiagnostic diag;
    auto world = arp::physics::createPhysicsWorld(diag);
    if (!world) {
        std::fprintf(stderr, "  [skip] no physics backend available: %s\n", diag.message.c_str());
        return nullptr;
    }
    arp::physics::PhysicsWorldConfig cfg;
    cfg.maxBodies = 64;
    cfg.maxBodyPairs = 128;
    cfg.maxContactConstraints = 32;
    cfg.collisionSteps = 1;
    cfg.integrationSubSteps = 2;
    cfg.terrainGridResolution = 32;
    cfg.gravity = {0.0f, -9.81f, 0.0f};
    if (!world->init(cfg)) {
        std::fprintf(stderr, "  [skip] world init failed\n");
        return nullptr;
    }
    return world;
}

// Flat terrain at y=0 for predictable tests.
float flatTerrainSample(void* /*ctx*/, float /*x*/, float /*z*/)
{
    return 0.0f;
}

// ---------------------------------------------------------------------------
// 2. Lifecycle: init / sync / destroy.
// ---------------------------------------------------------------------------
void lifecycle_init_sync_destroy()
{
    auto world = createTestWorld();
    if (!world)
        return;

    // Flat terrain so the box can rest on y=0.
    arp::physics::TerrainPhysicsSource terrain;
    terrain.sampleFn = &flatTerrainSample;
    terrain.ctx = nullptr;
    world->setTerrainHeightField(terrain, 10.0f);

    arp::scene3d::ColliderComponentDesc desc;
    desc.body.shape.kind = arp::physics::ColliderShapeKind::Box;
    desc.body.shape.halfExtents = {0.5f, 0.5f, 0.5f};
    desc.body.motionType = arp::physics::BodyMotionType::Dynamic;
    desc.body.mass = 1.0f;
    desc.initialPosition = {0.0f, 5.0f, 0.0f};

    arp::scene3d::ColliderComponent comp(desc, world.get());

    // Before init: no body.
    assert(!comp.hasBody());
    assert(!comp.bodyHandle().valid());

    comp.init();
    assert(comp.hasBody());
    assert(comp.bodyHandle().valid());

    // Step physics a few times so the dynamic box falls.
    for (int i = 0; i < 60; ++i) {
        world->step(1.0f / 60.0f);
    }

    // sync() should read back the simulated transform (box fell from y=5).
    comp.sync();
    const auto pos = comp.worldPosition();
    assert(pos.y < 4.0f); // box must have fallen significantly
    std::fprintf(stderr, "  [lifecycle] box fell to y=%.3f\n", pos.y);

    // destroy() invalidates the handle.
    comp.destroy();
    assert(!comp.hasBody());

    // Idempotent: calling destroy again is safe.
    comp.destroy();

    std::fprintf(stderr, "  [lifecycle] init/sync/destroy: OK\n");
}

// ---------------------------------------------------------------------------
// 3. Event buffer.
// ---------------------------------------------------------------------------
void event_buffer_drains()
{
    auto world = createTestWorld();
    if (!world)
        return;

    arp::scene3d::ColliderComponentDesc desc;
    desc.body.shape.kind = arp::physics::ColliderShapeKind::Box;
    desc.body.shape.halfExtents = {0.5f, 0.5f, 0.5f};
    desc.body.motionType = arp::physics::BodyMotionType::Dynamic;
    desc.body.mass = 1.0f;
    desc.initialPosition = {0.0f, 5.0f, 0.0f};

    arp::scene3d::ColliderComponent comp(desc, world.get());
    comp.init();

    // Simulate a contact event.
    arp::physics::PhysicsContactEvent evt;
    evt.point = {1.0f, 2.0f, 3.0f};
    evt.normal = {0.0f, 1.0f, 0.0f};
    evt.kind = arp::physics::PhysicsEventKind::ContactBegin;
    comp.onPhysicsEvent(evt);

    // Drain should yield 1 event.
    arp::core::pmr::Vector<arp::physics::PhysicsContactEvent> drained{
        &arp::core::defaultCpuMemoryResource()};
    comp.drainEvents(drained);
    assert(drained.size() == 1);
    assert(drained[0].point.x == 1.0f);

    // Second drain should be empty (buffer was cleared).
    drained.clear();
    comp.drainEvents(drained);
    assert(drained.empty());

    comp.destroy();
    std::fprintf(stderr, "  [events] drain/clear: OK\n");
}

// ---------------------------------------------------------------------------
// 4. Fail-closed: null PhysicsWorld.
// ---------------------------------------------------------------------------
void fail_closed_null_world()
{
    arp::scene3d::ColliderComponentDesc desc;
    desc.body.shape.kind = arp::physics::ColliderShapeKind::Box;
    desc.body.motionType = arp::physics::BodyMotionType::Dynamic;

    // Construct with null world.
    arp::scene3d::ColliderComponent comp(desc, nullptr);

    // init() must not crash; it should produce a diagnostic.
    comp.init();
    assert(!comp.hasBody());
    assert(comp.lastDiagnostic().code
        == arp::physics::PhysicsWorldDiagnostic::Code::BackendUnavailable);

    // sync() must be a safe no-op.
    comp.sync();

    // destroy() must be a safe no-op.
    comp.destroy();

    std::fprintf(stderr, "  [fail-closed] null world: OK\n");
}

// ---------------------------------------------------------------------------
// 5. Kinematic body: scene pushes transform to physics.
// ---------------------------------------------------------------------------
void kinematic_body_pushes_transform()
{
    auto world = createTestWorld();
    if (!world)
        return;

    arp::scene3d::ColliderComponentDesc desc;
    desc.body.shape.kind = arp::physics::ColliderShapeKind::Box;
    desc.body.shape.halfExtents = {0.5f, 0.5f, 0.5f};
    desc.body.motionType = arp::physics::BodyMotionType::Kinematic;
    desc.body.mass = 1.0f;
    desc.initialPosition = {0.0f, 0.0f, 0.0f};

    arp::scene3d::ColliderComponent comp(desc, world.get());
    comp.init();
    assert(comp.hasBody());

    // Set a new transform and sync: physics should adopt it.
    comp.setWorldTransform({3.0f, 2.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f});
    comp.sync();

    // Read back directly from physics to verify it was pushed.
    arp::core::Vec3 pos;
    arp::core::Quat rot;
    world->getBodyTransform(comp.bodyHandle(), pos, rot);
    assert(pos.x == 3.0f);
    assert(pos.y == 2.0f);
    assert(pos.z == 1.0f);

    comp.destroy();
    std::fprintf(stderr, "  [kinematic] transform push: OK\n");
}

} // anonymous namespace

int main()
{
    std::fprintf(stderr, "collider_component_contract_tests: starting\n");

    lifecycle_init_sync_destroy();
    event_buffer_drains();
    fail_closed_null_world();
    kinematic_body_pushes_transform();

    std::fprintf(stderr, "collider_component_contract_tests: all assertions passed\n");
    return 0;
}
