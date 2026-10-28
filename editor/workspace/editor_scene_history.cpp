#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <algorithm>
#include <cassert>
#include <optional>
#include <type_traits>
#include <utility>

namespace arp::editor {
using detail::sceneResourceManager;

EditorCommandOutcome EditorShell::beginSingleSceneTransformEdit(std::uint64_t objectId)
{
    const auto* object =
        sceneDocument_.scene().findObject(scene::GameObjectId::fromValue(objectId));
    if (!object || activeTransformEditObjectId_ || activeSingleSceneTransform_
        || activeSceneSelectionTransformEditId() || !sceneSingleTransformIds_.hasRemaining())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        auto prepared = core::makeUnique<SingleSceneTransformEdit>();
        prepared->entry.kind = SceneHistoryEntry::Kind::Transform;
        prepared->entry.objectId = objectId;
        prepared->entry.beforeTransform = object->local_transform();
        prepared->entry.afterTransform = prepared->entry.beforeTransform;
        prepared->undo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        prepared->sceneOwner = sceneDocument_.ownerTicket();
        prepared->historyOwner = sceneHistoryEpoch_.ticket();
        if (!prepared->sceneOwner.isCurrent() || !prepared->historyOwner.isCurrent())
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        prepared->parent = object->parentId();
        const auto* parent = object->parent_object();
        prepared->parentWorld = parent ? parent->world_transform() : scene::Transform::identity();
        prepared->documentEpoch = sceneDocumentEpoch_;
        prepared->id = sceneSingleTransformIds_.tryAcquire();
        activeSingleSceneTransform_ = std::move(prepared);
        activeTransformEditObjectId_ = objectId;
        return {true, false, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

bool EditorShell::singleSceneTransformEditCurrent() const noexcept
{
    if (!activeSingleSceneTransform_ || !activeTransformEditObjectId_)
        return false;
    const auto& state = *activeSingleSceneTransform_;
    if (state.entry.objectId != activeTransformEditObjectId_
        || state.documentEpoch != sceneDocumentEpoch_
        || !state.sceneOwner.sameCurrentEpoch(sceneDocument_.ownerTicket())
        || !state.historyOwner.sameCurrentEpoch(sceneHistoryEpoch_.ticket()))
        return false;
    const auto id = scene::GameObjectId::fromValue(state.entry.objectId);
    const auto* object = sceneDocument_.scene().findObject(id);
    if (!object || object->parentId() != state.parent || !sceneObjectEditable(id, true)
        || !EditorSceneTransformFrame::same(object->local_transform(), state.entry.afterTransform))
        return false;
    const auto* parent = object->parent_object();
    return (!state.parent.valid() || parent)
        && EditorSceneTransformFrame::same(
            parent ? parent->world_transform() : scene::Transform::identity(), state.parentWorld);
}

EditorCommandOutcome EditorShell::endSingleSceneTransformEdit(std::uint64_t objectId) noexcept
{
    if (!objectId || objectId != activeTransformEditObjectId_)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const bool current = singleSceneTransformEditCurrent();
    auto retired = std::move(activeSingleSceneTransform_);
    activeTransformEditObjectId_ = 0;
    // External writes are never folded into, or rolled back by, the old drag.
    if (!current)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (!EditorSceneTransformFrame::same(
            retired->entry.beforeTransform, retired->entry.afterTransform))
        publishPreparedSceneHistory(retired->undo, retired->retiredRedo, std::move(retired->entry));
    return {true, false, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::applySceneTransform(
    scene::GameObjectId objectId, const scene::Transform& transform)
{
    const auto gate = sceneObjectWriteGate(objectId.value(), true);
    if (!gate.success)
        return gate;
    auto* object = sceneDocument_.scene().findObject(objectId);
    if (!object)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (activeTransformEditObjectId_ && activeTransformEditObjectId_ != objectId.value())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (activeTransformEditObjectId_ && !singleSceneTransformEditCurrent()) {
        activeSingleSceneTransform_.reset();
        activeTransformEditObjectId_ = 0;
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    const auto before = object->transform().localTransform();
    const auto sameVec3 = [](core::Vec3 lhs, core::Vec3 rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    };
    const auto sameQuat = [](core::Quat lhs, core::Quat rhs) {
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
    };
    if (sameVec3(before.translation, transform.translation)
        && sameQuat(before.rotation, transform.rotation) && sameVec3(before.scale, transform.scale))
        return {true, false, EditorDiagnosticCode::None};
    if (activeTransformEditObjectId_ != objectId.value()) {
        try {
            SceneHistoryEntry history;
            history.kind = SceneHistoryEntry::Kind::Transform;
            history.objectId = objectId.value();
            history.beforeTransform = before;
            history.afterTransform = transform;
            core::pmr::Vector<SceneHistoryEntry> preparedUndo{&core::defaultCpuMemoryResource()};
            core::pmr::Vector<SceneHistoryEntry> retiredRedo{&core::defaultCpuMemoryResource()};
            preparedUndo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
            object->transform().setLocalTransform(transform);
            publishPreparedSceneHistory(preparedUndo, retiredRedo, std::move(history));
        } catch (const std::bad_alloc&) {
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        }
    } else {
        object->transform().setLocalTransform(transform);
        activeSingleSceneTransform_->entry.afterTransform = transform;
    }
    if (objectId == sceneDocument_.selectedObject()) {
        sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
        refreshSceneInspector();
    }
    return {true, true, EditorDiagnosticCode::None};
}

EditorScenePresence::Cost EditorShell::SceneHistoryEntry::charge() const noexcept
{
    auto result = cost;
    result.bytes += beforeName.capacity() + afterName.capacity() + componentType.capacity()
        + transforms.capacity() * sizeof(EditorSceneTransformBatch::Change);
    result.objects += transforms.size();
    for (const auto* field : {&beforeProperty, &afterProperty}) {
        result.bytes += field->name.capacity() + field->stringValue.capacity()
            + field->vector2ListValue.capacity() * sizeof(core::Vec2)
            + field->sceneObjectLinkListValue.capacity() * sizeof(SceneInspectorObjectLink);
        for (const auto& link : field->sceneObjectLinkListValue)
            result.bytes += link.name.capacity() + 1;
        result.references += field->sceneObjectLinkListValue.size();
        if (field->valueKind == ScenePropertyValueKind::SceneObjectReference
            || field->valueKind == ScenePropertyValueKind::ResourceReference)
            ++result.references;
    }
    return result;
}

EditorShell::SceneHistoryUsage EditorShell::sceneHistoryUsage() const noexcept
{
    SceneHistoryUsage usage;
    usage.undoEntries = sceneUndoHistory_.size();
    usage.redoEntries = sceneRedoHistory_.size();
    for (const auto& entry : sceneUndoHistory_)
        usage.undo += entry.charge();
    for (const auto& entry : sceneRedoHistory_)
        usage.redo += entry.charge();
    return usage;
}

void EditorShell::publishPreparedSceneHistory(core::pmr::Vector<SceneHistoryEntry>& preparedUndo,
    core::pmr::Vector<SceneHistoryEntry>& retiredRedo,
    SceneHistoryEntry&& entry) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<SceneHistoryEntry>);
    assert(preparedUndo.empty() && retiredRedo.empty());
    assert(
        preparedUndo.capacity() >= std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
    std::size_t first = sceneUndoHistory_.size() == kMaxSceneHistoryEntries ? 1 : 0;
    // Reclaim only oldest complete transactions, before publishing the new branch.
    // The per-operation budget was checked before any existence mutation.
    for (;;) {
        auto total = entry.charge();
        for (std::size_t i = first; i < sceneUndoHistory_.size(); ++i)
            total += sceneUndoHistory_[i].charge();
        if (EditorScenePresence::fits(total, EditorScenePresence::historyLimit))
            break;
        assert(first < sceneUndoHistory_.size());
        ++first;
    }
    for (std::size_t index = first; index < sceneUndoHistory_.size(); ++index)
        preparedUndo.push_back(std::move(sceneUndoHistory_[index]));
    preparedUndo.push_back(std::move(entry));
    preparedUndo.swap(sceneUndoHistory_);
    retiredRedo.swap(sceneRedoHistory_);
    sceneHistoryEpoch_.advance();
}

EditorCommandOutcome EditorShell::applySceneHistory(bool undo)
{
    // A drag owns an uncommitted transform; older entries cannot change its
    // branch before EndSceneTransformEdit commits the gesture.
    if (activeTransformEditObjectId_ != 0)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto& source = undo ? sceneUndoHistory_ : sceneRedoHistory_;
    auto& destination = undo ? sceneRedoHistory_ : sceneUndoHistory_;
    if (source.empty())
        return {true, false, EditorDiagnosticCode::None};
    if (source.back().kind == SceneHistoryEntry::Kind::TransformBatch)
        return applySceneBatchHistory(undo);
    const auto& next = source.back();
    const bool restoringObject = next.kind == SceneHistoryEntry::Kind::ObjectPresence
        && (undo ? next.objectPresentBefore : next.objectPresentAfter);
    const auto gate = restoringObject && next.subtree && !next.subtree->objects.empty()
        ? sceneObjectWriteGate(next.subtree->objects.front().parent.value(), false)
        : sceneObjectWriteGate(next.objectId, next.kind != SceneHistoryEntry::Kind::Name);
    if (!gate.success)
        return gate;
    if (next.kind == SceneHistoryEntry::Kind::Reparent) {
        for (const auto id : {next.beforeParentId, next.afterParentId}) {
            const auto parentGate = sceneObjectWriteGate(id, false);
            if (!parentGate.success)
                return parentGate;
        }
    }
    std::optional<SceneHistoryEntry> preparedEntry;
    core::pmr::Vector<SceneHistoryEntry> prepared{&core::defaultCpuMemoryResource()};
    try {
        preparedEntry.emplace(source.back());
        prepared.reserve(std::min(destination.size() + 1, kMaxSceneHistoryEntries));
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
    auto& entry = *preparedEntry;
    auto* object =
        sceneDocument_.scene().findObject(scene::GameObjectId::fromValue(entry.objectId));
    if (!object && entry.kind != SceneHistoryEntry::Kind::ObjectPresence)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    bool refreshObjectInspector = false;
    if (entry.kind == SceneHistoryEntry::Kind::Name) {
        const auto& expected = undo ? entry.afterName : entry.beforeName;
        const auto& restored = undo ? entry.beforeName : entry.afterName;
        if (object->name() != expected || !EditorSceneDocument::validObjectName(restored))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        core::pmr::String sceneLabel{&core::defaultCpuMemoryResource()};
        try {
            if (object->id() == sceneDocument_.scene().rootId())
                sceneLabel = restored;
            object->setName(restored);
        } catch (const std::bad_alloc&) {
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        }
        if (object->id() == sceneDocument_.scene().rootId())
            presentationState_.scene_.swap(sceneLabel);
        refreshObjectInspector = object->id() == sceneDocument_.selectedObject();
    } else if (entry.kind == SceneHistoryEntry::Kind::Active) {
        if (object->active() != (undo ? entry.afterActive : entry.beforeActive))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        object->setActive(undo ? entry.beforeActive : entry.afterActive);
        refreshObjectInspector = object->id() == sceneDocument_.selectedObject();
    } else if (entry.kind == SceneHistoryEntry::Kind::Transform) {
        object->transform().setLocalTransform(undo ? entry.beforeTransform : entry.afterTransform);
        refreshObjectInspector = sceneDocument_.selectedObject().value() == entry.objectId;
    } else if (entry.kind == SceneHistoryEntry::Kind::Reparent) {
        const auto parentId = undo ? entry.beforeParentId : entry.afterParentId;
        const auto childIndex = undo ? entry.beforeChildIndex : entry.afterChildIndex;
        try {
            if (!sceneDocument_.reparentObject(
                    object->id(), scene::GameObjectId::fromValue(parentId), childIndex))
                return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        } catch (const std::bad_alloc&) {
            // Native sibling edits may fill a retained target between commands.
            // Scene preparation has not published any hierarchy change here.
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        }
    } else if (entry.kind == SceneHistoryEntry::Kind::ScriptFields) {
        const auto result = applyScriptFieldHistory(entry, undo);
        if (!result.success)
            return result;
        refreshObjectInspector = true;
    } else if (entry.kind == SceneHistoryEntry::Kind::ComponentPresence
        || entry.kind == SceneHistoryEntry::Kind::ObjectPresence) {
        const auto result = applyPresenceHistory(entry, undo);
        if (!result.success)
            return result;
        refreshObjectInspector = true;
    } else {
        // A property may reject or throw during preparation. Always restore the
        // history mode so a subsequent normal edit still records its own entry.
        class ApplicationScope final {
        public:
            explicit ApplicationScope(bool& value)
                : value_(value), previous_(std::exchange(value, true))
            {}

            ~ApplicationScope() { value_ = previous_; }

        private:
            bool& value_;
            bool previous_;
        } application(applyingSceneHistory_);

        const auto response =
            execute(EditorCommandPayload{undo ? entry.beforeProperty : entry.afterProperty});
        if (!response.success || !response.stateChanged)
            return {false,
                false,
                response.diagnostic == EditorDiagnosticCode::None
                    ? EditorDiagnosticCode::InvalidCommandPayload
                    : response.diagnostic};
        if (sceneDocument_.selectedObject().value() == entry.objectId) {
            sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
            refreshSceneInspector();
        }
    }
    const std::size_t first = destination.size() == kMaxSceneHistoryEntries ? 1 : 0;
    for (std::size_t index = first; index < destination.size(); ++index)
        prepared.push_back(std::move(destination[index]));
    prepared.push_back(std::move(entry));
    prepared.swap(destination);
    sceneHistoryEpoch_.advance();
    source.pop_back();
    if (refreshObjectInspector) {
        sceneInspectorWidgetOwner_ = scene::GameObjectId::invalid();
        refreshSceneInspector();
    }
    return {true, true, EditorDiagnosticCode::None};
}

} // namespace arp::editor
