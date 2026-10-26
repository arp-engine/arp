#include "editor/viewport/editor_scene_transform_frame.h"

namespace arp::editor {
namespace {
constexpr std::array<core::Vec3, 3> canonicalAxes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

bool finite(core::Vec3 v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool validRotation(core::Quat q) noexcept
{
    const float length = core::dot(q, q);
    return std::isfinite(length) && length >= 1.0e-12f;
}

bool finite(const scene::Transform& value) noexcept
{
    return finite(value.translation) && finite(value.scale) && validRotation(value.rotation);
}

float& component(core::Vec3& value, unsigned index) noexcept
{
    return index == 0 ? value.x : index == 1 ? value.y : value.z;
}
} // namespace

bool EditorSceneTransformFrame::build(core::Quat worldRotation,
    EditorSceneTransformSpace space,
    EditorSceneTransformFrame& destination) noexcept
{
    if ((space != EditorSceneTransformSpace::World && space != EditorSceneTransformSpace::Local)
        || !validRotation(worldRotation))
        return false;
    EditorSceneTransformFrame candidate;
    const auto rotation = core::normalize(worldRotation);
    for (unsigned i = 0; i != 3; ++i)
        candidate.axes[i] = space == EditorSceneTransformSpace::World
            ? canonicalAxes[i]
            : core::rotate(rotation, canonicalAxes[i]);
    destination = candidate;
    return true;
}

const char* EditorSceneTransformFrame::prepare(EditorSceneTransformTool tool,
    EditorSceneTransformSpace space,
    unsigned axis,
    double amount,
    const scene::Transform& startLocal,
    const scene::Transform& parentWorld,
    scene::Transform& destination) noexcept
{
    if (axis >= 3
        || (tool != EditorSceneTransformTool::Move && tool != EditorSceneTransformTool::Rotate
            && tool != EditorSceneTransformTool::Scale)
        || !std::isfinite(amount) || std::abs(amount) > std::numeric_limits<float>::max()
        || !finite(startLocal) || !finite(parentWorld))
        return "EDITOR-TRANSFORM-INVALID";
    if (std::abs(parentWorld.scale.x) <= minimumParentScale
        || std::abs(parentWorld.scale.y) <= minimumParentScale
        || std::abs(parentWorld.scale.z) <= minimumParentScale)
        return "EDITOR-TRANSFORM-PARENT-SINGULAR";
    auto world = scene::composeTransforms(parentWorld, startLocal);
    EditorSceneTransformFrame frame;
    if (!finite(world) || !build(world.rotation, space, frame))
        return "EDITOR-TRANSFORM-INVALID";
    if (amount == 0) {
        destination = startLocal;
        return nullptr;
    }
    if (tool != EditorSceneTransformTool::Scale || space != EditorSceneTransformSpace::Local)
        return prepareWorld(tool, frame.axes[axis], amount, startLocal, parentWorld, destination);
    auto candidate = startLocal;
    component(candidate.scale, axis) += static_cast<float>(amount);
    if (!finite(candidate))
        return "EDITOR-TRANSFORM-INVALID";
    destination = candidate;
    return nullptr;
}

bool EditorSceneTransformFrame::validAuthoring(const scene::Transform& value) noexcept
{
    return finite(value) && std::abs(value.translation.x) <= maximumTranslation
        && std::abs(value.translation.y) <= maximumTranslation
        && std::abs(value.translation.z) <= maximumTranslation
        && std::abs(value.scale.x) <= maximumScale && std::abs(value.scale.y) <= maximumScale
        && std::abs(value.scale.z) <= maximumScale;
}

bool EditorSceneTransformFrame::canonicalize(
    const scene::Transform& value, scene::Transform& destination) noexcept
{
    if (!validAuthoring(value))
        return false;
    auto candidate = value;
    candidate.rotation = core::normalize(candidate.rotation);
    destination = candidate;
    return true;
}

bool EditorSceneTransformFrame::alignedAxis(
    core::Quat rotation, core::Vec3 worldAxis, unsigned& destination) noexcept
{
    if (!finite(worldAxis) || std::abs(core::length(worldAxis) - 1) > axisTolerance)
        return false;
    EditorSceneTransformFrame local;
    if (!build(rotation, EditorSceneTransformSpace::Local, local))
        return false;
    for (unsigned i = 0; i != 3; ++i)
        if (core::distance(local.axes[i], worldAxis) <= axisTolerance
            || core::distance(local.axes[i], core::scale(worldAxis, -1)) <= axisTolerance) {
            destination = i;
            return true;
        }
    return false;
}

const char* EditorSceneTransformFrame::prepareWorld(EditorSceneTransformTool tool,
    core::Vec3 worldAxis,
    double amount,
    const scene::Transform& startLocal,
    const scene::Transform& parentWorld,
    scene::Transform& destination) noexcept
{
    if (!finite(worldAxis) || std::abs(core::length(worldAxis) - 1) > axisTolerance
        || (tool != EditorSceneTransformTool::Move && tool != EditorSceneTransformTool::Rotate
            && tool != EditorSceneTransformTool::Scale)
        || !std::isfinite(amount) || std::abs(amount) > std::numeric_limits<float>::max()
        || !finite(startLocal) || !finite(parentWorld))
        return "EDITOR-TRANSFORM-INVALID";
    if (std::abs(parentWorld.scale.x) <= minimumParentScale
        || std::abs(parentWorld.scale.y) <= minimumParentScale
        || std::abs(parentWorld.scale.z) <= minimumParentScale)
        return "EDITOR-TRANSFORM-PARENT-SINGULAR";
    auto world = scene::composeTransforms(parentWorld, startLocal);
    if (!finite(world))
        return "EDITOR-TRANSFORM-INVALID";
    if (amount == 0) {
        destination = startLocal;
        return nullptr;
    }
    auto candidate = startLocal;
    const float delta = static_cast<float>(amount);
    if (tool == EditorSceneTransformTool::Move) {
        world.translation = world.translation + core::scale(worldAxis, delta);
        candidate.translation = scene::deriveLocalTransform(parentWorld, world).translation;
    } else if (tool == EditorSceneTransformTool::Rotate) {
        world.rotation =
            core::normalize(core::Quat::fromAxisAngle(worldAxis, delta) * world.rotation);
        candidate.rotation = scene::deriveLocalTransform(parentWorld, world).rotation;
    } else {
        unsigned aligned = 0;
        if (!alignedAxis(world.rotation, worldAxis, aligned))
            return "EDITOR-TRANSFORM-WORLD-SCALE-SHEAR";
        component(world.scale, aligned) += delta;
        candidate.scale = scene::deriveLocalTransform(parentWorld, world).scale;
    }
    if (!finite(candidate))
        return "EDITOR-TRANSFORM-INVALID";
    destination = candidate;
    return nullptr;
}

bool EditorSceneTransformFrame::same(const scene::Transform& a, const scene::Transform& b) noexcept
{
    return a.translation.x == b.translation.x && a.translation.y == b.translation.y
        && a.translation.z == b.translation.z && a.rotation.x == b.rotation.x
        && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z
        && a.rotation.w == b.rotation.w && a.scale.x == b.scale.x && a.scale.y == b.scale.y
        && a.scale.z == b.scale.z;
}
} // namespace arp::editor
