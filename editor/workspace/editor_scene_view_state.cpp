#include "editor/workspace/editor_scene_view_state.h"

#include <algorithm>
#include <limits>
#include <new>

namespace arp::editor {

bool EditorSceneViewState::validContext(
    const EditorSceneDocument& document, std::uint64_t epoch) const noexcept
{
    return epoch != 0 && capacity_ != 0 && capacity_ <= maxEntries
        && document.ownerTicket().isCurrent()
        && document.scene().objects().size() <= EditorSceneAuthoringCodec::maxObjects;
}

bool EditorSceneViewState::matches(
    const EditorSceneDocument& document, std::uint64_t epoch) const noexcept
{
    return scene_ == &document.scene() && documentEpoch_ == epoch
        && owner_.sameCurrentEpoch(document.ownerTicket());
}

EditorSceneViewState::Flags EditorSceneViewState::local(scene::GameObjectId id) const noexcept
{
    const auto found = std::find_if(entries_.begin(), entries_.end(), [id](const auto& entry) {
        return entry.id == id;
    });
    return found == entries_.end() ? Flags{} : found->flags;
}

EditorSceneViewState::Observation EditorSceneViewState::observe(
    const EditorSceneDocument& document, std::uint64_t epoch, scene::GameObjectId id) const noexcept
{
    Observation result;
    result.diagnostic = "EDITOR-VIEW-STATE-CONTEXT";
    if (!validContext(document, epoch))
        return result;
    result.diagnostic = "EDITOR-VIEW-STATE-OBJECT";
    const auto* object = document.scene().findObject(id);
    if (!object)
        return result;
    const bool bound = matches(document, epoch);
    result.local = bound ? local(id) : Flags{};
    // Validate ancestry even with no stored overrides: missing parents/cycles
    // must not grant selection or editing through an incomplete hierarchy.
    for (std::size_t depth = 0; depth <= document.scene().objects().size(); ++depth) {
        const auto flags = bound ? local(object->id()) : Flags{};
        result.effective.hidden = result.effective.hidden || flags.hidden;
        result.effective.locked = result.effective.locked || flags.locked;
        if (!object->parentId().valid()) {
            if (object->id() != document.scene().rootId())
                break;
            result.valid = true;
            result.diagnostic = nullptr;
            return result;
        }
        object = document.scene().findObject(object->parentId());
        if (!object)
            break;
    }
    result.diagnostic = "EDITOR-VIEW-STATE-HIERARCHY";
    return result;
}

bool EditorSceneViewState::subtreeEditable(
    const EditorSceneDocument& document, std::uint64_t epoch, scene::GameObjectId id) const noexcept
{
    const auto own = observe(document, epoch, id);
    if (!own.valid || own.effective.locked)
        return false;
    for (const auto& candidate : document.scene().objects()) {
        const auto view = observe(document, epoch, candidate->id());
        if (!view.valid)
            return false;
        if (!view.effective.locked)
            continue;
        const auto* current = candidate.get();
        for (std::size_t depth = 0; current && depth <= document.scene().objects().size();
            ++depth) {
            if (current->id() == id)
                return false;
            current = document.scene().findObject(current->parentId());
        }
    }
    return true;
}

std::optional<scene::SceneTraversalSnapshot> EditorSceneViewState::visibleSnapshot(
    const EditorSceneDocument& document,
    std::uint64_t epoch,
    const scene::SceneTraversalSnapshot& source,
    const char*& diagnostic) const noexcept
{
    diagnostic = "EDITOR-VIEW-STATE-SNAPSHOT";
    if (!validContext(document, epoch) || !source.ok()
        || source.nodes().size() != document.scene().objects().size() + 1)
        return std::nullopt;
    try {
        core::pmr::Vector<scene::SceneTraversalNode> nodes{&core::defaultCpuMemoryResource()};
        nodes.reserve(source.nodes().size());
        for (const auto& node : source.nodes()) {
            const auto* object = document.scene().findObject(node.objectId);
            if (!object || object->parentId() != node.parentId
                || std::count_if(source.nodes().begin(),
                       source.nodes().end(),
                       [&](const auto& other) {
                           return other.objectId == node.objectId;
                       })
                    != 1)
                return std::nullopt;
            const auto view = observe(document, epoch, node.objectId);
            if (!view.valid) {
                diagnostic = view.diagnostic;
                return std::nullopt;
            }
            if (!view.effective.hidden)
                nodes.push_back(node);
        }
        diagnostic = nullptr;
        return scene::SceneTraversalSnapshot::success(std::move(nodes));
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-VIEW-STATE-SNAPSHOT-ALLOCATION-FAILED";
        return std::nullopt;
    }
}

bool EditorSceneViewState::prepare(const EditorSceneDocument& document,
    std::uint64_t epoch,
    const Intent& intent,
    Prepared& destination,
    const char*& diagnostic) const noexcept
{
    diagnostic = "EDITOR-VIEW-STATE-STALE";
    if (!validContext(document, epoch) || intent.documentEpoch != epoch
        || !intent.owner.sameCurrentEpoch(document.ownerTicket()) || destination.ready_)
        return false;
    const auto current = observe(document, epoch, intent.objectId);
    if (!current.valid) {
        diagnostic = current.diagnostic;
        return false;
    }
    diagnostic = "EDITOR-VIEW-STATE-REVISION-EXHAUSTED";
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
        return false;
    try {
        core::pmr::Vector<Entry> entries{&core::defaultCpuMemoryResource()};
        // Count before reserving. Removed IDs never consume a future override's
        // capacity; no target or ancestor can be inferred from a reused name.
        std::size_t count = 0;
        if (matches(document, epoch))
            for (const auto& entry : entries_)
                if (entry.id != intent.objectId && document.scene().findObject(entry.id))
                    ++count;
        if (intent.flags.hidden || intent.flags.locked)
            ++count;
        diagnostic = "EDITOR-VIEW-STATE-CAPACITY";
        if (count > capacity_)
            return false;
        diagnostic = "EDITOR-VIEW-STATE-ALLOCATION-FAILED";
        entries.reserve(count);
        if (matches(document, epoch))
            for (const auto& entry : entries_)
                if (entry.id != intent.objectId && document.scene().findObject(entry.id))
                    entries.push_back(entry);
        if (intent.flags.hidden || intent.flags.locked)
            entries.push_back({intent.objectId, intent.flags});
        destination.state_ = this;
        destination.scene_ = &document.scene();
        destination.owner_ = document.ownerTicket();
        destination.documentEpoch_ = epoch;
        destination.revision_ = revision_;
        destination.target_ = intent.objectId;
        destination.changed_ = !(current.local == intent.flags);
        destination.entries_.swap(entries);
        destination.ready_ = true;
        diagnostic = nullptr;
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

EditorSceneViewState::CommitResult EditorSceneViewState::commit(
    const EditorSceneDocument& document, std::uint64_t epoch, Prepared& prepared) noexcept
{
    if (prepared.consumed_)
        return CommitResult::Consumed;
    if (!prepared.ready_ || prepared.state_ != this || !validContext(document, epoch)
        || prepared.scene_ != &document.scene() || prepared.documentEpoch_ != epoch
        || prepared.revision_ != revision_
        || !prepared.owner_.sameCurrentEpoch(document.ownerTicket())
        || !observe(document, epoch, prepared.target_).valid)
        return CommitResult::Stale;
    for (const auto& entry : prepared.entries_)
        if (!document.scene().findObject(entry.id))
            return CommitResult::Stale;
    prepared.consumed_ = true;
    if (!prepared.changed_)
        return CommitResult::Unchanged;
    entries_.swap(prepared.entries_);
    scene_ = &document.scene();
    owner_ = document.ownerTicket();
    documentEpoch_ = epoch;
    ++revision_;
    return CommitResult::Changed;
}

void EditorSceneViewState::reset() noexcept
{
    entries_.clear();
    scene_ = nullptr;
    owner_ = {};
    documentEpoch_ = 0;
    if (revision_ != std::numeric_limits<std::uint64_t>::max())
        ++revision_;
}

} // namespace arp::editor
