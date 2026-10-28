#pragma once

// ColliderComponent - 3D scene bridge to the physics world.
//
// This component is the single ECS-level integration point between the scene
// graph and the L2 physics layer. It owns no native backend types (no Jolt,
// no Bullet): it holds an L2 RigidBodyDesc and a non-owning PhysicsWorld*,
// and drives body create/destroy/transform-sync through the PhysicsWorld
// abstract facade (AGENTS.md §1: upper layer expresses L2 intent only).
//
// Lifetime:
//   - onStart: creates the physics body at the component's initial transform.
//   - onUpdate: syncs transforms. For Dynamic bodies, physics is authoritative
//     -> the component reads the body's transform back and stores it for the
//     scene to pick up via worldPosition()/worldRotation(). For Kinematic
//     bodies, the scene is authoritative -> the component pushes the stored
//     transform into physics. Static bodies are set once at creation.
//   - onDestroy: destroys the physics body (fail-closed: stale handles
//     surface as diagnostics, never crash).
//
// Event listening:
//   - The component implements PhysicsEventListener so gameplay code can
//     query accumulated contact events each frame via drainEvents().
//   - Events are buffered in a core-allocated vector (bounded by the number
//     of contacts in a single step; drained each frame by gameplay).
//
// Memory: dynamic containers use core::pmr aliases (AGENTS.md allocator rule).

#include "core/api.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "physics/3d/physics_event_listener.h"
#include "physics/3d/physics_types.h"
#include "physics/3d/physics_world.h"
#include "reflection/reflection_macros.h"
#include "scene/component.h"

#include <cstdint>

