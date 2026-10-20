#include "physics/physics_module.h"

#include "physics/physics_world.h"

#if !defined(ARP_HAS_JOLT)
#define ARP_HAS_JOLT 0
#endif

#if ARP_HAS_JOLT
#include "physics/backends/jolt/jolt_physics_world.h"
#endif

namespace arp::physics {

PhysicsModuleCapability queryPhysicsCapability()
{
    PhysicsModuleCapability cap;
#if ARP_HAS_JOLT
    cap.nativeSimulation = true;
    cap.heightFieldSupport = true;
    cap.eventCallbacks = true;
    cap.status = PhysicsBackendStatus::Native;
#else
    cap.nativeSimulation = false;
    cap.heightFieldSupport = false;
    cap.eventCallbacks = false;
    cap.status = PhysicsBackendStatus::DiagnosticFailure;
#endif
    return cap;
}

const char* physicsBackendStatusLabel()
{
    const auto cap = queryPhysicsCapability();
    return cap.status == PhysicsBackendStatus::Native ? "Native" : "DiagnosticFailure";
}

// Factory. When ARP_HAS_JOLT=1 the Jolt backend is created (Native). When
// ARP_HAS_JOLT=0 there is no backend, so the factory fails closed (returns
// nullptr + BackendUnavailable). No dummy world is ever produced
// (AGENTS.md §3: dummy/placeholder cannot substitute for Native/Equivalent
// semantics on the production path).
arp::core::UniquePtr<PhysicsWorld> createPhysicsWorld(PhysicsWorldDiagnostic& outDiagnostic)
{
#if ARP_HAS_JOLT
    return arp::physics::jolt_backend::createJoltPhysicsWorld(outDiagnostic);
#else
    outDiagnostic.code = PhysicsWorldDiagnostic::Code::BackendUnavailable;
    outDiagnostic.message = "No physics backend compiled in (ARP_HAS_JOLT=0)";
    return nullptr;
#endif
}

} // namespace arp::physics
