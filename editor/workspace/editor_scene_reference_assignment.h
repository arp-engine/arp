#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "editor/workspace/editor_scene_inspector.h"

#include <cstddef>

namespace arp::editor {
class ProjectResourceLocations;

// Cold single-field reference preflight. Only owned values leave preparation;
// applying the value still belongs to the shared property/history command.
class EditorSceneReferenceAssignment final {
public:
    enum class SourceKind : std::uint8_t { Object, Resource };

    struct Intent {
        SourceKind sourceKind = SourceKind::Object;
        std::uint64_t objectId = 0, componentId = 0;
        std::uint64_t documentEpoch = 0, resourceRevision = 0;
        std::uint64_t sourceObjectId = 0;
        core::pmr::String property, sourceResourceId;
    };

    struct Context {
        std::uint64_t documentEpoch = 0, resourceRevision = 0;
        std::uint64_t selectedObjectId = 0;
        std::size_t selectedCount = 0;
        core::LifetimeEpoch::Ticket sceneOwner;
    };

    struct Prepared {
        SceneInspectorPropertyKind kind = SceneInspectorPropertyKind::Unsupported;
        std::uint64_t objectId = 0;
        core::pmr::String resourceId;
        bool changed = false;

        void swap(Prepared& other) noexcept;
    };

    // Context is the caller's retained publication/Inspector binding, not a new
    // ticket synthesized from the document after a drag or request goes stale.
    // nullptr means success. Rejection preserves output, Scene and selection.
    static const char* prepare(const EditorSceneDocument& document,
        const resource::ResourceManager& resources,
        const ProjectResourceLocations& locations,
        const Context& context,
        const Intent& intent,
        Prepared& destination) noexcept;
};
} // namespace arp::editor
