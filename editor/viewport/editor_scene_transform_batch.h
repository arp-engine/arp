#pragma once

#include "core/container/array_view.h"
#include "editor/viewport/editor_scene_transform_frame.h"
#include "scene/scene_traversal_snapshot.h"

namespace arp::editor {

enum class EditorSceneTransformPivot : unsigned char { Active, Center, Individual };

// Cold, owned numeric preparation. No Scene, history, UI or live object owner is
// retained or mutated. The caller must separately authorize a later commit.
class EditorSceneTransformBatch final {
public:
    struct Budget {
        std::size_t maxSelectedObjects = 0;
        std::size_t maxSnapshotNodes = 0;
    };

    struct Member {
        scene::GameObjectId objectId, parentId;
        scene::Transform local, world, parentWorld;
        bool effective = false;
    };

    struct Change {
        scene::GameObjectId objectId, parentId;
        scene::Transform beforeLocal, afterLocal, parentWorld;
    };

    explicit EditorSceneTransformBatch(
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : members_(resource)
    {}

    EditorSceneTransformBatch(const EditorSceneTransformBatch&) = delete;
    EditorSceneTransformBatch& operator=(const EditorSceneTransformBatch&) = delete;

    static const char* capture(const scene::SceneTraversalSnapshot& snapshot,
        scene::GameObjectId primary,
        core::ArrayView<scene::GameObjectId> selected,
        EditorSceneTransformPivot pivot,
        Budget budget,
        EditorSceneTransformBatch& destination) noexcept;

    // All changes are prepared before output is swapped. Rejection, including
    // allocation failure, leaves the caller's previous output unchanged.
    const char* prepare(EditorSceneTransformTool tool,
        EditorSceneTransformSpace space,
        unsigned axis,
        double amount,
        core::pmr::Vector<Change>& destination) const noexcept;

    // Checks the frozen start only, not a Scene after applying these changes.
    // Scene/history epochs and accepted-gesture state belong to the commit host.
    bool matchesStart(const scene::SceneTraversalSnapshot& snapshot,
        scene::GameObjectId primary,
        core::ArrayView<scene::GameObjectId> selected) const noexcept;

    core::ArrayView<Member> members() const noexcept { return members_; }

    std::size_t effectiveCount() const noexcept { return effectiveCount_; }

    core::Vec3 origin() const noexcept { return origin_; }

    EditorSceneTransformPivot pivot() const noexcept { return pivot_; }

    bool frame(
        EditorSceneTransformSpace space, EditorSceneTransformFrame& destination) const noexcept
    {
        return EditorSceneTransformFrame::build(primaryWorld_.rotation, space, destination);
    }

private:
    core::pmr::Vector<Member> members_;
    scene::GameObjectId primary_;
    scene::Transform primaryLocal_, primaryWorld_;
    EditorSceneTransformPivot pivot_ = EditorSceneTransformPivot::Active;
    core::Vec3 origin_{};
    std::size_t effectiveCount_ = 0;
    Budget budget_;
};
} // namespace arp::editor
