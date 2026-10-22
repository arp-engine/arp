#include "2d/physics_script_domain.h"
#include "physics/2d/physics_world_2d.h"

#include <cassert>
#include <cmath>
#include <cstdio>

int main()
{
    using namespace arp;
    using namespace physics2d;
    WorldConfig config{{0, -10}, 32, 64, 16, 128, 256, 128, 32, 4, 1.f / 60.f};
    {
        PhysicsWorld2D reflectionWarmup(config);
        assert(reflectionWarmup.valid());
    }
    const auto before = core::defaultCpuAllocator().stats().liveBytes;
    {
        PhysicsWorld2D world(config);
        BodyDesc desc;
        desc.type = BodyType::Dynamic;
        auto first = world.createBody(desc), second = world.createBody(desc);
        Physics2DScriptDomain domain(world, 2);
        assert(domain.bind(1, first) && domain.bind(2, second));
        assert(domain.queueVelocity(1, 3, 4) && domain.queueVelocity(2, 5, 6));
        assert(world.destroyBody(second) && !domain.commitVelocities());
        BodyState state;
        assert(world.state(first, state) && state.velocity.x == 0 && state.velocity.y == 0);
        assert(!domain.snapshot());
        assert(domain.queueVelocity(1, 3, 4) && domain.commitVelocities());
        assert(world.state(first, state) && state.velocity.x == 3 && state.velocity.y == 4);
        BodyDesc properties;
        properties.type = BodyType::Kinematic;
        properties.velocity = {7, -2};
        properties.angularVelocity = .5f;
        properties.gravityScale = .25f;
        properties.linearDamping = .2f;
        properties.angularDamping = .3f;
        properties.bullet = true;
        properties.fixedRotation = true;
        assert(world.updateBodyProperties(first, properties));
        assert(world.state(first, state) && state.velocity.x == 7 && state.velocity.y == -2);
        auto rejectedProperties = properties;
        rejectedProperties.velocity = {90, 90};
        rejectedProperties.linearDamping = -1;
        assert(!world.updateBodyProperties(first, rejectedProperties));
        assert(world.state(first, state) && state.velocity.x == 7 && state.velocity.y == -2);
    }
    {
        PhysicsWorld2D ccd(config);
        auto wall = ccd.createBody({BodyType::Static, {0, 0}});
        ShapeDesc thin;
        thin.type = ShapeType::Box;
        thin.halfSize = {.05f, 10};
        assert(ccd.createCollider(wall, thin));
        BodyDesc bullet;
        bullet.type = BodyType::Dynamic;
        bullet.position = {-4, 0};
        bullet.velocity = {120, 0};
        bullet.gravityScale = 0;
        bullet.bullet = true;
        auto fast = ccd.createBody(bullet);
        ShapeDesc ball;
        ball.radius = .1f;
        assert(ccd.createCollider(fast, ball));
        for (int i = 0; i < 10; ++i) {
            assert(ccd.step());
            BodyState s;
            assert(ccd.state(fast, s) && s.position.x < 0);
        }
        assert(ccd.raycast({-3, 0}, {6, 0}, {4, 4, 0}) && ccd.queryResults().empty());
    }
    {
        PhysicsWorld2D replacement(config);
        BodyDesc bodyDesc;
        bodyDesc.type = BodyType::Dynamic;
        bodyDesc.objectId = 731;
        const auto body = replacement.createBody(bodyDesc);
        ShapeDesc circle;
        circle.radius = 0.5f;
        const auto original = replacement.createCollider(body, circle);
        assert(body && original);

        auto invalid = circle;
        invalid.radius = 0.0f;
        assert(!replacement.replaceCollider(original, invalid));
        assert(replacement.overlap(circle, {}, 0.0f, {}));
        assert(replacement.queryResults().size() == 1
            && replacement.queryResults()[0].collider == original);

        ShapeDesc box;
        box.type = ShapeType::Box;
        box.halfSize = {1.0f, 0.25f};
        const auto updated = replacement.replaceCollider(original, box);
        assert(updated && !(updated == original));
        std::uint64_t objectId = 0;
        assert(replacement.objectId(updated, objectId) && objectId == bodyDesc.objectId);
        assert(!replacement.destroyCollider(original));
        assert(replacement.overlap(box, {}, 0.0f, {}));
        assert(replacement.queryResults().size() == 1
            && replacement.queryResults()[0].collider == updated);
    }
    {
        PhysicsWorld2D stack(config);
        auto floor = stack.createBody({BodyType::Static, {0, -.5f}});
        ShapeDesc box;
        box.type = ShapeType::Box;
        box.halfSize = {10, .5f};
        box.friction = 1;
        assert(stack.createCollider(floor, box));
        box.halfSize = {.5f, .5f};
        BodyHandle top;
        for (int i = 0; i < 5; ++i) {
            BodyDesc b;
            b.type = BodyType::Dynamic;
            b.position = {0, .6f + i * 1.05f};
            top = stack.createBody(b);
            assert(stack.createCollider(top, box));
        }
        BodyDesc sliding;
        sliding.type = BodyType::Dynamic;
        sliding.position = {4, .55f};
        sliding.velocity = {4, 0};
        auto slide = stack.createBody(sliding);
        assert(stack.createCollider(slide, box));
        for (int i = 0; i < 240; ++i)
            assert(stack.step());
        BodyState s;
        assert(stack.state(top, s) && s.position.y > 4.2f && s.position.y < 4.8f);
        assert(stack.state(slide, s) && std::abs(s.velocity.x) < .05f);
    }
    {
        PhysicsWorld2D world(config);
        assert(world.valid());
        auto floor = world.createBody({BodyType::Static, {0, -1}});
        assert(floor);
        ShapeDesc ground;
        ground.type = ShapeType::Box;
        ground.halfSize = {20, .5f};
        ground.friction = 1;
        auto floorShape = world.createCollider(floor, ground);
        assert(floorShape);
        BodyDesc dynamic;
        dynamic.type = BodyType::Dynamic;
        dynamic.position = {0, 5};
        dynamic.angularVelocity = 1;
        auto body = world.createBody(dynamic);
        assert(body);
        ShapeDesc ball;
        ball.radius = .5f;
        ball.restitution = .8f;
        ball.friction = .5f;
        auto ballShape = world.createCollider(body, ball);
        assert(ballShape);
        bool bounced = false, contact = false;
        for (int i = 0; i < 120; ++i) {
            assert(world.step());
            BodyState state;
            assert(world.state(body, state));
            bounced |= state.velocity.y > 1;
            contact |= !world.events().empty();
            assert(state.position.y > -.51f);
        }
        assert(bounced && contact);
        BodyState state;
        assert(world.state(body, state) && std::abs(state.angle) > .1f);
        assert(world.raycast({0, 10}, {0, -15}, {}) && world.queryResults().size() >= 2);
        assert(world.overlap(ball, state.position, 0, {}) && !world.queryResults().empty());
        assert(world.shapeCast(ball, {0, 10}, 0, {0, -15}, {}) && !world.queryResults().empty());
        ShapeDesc sensor = ball;
        sensor.radius = 2;
        sensor.sensor = true;
        auto sensorBody = world.createBody({BodyType::Static, state.position});
        auto sensorShape = world.createCollider(sensorBody, sensor);
        assert(sensorShape);
        assert(world.step());
        bool sensorBegin = false;
        for (const auto& event : world.events())
            sensorBegin |= event.type == EventType::SensorBegin;
        assert(sensorBegin);
        assert(world.destroyBody(body) && !world.state(body, state));
        assert(world.step());
        bool sensorEnd = false;
        for (const auto& event : world.events())
            sensorEnd |= event.type == EventType::SensorEnd;
        assert(sensorEnd);
        dynamic.position = {4, 3};
        auto moving = world.createBody(dynamic);
        ShapeDesc box;
        box.type = ShapeType::Box;
        box.halfSize = {.4f, .7f};
        assert(world.createCollider(moving, box));
        auto anchor = world.createBody({BodyType::Static, {4, 5}});
        JointDesc distance;
        distance.bodyA = anchor;
        distance.bodyB = moving;
        distance.length = 2;
        auto joint = world.createJoint(distance);
        assert(joint);
        for (int i = 0; i < 120; ++i)
            assert(world.step());
        assert(world.state(moving, state));
        assert(std::abs(core::length(state.position - core::Vec2{4, 5}) - 2) < .05f);
        assert(world.destroyJoint(joint));
        JointDesc revolute;
        revolute.type = JointType::Revolute;
        revolute.bodyA = anchor;
        revolute.bodyB = moving;
        revolute.localAnchorB = {0, 2};
        auto hinge = world.createJoint(revolute);
        assert(hinge);
        assert(world.step() && world.destroyJoint(hinge));
        assert(world.setPose(moving, {4, 5}, 0));
        JointDesc slider;
        slider.type = JointType::Prismatic;
        slider.bodyA = anchor;
        slider.bodyB = moving;
        slider.enableLimit = true;
        slider.lowerLimit = -1;
        slider.upperLimit = 1;
        auto rail = world.createJoint(slider);
        assert(rail);
        assert(world.setVelocity(moving, {20, -20}));
        for (int i = 0; i < 120; ++i)
            assert(world.step());
        assert(world.state(moving, state) && std::abs(state.position.y - 5) < .05f
            && std::abs(state.position.x - 4) < 1.1f);
        assert(world.destroyBody(moving) && !world.destroyJoint(rail));
        auto kin = world.createBody({BodyType::Kinematic, {-4, 0}, {1, 0}});
        ShapeDesc capsule;
        capsule.type = ShapeType::Capsule;
        capsule.pointA = {0, -.5f};
        capsule.pointB = {0, .5f};
        capsule.radius = .2f;
        assert(world.createCollider(kin, capsule));
        for (int i = 0; i < 60; ++i)
            assert(world.step());
        assert(world.state(kin, state) && std::abs(state.position.x + 3) < .01f);
        ShapeDesc convex;
        convex.type = ShapeType::Convex;
        convex.points = {{0, 0}, {1, 0}, {.5f, 1}};
        assert(world.createCollider(kin, convex));
        std::swap(convex.points[0], convex.points[1]);
        assert(!world.createCollider(kin, convex));
        ShapeDesc chain;
        chain.type = ShapeType::StaticChain;
        chain.points = {{-3, 0}, {-1, 0}, {1, 0}, {3, 0}};
        const auto chainHandle = world.createCollider(floor, chain);
        assert(chainHandle);
        assert(!world.createCollider(kin, chain));
        ShapeDesc broad;
        broad.radius = 10;
        assert(world.overlap(broad, {0, 0}, 0, {}));
        std::uint32_t chainHits = 0;
        for (const auto& hit : world.queryResults())
            if (hit.collider == chainHandle)
                ++chainHits;
        assert(chainHits == 1);
        assert(world.telemetry().thirdPartyBytes > 0);
        std::printf(
            "PASS: Box2D 3.1.1 shapes, queries, bounce, rotation, sensors, three joints; bytes=%llu peak=%llu\n",
            (unsigned long long)world.telemetry().thirdPartyBytes,
            (unsigned long long)world.telemetry().thirdPartyPeakBytes);
    }
    assert(core::defaultCpuAllocator().stats().liveBytes == before);
}
