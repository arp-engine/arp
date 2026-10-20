#pragma once

// Physics module entry point and capability contract.
//
// This header is the only physics-layer symbol the rest of the engine learns
// about at link time before the L2 PhysicsWorld abstraction (see
// physics_world.h). It exposes a deterministic capability query so that
// upstream code can branch on whether a real physics backend is available,
// following the AGENTS.md "capability is the single source of truth" rule:
//
//   - ARP_HAS_JOLT=1 -> native CPU simulation available (Backend Validated).
//   - ARP_HAS_JOLT=0 -> no backend; createPhysicsWorld() must fail closed
//     with a DiagnosticFailure. No dummy/silent-fallback world is produced.
//
// The physics layer is renderer-agnostic and CPU-only: it does not interact
// with the RHI/GPU backends (Null/Vulkan/Metal/WebGPU). The four-backend
// sync obligation is therefore N/A for physics and documented in
// PHYSICS_CAPABILITY_MATRIX.md.

#include "core/api.h"

namespace arp::physics {

/// Status of the physics backend, following the AGENTS.md fallback contract
/// (Native / Equivalent / Prototype-Legacy / DiagnosticFailure are the only
/// legal outcomes; "best effort" is forbidden).
enum class PhysicsBackendStatus {
    Native,            ///< Real Jolt CPU simulation is available and tested.
    DiagnosticFailure, ///< No backend present; world creation must fail.
};

/// Deterministic capability snapshot. Queried at runtime; the same value is
/// reflected by the ARP_HAS_JOLT compile definition so C++ and build config
/// cannot disagree.
struct ARP_API PhysicsModuleCapability {
    /// True when a native physics backend (Jolt) is compiled in.
    bool nativeSimulation = false;
    /// True when HeightFieldShape terrain colliders are supported.
    bool heightFieldSupport = false;
    /// True when ContactListener event callbacks are wired.
    bool eventCallbacks = false;
    /// Aggregate status label (Native or DiagnosticFailure).
    PhysicsBackendStatus status = PhysicsBackendStatus::DiagnosticFailure;
};

/// Query the physics backend capability. Pure and deterministic; safe to call
/// before any PhysicsWorld is created.
ARP_API PhysicsModuleCapability queryPhysicsCapability();

/// Human-readable status label for telemetry / diagnostics.
ARP_API const char* physicsBackendStatusLabel();

} // namespace arp::physics
