#include "editor/viewport/editor_scene_viewport_physics_overlay.h"

#include "2d/physics_components.h"

#include <cmath>

namespace arp::editor {
namespace {
constexpr float kPi = 3.14159265358979323846f;

core::Vec2 transformPoint(const physics2d::BodyState& body, core::Vec2 point)
{
    const auto c = std::cos(body.angle), s = std::sin(body.angle);
    return {
        body.position.x + c * point.x - s * point.y, body.position.y + s * point.x + c * point.y};
}

bool finite(core::Vec2 point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool activeInHierarchy(const scene::Scene& scene, const scene::GameObject& object)
{
    const scene::GameObject* current = &object;
    for (std::size_t depth = 0; current && depth <= scene.objects().size(); ++depth) {
        if (!current->active())
            return false;
        if (!current->parentId().valid())
            return true;
        current = scene.findObject(current->parentId());
    }
    return false;
}
} // namespace

EditorScenePhysicsOverlay buildEditorScenePhysicsOverlay(
    const scene::Scene& scene, std::size_t maxSegments, std::size_t circleSegments)
{
    EditorScenePhysicsOverlay result;
    auto fail = [&](const char* diagnostic) {
        result.valid = false;
        result.diagnostic = diagnostic;
        return result;
    };
    if (maxSegments == 0u || circleSegments < 8u || circleSegments % 4u != 0u)
        return fail("ARP-EDITOR-PHYSICS-OVERLAY-BUDGET-INVALID");
    auto add = [&](scene::GameObjectId objectId,
                   scene::ComponentId componentId,
                   EditorScenePhysicsOverlayKind kind,
                   core::Vec2 from,
                   core::Vec2 to,
                   bool sensor) {
        if (!finite(from) || !finite(to)) {
            result.valid = false;
            result.diagnostic = "ARP-EDITOR-PHYSICS-OVERLAY-GEOMETRY-INVALID";
            return false;
        }
        if (result.segments.size() == maxSegments) {
            result.valid = false;
            result.diagnostic = "ARP-EDITOR-PHYSICS-OVERLAY-SEGMENT-BUDGET-EXCEEDED";
            return false;
        }
        result.segments.push_back({objectId, componentId, kind, from, to, sensor});
        return true;
    };

    for (const auto& owned : scene.objects()) {
        const auto* object = owned.get();
        if (!object || !activeInHierarchy(scene, *object))
            continue;
        const auto* body = object->getComponent<physics2d::RigidBody2DComponent>();
        physics2d::BodyState state{};
        const bool bodyReady = body && body->enabled() && body->handle() && body->world()
            && body->world()->state(body->handle(), state);
        for (const auto& component : object->components()) {
            if (!component->enabled())
                continue;
            const auto* collider =
                dynamic_cast<const physics2d::Collider2DComponent*>(component.get());
            if (!collider || !collider->handle())
                continue;
            if (!bodyReady)
                return fail("ARP-EDITOR-PHYSICS-OVERLAY-BODY-UNAVAILABLE");
            const auto& shape = collider->desc();
            auto edge = [&](core::Vec2 a, core::Vec2 b) {
                return add(object->id(),
                    collider->id(),
                    EditorScenePhysicsOverlayKind::Collider,
                    transformPoint(state, a),
                    transformPoint(state, b),
                    shape.sensor);
            };
            if (shape.type == physics2d::ShapeType::Circle) {
                if (!std::isfinite(shape.radius) || shape.radius <= 0.0f)
                    return fail("ARP-EDITOR-PHYSICS-OVERLAY-SHAPE-INVALID");
                core::Vec2 previous{};
                for (std::size_t i = 0; i <= circleSegments; ++i) {
                    const auto angle =
                        2.0f * kPi * static_cast<float>(i) / static_cast<float>(circleSegments);
                    const core::Vec2 point{shape.center.x + shape.radius * std::cos(angle),
                        shape.center.y + shape.radius * std::sin(angle)};
                    if (i && !edge(previous, point))
                        return result;
                    previous = point;
                }
            } else if (shape.type == physics2d::ShapeType::Box) {
                if (!finite(shape.center) || !finite(shape.halfSize) || shape.halfSize.x <= 0.0f
                    || shape.halfSize.y <= 0.0f)
                    return fail("ARP-EDITOR-PHYSICS-OVERLAY-SHAPE-INVALID");
                const core::Vec2 corners[] = {
                    {shape.center.x - shape.halfSize.x, shape.center.y - shape.halfSize.y},
                    {shape.center.x + shape.halfSize.x, shape.center.y - shape.halfSize.y},
                    {shape.center.x + shape.halfSize.x, shape.center.y + shape.halfSize.y},
                    {shape.center.x - shape.halfSize.x, shape.center.y + shape.halfSize.y}};
                for (std::size_t i = 0; i < 4u; ++i) {
                    if (!edge(corners[i], corners[(i + 1u) % 4u]))
                        return result;
                }
            } else if (shape.type == physics2d::ShapeType::Convex
                || shape.type == physics2d::ShapeType::StaticChain) {
                const auto count = shape.points.size();
                const auto edgeCount = shape.type == physics2d::ShapeType::Convex || shape.loop
                    ? count
                    : count > 0u ? count - 1u
                                 : 0u;
                if (count < 2u)
                    return fail("ARP-EDITOR-PHYSICS-OVERLAY-SHAPE-INVALID");
                for (std::size_t i = 0; i < edgeCount; ++i) {
                    if (!edge(shape.points[i], shape.points[(i + 1u) % count]))
                        return result;
                }
            } else if (shape.type == physics2d::ShapeType::Capsule) {
                const auto delta = shape.pointB - shape.pointA;
                if (!finite(delta) || !std::isfinite(shape.radius) || shape.radius <= 0.0f)
                    return fail("ARP-EDITOR-PHYSICS-OVERLAY-SHAPE-INVALID");
                if (core::length(delta) <= 1e-6f) {
                    core::Vec2 previous{};
                    for (std::size_t i = 0; i <= circleSegments; ++i) {
                        const auto angle =
                            2.0f * kPi * static_cast<float>(i) / static_cast<float>(circleSegments);
                        const core::Vec2 point{shape.pointA.x + shape.radius * std::cos(angle),
                            shape.pointA.y + shape.radius * std::sin(angle)};
                        if (i && !edge(previous, point))
                            return result;
                        previous = point;
                    }
                } else {
                    const auto angle = std::atan2(delta.y, delta.x);
                    const auto half = circleSegments / 2u;
                    core::Vec2 previous{};
                    for (std::size_t i = 0; i <= circleSegments; ++i) {
                        const bool endCap = i <= half;
                        const auto a = endCap
                            ? angle - kPi * 0.5f + kPi * static_cast<float>(i) / half
                            : angle + kPi * 0.5f + kPi * static_cast<float>(i - half) / half;
                        const auto center = endCap ? shape.pointB : shape.pointA;
                        const core::Vec2 point{center.x + shape.radius * std::cos(a),
                            center.y + shape.radius * std::sin(a)};
                        if (i && !edge(previous, point))
                            return result;
                        previous = point;
                    }
                }
            } else {
                return fail("ARP-EDITOR-PHYSICS-OVERLAY-SHAPE-UNSUPPORTED");
            }
            if (!result.valid)
                return result;
        }
    }

    for (const auto& owned : scene.objects()) {
        const auto& object = *owned;
        if (!activeInHierarchy(scene, object))
            continue;
        for (const auto& component : object.components()) {
            const auto* joint = dynamic_cast<const physics2d::Joint2DComponent*>(component.get());
            if (!joint || !joint->enabled() || !joint->handle())
                continue;
            const auto* a =
                scene.findObject(scene::GameObjectId::fromValue(joint->bodyAObjectId()));
            const auto* b =
                scene.findObject(scene::GameObjectId::fromValue(joint->bodyBObjectId()));
            const auto* bodyA = a ? a->getComponent<physics2d::RigidBody2DComponent>() : nullptr;
            const auto* bodyB = b ? b->getComponent<physics2d::RigidBody2DComponent>() : nullptr;
            physics2d::BodyState stateA{}, stateB{};
            if (!bodyA || !bodyB || !bodyA->enabled() || !bodyB->enabled() || !bodyA->world()
                || bodyA->world() != bodyB->world()
                || !bodyA->world()->state(bodyA->handle(), stateA)
                || !bodyB->world()->state(bodyB->handle(), stateB))
                return fail("ARP-EDITOR-PHYSICS-OVERLAY-JOINT-BODY-UNAVAILABLE");
            const auto& desc = joint->desc();
            if (!add(object.id(),
                    joint->id(),
                    EditorScenePhysicsOverlayKind::Joint,
                    transformPoint(stateA, desc.localAnchorA),
                    transformPoint(stateB, desc.localAnchorB),
                    false))
                return result;
        }
    }
    return result;
}

} // namespace arp::editor
