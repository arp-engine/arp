#pragma once

#include "core/api.h"
#include "core/math/math.h"
#include "core/memory/cpu_allocator.h"
#include "scene/ids.h"
#include "scene/scene.h"

#include <cstddef>

namespace arp::editor {

inline constexpr std::size_t kEditorScenePhysicsOverlaySegmentBudget = 1024u;
inline constexpr std::size_t kEditorScenePhysicsOverlayCircleSegments = 16u;

enum class EditorScenePhysicsOverlayKind { Collider, Joint };

struct EditorScenePhysicsOverlaySegment {
    scene::GameObjectId objectId;
    scene::ComponentId componentId;
    EditorScenePhysicsOverlayKind kind = EditorScenePhysicsOverlayKind::Collider;
    core::Vec2 from{};
    core::Vec2 to{};
    bool sensor = false;
};

struct EditorScenePhysicsOverlay {
    core::pmr::Vector<EditorScenePhysicsOverlaySegment> segments{&core::defaultCpuMemoryResource()};
    bool valid = true;
    const char* diagnostic = "";
};

ARP_API EditorScenePhysicsOverlay buildEditorScenePhysicsOverlay(const scene::Scene& scene,
    std::size_t maxSegments,
    std::size_t circleSegments,
    const scene::SceneTraversalSnapshot* visibleSnapshot = nullptr);

} // namespace arp::editor
