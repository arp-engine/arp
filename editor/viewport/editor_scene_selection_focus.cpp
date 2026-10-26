#include "editor/viewport/editor_scene_selection_focus.h"

#include <algorithm>
#include <cmath>

namespace arp::editor {

EditorSceneSelectionFocus::Result EditorSceneSelectionFocus::apply(
    EditorSceneViewportCamera& camera,
    const scene::SceneTraversalSnapshot& snapshot,
    scene::GameObjectId primary,
    core::ArrayView<scene::GameObjectId> additional,
    float viewportWidth,
    float viewportHeight) noexcept
{
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth <= 0
        || viewportHeight <= 0)
        return Result::InvalidViewport;
    if (!snapshot.ok() || !primary.valid() || (!additional.empty() && !additional.data()))
        return Result::InvalidSelection;
    const auto* first = snapshot.findNode(primary);
    if (!first)
        return Result::InvalidSelection;
    const auto finite = [](core::Vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    auto minimum = first->worldTransform.translation;
    auto maximum = minimum;
    if (!finite(minimum))
        return Result::InvalidTransform;
    for (std::size_t i = 0; i < additional.size(); ++i) {
        const auto id = additional[i];
        if (!id.valid() || id == primary
            || std::find(additional.begin(), additional.begin() + i, id) != additional.begin() + i)
            return Result::InvalidSelection;
        const auto* node = snapshot.findNode(id);
        if (!node)
            return Result::InvalidSelection;
        const auto position = node->worldTransform.translation;
        if (!finite(position))
            return Result::InvalidTransform;
        minimum = {std::min(minimum.x, position.x),
            std::min(minimum.y, position.y),
            std::min(minimum.z, position.z)};
        maximum = {std::max(maximum.x, position.x),
            std::max(maximum.y, position.y),
            std::max(maximum.z, position.z)};
    }
    const core::Vec3 target{float((double(minimum.x) + maximum.x) * 0.5),
        float((double(minimum.y) + maximum.y) * 0.5),
        float((double(minimum.z) + maximum.z) * 0.5)};
    auto prepared = camera;
    prepared.orbitAround(0, 0, target);
    // Large finite coordinates can lose the eye/target separation in float.
    // Reject the real projection instead of guessing an allowed world radius.
    float x = 0, y = 0;
    if (!finite(prepared.position())
        || !prepared.project(target, viewportWidth, viewportHeight, x, y))
        return Result::CameraRange;
    const auto old = camera.target();
    if (old.x == target.x && old.y == target.y && old.z == target.z)
        return Result::Unchanged;
    camera = prepared;
    return Result::Changed;
}
} // namespace arp::editor
