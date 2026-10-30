#pragma once
#include "2d/sprite_draw_plan.h"
#include "rhi/gpu_common.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::editor {
class EditorShell;

// One authoring extraction/residency path for native and ordered viewports.
ARP_API bool extractEditorSceneViewportSprites(EditorShell&,
    const scene::SceneTraversalSnapshot&,
    rhi::Extent2D,
    std::uint32_t maxDraws,
    core::pmr::Vector<scene2d::SpriteDrawItem>&,
    core::pmr::String& diagnostic);

} // namespace arp::editor
