# Physics Integration Capability Matrix

Status: **Contract Complete** + **Backend Validated (Jolt/Vulkan/MinGW)**

This document follows the AGENTS.md requirement to distinguish
`Contract Complete`, `Prototype Complete`, `Backend Validated`,
`Platform Validated`, and `Production Ready` status for every physics
integration component.

## Overview

The source package is `src/physics/`: `3d/` contains the Jolt integration
covered by this matrix, and `2d/` contains the Box2D runtime and its components.
See `docs/physics-package-organization.md` for the source migration and its
focused validation; the two runtimes retain separate targets and contracts.

JoltPhysics 5.6.0 is integrated as a static library (MinGW build) to provide
CPU-side rigid body simulation, terrain heightfield collision, and contact
event callbacks. The integration follows the AGENTS.md contract-first
architecture: L2 domain semantics are defined first, the Jolt backend lowers
them, and upper-layer code (ColliderComponent, PhysicsMovementController)
expresses L2 intent only.

## Component Status Matrix

| Component | Contract Complete | Prototype Complete | Backend Validated | Platform Validated | Production Ready |
|---|---|---|---|---|---|
| L2 Physics Types (`physics_types.h`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| L2 Physics World (`physics_world.h`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| L2 Event Listener (`physics_event_listener.h`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| Physics Module / Capability (`physics_module.h`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| Jolt Backend (`jolt_physics_world.cpp`) | ✅ | ✅ | ✅ (MinGW/Vulkan) | ❌ | ❌ |
| Jolt Shape Factory (`jolt_shape_factory.cpp`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| Jolt Contact Bridge (`jolt_contact_listener_bridge.cpp`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| Jolt Layers (`jolt_layers.h`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| ColliderComponent (`3d/collider_component.h/.cpp`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| PhysicsMovementController (`physics_movement.h/.cpp`) | ✅ | ✅ (Prototype) | ❌ | ❌ | ❌ |
| GameScene Physics Wiring (`scene.cpp`) | ✅ | ✅ | ✅ | ❌ | ❌ |
| Terrain Heightfield Physics (`setTerrainHeightField`) | ✅ | ✅ | ✅ | ❌ | ❌ |

## Test Matrix

| Test | Type | Status |
|---|---|---|
| `arp_physics_l2_contract_tests` | L2 contract (layers, factory, capability) | ✅ Pass |
| `arp_physics_jolt_backend_tests` | Backend (init, body fall, contact, terrain) | ✅ Pass |
| `arp_collider_component_contract_tests` | Component (lifecycle, events, fail-closed, kinematic) | ✅ Pass |
| `arp_physics_movement_contract_tests` | Controller (init, move, ground follow, fail-closed) | ✅ Pass |

## Capability Details

### nativeSimulation
- **Status**: Native (ARP_HAS_JOLT=1)
- **Source**: `queryPhysicsCapability().nativeSimulation` returns true when
  Jolt is compiled in (`ARP_HAS_JOLT` compile definition, set by
  `cmake/ThirdParty.cmake` when `3rd/JoltPhysics-5.6.0` exists).
- **Fallback**: DiagnosticFailure when ARP_HAS_JOLT=0.
  `createPhysicsWorld()` returns nullptr and fills the diagnostic.
  No dummy/placeholder world is produced (AGENTS.md §3: fail-closed).

### heightFieldSupport
- **Status**: Native (Jolt `HeightFieldShapeSettings`)
- **Source**: `queryPhysicsCapability().heightFieldSupport` returns true.
  The Jolt backend builds a `HeightFieldShape` from a `TerrainPhysicsSource`
  sample function on a resolution×resolution grid covering
  [-halfExtent, +halfExtent] on X/Z.
- **Contract**: The terrain physics ground matches the rendered terrain
  surface exactly (same `TerrainHeightField::sample` instance feeds both the
  mesh generator and the physics heightfield).

### eventCallbacks
- **Status**: Native (Jolt `ContactListener`)
- **Source**: `queryPhysicsCapability().eventCallbacks` returns true.
  `JoltContactBridge` implements `JPH::ContactListener` and translates
  `OnContactAdded/Persisted/Removed` into L2 `PhysicsContactEvent` structs.
  Events are delivered to `PhysicsEventListener::onPhysicsEvent()` on the
  physics thread.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Upper Layer (game, renderer, movement)                      │
│  ┌──────────────────┐  ┌──────────────────────────────────┐ │
│  │ ColliderComponent│  │ PhysicsMovementController         │ │
│  │ (3d/ Component)  │  │ (gameplay/)                      │ │
│  └────────┬─────────┘  └──────────────┬───────────────────┘ │
│           │ L2 intent                  │ L2 intent            │
│  ┌────────▼────────────────────────────▼───────────────────┐│
│  │ PhysicsWorld (L2 abstract facade, physics_world.h)       ││
│  │ PhysicsBodyHandle / RigidBodyDesc / PhysicsContactEvent  ││
│  └────────┬─────────────────────────────────────────────────┘│
│           │ lowering contract                                   │
│  ┌────────▼─────────────────────────────────────────────────┐│
│  │ Jolt Backend (src/physics/3d/backends/jolt/)                ││
│  │ JoltPhysicsWorld / JoltShapeFactory / JoltContactBridge  ││
│  └────────┬─────────────────────────────────────────────────┘│
│           │ JPH::PhysicsSystem                                  │
│  ┌────────▼─────────────────────────────────────────────────┐│
│  │ JoltPhysics 5.6.0 (3rd/, static lib, MinGW)              ││
│  └──────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────┘
```

### Layer Separation (AGENTS.md §1)
- **L2 headers** (`src/physics/3d/physics_*.h`) contain zero backend includes.
  Verified by compile-time check in `physics_l2_contract_tests.cpp`:
  `JPH_NAMESPACE_BEGIN` is never defined when only L2 headers are included.
- **Backend code** (`src/physics/3d/backends/jolt/`) is the only place that
  includes Jolt headers. It lowers L2 descriptors into native Jolt types.
- **Upper layer** (`ColliderComponent`, `PhysicsMovementController`,
  `GameScene`) holds `PhysicsWorld*` and never references Jolt types.

### Fail-Closed Contract (AGENTS.md §3)
- `createPhysicsWorld()` returns nullptr when ARP_HAS_JOLT=0.
- `ColliderComponent::init()` with null world sets
  `BackendUnavailable` diagnostic; no body is created.
- `PhysicsMovementController::init()` with null world returns false;
  the caller falls back to `MovementController` (legacy heightfield sampling).
- No dummy/placeholder body or world is ever produced.

## Memory (AGENTS.md CPU Allocator Rule)
- L2 types use `arp::core::pmr::String` / `arp::core::pmr::Vector` for
  dynamic containers (`PhysicsWorldDiagnostic::message`,
  `ColliderShapeDesc::convexPoints`, `ColliderComponent::events_`).
- `PhysicsWorld` is owned via `arp::core::UniquePtr` (returned by
  `createPhysicsWorld()`).
- Jolt's internal allocator is registered via `RegisterDefaultAllocator()`
  in `ensureJoltRegistered()` (uses Jolt's default `Allocate`/`Free` which
  route to the system allocator; Jolt does not use STL containers when
  `ENABLE_OBJECT_STREAM OFF`).

## Four-Backend Sync (AGENTS.md §5)
Physics is CPU-only and renderer-agnostic. It does not interact with the
RHI/GPU backends (Null/Vulkan/Metal/WebGPU). The four-backend sync
obligation is therefore **N/A** for physics: there is no GPU work to
synchronize, no resource state to lower, no swapchain to present. This is
documented here so the N/A is explicit, not implicit.

## Known Limitations (Prototype Status)

### PhysicsMovementController
- **Status**: Prototype Complete
- **Limitation**: Ground-follow uses a simple Y-pin heuristic rather than
  a sweep-test-based ground detection. This is adequate for flat and gently
  sloped terrain but does not handle steep slopes, overhangs, or complex
  collision geometry.
- **Legacy fallback**: `MovementController` (heightfield-sampling) remains
  the production path. `PhysicsMovementController` is opt-in.
- **Upgrade path**: Implement proper ground sweep query using
  `PhysicsWorld::castRay()` (to be added) or a dedicated character
  controller interface.

### ColliderComponent
- **Status**: Backend Validated
- **Limitation**: The component does not automatically read the owner
  GameObject's TransformComponent in onStart (the scene loop in WoCC does
  not call `Scene::update()`). Initial transform must be set explicitly
  via `ColliderComponentDesc::initialPosition/initialRotation`.
- When `Scene::update()` IS called (other games/tests), the lifecycle hooks
  (`onStart`/`onUpdate`/`onDestroy`) fire automatically and delegate to
  `init()`/`sync()`/`destroy()`.

## Build Integration

- **ThirdParty**: `cmake/ThirdParty.cmake` adds Jolt via
  `add_subdirectory(3rd/JoltPhysics-5.6.0/Build)` with all noisy options
  disabled (shared libs, warnings, LTO, debug renderer, asserts, exceptions,
  RTTI, GPU compute).
- **Module definition**: `build.py` `_auto_discover_modules()` discovers
  `src/physics/*.cpp` as the `arp_physics` library, linked to
  `arp_core`, `arp_scene`, and `arp_jolt_headers` (INTERFACE target from
  `ThirdParty.cmake`).
- **Engine DLL**: `arp_physics` is included in the engine whole-archive
  block. Jolt's static library is excluded from symbol export via
  `-Wl,--exclude-libs,libJoltd.a` (added in `cmake_gen.py`).
- **Tests**: Physics tests use `-static-libstdc++ -static-libgcc` link
  flags to avoid MinGW runtime DLL mismatch (0xc0000139 entrypoint crash).
