#pragma once

#include "editor/workspace/editor_scene_authoring_codec.h"

#include <optional>

namespace arp::editor {

// Session-only authoring view policy. Never writes Scene active, components,
// resources, serialization or history. All calls belong to the Editor owner thread.
class EditorSceneViewState final {
public:
    static constexpr std::size_t maxEntries = EditorSceneAuthoringCodec::maxObjects + 1;

    struct Flags {
        bool hidden = false;
        bool locked = false;

        bool operator==(const Flags& other) const noexcept
        {
            return hidden == other.hidden && locked == other.locked;
        }
    };

    struct Observation {
        bool valid = false;
        Flags local, effective;
        const char* diagnostic = nullptr;
    };

    struct Intent {
        std::uint64_t documentEpoch = 0;
        core::LifetimeEpoch::Ticket owner;
        scene::GameObjectId objectId;
        Flags flags;
    };

private:
    struct Entry {
        scene::GameObjectId id;
        Flags flags;
    };

public:
    // Prepared storage is one-shot, noncopyable and allocator-aware. It is not
    // an authorization to commit against another live document or state revision.
    class Prepared final {
    public:
        Prepared() = default;
        Prepared(const Prepared&) = delete;
        Prepared& operator=(const Prepared&) = delete;

    private:
        friend class EditorSceneViewState;
        const EditorSceneViewState* state_ = nullptr;
        const scene::Scene* scene_ = nullptr;
        core::LifetimeEpoch::Ticket owner_;
        std::uint64_t documentEpoch_ = 0, revision_ = 0;
        scene::GameObjectId target_;
        core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
        bool ready_ = false, changed_ = false, consumed_ = false;
    };

    enum class CommitResult { Changed, Unchanged, Stale, Consumed };

    explicit EditorSceneViewState(std::size_t capacity = maxEntries) noexcept : capacity_(capacity)
    {}

    Observation observe(const EditorSceneDocument&,
        std::uint64_t documentEpoch,
        scene::GameObjectId) const noexcept;
    // A detached Editor projection for existing native extractors. The source
    // must be the current full traversal of this document on its owner thread.
    // Camera resolution keeps the original source. OOM never changes either.
    std::optional<scene::SceneTraversalSnapshot> visibleSnapshot(const EditorSceneDocument&,
        std::uint64_t documentEpoch,
        const scene::SceneTraversalSnapshot& source,
        const char*& diagnostic) const noexcept;
    // Ancestor edits must not indirectly change a locked descendant's pose,
    // active state, ownership or existence. Names and own fields use observe.
    bool subtreeEditable(const EditorSceneDocument&,
        std::uint64_t documentEpoch,
        scene::GameObjectId) const noexcept;
    bool prepare(const EditorSceneDocument&,
        std::uint64_t documentEpoch,
        const Intent&,
        Prepared&,
        const char*& diagnostic) const noexcept;
    CommitResult commit(
        const EditorSceneDocument&, std::uint64_t documentEpoch, Prepared&) noexcept;
    void reset() noexcept;

    std::size_t storedCount() const noexcept { return entries_.size(); }

    std::uint64_t revision() const noexcept { return revision_; }

private:
    bool matches(const EditorSceneDocument&, std::uint64_t documentEpoch) const noexcept;
    bool validContext(const EditorSceneDocument&, std::uint64_t documentEpoch) const noexcept;
    Flags local(scene::GameObjectId) const noexcept;

    std::size_t capacity_;
    const scene::Scene* scene_ = nullptr;
    core::LifetimeEpoch::Ticket owner_;
    std::uint64_t documentEpoch_ = 0, revision_ = 1;
    core::pmr::Vector<Entry> entries_{&core::defaultCpuMemoryResource()};
};

} // namespace arp::editor
