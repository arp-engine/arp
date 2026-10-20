#pragma once
#include "physics2d/components.h"
#include "scene/scene_asset_resource.h"

#include <cstdio>

namespace arp::physics2d {
// Serialized references are local scene object IDs, never Box2D IDs or pointers.
class PhysicsSceneCodecBase : public scene::SceneComponentCodec {
protected:
    using Y = core::YamlContract;
    using Node = Y::Node;

public:
    explicit PhysicsSceneCodecBase(core::SharedPtr<PhysicsWorld2D> world) : world_(std::move(world))
    {}

protected:
    static core::Vec2 vector(const Node& n)
    {
        return {float(Y::number(n, "x")), float(Y::number(n, "y"))};
    }

    static Node vector(core::Vec2 v)
    {
        auto n = Node::mapping();
        n["x"] = v.x;
        n["y"] = v.y;
        return n;
    }

    static bool fail(core::pmr::String& d, const char* id)
    {
        d = id;
        return false;
    }

    static const RigidBody2DComponent* body(const scene::GameObject* object)
    {
        return object ? object->getComponent<RigidBody2DComponent>() : nullptr;
    }

    core::SharedPtr<PhysicsWorld2D> world_;
};

class RigidBody2DSceneCodec final : public PhysicsSceneCodecBase {
public:
    using PhysicsSceneCodecBase::PhysicsSceneCodecBase;

    const char* type() const override { return "arp.rigid-body2d/1"; }

    Phase phase() const override { return Phase::Bodies; }

    bool accepts(const scene::Component& c) const override
    {
        return dynamic_cast<const RigidBody2DComponent*>(&c);
    }

    bool attach(scene::GameObject& object,
        const Node& n,
        const scene::SceneObjectBindings&,
        resource::ResourceManager&,
        core::pmr::String& d) const override
    {
        if (object.getComponent<RigidBody2DComponent>() || !world_)
            return fail(d, "ARP-SCENE-BODY2D-DUPLICATE-OR-WORLD");
        const auto transform = object.world_transform();
        if (transform.scale.x != 1 || transform.scale.y != 1 || transform.scale.z != 1)
            return fail(d, "ARP-SCENE-BODY2D-SCALE");
        BodyDesc desc;
        const auto type = Y::text(n, "bodyType");
        if (type == "static")
            desc.type = BodyType::Static;
        else if (type == "kinematic")
            desc.type = BodyType::Kinematic;
        else if (type == "dynamic")
            desc.type = BodyType::Dynamic;
        else
            return fail(d, "ARP-SCENE-BODY2D-TYPE");
        desc.velocity = vector(Y::required(n, "velocity"));
        desc.objectId = object.id().value();
        desc.gravityScale = float(Y::number(n, "gravityScale"));
        desc.linearDamping = float(Y::number(n, "linearDamping"));
        desc.angularDamping = float(Y::number(n, "angularDamping"));
        desc.angularVelocity = float(Y::number(n, "angularVelocity"));
        desc.bullet = Y::boolean(n, "bullet");
        desc.fixedRotation = Y::boolean(n, "fixedRotation");
        auto& component = object.addComponent<RigidBody2DComponent>(world_, desc);
        return component.initialize(object.world_transform())
            || fail(d, "ARP-SCENE-BODY2D-INITIALIZE");
    }

    bool encode(const scene::Component& c,
        const scene::SceneObjectBindings&,
        Node& n,
        core::pmr::String& d) const override
    {
        const auto& component = static_cast<const RigidBody2DComponent&>(c);
        const auto& desc = component.desc();
        BodyState state;
        if (!world_ || !world_->state(component.handle(), state))
            return fail(d, "ARP-SCENE-BODY2D-STALE");
        const char* types[] = {"static", "kinematic", "dynamic"};
        n["bodyType"] = types[int(desc.type)];
        n["velocity"] = vector(state.velocity);
        n["gravityScale"] = desc.gravityScale;
        n["linearDamping"] = desc.linearDamping;
        n["angularDamping"] = desc.angularDamping;
        n["angularVelocity"] = state.angularVelocity;
        n["bullet"] = desc.bullet;
        n["fixedRotation"] = desc.fixedRotation;
        return true;
    }
};

class Collider2DSceneCodec final : public PhysicsSceneCodecBase {
public:
    using PhysicsSceneCodecBase::PhysicsSceneCodecBase;

    const char* type() const override { return "arp.collider2d/1"; }

    Phase phase() const override { return Phase::Colliders; }

    bool accepts(const scene::Component& c) const override
    {
        return dynamic_cast<const Collider2DComponent*>(&c);
    }

    bool attach(scene::GameObject& object,
        const Node& n,
        const scene::SceneObjectBindings&,
        resource::ResourceManager&,
        core::pmr::String& d) const override
    {
        const auto* rigid = body(&object);
        if (!rigid || !world_)
            return fail(d, "ARP-SCENE-COLLIDER2D-BODY");
        ShapeDesc desc;
        const auto type = Y::text(n, "shapeType");
        const char* types[] = {"circle", "capsule", "box", "convex", "static-chain"};
        bool found = false;
        for (int i = 0; i < 5; ++i)
            if (type == types[i]) {
                desc.type = ShapeType(i);
                found = true;
            }
        if (!found)
            return fail(d, "ARP-SCENE-COLLIDER2D-TYPE");
        desc.center = vector(Y::required(n, "center"));
        desc.halfSize = vector(Y::required(n, "halfSize"));
        desc.pointA = vector(Y::required(n, "pointA"));
        desc.pointB = vector(Y::required(n, "pointB"));
        for (const auto& point : Y::sequence(n, "points"))
            desc.points.push_back(vector(point));
        desc.filter.category = Y::hex64(n, "category");
        desc.filter.mask = Y::hex64(n, "mask");
        desc.filter.group = Y::int32(n, "group");
        desc.radius = float(Y::number(n, "radius"));
        desc.density = float(Y::number(n, "density"));
        desc.friction = float(Y::number(n, "friction"));
        desc.restitution = float(Y::number(n, "restitution"));
        desc.sensor = Y::boolean(n, "sensor");
        desc.loop = Y::boolean(n, "loop");
        return bool(object
                       .addComponent<Collider2DComponent>(world_, rigid->handle(), std::move(desc))
                       .handle())
            || fail(d, "ARP-SCENE-COLLIDER2D-CREATE");
    }

