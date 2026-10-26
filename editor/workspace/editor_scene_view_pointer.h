#pragma once

#include "core/ownership/lifetime_epoch.h"
#include "editor/workspace/editor_layout.h"
#include "scene/ids.h"

namespace arp::editor {

// Owns an entire primary pointer sequence. Cancellation keeps swallowing its
// tail; FocusLost separately lets Runtime receive its cancellation barrier.
class EditorSceneViewPointer final {
public:
    struct Target {
        scene::GameObjectId object;
        ui::WidgetId panel;
        std::size_t row = 0, offset = 0;
        layout::SceneHierarchyAction action = layout::SceneHierarchyAction::Visibility;
        ui::LayoutRect rect;

        bool same(const Target& other) const noexcept
        {
            return object == other.object && panel == other.panel && row == other.row
                && offset == other.offset && action == other.action && rect.x == other.rect.x
                && rect.y == other.rect.y && rect.width == other.rect.width
                && rect.height == other.rect.height;
        }
    };

    bool captured = false, cancelled = false, suppressRelease = false;
    Target target;
    std::uint64_t documentEpoch = 0, viewRevision = 0;
    core::LifetimeEpoch::Ticket owner;

    void cancel() noexcept { cancelled = captured; }

    void reset() noexcept { *this = {}; }
};

} // namespace arp::editor
