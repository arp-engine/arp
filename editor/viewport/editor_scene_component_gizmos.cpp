#include "editor/viewport/editor_scene_component_gizmos.h"

#include "3d/authoring_components.h"
#include "3d/collider_component.h"
#include "3d/components.h"
#include "physics/3d/physics_types.h"
#include "resource/terrain_asset_resource.h"
#include "scene/camera.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace arp::editor {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kCircleSegments = 16u;

bool finite(core::Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
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

EditorSceneComponentGizmos buildEditorSceneComponentGizmos(const scene::Scene& scene,
    const resource::ResourceManager& resources,
    float viewportAspect,
    std::size_t maxSegments,
    const scene::SceneTraversalSnapshot* visibleSnapshot)
{
    EditorSceneComponentGizmos result;
    auto fail = [&](const char* diagnostic) {
        result.valid = false;
        result.diagnostic = diagnostic;
        result.segments.clear();
        return result;
    };
    if (!std::isfinite(viewportAspect) || viewportAspect <= 0.0f)
        return fail("ARP-EDITOR-COMPONENT-GIZMO-VIEWPORT-ASPECT-INVALID");
    if (maxSegments == 0u)
        return fail("ARP-EDITOR-COMPONENT-GIZMO-BUDGET-INVALID");
    if (visibleSnapshot && !visibleSnapshot->ok())
        return fail("ARP-EDITOR-COMPONENT-GIZMO-VIEW-SNAPSHOT-INVALID");

    auto add = [&](const scene::GameObject& object,
                   const scene::Component& component,
                   EditorSceneComponentGizmoKind kind,
                   const core::Mat4& transform,
                   core::Vec3 from,
                   core::Vec3 to) {
        from = core::transformPoint(transform, from);
        to = core::transformPoint(transform, to);
        if (!finite(from) || !finite(to))
            return false;
        if (result.segments.size() >= maxSegments)
            return false;
        result.segments.push_back({object.id(), component.id(), kind, from, to});
        return true;
    };

    for (const auto& owned : scene.objects()) {
        const auto& object = *owned;
        if (!activeInHierarchy(scene, object)
            || (visibleSnapshot && !visibleSnapshot->findNode(object.id())))
            continue;
        const auto world = object.world_transform();
        const auto transform =
            core::multiply(core::multiply(core::Mat4::translation(world.translation),
                               core::Mat4::rotation(world.rotation)),
                core::Mat4::scale(world.scale));
        for (const auto& ownedComponent : object.components()) {
            const auto& component = *ownedComponent;
            if (!component.enabled())
                continue;
            if (const auto* camera = dynamic_cast<const scene::CameraComponent*>(&component)) {
                const auto& desc = camera->desc();
                const float aspect = desc.aspectPolicy.kind == scene::CameraAspectPolicyKind::Fixed
                    ? desc.aspectPolicy.aspectRatio
                    : desc.aspectPolicy.kind == scene::CameraAspectPolicyKind::MatchViewport
                    ? viewportAspect * desc.viewport.width / desc.viewport.height
                    : 0.0f;
                if (!std::isfinite(aspect) || aspect <= 0.0f || !std::isfinite(desc.nearPlane)
                    || !std::isfinite(desc.farPlane) || desc.nearPlane <= 0.0f
                    || desc.farPlane <= desc.nearPlane || !desc.viewport.valid()
                    || (desc.viewHandedness != scene::CameraViewHandedness::LeftHanded
                        && desc.viewHandedness != scene::CameraViewHandedness::RightHanded))
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-CAMERA-DESC-INVALID");
                float nearHalfHeight = 0.0f;
                float farHalfHeight = 0.0f;
                if (desc.projection == scene::CameraProjection::Perspective) {
                    if (!std::isfinite(desc.verticalFovDegrees) || desc.verticalFovDegrees <= 0.0f
                        || desc.verticalFovDegrees >= 180.0f)
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-CAMERA-DESC-INVALID");
                    const float tangent = std::tan(desc.verticalFovDegrees * kPi / 360.0f);
                    nearHalfHeight = tangent * desc.nearPlane;
                    farHalfHeight = tangent * desc.farPlane;
                } else if (desc.projection == scene::CameraProjection::Orthographic) {
                    if (!std::isfinite(desc.orthographicHeight) || desc.orthographicHeight <= 0.0f)
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-CAMERA-DESC-INVALID");
                    nearHalfHeight = farHalfHeight = desc.orthographicHeight * 0.5f;
                } else {
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-CAMERA-DESC-INVALID");
                }
                const float forward =
                    desc.viewHandedness == scene::CameraViewHandedness::LeftHanded ? 1.0f : -1.0f;
                const float depths[] = {desc.nearPlane * forward, desc.farPlane * forward};
                const float halfHeights[] = {nearHalfHeight, farHalfHeight};
                core::Vec3 corners[2][4]{};
                for (unsigned plane = 0; plane < 2u; ++plane) {
                    const float halfWidth = halfHeights[plane] * aspect;
                    corners[plane][0] = {-halfWidth, -halfHeights[plane], depths[plane]};
                    corners[plane][1] = {halfWidth, -halfHeights[plane], depths[plane]};
                    corners[plane][2] = {halfWidth, halfHeights[plane], depths[plane]};
                    corners[plane][3] = {-halfWidth, halfHeights[plane], depths[plane]};
                }
                auto edge = [&](core::Vec3 from, core::Vec3 to) {
                    return add(object,
                        component,
                        EditorSceneComponentGizmoKind::Camera,
                        transform,
                        from,
                        to);
                };
                for (unsigned i = 0; i < 4u; ++i) {
                    if (!edge(corners[0][i], corners[0][(i + 1u) % 4u])
                        || !edge(corners[1][i], corners[1][(i + 1u) % 4u])
                        || !edge(corners[0][i], corners[1][i]))
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                }
            } else if (const auto* light =
                           dynamic_cast<const scene3d::LightComponent*>(&component)) {
                const auto& desc = light->desc();
                if (desc.type != scene3d::LightType::Point
                    && (!finite(desc.direction) || core::length(desc.direction) <= 1e-6f))
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-LIGHT-DESC-INVALID");
                const auto direction = desc.type == scene3d::LightType::Point
                    ? core::Vec3{0.0f, 0.0f, 1.0f}
                    : core::normalizeOr(desc.direction, core::Vec3{0.0f, -1.0f, 0.0f});
                const auto right =
                    core::normalizeOr(core::cross(direction, core::Vec3{0.0f, 1.0f, 0.0f}),
                        core::Vec3{1.0f, 0.0f, 0.0f});
                const auto up =
                    core::normalizeOr(core::cross(right, direction), core::Vec3{0.0f, 1.0f, 0.0f});
                auto edge = [&](core::Vec3 from, core::Vec3 to) {
                    return add(object,
                        component,
                        EditorSceneComponentGizmoKind::Light,
                        transform,
                        from,
                        to);
                };
                if (desc.type == scene3d::LightType::Directional) {
                    if (!edge({}, direction)
                        || !edge(direction, direction + core::scale(right, 0.2f))
                        || !edge(direction, direction - core::scale(right, 0.2f)))
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                } else if (desc.type == scene3d::LightType::Point) {
                    if (!std::isfinite(desc.range) || desc.range < 0.0f)
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-LIGHT-DESC-INVALID");
                    if (desc.range > 0.0f) {
                        const core::Vec3 axes[][2] = {
                            {right, up}, {right, direction}, {up, direction}};
                        for (const auto& axis : axes) {
                            core::Vec3 previous = core::scale(axis[0], desc.range);
                            for (std::size_t i = 1; i <= kCircleSegments; ++i) {
                                const float angle = 2.0f * kPi * static_cast<float>(i)
                                    / static_cast<float>(kCircleSegments);
                                const auto point =
                                    core::scale(axis[0], desc.range * std::cos(angle))
                                    + core::scale(axis[1], desc.range * std::sin(angle));
                                if (!edge(previous, point))
                                    return fail(
                                        "ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                                previous = point;
                            }
                        }
                    } else {
                        constexpr float markerRadius = 0.25f;
                        if (!edge({-markerRadius, 0.0f, 0.0f}, {markerRadius, 0.0f, 0.0f})
                            || !edge({0.0f, -markerRadius, 0.0f}, {0.0f, markerRadius, 0.0f})
                            || !edge({0.0f, 0.0f, -markerRadius}, {0.0f, 0.0f, markerRadius}))
                            return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                    }
                } else if (desc.type == scene3d::LightType::Spot) {
                    if (!std::isfinite(desc.range) || desc.range <= 0.0f
                        || !std::isfinite(desc.spotConeAngle) || desc.spotConeAngle <= 0.0f
                        || desc.spotConeAngle >= kPi)
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-LIGHT-DESC-INVALID");
                    const float radius = desc.range * std::tan(desc.spotConeAngle * 0.5f);
                    if (!std::isfinite(radius))
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-LIGHT-DESC-INVALID");
                    const auto center = core::scale(direction, desc.range);
                    for (std::size_t i = 0; i < kCircleSegments; ++i) {
                        const auto a0 = 2.0f * kPi * static_cast<float>(i) / kCircleSegments;
                        const auto a1 = 2.0f * kPi * static_cast<float>(i + 1u) / kCircleSegments;
                        const auto p0 = center + core::scale(right, radius * std::cos(a0))
                            + core::scale(up, radius * std::sin(a0));
                        const auto p1 = center + core::scale(right, radius * std::cos(a1))
                            + core::scale(up, radius * std::sin(a1));
                        if (!edge({}, p0) || !edge(p0, p1))
                            return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                    }
                } else {
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-LIGHT-DESC-INVALID");
                }
            } else if (const auto* collider =
                           dynamic_cast<const scene3d::ColliderComponent*>(&component)) {
                const auto& shape = collider->desc().body.shape;
                auto edge = [&](core::Vec3 from, core::Vec3 to) {
                    return add(object,
                        component,
                        EditorSceneComponentGizmoKind::Collider,
                        transform,
                        from,
                        to);
                };
                if (shape.kind == physics::ColliderShapeKind::Box) {
                    if (!finite(shape.halfExtents) || shape.halfExtents.x <= 0.0f
                        || shape.halfExtents.y <= 0.0f || shape.halfExtents.z <= 0.0f)
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-COLLIDER-DESC-INVALID");
                    std::array<core::Vec3, 8> corners{};
                    for (unsigned corner = 0; corner < corners.size(); ++corner) {
                        corners[corner] = {
                            (corner & 1u) ? shape.halfExtents.x : -shape.halfExtents.x,
                            (corner & 2u) ? shape.halfExtents.y : -shape.halfExtents.y,
                            (corner & 4u) ? shape.halfExtents.z : -shape.halfExtents.z};
                    }
                    for (unsigned corner = 0; corner < corners.size(); ++corner) {
                        for (unsigned axis = 0; axis < 3u; ++axis) {
                            if ((corner & (1u << axis)) == 0u
                                && !edge(corners[corner], corners[corner | (1u << axis)]))
                                return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                        }
                    }
                } else if (shape.kind == physics::ColliderShapeKind::Sphere
                    || shape.kind == physics::ColliderShapeKind::Cylinder
                    || shape.kind == physics::ColliderShapeKind::Capsule) {
                    if (!std::isfinite(shape.radius) || shape.radius <= 0.0f
                        || (shape.kind != physics::ColliderShapeKind::Sphere
                            && (!std::isfinite(shape.height) || shape.height <= 0.0f)))
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-COLLIDER-DESC-INVALID");
                    const auto circle = [&](float y) {
                        core::Vec3 previous{shape.radius, y, 0.0f};
                        for (std::size_t i = 1; i <= kCircleSegments; ++i) {
                            const auto angle = 2.0f * kPi * static_cast<float>(i)
                                / static_cast<float>(kCircleSegments);
                            const core::Vec3 point{
                                shape.radius * std::cos(angle), y, shape.radius * std::sin(angle)};
                            if (!edge(previous, point))
                                return false;
                            previous = point;
                        }
                        return true;
                    };
                    if (shape.kind == physics::ColliderShapeKind::Sphere) {
                        for (unsigned plane = 0; plane < 3u; ++plane) {
                            core::Vec3 previous = plane == 2u
                                ? core::Vec3{0.0f, shape.radius, 0.0f}
                                : core::Vec3{shape.radius, 0.0f, 0.0f};
                            for (std::size_t i = 1; i <= kCircleSegments; ++i) {
                                const auto angle = 2.0f * kPi * static_cast<float>(i)
                                    / static_cast<float>(kCircleSegments);
                                const float a = shape.radius * std::cos(angle);
                                const float b = shape.radius * std::sin(angle);
                                const core::Vec3 point = plane == 0u ? core::Vec3{a, b, 0.0f}
                                    : plane == 1u                    ? core::Vec3{a, 0.0f, b}
                                                                     : core::Vec3{0.0f, a, b};
                                if (!edge(previous, point))
                                    return fail(
                                        "ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                                previous = point;
                            }
                        }
                    } else if (shape.kind == physics::ColliderShapeKind::Cylinder) {
                        if (!circle(-shape.height) || !circle(shape.height))
                            return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                        for (unsigned side = 0; side < 4u; ++side) {
                            const auto angle = 0.5f * kPi * static_cast<float>(side);
                            const core::Vec3 point{shape.radius * std::cos(angle),
                                0.0f,
                                shape.radius * std::sin(angle)};
                            if (!edge({point.x, -shape.height, point.z},
                                    {point.x, shape.height, point.z}))
                                return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                        }
                    } else {
                        if (!circle(-shape.height) || !circle(shape.height))
                            return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                        for (unsigned side = 0; side < 4u; ++side) {
                            const auto azimuth = 0.5f * kPi * static_cast<float>(side);
                            const float x = std::cos(azimuth), z = std::sin(azimuth);
                            const core::Vec3 lower{
                                shape.radius * x, -shape.height, shape.radius * z};
                            const core::Vec3 upper{
                                shape.radius * x, shape.height, shape.radius * z};
                            if (!edge(lower, upper))
                                return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                            core::Vec3 previousLower = lower;
                            core::Vec3 previousUpper = upper;
                            for (unsigned segment = 1u; segment <= 8u; ++segment) {
                                const float angle = 0.5f * kPi * static_cast<float>(segment) / 8.0f;
                                const core::Vec3 lowerPoint{shape.radius * std::cos(angle) * x,
                                    -shape.height - shape.radius * std::sin(angle),
                                    shape.radius * std::cos(angle) * z};
                                const core::Vec3 upperPoint{shape.radius * std::cos(angle) * x,
                                    shape.height + shape.radius * std::sin(angle),
                                    shape.radius * std::cos(angle) * z};
                                if (!edge(previousLower, lowerPoint)
                                    || !edge(previousUpper, upperPoint))
                                    return fail(
                                        "ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                                previousLower = lowerPoint;
                                previousUpper = upperPoint;
                            }
                        }
                    }
                } else {
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-COLLIDER-SHAPE-TOPOLOGY-UNAVAILABLE");
                }
            } else if (const auto* terrain =
                           dynamic_cast<const scene3d::TerrainComponent*>(&component)) {
                if (terrain->resourceId().empty())
                    continue;

                const auto& resourceId = terrain->resourceId();
                const auto* resourceRecord = resources.recordSnapshot(resourceId);
                if (resourceRecord == nullptr)
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-RESOURCE-MISSING");
                if (resourceRecord->type != resource::TerrainAssetResource::resourceTypeName())
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-RESOURCE-TYPE-MISMATCH");

                const auto handle = resources.find<resource::TerrainAssetResource>(resourceId);
                if (!handle.ready())
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-RESOURCE-NOT-READY");

                const auto* asset = handle.get();
                if (asset == nullptr || !asset->bounds().valid() || !asset->heightRange().valid()
                    || !std::isfinite(asset->tileWorldSize()) || asset->tileWorldSize() <= 0.0f
                    || asset->tileCountX() == 0u || asset->tileCountZ() == 0u)
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-BOUNDS-INVALID");

                const auto assetBounds = asset->bounds();
                const auto heightRange = asset->heightRange();
                const auto bounds = scene3d::Bounds3D::explicitBounds(
                    {(assetBounds.minX + assetBounds.maxX) * 0.5f,
                        (heightRange.minimum + heightRange.maximum) * 0.5f,
                        (assetBounds.minZ + assetBounds.maxZ) * 0.5f},
                    {(assetBounds.maxX - assetBounds.minX) * 0.5f,
                        (heightRange.maximum - heightRange.minimum) * 0.5f,
                        (assetBounds.maxZ - assetBounds.minZ) * 0.5f});
                const auto metadata = scene3d::TerrainMetadata::explicitGrid(
                    asset->tileCountX(), asset->tileCountZ(), asset->tileWorldSize());
                if (!metadata.valid() || !bounds.valid() || !finite(bounds.center)
                    || !finite(bounds.extents))
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-BOUNDS-INVALID");

                std::array<core::Vec3, 8> corners{};
                for (unsigned corner = 0; corner < corners.size(); ++corner) {
                    corners[corner] = {
                        bounds.center.x + ((corner & 1u) ? bounds.extents.x : -bounds.extents.x),
                        bounds.center.y + ((corner & 2u) ? bounds.extents.y : -bounds.extents.y),
                        bounds.center.z + ((corner & 4u) ? bounds.extents.z : -bounds.extents.z)};
                }
                for (unsigned corner = 0; corner < corners.size(); ++corner) {
                    for (unsigned axis = 0; axis < 3u; ++axis) {
                        if ((corner & (1u << axis)) == 0u
                            && !add(object,
                                component,
                                EditorSceneComponentGizmoKind::TerrainBounds,
                                transform,
                                corners[corner],
                                corners[corner | (1u << axis)]))
                            return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                    }
                }
            } else if (const auto* terrainAuthoring =
                           dynamic_cast<const scene3d::TerrainAuthoringComponent*>(&component)) {
                const float halfExtent = terrainAuthoring->half_extent();
                if (!std::isfinite(halfExtent) || halfExtent <= 0.0f)
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-TERRAIN-AUTHORING-DESC-INVALID");
                const std::array<core::Vec3, 4> corners{{
                    {-halfExtent, 0.0f, -halfExtent},
                    {halfExtent, 0.0f, -halfExtent},
                    {halfExtent, 0.0f, halfExtent},
                    {-halfExtent, 0.0f, halfExtent},
                }};
                for (std::size_t index = 0; index < corners.size(); ++index) {
                    if (!add(object,
                            component,
                            EditorSceneComponentGizmoKind::TerrainAuthoring,
                            transform,
                            corners[index],
                            corners[(index + 1u) % corners.size()]))
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                }
            } else if (const auto* critterField =
                           dynamic_cast<const scene3d::CritterFieldComponent*>(&component)) {
                const float worldMaxX = critterField->world_max_x();
                const float worldMinZ = critterField->world_min_z();
                const float worldMaxZ = critterField->world_max_z();
                const float waterLevel = critterField->water_level();
                if (!std::isfinite(worldMaxX) || worldMaxX <= 0.0f || !std::isfinite(worldMinZ)
                    || !std::isfinite(worldMaxZ) || worldMinZ >= worldMaxZ
                    || !std::isfinite(waterLevel))
                    return fail("ARP-EDITOR-COMPONENT-GIZMO-CRITTER-FIELD-DESC-INVALID");
                const std::array<core::Vec3, 4> corners{{
                    {-worldMaxX, waterLevel, worldMinZ},
                    {worldMaxX, waterLevel, worldMinZ},
                    {worldMaxX, waterLevel, worldMaxZ},
                    {-worldMaxX, waterLevel, worldMaxZ},
                }};
                for (std::size_t index = 0; index < corners.size(); ++index) {
                    if (!add(object,
                            component,
                            EditorSceneComponentGizmoKind::CritterField,
                            core::Mat4::identity(),
                            corners[index],
                            corners[(index + 1u) % corners.size()]))
                        return fail("ARP-EDITOR-COMPONENT-GIZMO-SEGMENT-BUDGET-EXCEEDED");
                }
            }
        }
    }
    return result;
}

} // namespace arp::editor
