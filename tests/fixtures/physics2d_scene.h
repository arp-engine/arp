#pragma once
#include "2d/physics_components.h"
#include "2d/scene_sprite_extractor.h"
#include "tests/fixtures/scene_2d_fixture.h"

namespace arp::tests {
class Physics2DShowcase final {
public:
    Physics2DShowcase()
        : world(core::makeShared<physics2d::PhysicsWorld2D>(
              physics2d::WorldConfig{{0, -10}, 32, 32, 8, 128, 256, 128, 32, 4, 1.f / 60.f}))
    {
        bodies.reserve(16);
    }

    bool create()
    {
        using namespace physics2d;
        auto* floor = add(BodyType::Static, {0, -3}, {14, 1}, ShapeType::Box, 0, 0);
        auto* box = add(BodyType::Dynamic, {-4, 0}, {1, 1}, ShapeType::Box, .7f, 0);
        auto* ball = add(BodyType::Dynamic, {-2, 2}, {1, 1}, ShapeType::Circle, .1f, .9f);
        auto* anchor = add(BodyType::Static, {2, 2}, {.4f, .4f}, ShapeType::Circle, 0, 0);
        auto* pendulum = add(BodyType::Dynamic, {2, 0}, {1, 1}, ShapeType::Box, .5f, 0);
        auto* hingeBob = add(BodyType::Dynamic, {4, 0}, {1, 1}, ShapeType::Box, .5f, 0);
        auto* slider = add(BodyType::Dynamic, {6, 2}, {1, 1}, ShapeType::Box, .7f, 0);
        if (!floor || !box || !ball || !anchor || !pendulum || !hingeBob || !slider)
            return false;
        JointDesc joint;
        joint.bodyA = anchor->handle();
        joint.bodyB = pendulum->handle();
        joint.length = 2;
        auto distance = world->createJoint(joint);
        if (!distance)
            return false;
        joint.type = JointType::Revolute;
        joint.bodyB = hingeBob->handle();
        joint.localAnchorA = {2, 0};
        joint.localAnchorB = {0, 2};
        if (!world->createJoint(joint))
            return false;
        joint.type = JointType::Prismatic;
        joint.bodyB = slider->handle();
        joint.localAnchorA = {4, 0};
        joint.localAnchorB = {};
        joint.enableLimit = true;
        joint.lowerLimit = -1;
        joint.upperLimit = 1;
        joint.enableMotor = true;
        joint.maxMotorForce = 100;
        joint.motorSpeed = -1;
        if (!world->createJoint(joint))
            return false;
        return world->applyImpulse(pendulum->handle(), {2, 0}, {2, 0});
    }

    bool step()
    {
        for (auto* body : bodies)
            body->beforeStep();
        if (!world->step())
            return false;
        for (auto* body : bodies)
            if (!body->afterStep() || !body->synchronize(scene, 1))
                return false;
        return true;
    }

    core::SharedPtr<physics2d::PhysicsWorld2D> world;
    scene::Scene scene;

private:
    physics2d::RigidBody2DComponent* add(physics2d::BodyType type,
        core::Vec2 position,
        core::Vec2 size,
        physics2d::ShapeType shapeType,
        float friction,
        float bounce)
    {
        auto& object = scene.createObject("physics showcase");
        scene::Transform transform;
        transform.translation = {position.x, position.y, 0};
        object.set_local_transform(transform);
        physics2d::BodyDesc desc;
        desc.type = type;
        auto& body = object.addComponent<physics2d::RigidBody2DComponent>(world, desc);
        if (!body.initialize(transform))
            return nullptr;
        physics2d::ShapeDesc shape;
        shape.type = shapeType;
        shape.halfSize = core::scale(size, .5f);
        shape.radius = size.x * .5f;
        shape.friction = friction;
        shape.restitution = bounce;
        auto& collider =
            object.addComponent<physics2d::Collider2DComponent>(world, body.handle(), shape);
        if (!collider.handle())
            return nullptr;
        scene2d::SpriteRendererComponentDesc sprite;
        sprite.spriteResourceId = tests::spriteA;
        sprite.sortingOrder = type == physics2d::BodyType::Static ? 0 : 1;
        auto& visual = scene.createObject("physics showcase sprite");
        if (!scene.reparent(visual.id(), object.id()).ok())
            return nullptr;
        scene::Transform visualTransform;
        visualTransform.scale = {size.x, size.y, 1};
        visual.set_local_transform(visualTransform);
        visual.addComponent<scene2d::SpriteRendererComponent>(sprite);
        bodies.push_back(&body);
        return &body;
    }

    core::pmr::Vector<physics2d::RigidBody2DComponent*> bodies{&core::defaultCpuMemoryResource()};
};
} // namespace arp::tests
