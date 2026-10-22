#pragma once

// L2 Physics World abstract facade (Contract Layer).
//
// PhysicsWorld is the engine's sole physics simulation surface. It is an
// abstract base class: the Jolt backend provides the only production
// implementation (JoltPhysicsWorld). Upstream code holds a PhysicsWorld*
// and never a backend concrete type (AGENTS.md §1: upper layer expresses
// L2 intent only).
//
// Lifetime contract:
//   - init() must succeed before any body/step call. Failure is fatal and
//     reported via lastDiagnostic(); no partial state is observable.
//   - Body handles are valid from createBody() until destroyBody() or
//     world destruction. Using a stale handle is a contract violation that
//     surfaces as a diagnostic (no crash, no silent no-op).
//   - step() advances the whole world by deltaSeconds. Hot-path: bounded
//     allocation, no file/IO, no per-step shader/pipeline creation
//     (AGENTS.md §6). deltaSeconds is clamped internally to a bounded max.
//
// Transform contract:
//   - Positions/rotations use core::Vec3 / core::Quat (engine math). The
//     backend converts to its native math internally. No matrix layout
//     contract is involved here (Vec3/Quat only, AGENTS.md matrix rule
//     is N/A for the physics transform interface).

#include "core/api.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "physics/3d/physics_event_listener.h"
#include "physics/3d/physics_types.h"

#include <cstdint>

namespace arp::physics {

struct PhysicsWorldDiagnostic;

/// Configuration for PhysicsWorld::init(). All values are explicit; the
/// backend reads no native defaults (AGENTS.md §4). Budgets must be bounded
/// and traced to a configured source (no magic numbers).
struct PhysicsWorldConfig {
    /// Max simultaneously existing bodies. Bounded; chosen by the game.
    std::uint32_t maxBodies = 1024;
    /// Max body mutexes (Jolt body locking granularity).
    std::uint32_t numBodyMutexes = 0;
    /// Max concurrent body pairs in contact.
    std::uint32_t maxBodyPairs = 4096;
    /// Max contact constraints resolved per step.
    std::uint32_t maxContactConstraints = 1024;
    /// Number of collision sub-steps per step() call. Higher = more stable
    /// at the cost of CPU. Must be >= 1.
    std::uint32_t collisionSteps = 1;
    /// Terrain heightfield grid resolution (samples per axis) when
    /// setTerrainHeightField is called. Bounded; the resulting grid is
    /// resolution^2 floats. Derived from world config, not a magic number.
    std::uint32_t terrainGridResolution = 256;
    /// Integration sub-steps per collision step (Jolt Update integration).
    std::uint32_t integrationSubSteps = 1;
    /// Gravity vector in world units/s^2.
    arp::core::Vec3 gravity{0.0f, -9.81f, 0.0f};
    /// CPU float-grid budget, independent of native collider allocations.
    /// The default exactly admits the default 256x256 float32 grid above.
    std::size_t maxTerrainSampleBytes = 256u * 256u * sizeof(float);
};

/// Abstract physics world. See file header for the lifetime/transform
/// contracts. Implementations live in src/physics/3d/backends/.
class ARP_API PhysicsWorld {
public:
    /// Shared L2 step bound. Hosts reject larger fixed deltas rather than
    /// dispatching scene callbacks with a delta the backend would clamp.
    static constexpr float maximumStepDeltaSeconds = 1.0f / 15.0f;

    virtual ~PhysicsWorld() = default;

    /// Initialise the world. Returns true on success. On failure the world
    /// is unusable and lastDiagnostic() describes the cause; subsequent
    /// calls return diagnostics without side effects (fail-closed).
    virtual bool init(const PhysicsWorldConfig& config) = 0;

    /// Create a rigid body at the given world transform. Returns an invalid
    /// handle (id==0) on failure; lastDiagnostic() has the cause.
    virtual PhysicsBodyHandle createBody(const RigidBodyDesc& desc,
        const arp::core::Vec3& position,
        const arp::core::Quat& rotation) = 0;

    /// Destroy a body. The handle becomes invalid. No-op (with diagnostic)
    /// if the handle is not live.
    virtual void destroyBody(PhysicsBodyHandle handle) = 0;

    /// Set a body's world transform (kinematic/static) or teleport it
    /// (dynamic). For dynamic bodies prefer velocity/impulse integration.
    virtual void setBodyTransform(PhysicsBodyHandle handle,
        const arp::core::Vec3& position,
        const arp::core::Quat& rotation) = 0;