namespace arp::scene3d {

/// Descriptor for a ColliderComponent. Carries the L2 rigid body descriptor
/// plus the initial world transform (set by the scene assembler before the
/// component's onStart fires).
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct ColliderComponentDesc {
    APROP(Serialize, Editor)
    arp::physics::RigidBodyDesc body;
    /// Initial world-space position. Must be set before onStart; if left at
    /// default (origin) the body spawns at world origin.
    APROP(Runtime)
    arp::core::Vec3 initialPosition{0.0f, 0.0f, 0.0f};
    /// Initial world-space rotation (identity quaternion by default).
    APROP(Runtime)
    arp::core::Quat initialRotation{0.0f, 0.0f, 0.0f, 1.0f};
};

/// ECS component that bridges a scene-graph GameObject to a physics rigid body.
///
/// The component does NOT hold any backend type. It talks to PhysicsWorld (the
/// L2 abstract facade) exclusively. The PhysicsWorld pointer is injected at
/// construction time and must outlive the component (typically owned by the
/// scene/game-session).
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class ARP_API ColliderComponent final : public arp::scene::Component,
                                        public arp::physics::PhysicsEventListener {
public:
    const char* scriptTypeName() const override { return "ColliderComponent"; }

    ColliderComponent();
    explicit ColliderComponent(ColliderComponentDesc desc, arp::physics::PhysicsWorld* world);
    ~ColliderComponent() override;

    AFUNC()

    std::uint64_t component_id() const { return id().value(); }
    AFUNC()

    std::uint64_t owner_id() const { return ownerId().value(); }
    AFUNC()

    bool enabled() const { return Component::enabled(); }
    AFUNC()

    void set_enabled(bool value) { Component::setEnabled(value); }

    const ColliderComponentDesc& desc() const { return desc_; }

    void setDesc(ColliderComponentDesc desc) { desc_ = std::move(desc); }

    arp::reflection::object editorAuthoringProperties() const override
    {
        return arp::reflection::object(desc_);
    }

    bool applyEditorAuthoringProperties(const arp::reflection::object& value) override
    {
        if (hasBody() || !value.is<ColliderComponentDesc>())
            return false;
        setDesc(value.cast<const ColliderComponentDesc&>());
        return true;
    }

    AFUNC()

    ColliderComponentDesc script_desc() const { return desc_; }
    AFUNC()

    void set_script_desc(const ColliderComponentDesc& value) { desc_ = value; }

    // -----------------------------------------------------------------
    // Component lifecycle (called by Scene::update).
    //
    // These fire when the host scene drives Scene::update(). In game loops
    // that do NOT call Scene::update() (e.g. WoCC's explicit per-system
    // update), use init()/sync()/destroy() directly instead.
    // -----------------------------------------------------------------
    void onStart(const arp::scene::SceneUpdateContext& ctx) override;
    void onUpdate(const arp::scene::SceneUpdateContext& ctx) override;
    void onDestroy(const arp::scene::SceneUpdateContext& ctx) override;

    // -----------------------------------------------------------------
    // Explicit lifecycle (for game loops that do not call Scene::update).
    // These are the primary entry points when the host application drives
    // updates per-system rather than via the scene graph traversal.
    // -----------------------------------------------------------------

    /// Create the physics body at the stored initial transform. Idempotent:
    /// if the body already exists, returns immediately. Must be called after
    /// the component is attached and before sync().
    void init();

    /// Per-frame transform sync. For Dynamic bodies: reads the simulated
    /// transform back from physics. For Kinematic bodies: pushes the stored
    /// transform into physics. For Static: no-op. Safe to call before init()
    /// (returns immediately if the body is not live).
    void sync();

    /// Destroy the physics body. Safe to call multiple times. Called
    /// automatically by onDestroy and the destructor.
    void destroy();

    // -----------------------------------------------------------------
    // PhysicsEventListener (called on the physics thread by the backend).
    // Buffers events; gameplay drains them each frame.
    // -----------------------------------------------------------------
    void onPhysicsEvent(const arp::physics::PhysicsContactEvent& event) override;

    // -----------------------------------------------------------------
    // Transform authority.
    // -----------------------------------------------------------------

    /// For Dynamic bodies: the world-space position read back from physics
    /// during the last onUpdate. For Kinematic bodies: the position the
    /// scene wants physics to adopt (set by gameplay before onUpdate).
    arp::core::Vec3 worldPosition() const { return worldPosition_; }

    arp::core::Quat worldRotation() const { return worldRotation_; }

    /// Set the transform the component will push into physics (Kinematic) or
    /// teleport to (Dynamic). Called by gameplay/movement code each frame.
    void setWorldTransform(arp::core::Vec3 position, arp::core::Quat rotation);

    /// Set the body's linear velocity (world space). Dynamic only; no-op for
    /// Static/Kinematic (surfaces as a diagnostic, not a crash).
    void setLinearVelocity(arp::core::Vec3 velocity);

    /// Get the body's linear velocity (world space). Returns zero if the
    /// body is not live or not dynamic.
    arp::core::Vec3 linearVelocity() const;

    /// Apply an impulse at the body's center of mass (Dynamic only).
    void applyImpulse(arp::core::Vec3 impulse);

    AFUNC()

    arp::core::Vec3 world_position() const { return worldPosition(); }
    AFUNC()

    arp::core::Quat world_rotation() const { return worldRotation(); }
    AFUNC()

    void set_world_transform(arp::core::Vec3 position, arp::core::Quat rotation)
    {
        setWorldTransform(position, rotation);
    }
    AFUNC()

    void set_linear_velocity(arp::core::Vec3 velocity) { setLinearVelocity(velocity); }
    AFUNC()

    void apply_impulse(arp::core::Vec3 impulse) { applyImpulse(impulse); }
    AFUNC()

    bool has_body() const { return hasBody(); }
    AFUNC()

    void initialize() { init(); }
    AFUNC()

    void synchronize() { sync(); }
    AFUNC()

    void release() { destroy(); }

    // -----------------------------------------------------------------
    // Event buffer (gameplay queries).
    // -----------------------------------------------------------------

    /// Drain buffered contact events into `out`. Clears the internal buffer.
    /// Called once per frame by gameplay; events not drained are discarded
    /// on the next step to keep the buffer bounded.
    void drainEvents(arp::core::pmr::Vector<arp::physics::PhysicsContactEvent>& out);

    /// True if the physics body is live (created and not yet destroyed).
    bool hasBody() const { return bodyHandle_.valid(); }

    /// The opaque L2 handle (invalid before onStart or after onDestroy).
    arp::physics::PhysicsBodyHandle bodyHandle() const { return bodyHandle_; }

    /// Last diagnostic from the physics world (for telemetry / fail-closed).
    arp::physics::PhysicsWorldDiagnostic lastDiagnostic() const { return lastDiagnostic_; }

private:
    ColliderComponentDesc desc_;
    arp::physics::PhysicsWorld* world_ = nullptr;
    arp::physics::PhysicsBodyHandle bodyHandle_;
    arp::physics::PhysicsWorldDiagnostic lastDiagnostic_;

    /// Transform authority. For Dynamic: written by onUpdate (read-back from
    /// physics). For Kinematic: written by setWorldTransform (gameplay), read
    /// by onUpdate (push to physics).
    arp::core::Vec3 worldPosition_{0.0f, 0.0f, 0.0f};
    arp::core::Quat worldRotation_{0.0f, 0.0f, 0.0f, 1.0f};

    /// Buffered contact events (bounded: drained each frame by gameplay).
    /// Uses the default CPU memory resource; capacity grows with contact
    /// count per step but is reset on drain.
    arp::core::pmr::Vector<arp::physics::PhysicsContactEvent> events_{
        &arp::core::defaultCpuMemoryResource()};
};

} // namespace arp::scene3d
