#pragma once
#include "physics/2d/physics_world_2d.h"
#include "scene/component.h"
#include "scene/scene.h"

#include <stdexcept>

namespace arp::physics2d {
ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct RigidBody2DEditorProperties {
    APROP(Serialize, Editor)
    BodyType type = BodyType::Static;
    APROP(Serialize, Editor)
    core::Vec2 velocity{};
    APROP(Serialize, Editor)
    float angularVelocity = 0;
    APROP(Serialize, Editor)
    float gravityScale = 1;
    APROP(Serialize, Editor)
    float linearDamping = 0;
    APROP(Serialize, Editor)
    float angularDamping = 0;
    APROP(Serialize, Editor)
    bool bullet = false;
    APROP(Serialize, Editor)
    bool fixedRotation = false;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Collider2DEditorProperties {
    APROP(Serialize, Editor)
    ShapeType type = ShapeType::Circle;
    APROP(Serialize, Editor)
    core::Vec2 center{};
    APROP(Serialize, Editor)
    core::Vec2 halfSize{};
    APROP(Serialize, Editor)
    core::Vec2 pointA{};
    APROP(Serialize, Editor)
    core::Vec2 pointB{};
    APROP(Serialize, Editor)
    core::pmr::Vector<core::Vec2> points{&core::defaultCpuMemoryResource()};
    APROP(Serialize, Editor)
    float radius = 0.5f;
    APROP(Serialize, Editor)
    float density = 1.0f;
    APROP(Serialize, Editor)
    float friction = 0.6f;
    APROP(Serialize, Editor)
    float restitution = 0.0f;
    APROP(Serialize, Editor)
    bool sensor = false;
    APROP(Serialize, Editor)
    bool loop = false;
    APROP(Serialize, Editor)
    CollisionFilter filter;
};

ASTRUCT(Construct = Default, Ownership = Value, Lifecycle = Value)

struct Joint2DEditorProperties {
    APROP(Serialize,
        Editor,
        SceneObjectReference,
        OptionalReference,
        ReferenceTarget = "RigidBody2DComponent",
        Doc = "sceneObject:RigidBody2DComponent:optional")
    std::uint64_t bodyA = 0;
    APROP(Serialize,
        Editor,
        SceneObjectReference,
        OptionalReference,
        ReferenceTarget = "RigidBody2DComponent",
        Doc = "sceneObject:RigidBody2DComponent:optional")
    std::uint64_t bodyB = 0;
    APROP(Serialize, Editor)
    JointType type = JointType::Distance;
    APROP(Serialize, Editor)
    core::Vec2 localAnchorA{};
    APROP(Serialize, Editor)
    core::Vec2 localAnchorB{};
    APROP(Serialize, Editor)
    core::Vec2 localAxisA{1, 0};
    APROP(Serialize, Editor)
    float length = 1;
    APROP(Serialize, Editor)
    float referenceAngle = 0;
    APROP(Serialize, Editor)
    float lowerLimit = 0;
    APROP(Serialize, Editor)
    float upperLimit = 0;
    APROP(Serialize, Editor)
    float motorSpeed = 0;
    APROP(Serialize, Editor)
    float maxMotorForce = 0;
    APROP(Serialize, Editor)
    bool enableLimit = false;
    APROP(Serialize, Editor)
    bool enableMotor = false;
    APROP(Serialize, Editor)
    bool collideConnected = false;
};

// Components retain the world. Host owns fixed stepping, then synchronizes
// bodies into scene world transforms; hierarchy conversion stays in Scene.
// Collider dimensions are explicit world units. Visual object scale is not
// silently baked into physics; scene authoring restoration requires unit scale.
class Collider2DComponent;
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class RigidBody2DComponent final : public scene::Component {
public:
    RigidBody2DComponent(core::SharedPtr<PhysicsWorld2D> world,
        BodyDesc desc,
        ComponentUsage usage = ComponentUsage::Simulation)
        : world_(std::move(world)), desc_(desc), usage_(requireComponentUsage(usage))
    {
        if (usage_ == ComponentUsage::Authoring)
            if (const auto* diagnostic = validateBodyDesc(desc_))
                throw std::invalid_argument(diagnostic);
    }

    explicit RigidBody2DComponent(
        core::SharedPtr<PhysicsWorld2D> world, ComponentUsage usage = ComponentUsage::Simulation)
        : world_(std::move(world)), usage_(requireComponentUsage(usage))
    {}
    AFUNC()

    bool initialize_body(const BodyDesc& desc)
    {
        if (usage_ != ComponentUsage::Simulation)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
        if (!gameObject() || handle_)
            return false;
        desc_ = desc;
        desc_.objectId = ownerId().value();
        return initialize(gameObject()->world_transform());
    }
    AFUNC()

    bool set_velocity(core::Vec2 velocity, float angular)
    {
        if (usage_ == ComponentUsage::Authoring)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
        return handle_ && world_->setVelocity(handle_, velocity, angular);
    }
    AFUNC()

    bool set_pose(core::Vec2 position, float angle)
    {
        if (usage_ == ComponentUsage::Authoring)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
        return handle_ && world_->setPose(handle_, position, angle);
    }
    AFUNC()

    BodyState body_state() const
    {
        if (usage_ == ComponentUsage::Authoring)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
        BodyState result;
        if (!handle_ || !world_->state(handle_, result))
            throw std::runtime_error("ARP-PHYSICS2D-BODY-STALE");
        return result;
    }
    AFUNC()
    Collider2DComponent* add_collider(const ShapeDesc& desc);

    ~RigidBody2DComponent() override { release(); }

    const char* scriptTypeName() const override { return "RigidBody2DComponent"; }

    // Authoring geometry derives from Scene TRS, never stale simulation state.
    static bool poseFromTransform(const scene::Transform& transform, BodyState& result)
    {
        if (transform.scale.x != 1 || transform.scale.y != 1 || transform.scale.z != 1
            || transform.rotation.x != 0 || transform.rotation.y != 0
            || !std::isfinite(transform.translation.x) || !std::isfinite(transform.translation.y)
            || !std::isfinite(transform.translation.z) || !std::isfinite(transform.rotation.z)
            || !std::isfinite(transform.rotation.w)
            || std::abs(core::dot(transform.rotation, transform.rotation) - 1) > 1e-5f)
            return false;
        result = {{transform.translation.x, transform.translation.y},
            {},
            2 * std::atan2(transform.rotation.z, transform.rotation.w),
            0};
        return true;
    }

    bool authoringPose(BodyState& result) const
    {
        return usage_ == ComponentUsage::Authoring && gameObject()
            && poseFromTransform(gameObject()->world_transform(), result);
    }

    bool initialize(const scene::Transform& transform)
    {
        return initializeForOwner(transform,
            scene::GameObjectId::fromValue(desc_.objectId ? desc_.objectId : ownerId().value()));
    }

    bool initializeForOwner(const scene::Transform& transform, scene::GameObjectId objectId)
    {
        BodyState pose;
        if (handle_ || !world_ || !objectId.valid() || !poseFromTransform(transform, pose))
            return false;
        auto candidate = desc_;
        candidate.objectId = objectId.value();
        candidate.position = pose.position;
        candidate.angle = pose.angle;
        if (validateBodyDesc(candidate))
            return false;
        if (usage_ == ComponentUsage::Authoring) {
            desc_ = candidate;
            return true;
        }
        const auto handle = world_->createBody(candidate);
        if (!handle || !world_->state(handle, current_))
            return false;
        desc_ = candidate;
        handle_ = handle;
        previous_ = current_;
        return true;
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
        return applyRestoredAuthoringProperties(value, nullptr);
    }

    bool applyRestoredAuthoringProperties(
        const reflection::object& value, const scene::Scene::IdentityRestoration* transaction)
    {
        if (!value.is<RigidBody2DEditorProperties>() || !world_)
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
        if (validateBodyDesc(candidate))
            return false;
        if (usage_ == ComponentUsage::Authoring) {
            scene::Transform transform;
            BodyState pose;
            if (!gameObject())
                return false;
            if (transaction) {
                if (!transaction->worldTransform(ownerId(), transform))
                    return false;
            } else
                transform = gameObject()->world_transform();
            if (!poseFromTransform(transform, pose))
                return false;
            candidate.position = pose.position;
            candidate.angle = pose.angle;
            candidate.objectId = ownerId().value();
            if (candidate.type != BodyType::Static && gameObject())
                for (const auto& sibling : gameObject()->components()) {
                    const auto properties = sibling->editorAuthoringProperties();
                    if (properties.is<Collider2DEditorProperties>()
                        && properties.cast<const Collider2DEditorProperties&>().type
                            == ShapeType::StaticChain)
                        return false;
                }
            desc_ = candidate;
            return true;
        }
        if (!handle_) {
            desc_ = candidate;
            return !restoring() || (gameObject() && initialize(gameObject()->world_transform()));
        }
        if (!world_->updateBodyProperties(handle_, candidate))
            return false;
        desc_ = candidate;
        return true;
    }

    ComponentUsage usage() const { return usage_; }

    void beforeStep()
    {
        if (usage_ != ComponentUsage::Simulation)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
        previous_ = current_;
    }

    bool afterStep()
    {
        if (usage_ != ComponentUsage::Simulation)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
        return handle_ && world_->state(handle_, current_);
    }

    bool synchronize(scene::Scene& scene, float interpolation)
    {
        if (usage_ != ComponentUsage::Simulation)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
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
    const ComponentUsage usage_;
    BodyHandle handle_;
    BodyState previous_, current_;
};
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class Collider2DComponent final : public scene::Component {
public:
    explicit Collider2DComponent(
        core::SharedPtr<PhysicsWorld2D> world, ComponentUsage usage = ComponentUsage::Simulation)
        : world_(std::move(world)), usage_(requireComponentUsage(usage))
    {}

    Collider2DComponent(core::SharedPtr<PhysicsWorld2D> world,
        BodyHandle body,
        ShapeDesc desc,
        ComponentUsage usage = ComponentUsage::Simulation)
        : world_(std::move(world)), desc_(std::move(desc)), usage_(requireComponentUsage(usage))
    {
        if (usage_ == ComponentUsage::Authoring && world_) {
            if (const auto* diagnostic = validateShapeDesc(desc_, world_->chainPointCapacity()))
                throw std::invalid_argument(diagnostic);
        }
        if (world_ && usage_ == ComponentUsage::Simulation)
            handle_ = world_->createCollider(body, desc_);
    }

    ~Collider2DComponent() override { release(); }

    const char* scriptTypeName() const override { return "Collider2DComponent"; }

    const ShapeDesc& desc() const { return desc_; }

    ColliderHandle handle() const { return handle_; }

    ComponentUsage usage() const { return usage_; }

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
            desc_.loop,
            desc_.filter});
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
        candidate.filter = properties.filter;
        if (!world_ || validateShapeDesc(candidate, world_->chainPointCapacity()))
            return false;
        if (usage_ == ComponentUsage::Authoring) {
            const auto* body =
                gameObject() ? gameObject()->getComponent<RigidBody2DComponent>() : nullptr;
            if (!body || body->usage() != ComponentUsage::Authoring || body->world() != world_.get()
                || (candidate.type == ShapeType::StaticChain
                    && body->desc().type != BodyType::Static))
                return false;
            desc_ = std::move(candidate);
            return true;
        }
        if (handle_) {
            if (!world_)
                return false;
            const auto replacement = world_->replaceCollider(handle_, candidate);
            if (!replacement)
                return false;
            handle_ = replacement;
        }
        if (!handle_ && restoring()) {
            auto* owner = gameObject();
            auto* body = owner ? owner->getComponent<RigidBody2DComponent>() : nullptr;
            if (!body || body->world() != world_.get() || !body->handle())
                return false;
            handle_ = world_->createCollider(body->handle(), candidate);
            if (!handle_)
                return false;
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
    const ComponentUsage usage_;
    ColliderHandle handle_;
};
ACLASS(Ownership = Engine, Lifecycle = Explicit)

class Joint2DComponent final : public scene::Component {
public:
    explicit Joint2DComponent(
        core::SharedPtr<PhysicsWorld2D> world, ComponentUsage usage = ComponentUsage::Simulation)
        : world_(std::move(world)), usage_(requireComponentUsage(usage))
    {}
    AFUNC()

    reflection::object editorAuthoringProperties() const override
    {
        auto a = bodyAObjectId_, b = bodyBObjectId_;
        const auto* owner = gameObject();
        if (owner && owner->scene())
            for (const auto& object : owner->scene()->objects()) {
                const auto* body = object->getComponent<RigidBody2DComponent>();
                if (body && body->world() == world_.get()) {
                    if (!a && desc_.bodyA && body->handle() == desc_.bodyA)
                        a = object->id().value();
                    if (!b && desc_.bodyB && body->handle() == desc_.bodyB)
                        b = object->id().value();
                }
            }
        return reflection::object(Joint2DEditorProperties{a,
            b,
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
        return applyRestoredAuthoringProperties(value, nullptr);
    }

    class PreparedAuthoring final {
        friend class Joint2DComponent;
        const Joint2DComponent* owner_ = nullptr;
        JointDesc desc_;
        std::uint64_t a_ = 0, b_ = 0;

    public:
        PreparedAuthoring() = default;
    };

    // Cold preparation validates endpoints without mutating live authoring or
    // shared simulation. Publication only copies POD after Scene commits.
    bool prepareAuthoringProperties(const Joint2DEditorProperties& properties,
        const scene::Scene::IdentityRestoration* transaction,
        PreparedAuthoring& prepared) const
    {
        prepared = {};
        if (usage_ != ComponentUsage::Authoring || !world_ || !world_->valid())
            return false;
        const auto candidate = describe(properties);
        if (validateJointDesc(candidate))
            return false;
        const auto* owner = gameObject();
        const auto* ownerScene = owner ? owner->scene() : nullptr;
        const auto resolve = [&](std::uint64_t id) -> const scene::GameObject* {
            return !id || !ownerScene ? nullptr
                : transaction ? transaction->resolveObject(scene::GameObjectId::fromValue(id))
                              : ownerScene->findObject(scene::GameObjectId::fromValue(id));
        };
        for (const auto id : {properties.bodyA, properties.bodyB}) {
            if (!id)
                continue; // Explicit incomplete authoring; never a simulated joint.
            const auto* object = resolve(id);
            const auto* body = object ? object->getComponent<RigidBody2DComponent>() : nullptr;
            if (!body || body->usage() != ComponentUsage::Authoring
                || body->world() != world_.get())
                return false;
        }
        if (properties.bodyA && properties.bodyA == properties.bodyB)
            return false;
        prepared.owner_ = this;
        prepared.desc_ = candidate;
        prepared.a_ = properties.bodyA;
        prepared.b_ = properties.bodyB;
        return true;
    }

    bool commitAuthoringProperties(PreparedAuthoring&& prepared) noexcept
    {
        if (prepared.owner_ != this || usage_ != ComponentUsage::Authoring)
            return false;
        desc_ = prepared.desc_;
        bodyAObjectId_ = prepared.a_;
        bodyBObjectId_ = prepared.b_;
        prepared.owner_ = nullptr;
        return true;
    }

    // Explicit transaction-scoped lookup; ordinary Scene queries stay unchanged.
    bool applyRestoredAuthoringProperties(
        const reflection::object& value, const scene::Scene::IdentityRestoration* transaction)
    {
        if (!value.is<Joint2DEditorProperties>() || !world_ || !world_->valid())
            return false;
        const auto& properties = value.cast<const Joint2DEditorProperties&>();
        auto candidate = describe(properties);

        if (validateJointDesc(candidate))
            return false;
        if (usage_ == ComponentUsage::Authoring) {
            PreparedAuthoring prepared;
            return prepareAuthoringProperties(properties, transaction, prepared)
                && commitAuthoringProperties(std::move(prepared));
        }
        JointHandle candidateHandle{};
        if (properties.bodyA != 0u && properties.bodyB != 0u) {
            auto* owner = gameObject();
            auto* ownerScene = owner ? owner->scene() : nullptr;
            auto* objectA = ownerScene
                ? ownerScene->findObject(scene::GameObjectId::fromValue(properties.bodyA))
                : nullptr;
            auto* objectB = ownerScene
                ? ownerScene->findObject(scene::GameObjectId::fromValue(properties.bodyB))
                : nullptr;
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
        if (usage_ != ComponentUsage::Simulation)
            throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
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

    Joint2DComponent(core::SharedPtr<PhysicsWorld2D> world,
        JointDesc desc,
        ComponentUsage usage = ComponentUsage::Simulation)
        : world_(std::move(world)), desc_(desc), usage_(requireComponentUsage(usage))
    {
        if (usage_ == ComponentUsage::Authoring) {
            if (const auto* diagnostic = validateJointDesc(desc_))
                throw std::invalid_argument(diagnostic);
            if (desc_.bodyA || desc_.bodyB)
                throw std::invalid_argument("ARP-PHYSICS2D-AUTHORING-NATIVE-ENDPOINT");
        }
        if (world_ && usage_ == ComponentUsage::Simulation)
            handle_ = world_->createJoint(desc_);
    }

    ~Joint2DComponent() override { release(); }

    const char* scriptTypeName() const override { return "Joint2DComponent"; }

    const JointDesc& desc() const { return desc_; }

    JointHandle handle() const { return handle_; }

    ComponentUsage usage() const { return usage_; }

    std::uint64_t bodyAObjectId() const { return bodyAObjectId_; }

    std::uint64_t bodyBObjectId() const { return bodyBObjectId_; }

    void onDestroy(const scene::SceneUpdateContext&) override { release(); }

private:
    static JointDesc describe(const Joint2DEditorProperties& properties)
    {
        JointDesc candidate;
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

        return candidate;
    }

    void release()
    {
        if (handle_) {
            world_->destroyJoint(handle_);
            handle_ = {};
        }
    }

    core::SharedPtr<PhysicsWorld2D> world_;
    JointDesc desc_;
    const ComponentUsage usage_;
    JointHandle handle_;
    std::uint64_t bodyAObjectId_ = 0;
    std::uint64_t bodyBObjectId_ = 0;
};

inline Collider2DComponent* RigidBody2DComponent::add_collider(const ShapeDesc& desc)
{
    if (usage_ == ComponentUsage::Authoring)
        throw std::logic_error("ARP-PHYSICS2D-AUTHORING-NOT-SIMULATED");
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
