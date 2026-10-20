// Physics L2 semantic-layer contract tests.
//
// Validates that the L2 physics contract (src/physics/*.h) is backend-free
// and self-consistent. These are fast unit tests; no physics world is
// created. Backend behaviour is covered by physics_jolt_backend_tests.cpp.

#include "physics/physics_module.h"
#include "physics/physics_types.h"
#include "physics/physics_world.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Negative contract: L2 public headers MUST NOT include any backend type.
// We verify this at compile time: if any L2 header transitively included a
// Jolt header, the JPH_NAMESPACE_BEGIN macro would be defined and we could
// detect it. A stronger runtime grep is intentionally avoided here to keep
// the test free of <filesystem> (which pulls an api-set DLL that is not
// reliably present on all Windows targets). The contract is additionally
// enforced by the build system: arp_physics' public headers are under
// src/physics/*.h and never include <Jolt/...> (code review + the shape
// factory is the only file that includes Jolt headers).
// ---------------------------------------------------------------------------
#if defined(JPH_NAMESPACE_BEGIN)
#error "L2 public header leaked a Jolt include into the translation unit"
#endif

// ---------------------------------------------------------------------------
// Collision-layer matrix contract (versioned, must match physics_types.cpp).
// ---------------------------------------------------------------------------
void static_layer_does_not_collide_with_static()
{
    assert(!arp::physics::layersCollide(
        arp::physics::CollisionLayer::Static, arp::physics::CollisionLayer::Static));
}

void trigger_layer_never_collides_physically()
{
    using CL = arp::physics::CollisionLayer;
    // Triggers produce events only, never physical response.
    assert(!arp::physics::layersCollide(CL::Trigger, CL::Static));
    assert(!arp::physics::layersCollide(CL::Static, CL::Trigger));
    assert(!arp::physics::layersCollide(CL::Trigger, CL::Dynamic));
    assert(!arp::physics::layersCollide(CL::Dynamic, CL::Trigger));
    assert(!arp::physics::layersCollide(CL::Trigger, CL::Character));
    assert(!arp::physics::layersCollide(CL::Trigger, CL::Trigger));
}

void dynamic_character_static_collide()
{
    using CL = arp::physics::CollisionLayer;
    assert(arp::physics::layersCollide(CL::Dynamic, CL::Static));
    assert(arp::physics::layersCollide(CL::Static, CL::Dynamic));
    assert(arp::physics::layersCollide(CL::Character, CL::Static));
    assert(arp::physics::layersCollide(CL::Static, CL::Character));
    assert(arp::physics::layersCollide(CL::Dynamic, CL::Character));
    assert(arp::physics::layersCollide(CL::Dynamic, CL::Dynamic));
}

void collision_matrix_is_symmetric()
{
    using CL = arp::physics::CollisionLayer;
    for (std::uint16_t a = 0; a < static_cast<std::uint16_t>(CL::Count); ++a) {
        for (std::uint16_t b = 0; b < static_cast<std::uint16_t>(CL::Count); ++b) {
            const auto la = static_cast<CL>(a);
            const auto lb = static_cast<CL>(b);
            assert(arp::physics::layersCollide(la, lb) == arp::physics::layersCollide(lb, la));
        }
    }
}

// ---------------------------------------------------------------------------
// Capability contract: factory now wires the Jolt backend (Phase 3 landed),
// so createPhysicsWorld() MUST succeed and report Native capability.
// ---------------------------------------------------------------------------
void factory_creates_native_backend()
{
    auto diag = arp::physics::PhysicsWorldDiagnostic{};
    auto world = arp::physics::createPhysicsWorld(diag);
    assert(world != nullptr);
    assert(diag.code == arp::physics::PhysicsWorldDiagnostic::Code::None);
}

void capability_status_matches_compile_definition()
{
    const auto cap = arp::physics::queryPhysicsCapability();
#if ARP_HAS_JOLT
    assert(cap.status == arp::physics::PhysicsBackendStatus::Native);
    assert(cap.nativeSimulation);
    assert(cap.heightFieldSupport);
    assert(cap.eventCallbacks);
#else
    assert(cap.status == arp::physics::PhysicsBackendStatus::DiagnosticFailure);
    assert(!cap.nativeSimulation);
#endif
}

// ---------------------------------------------------------------------------
// Shape desc default-state contract: defaults are explicit and bounded.
// ---------------------------------------------------------------------------
void box_desc_defaults_are_explicit()
{
    arp::physics::ColliderShapeDesc d;
    assert(d.kind == arp::physics::ColliderShapeKind::Box);
    assert(d.halfExtents.x == 0.5f && d.halfExtents.y == 0.5f && d.halfExtents.z == 0.5f);
    assert(d.radius == 0.5f);
    assert(d.height == 1.0f);
    assert(d.convexPoints.empty());
}

void rigid_body_desc_defaults_are_explicit()
{
    arp::physics::RigidBodyDesc d;
    assert(d.motionType == arp::physics::BodyMotionType::Dynamic);
    assert(d.layer == arp::physics::CollisionLayer::Dynamic);
    assert(d.mass == 1.0f);
    assert(d.friction == 0.2f);
    assert(d.restitution == 0.0f);
    assert(!d.isTrigger);
}

void invalid_handle_is_not_valid()
{
    arp::physics::PhysicsBodyHandle h;
    assert(!h.valid());
    assert(h.id == 0);
    arp::physics::PhysicsBodyHandle live{42};
    assert(live.valid());
    assert(h != live);
    assert(h == arp::physics::PhysicsBodyHandle{});
}

void scene_query_defaults_are_explicit()
{
    using namespace arp::physics;
    PhysicsQueryFilter filter;
    assert(filter.layerMask == 0x0fu);
    assert(!filter.ignoredBody.valid());
    assert(!filter.includeTriggers);

    RaycastQuery ray;
    assert(ray.direction.y == -1.0f && ray.distance == 1.0f);
    CapsuleSweepQuery sweep;
    assert(sweep.radius == 0.4f && sweep.halfHeight == 0.9f);
    CapsuleOverlapQuery overlap;
    assert(overlap.radius == 0.4f && overlap.halfHeight == 0.9f);
}

} // namespace

int main()
{
    static_layer_does_not_collide_with_static();
    trigger_layer_never_collides_physically();
    dynamic_character_static_collide();
    collision_matrix_is_symmetric();
    factory_creates_native_backend();
    capability_status_matches_compile_definition();
    box_desc_defaults_are_explicit();
    rigid_body_desc_defaults_are_explicit();
    invalid_handle_is_not_valid();
    scene_query_defaults_are_explicit();

    std::fprintf(stderr, "physics_l2_contract_tests: all assertions passed\n");
    return 0;
}
