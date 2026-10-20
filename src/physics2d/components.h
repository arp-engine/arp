#pragma once
#include "physics2d/physics_world_2d.h"
#include "scene/component.h"
#include "scene/scene.h"

#include <stdexcept>

namespace arp::physics2d {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct RigidBody2DEditorProperties {
    APROP(Editor)
    BodyType type = BodyType::Static;
    APROP(Editor)
    core::Vec2 velocity{};
    APROP(Editor)
    float angularVelocity = 0;
    APROP(Editor)
    float gravityScale = 1;
    APROP(Editor)
    float linearDamping = 0;
    APROP(Editor)
    float angularDamping = 0;
    APROP(Editor)
    bool bullet = false;
    APROP(Editor)
    bool fixedRotation = false;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)
struct Collider2DEditorProperties {
    APROP(Editor)
    ShapeType type = ShapeType::Circle;
    APROP(Editor)
    core::Vec2 center{};
    APROP(Editor)
    core::Vec2 halfSize{};
    APROP(Editor)
    core::Vec2 pointA{};
    APROP(Editor)
    core::Vec2 pointB{};
    APROP(Editor)
    core::pmr::Vector<core::Vec2> points{&core::defaultCpuMemoryResource()};
    APROP(Editor)
    float radius = 0.5f;
    APROP(Editor)
    float density = 1.0f;
    APROP(Editor)
    float friction = 0.6f;
    APROP(Editor)
    float restitution = 0.0f;
    APROP(Editor)
    bool sensor = false;
    APROP(Editor)
    bool loop = false;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)
struct Joint2DEditorProperties {
    APROP(Editor, Doc = "sceneObject:RigidBody2DComponent:required")
    std::uint64_t bodyA = 0;
    APROP(Editor, Doc = "sceneObject:RigidBody2DComponent:required")
    std::uint64_t bodyB = 0;
    APROP(Editor)
    JointType type = JointType::Distance;
    APROP(Editor)
    core::Vec2 localAnchorA{};
    APROP(Editor)
    core::Vec2 localAnchorB{};
    APROP(Editor)
    core::Vec2 localAxisA{1, 0};
    APROP(Editor)
    float length = 1;
    APROP(Editor)
    float referenceAngle = 0;
    APROP(Editor)
    float lowerLimit = 0;
    APROP(Editor)
    float upperLimit = 0;
    APROP(Editor)
    float motorSpeed = 0;
    APROP(Editor)
    float maxMotorForce = 0;
    APROP(Editor)
    bool enableLimit = false;
    APROP(Editor)
    bool enableMotor = false;
    APROP(Editor)
    bool collideConnected = false;
};

// Components retain the world. Host owns fixed stepping, then synchronizes
// bodies into scene world transforms; hierarchy conversion stays in Scene.
// Collider dimensions are explicit world units. Visual object scale is not
// silently baked into physics; the scene-asset v1 codec separately requires unit scale.
class Collider2DComponent;
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class RigidBody2DComponent final : public scene::Component {
public:
    RigidBody2DComponent(core::SharedPtr<PhysicsWorld2D> world, BodyDesc desc)
        : world_(std::move(world)), desc_(desc)
    {}

    explicit RigidBody2DComponent(core::SharedPtr<PhysicsWorld2D> world) : world_(std::move(world))
    {}
    AFUNC()

    bool initialize_body(const BodyDesc& desc)
    {
        if (!gameObject() || handle_)
            return false;
        desc_ = desc;
        desc_.objectId = ownerId().value();
        return initialize(gameObject()->world_transform());
    }
    AFUNC()

    bool set_velocity(core::Vec2 velocity, float angular)
    {
        return handle_ && world_->setVelocity(handle_, velocity, angular);
    }
    AFUNC()

    bool set_pose(core::Vec2 position, float angle)
    {
        return handle_ && world_->setPose(handle_, position, angle);
    }
    AFUNC()

    BodyState body_state() const
    {
        BodyState result;
        if (!handle_ || !world_->state(handle_, result))
            throw std::runtime_error("ARP-PHYSICS2D-BODY-STALE");
        return result;
    }
    AFUNC()
    Collider2DComponent* add_collider(const ShapeDesc& desc);

    ~RigidBody2DComponent() override { release(); }

    const char* scriptTypeName() const override { return "RigidBody2DComponent"; }

    bool initialize(const scene::Transform& transform)
    {
        if (!desc_.objectId && gameObject())
            desc_.objectId = ownerId().value();
        if (handle_ || !world_ || !desc_.objectId || transform.scale.x != 1
            || transform.scale.y != 1 || transform.scale.z != 1 || transform.rotation.x != 0
            || transform.rotation.y != 0
            || std::abs(core::dot(transform.rotation, transform.rotation) - 1) > 1e-5f)
            return false;
        desc_.position = {transform.translation.x, transform.translation.y};
        desc_.angle = 2 * std::atan2(transform.rotation.z, transform.rotation.w);
        handle_ = world_->createBody(desc_);
        return handle_ && world_->state(handle_, current_) && (previous_ = current_, true);
    }

    bool initializeForOwner(const scene::Transform& transform, scene::GameObjectId objectId)
    {
        if (handle_ || !objectId.valid())
            return false;
        desc_.objectId = objectId.value();
        return initialize(transform);
    }

    reflection::object editorAuthoringProperties() const override
    {
        return reflection::object(RigidBody2DEditorProperties{desc_.type,
            desc_.velocity,
            desc_.angularVelocity,
            desc_.gravityScale,
            desc_.linearDamping,
            desc_.angularDamping,
            desc_.bullet,
            desc_.fixedRotation});
    }

    bool applyEditorAuthoringProperties(const reflection::object& value) override
    {
        if (!value.is<RigidBody2DEditorProperties>() || !handle_ || !world_)
            return false;
        const auto& properties = value.cast<const RigidBody2DEditorProperties&>();
        auto candidate = desc_;
        candidate.type = properties.type;
        candidate.velocity = properties.velocity;
        candidate.angularVelocity = properties.angularVelocity;
        candidate.gravityScale = properties.gravityScale;
        candidate.linearDamping = properties.linearDamping;
        candidate.angularDamping = properties.angularDamping;
        candidate.bullet = properties.bullet;
        candidate.fixedRotation = properties.fixedRotation;
        if (!world_->updateBodyProperties(handle_, candidate))
            return false;
        desc_ = candidate;
        return true;
    }

    void beforeStep() { previous_ = current_; }

    bool afterStep() { return handle_ && world_->state(handle_, current_); }

    bool synchronize(scene::Scene& scene, float interpolation)
    {
        if (!handle_ || !std::isfinite(interpolation) || interpolation < 0 || interpolation > 1)
            return false;
        auto* object = scene.findObject(ownerId());
        if (!object)
            return false;
        auto transform = object->world_transform();
        auto position =
            previous_.position + core::scale(current_.position - previous_.position, interpolation);
        const float angle = previous_.angle
            + std::remainder(current_.angle - previous_.angle, 2 * 3.14159265358979323846f)
                * interpolation;
        transform.translation.x = position.x;
        transform.translation.y = position.y;
        transform.rotation = {0, 0, std::sin(angle * .5f), std::cos(angle * .5f)};
        return scene.setWorldTransform(ownerId(), transform).ok();
    }

    const BodyDesc& desc() const { return desc_; }

    BodyHandle handle() const { return handle_; }

    const PhysicsWorld2D* world() const { return world_.get(); }

    void onDestroy(const scene::SceneUpdateContext&) override { release(); }

private:
    void release()
    {
        if (handle_) {
            world_->destroyBody(handle_);
            handle_ = {};
        }
    }

    core::SharedPtr<PhysicsWorld2D> world_;
    BodyDesc desc_;
    BodyHandle handle_;
    BodyState previous_, current_;
};
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class Collider2DComponent final : public scene::Component {
public:
    Collider2DComponent(core::SharedPtr<PhysicsWorld2D> world, BodyHandle body, ShapeDesc desc)
        : world_(std::move(world)), desc_(std::move(desc))
    {
        if (world_)
            handle_ = world_->createCollider(body, desc_);
    }

    ~Collider2DComponent() override { release(); }

    const char* scriptTypeName() const override { return "Collider2DComponent"; }

    const ShapeDesc& desc() const { return desc_; }

    ColliderHandle handle() const { return handle_; }

    reflection::object editorAuthoringProperties() const override
    {
        return reflection::object(Collider2DEditorProperties{desc_.type,
            desc_.center,
            desc_.halfSize,
            desc_.pointA,
            desc_.pointB,
            desc_.points,
            desc_.radius,
            desc_.density,
            desc_.friction,
            desc_.restitution,
            desc_.sensor,
            desc_.loop});
    }

    bool applyEditorAuthoringProperties(const reflection::object& value) override
    {
        if (!value.is<Collider2DEditorProperties>())
            return false;
        const auto& properties = value.cast<const Collider2DEditorProperties&>();
        auto candidate = desc_;
        candidate.type = properties.type;
        candidate.center = properties.center;
        candidate.halfSize = properties.halfSize;
        candidate.pointA = properties.pointA;
        candidate.pointB = properties.pointB;
        candidate.points = properties.points;
        candidate.radius = properties.radius;
        candidate.density = properties.density;
        candidate.friction = properties.friction;
        candidate.restitution = properties.restitution;
        candidate.sensor = properties.sensor;
        candidate.loop = properties.loop;
        if (handle_) {
            if (!world_)
                return false;
            const auto replacement = world_->replaceCollider(handle_, candidate);
            if (!replacement)
                return false;
            handle_ = replacement;
        }
        desc_ = std::move(candidate);
        return true;
    }

    void onDestroy(const scene::SceneUpdateContext&) override { release(); }

private:
    void release()
    {
        if (handle_) {
            world_->destroyCollider(handle_);
            handle_ = {};
        }
    }

    core::SharedPtr<PhysicsWorld2D> world_;
    ShapeDesc desc_;
    ColliderHandle handle_;
};
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class Joint2DComponent final : public scene::Component {
public:
    explicit Joint2DComponent(core::SharedPtr<PhysicsWorld2D> world) : world_(std::move(world)) {}
    AFUNC()

    reflection::object editorAuthoringProperties() const override
    {
        return reflection::object(Joint2DEditorProperties{bodyAObjectId_,
            bodyBObjectId_,
            desc_.type,
            desc_.localAnchorA,
            desc_.localAnchorB,
            desc_.localAxisA,
            desc_.length,
            desc_.referenceAngle,
            desc_.lowerLimit,
            desc_.upperLimit,
            desc_.motorSpeed,
            desc_.maxMotorForce,
            desc_.enableLimit,
            desc_.enableMotor,
            desc_.collideConnected});
    }

    bool applyEditorAuthoringProperties(const reflection::object& value) override
    {
        if (!value.is<Joint2DEditorProperties>() || !world_ || !world_->valid())
            return false;
        const auto& properties = value.cast<const Joint2DEditorProperties&>();
        auto candidate = desc_;
        candidate.type = properties.type;
        candidate.localAnchorA = properties.localAnchorA;
        candidate.localAnchorB = properties.localAnchorB;
        candidate.localAxisA = properties.localAxisA;
        candidate.length = properties.length;
        candidate.referenceAngle = properties.referenceAngle;
        candidate.lowerLimit = properties.lowerLimit;
        candidate.upperLimit = properties.upperLimit;
        candidate.motorSpeed = properties.motorSpeed;
        candidate.maxMotorForce = properties.maxMotorForce;
        candidate.enableLimit = properties.enableLimit;
        candidate.enableMotor = properties.enableMotor;
        candidate.collideConnected = properties.collideConnected;

        JointHandle candidateHandle{};
        if (properties.bodyA != 0u && properties.bodyB != 0u) {
            auto* owner = gameObject();
            auto* ownerScene = owner ? owner->scene() : nullptr;
            auto* objectA = ownerScene ? ownerScene->findObject(
                scene::GameObjectId::fromValue(properties.bodyA)) : nullptr;
            auto* objectB = ownerScene ? ownerScene->findObject(
                scene::GameObjectId::fromValue(properties.bodyB)) : nullptr;
            auto* bodyA = objectA ? objectA->getComponent<RigidBody2DComponent>() : nullptr;
            auto* bodyB = objectB ? objectB->getComponent<RigidBody2DComponent>() : nullptr;
            if (!bodyA || !bodyB || !bodyA->handle() || !bodyB->handle()
                || bodyA->world() != world_.get() || bodyB->world() != world_.get())
                return false;
            candidate.bodyA = bodyA->handle();
            candidate.bodyB = bodyB->handle();
            candidateHandle = world_->createJoint(candidate);
            if (!candidateHandle)
                return false;
        } else {
            candidate.bodyA = {};
            candidate.bodyB = {};
        }

        release();
        desc_ = candidate;
        handle_ = candidateHandle;
        bodyAObjectId_ = properties.bodyA;
        bodyBObjectId_ = properties.bodyB;
        return true;
    }

    bool initialize_joint(RigidBody2DComponent* a, RigidBody2DComponent* b, const JointDesc& desc)
    {
        if (handle_ || !world_ || !a || !b || !a->handle() || !b->handle()
            || a->world() != world_.get() || b->world() != world_.get())
            return false;
        desc_ = desc;
        desc_.bodyA = a->handle();
        desc_.bodyB = b->handle();
        handle_ = world_->createJoint(desc_);
        if (handle_) {
            bodyAObjectId_ = a->ownerId().value();
            bodyBObjectId_ = b->ownerId().value();
        }
        return bool(handle_);
    }

    Joint2DComponent(core::SharedPtr<PhysicsWorld2D> world, JointDesc desc)
        : world_(std::move(world)), desc_(desc)
    {
        if (world_)
            handle_ = world_->createJoint(desc_);
    }

    ~Joint2DComponent() override { release(); }

    const char* scriptTypeName() const override { return "Joint2DComponent"; }

    const JointDesc& desc() const { return desc_; }

    JointHandle handle() const { return handle_; }
    std::uint64_t bodyAObjectId() const { return bodyAObjectId_; }
    std::uint64_t bodyBObjectId() const { return bodyBObjectId_; }

    void onDestroy(const scene::SceneUpdateContext&) override { release(); }

private:
    void release()
    {
        if (handle_) {
            world_->destroyJoint(handle_);
            handle_ = {};
        }
    }

    core::SharedPtr<PhysicsWorld2D> world_;
    JointDesc desc_;
    JointHandle handle_;
    std::uint64_t bodyAObjectId_ = 0;
    std::uint64_t bodyBObjectId_ = 0;
};

inline Collider2DComponent* RigidBody2DComponent::add_collider(const ShapeDesc& desc)
{
    if (!gameObject() || !handle_)
        return nullptr;
    auto component = core::makeUnique<Collider2DComponent>(world_, handle_, desc);
    if (!component->handle())
        throw std::runtime_error(world_->diagnostic());
    auto* result = component.get();
    gameObject()->addComponent(std::move(component));
    result->bindNativeReflectionObject();
    return result;
}
} // namespace arp::physics2d
