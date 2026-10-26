#pragma once

#include "core/container/array_view.h"
#include "editor/viewport/editor_scene_viewport_camera.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::editor {

// Editor navigation targets authored world origins, independent of renderable
// geometry or resource readiness. The caller owns the snapshot/selection budget.
class EditorSceneSelectionFocus final {
public:
    enum class Result {
        Changed,
        Unchanged,
        InvalidSelection,
        InvalidViewport,
        InvalidTransform,
        CameraRange
    };

    static Result apply(EditorSceneViewportCamera& camera,
        const scene::SceneTraversalSnapshot& snapshot,
        scene::GameObjectId primary,
        core::ArrayView<scene::GameObjectId> additional,
        float viewportWidth,
        float viewportHeight) noexcept;
};
} // namespace arp::editor
