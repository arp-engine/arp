#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/math.h"
#include "resource/resource_manager.h"
#include "scene/ids.h"
#include "scene/scene.h"

#include <cstddef>

namespace arp::editor {

inline constexpr std::size_t kEditorSceneComponentGizmoSegmentBudget = 2048u;

enum class EditorSceneComponentGizmoKind {
    Camera,
    Light,
    Collider,
    TerrainAuthoring,
    TerrainBounds,
    CritterField
};

struct EditorSceneComponentGizmoSegment {
    scene::GameObjectId objectId;
    scene::ComponentId componentId;
    EditorSceneComponentGizmoKind kind = EditorSceneComponentGizmoKind::Camera;
    core::Vec3 from{};
    core::Vec3 to{};
};

struct EditorSceneComponentGizmos {
    core::pmr::Vector<EditorSceneComponentGizmoSegment> segments{&core::defaultCpuMemoryResource()};
    bool valid = true;
    const char* diagnostic = "";
};

ARP_API EditorSceneComponentGizmos buildEditorSceneComponentGizmos(const scene::Scene& scene,
    const resource::ResourceManager& resources,
    float viewportAspect,
    std::size_t maxSegments);

} // namespace arp::editor
