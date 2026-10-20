#include "3d/collider_component.h"

namespace arp::scene3d {

ColliderComponent::ColliderComponent() : ColliderComponent(ColliderComponentDesc{}, nullptr)
{}

ColliderComponent::ColliderComponent(ColliderComponentDesc desc, arp::physics::PhysicsWorld* world)
    : desc_(desc), world_(world), worldPosition_(desc.initialPosition),
      worldRotation_(desc.initialRotation)
{}

ColliderComponent::~ColliderComponent()
{
    // If the component is destroyed without destroy() being called (e.g. scene
    // teardown without a final update), clean up the body defensively. This
    // is NOT the primary destroy path: destroy() is. But we must not leak a
    // native body if the scene is torn down abruptly.
    destroy();
}

void ColliderComponent::onStart(const arp::scene::SceneUpdateContext& /*ctx*/)
{
    init();
}

void ColliderComponent::onUpdate(const arp::scene::SceneUpdateContext& /*ctx*/)
{
    sync();
}

void ColliderComponent::onDestroy(const arp::scene::SceneUpdateContext& /*ctx*/)
{
    destroy();
}

void ColliderComponent::init()
{
    if (bodyHandle_.valid()) {
        return; // Idempotent: body already created.
    }
    if (world_ == nullptr) {
        lastDiagnostic_.code = arp::physics::PhysicsWorldDiagnostic::Code::BackendUnavailable;
        lastDiagnostic_.message = arp::core::pmr::String(
            "ColliderComponent: no PhysicsWorld bound", &arp::core::defaultCpuMemoryResource());
        return;
    }

    bodyHandle_ = world_->createBody(desc_.body, worldPosition_, worldRotation_);

    if (!bodyHandle_.valid()) {
        lastDiagnostic_ = world_->lastDiagnostic();
        return;
    }

    // Register self as the event listener for this body. The component
    // implements PhysicsEventListener; the backend bridges native contact
    // callbacks into onPhysicsEvent().
    world_->registerEventListener(bodyHandle_, this);
}

void ColliderComponent::sync()
{
    if (!bodyHandle_.valid() || world_ == nullptr) {
        return;
    }

    switch (desc_.body.motionType) {
    case arp::physics::BodyMotionType::Dynamic: {
        // Physics is authoritative: read back the simulated transform.
        arp::core::Vec3 pos;
        arp::core::Quat rot;
        world_->getBodyTransform(bodyHandle_, pos, rot);
        worldPosition_ = pos;
        worldRotation_ = rot;
        break;
    }
    case arp::physics::BodyMotionType::Kinematic: {
        // Scene is authoritative: push the transform set by gameplay.
        world_->setBodyTransform(bodyHandle_, worldPosition_, worldRotation_);
        break;
    }
    case arp::physics::BodyMotionType::Static:
        // Static bodies do not move after creation. No per-frame work.
        break;
    }
}

void ColliderComponent::destroy()
{
    if (bodyHandle_.valid() && world_ != nullptr) {
        // Unregister the listener first to prevent the backend from delivering
        // events to a body that is about to be destroyed.
        world_->registerEventListener(bodyHandle_, nullptr);
        world_->destroyBody(bodyHandle_);
        bodyHandle_ = {};
    }
}

void ColliderComponent::onPhysicsEvent(const arp::physics::PhysicsContactEvent& event)
{
    // Buffer the event. The buffer is bounded by the number of contacts in a
    // single physics step; gameplay drains it each frame via drainEvents().
    events_.push_back(event);
}

void ColliderComponent::setWorldTransform(arp::core::Vec3 position, arp::core::Quat rotation)
{
    worldPosition_ = position;
    worldRotation_ = rotation;
}

void ColliderComponent::setLinearVelocity(arp::core::Vec3 velocity)
{
    if (!bodyHandle_.valid() || world_ == nullptr) {
        return;
    }
    world_->setBodyVelocity(bodyHandle_, velocity);
}

arp::core::Vec3 ColliderComponent::linearVelocity() const
{
    if (!bodyHandle_.valid() || world_ == nullptr) {
        return {0.0f, 0.0f, 0.0f};
    }
    return world_->getBodyVelocity(bodyHandle_);
}

void ColliderComponent::applyImpulse(arp::core::Vec3 impulse)
{
    if (!bodyHandle_.valid() || world_ == nullptr) {
        return;
    }
    world_->addImpulse(bodyHandle_, impulse);
}

void ColliderComponent::drainEvents(arp::core::pmr::Vector<arp::physics::PhysicsContactEvent>& out)
{
    // Move buffered events into the caller's vector, then clear. This keeps
    // the internal buffer bounded: it never holds more than one step's worth
    // of contacts across a frame boundary (gameplay drains every frame).
    for (auto& evt : events_) {
        out.push_back(evt);
    }
    events_.clear();
}

} // namespace arp::scene3d