    /// Replace a live body's collider while preserving its handle and pose.
    /// Invalid shapes fail atomically and leave the prior shape active.
    virtual bool setBodyShape(PhysicsBodyHandle handle, const ColliderShapeDesc& shape) = 0;

    /// Read a body's world transform. Out params are untouched on failure.
    virtual void getBodyTransform(PhysicsBodyHandle handle,
        arp::core::Vec3& outPosition,
        arp::core::Quat& outRotation) const = 0;

    /// Set a body's linear velocity (world space).
    virtual void setBodyVelocity(PhysicsBodyHandle handle, const arp::core::Vec3& velocity) = 0;

    /// Get a body's linear velocity.
    virtual arp::core::Vec3 getBodyVelocity(PhysicsBodyHandle handle) const = 0;

    /// Apply an impulse at the body's center of mass (dynamic only).
    virtual void addImpulse(PhysicsBodyHandle handle, const arp::core::Vec3& impulse) = 0;

    /// Closest scene-query hit. False with a clear diagnostic means no hit;
    /// invalid input or backend failure sets lastDiagnostic().
    virtual bool raycast(const RaycastQuery& query, RaycastHit& outHit) = 0;
    virtual bool sweepCapsule(const CapsuleSweepQuery& query, CapsuleSweepHit& outHit) = 0;
    virtual bool overlapsCapsule(const CapsuleOverlapQuery& query) = 0;

    /// Advance the simulation by deltaSeconds. deltaSeconds is clamped to a
    /// bounded max internally (hot-path invariant). Returns false on failure.
    virtual bool step(float deltaSeconds) = 0;

    /// Register a listener for events on a specific body. Pass nullptr to
    /// unregister. A body has at most one listener.
    virtual void registerEventListener(
        PhysicsBodyHandle handle, PhysicsEventListener* listener) = 0;

    /// Build a static terrain collider from a heightfield source over explicit
    /// asset world bounds. Implementations must validate every sample and
    /// atomically retain the prior collider when the new build fails.
    virtual bool setTerrainHeightField(
        const TerrainPhysicsSource& source, const TerrainPhysicsBounds& bounds) = 0;

    /// TerrainPhysicsLifecycle/1: the single slot's current L2 body lease.
    /// Empty before build/after clear; replaced only after a successful build.
    virtual PhysicsBodyHandle terrainHeightFieldHandle() const noexcept = 0;

    /// Requires the exact current nonempty lease. Stale/unrelated handles fail
    /// without removing any body. Must run outside simulation/query dispatch.
    virtual bool clearTerrainHeightField(PhysicsBodyHandle expected) = 0;

    /// Legacy symmetric-square compatibility entry point. Asset-backed code
    /// must use the explicit-bounds overload above.
    bool setTerrainHeightField(const TerrainPhysicsSource& source, float halfExtent)
    {
        return setTerrainHeightField(
            source, TerrainPhysicsBounds{-halfExtent, halfExtent, -halfExtent, halfExtent});
    }

    /// Last diagnostic (capability/降级/failure). Stable for telemetry.
    virtual PhysicsWorldDiagnostic lastDiagnostic() const = 0;
};

/// Diagnostic outcome. Follows the AGENTS.md fallback contract: only Native,
/// Equivalent/Prototype-Legacy (opt-in) or DiagnosticFailure are legal.
struct PhysicsWorldDiagnostic {
    enum class Code {
        None,
        BackendUnavailable,  ///< ARP_HAS_JOLT=0; no native backend.
        InitFailed,          ///< PhysicsSystem::Init failed.
        ShapeLoweringFailed, ///< ColliderShapeDesc could not be lowered.
        BodyCreationFailed,  ///< CreateBody returned null/invalid.
        TerrainBuildFailed,  ///< HeightField shape creation failed.
        StaleHandle,         ///< Handle not live.
        InvalidArgument,     ///< Desc/config invalid (e.g. zero mass dynamic).
        QueryFailed,         ///< Native scene query could not be completed.
    };
    Code code = Code::None;
    arp::core::pmr::String message;
};

/// Factory: create the best available physics world. Returns nullptr and
/// fills `outDiagnostic` when no backend is available (fail-closed). The
/// returned world is owned by the caller (core::UniquePtr).
ARP_API arp::core::UniquePtr<PhysicsWorld> createPhysicsWorld(
    PhysicsWorldDiagnostic& outDiagnostic);

} // namespace arp::physics