    bool encode(const scene::Component& c,
        const scene::SceneObjectBindings&,
        Node& n,
        core::pmr::String&) const override
    {
        const auto& desc = static_cast<const Collider2DComponent&>(c).desc();
        const char* types[] = {"circle", "capsule", "box", "convex", "static-chain"};
        n["shapeType"] = types[int(desc.type)];
        n["center"] = vector(desc.center);
        n["halfSize"] = vector(desc.halfSize);
        n["pointA"] = vector(desc.pointA);
        n["pointB"] = vector(desc.pointB);
        n["points"] = Node::sequence();
        for (auto p : desc.points)
            n["points"].get_value_ref<Node::sequence_type&>().push_back(vector(p));
        char hex[17];
        std::snprintf(
            hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(desc.filter.category));
        n["category"] = hex;
        std::snprintf(
            hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(desc.filter.mask));
        n["mask"] = hex;
        n["group"] = desc.filter.group;
        n["radius"] = desc.radius;
        n["density"] = desc.density;
        n["friction"] = desc.friction;
        n["restitution"] = desc.restitution;
        n["sensor"] = desc.sensor;
        n["loop"] = desc.loop;
        return true;
    }
};

class Joint2DSceneCodec final : public PhysicsSceneCodecBase {
public:
    using PhysicsSceneCodecBase::PhysicsSceneCodecBase;

    const char* type() const override { return "arp.joint2d/1"; }

    Phase phase() const override { return Phase::Joints; }

    bool accepts(const scene::Component& c) const override
    {
        return dynamic_cast<const Joint2DComponent*>(&c);
    }

    bool attach(scene::GameObject& object,
        const Node& n,
        const scene::SceneObjectBindings& bindings,
        resource::ResourceManager&,
        core::pmr::String& d) const override
    {
        const auto* a = body(bindings.findObject(Y::uint64(Y::required(n, "bodyA"))));
        const auto* b = body(bindings.findObject(Y::uint64(Y::required(n, "bodyB"))));
        if (!a || !b || !world_)
            return fail(d, "ARP-SCENE-JOINT2D-REFERENCE");
        JointDesc desc;
        desc.bodyA = a->handle();
        desc.bodyB = b->handle();
        const auto type = Y::text(n, "jointType");
        if (type == "distance")
            desc.type = JointType::Distance;
        else if (type == "revolute")
            desc.type = JointType::Revolute;
        else if (type == "prismatic")
            desc.type = JointType::Prismatic;
        else
            return fail(d, "ARP-SCENE-JOINT2D-TYPE");
        desc.localAnchorA = vector(Y::required(n, "localAnchorA"));
        desc.localAnchorB = vector(Y::required(n, "localAnchorB"));
        desc.localAxisA = vector(Y::required(n, "localAxisA"));
        desc.length = float(Y::number(n, "length"));
        desc.referenceAngle = float(Y::number(n, "referenceAngle"));
        desc.lowerLimit = float(Y::number(n, "lowerLimit"));
        desc.upperLimit = float(Y::number(n, "upperLimit"));
        desc.motorSpeed = float(Y::number(n, "motorSpeed"));
        desc.maxMotorForce = float(Y::number(n, "maxMotorForce"));
        desc.enableLimit = Y::boolean(n, "enableLimit");
        desc.enableMotor = Y::boolean(n, "enableMotor");
        desc.collideConnected = Y::boolean(n, "collideConnected");
        return bool(object.addComponent<Joint2DComponent>(world_, desc).handle())
            || fail(d, "ARP-SCENE-JOINT2D-CREATE");
    }

    bool encode(const scene::Component& c,
        const scene::SceneObjectBindings& bindings,
        Node& n,
        core::pmr::String& d) const override
    {
        const auto& desc = static_cast<const Joint2DComponent&>(c).desc();
        std::uint64_t a = 0, b = 0;
        for (const auto& object : bindings.scene().objects())
            if (const auto* rigid = body(object.get())) {
                if (rigid->handle() == desc.bodyA)
                    a = bindings.local(object->id());
                if (rigid->handle() == desc.bodyB)
                    b = bindings.local(object->id());
            }
        if (!a || !b)
            return fail(d, "ARP-SCENE-JOINT2D-EXTERNAL-REFERENCE");
        n["bodyA"] = a;
        n["bodyB"] = b;
        const char* types[] = {"distance", "revolute", "prismatic"};
        n["jointType"] = types[int(desc.type)];
        n["localAnchorA"] = vector(desc.localAnchorA);
        n["localAnchorB"] = vector(desc.localAnchorB);
        n["localAxisA"] = vector(desc.localAxisA);
        n["length"] = desc.length;
        n["referenceAngle"] = desc.referenceAngle;
        n["lowerLimit"] = desc.lowerLimit;
        n["upperLimit"] = desc.upperLimit;
        n["motorSpeed"] = desc.motorSpeed;
        n["maxMotorForce"] = desc.maxMotorForce;
        n["enableLimit"] = desc.enableLimit;
        n["enableMotor"] = desc.enableMotor;
        n["collideConnected"] = desc.collideConnected;
        return true;
    }
};
} // namespace arp::physics2d
