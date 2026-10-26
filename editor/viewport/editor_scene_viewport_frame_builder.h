#pragma once

#include "editor/viewport/editor_scene_viewport_contract.h"
#include "resource/resource_manager.h"
#include "scene/scene.h"

namespace arp::editor {

struct EditorSceneViewportFrameBuildResult {
    EditorSceneViewportFrame frame;
    EditorSceneViewportValidationResult validation;

    bool valid() const noexcept { return validation.valid(); }
};

ARP_API EditorSceneViewportFrameBuildResult buildEditorSceneViewportFrame(const scene::Scene& scene,
    const scene::SceneTraversalSnapshot& snapshot,
    std::uint64_t stateVersion,
    std::uint32_t targetWidth,
    std::uint32_t targetHeight,
    EditorSceneViewportRect physicalViewport,
    const resource::ResourceManager* resources = nullptr,
    std::size_t maxDrawItems = 0u,
    const scene::SceneTraversalSnapshot* visibleSnapshot = nullptr);

} // namespace arp::editor
