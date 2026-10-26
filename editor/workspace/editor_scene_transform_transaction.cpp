#include "editor/workspace/editor_scene_transform_transaction.h"

#include "editor/workspace/editor_scene_authoring_codec.h"

#include <cassert>
#include <new>
#include <type_traits>

namespace arp::editor {
namespace {
using Frame = EditorSceneTransformFrame;

// Every effective member has no selected ancestor, so no other batch write can
// change its parent world pose. All checks complete before the first write.
bool matchesChange(
    const scene::Scene& scene, const EditorSceneTransformBatch::Change& change, bool after) noexcept
{
    const auto* object = scene.findObject(change.objectId);
    if (!object || object->parentId() != change.parentId
        || !Frame::same(object->local_transform(), after ? change.afterLocal : change.beforeLocal))
        return false;
    const auto* parent = object->parent_object();
    return (!change.parentId.valid() || parent)
        && Frame::same(
            parent ? parent->world_transform() : scene::Transform::identity(), change.parentWorld);
}
} // namespace

struct EditorShell::PreparedSceneTransform::State {
    const EditorShell* owner = nullptr;
    std::thread::id thread = std::this_thread::get_id();
    core::LifetimeEpoch::Ticket shell, scene, history;
    std::uint64_t version = 0, documentEpoch = 0;
    scene::GameObjectId primary;
    core::pmr::Vector<scene::GameObjectId> additional{&core::defaultCpuMemoryResource()};
    EditorSceneTransformBatch batch;
    SceneHistoryEntry entry;
    core::pmr::Vector<EditorSceneTransformBatch::Member> accepted{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<EditorSceneTransformBatch::Change> candidate{
        &core::defaultCpuMemoryResource()};
    TransformSceneSelectionCommand operation;
    std::uint64_t gestureId = 0;
    bool gesture = false;
    core::pmr::Vector<SceneHistoryEntry> undo{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneHistoryEntry> retiredRedo{&core::defaultCpuMemoryResource()};
    const SceneHistoryEntry* undoData = nullptr;
    const SceneHistoryEntry* redoData = nullptr;
    std::size_t undoCount = 0, redoCount = 0;
    bool changed = false, committed = false;
};

EditorShell::PreparedSceneTransform::PreparedSceneTransform() = default;

EditorShell::PreparedSceneTransform::~PreparedSceneTransform()
{
    // Even an abandoned token contains owner-thread reflection values.
    assert(!state_ || state_->thread == std::this_thread::get_id());
}

bool EditorShell::PreparedSceneTransform::committed() const noexcept
{
    return state_ && state_->committed;
}

std::size_t EditorShell::PreparedSceneTransform::changeCount() const noexcept
{
    return state_ ? state_->batch.effectiveCount() : 0;
}

std::size_t EditorShell::PreparedSceneTransform::retiredRedoCount() const noexcept
{
    return state_ ? state_->retiredRedo.size() : 0;
}

bool EditorShell::sceneBatchTransformBusy(bool allowOwnGesture) const noexcept
{
    if (!sceneInspectorContent_.valid() || exitRequested_ || authoringUiBlocked()
        || activeTransformEditObjectId_ != 0 || (activeSceneBatchTransform_ && !allowOwnGesture)
        || (sceneViewportGesture_ != SceneViewportGesture::None
            && !(allowOwnGesture && sceneViewportGesture_ == SceneViewportGesture::TransformAxis))
        || sceneViewportRightButtonHeld_ || sceneExport_.pending() || terrainBuild_.pending()
        || projectSaveRequest_ || gamePreview_.running() || gamePreview_.starting()
        || gamePreview_.hasPendingRequest())
        return true;
    for (const auto& task : scenePersistenceTasks_)
        if (task.status == ScenePersistenceTaskStatus::Pending)
            return true;
    return false;
}

const char* EditorShell::captureSceneTransformBatch(
    EditorSceneTransformBatch& destination, EditorSceneTransformPivot pivot)
{
    if (!sceneObjectEditable(sceneDocument_.selectedObject(), true))
        return "EDITOR-TRANSFORM-LOCKED";
    for (const auto id : sceneViewportAdditionalSelection_)
        if (!sceneObjectEditable(id, true))
            return "EDITOR-TRANSFORM-LOCKED";
    core::pmr::Vector<scene::GameObjectId> selection(sceneViewportAdditionalSelection_.begin(),
        sceneViewportAdditionalSelection_.end(),
        &core::defaultCpuMemoryResource());
    selection.push_back(sceneDocument_.selectedObject());
    scene::SceneTraversalOptions options;
    options.maxNodeCount = EditorSceneAuthoringCodec::maxObjects + 1;
    const auto snapshot = sceneDocument_.scene().extractTraversalSnapshot(options);
    return EditorSceneTransformBatch::capture(snapshot,
        sceneDocument_.selectedObject(),
        selection,
        pivot,
        {EditorSceneAuthoringCodec::maxObjects, options.maxNodeCount},
        destination);
}

core::UniquePtr<EditorShell::PreparedSceneTransform> EditorShell::prepareSceneTransform(
    const TransformSceneSelectionCommand& command, const char*& diagnostic)
{
    diagnostic = "EDITOR-TRANSFORM-WRONG-THREAD";
    if (std::this_thread::get_id() != sceneAuthoringThread_)
        return {};
    diagnostic = "EDITOR-TRANSFORM-BUSY";
    if (sceneBatchTransformBusy())
        return {};
    diagnostic = "EDITOR-TRANSFORM-ALLOCATION-FAILED";
    try {
        auto token = core::makeUnique<PreparedSceneTransform>();
        token->state_ = core::makeUnique<PreparedSceneTransform::State>();
        auto& state = *token->state_;
        state.owner = this;
        state.shell = recoveryOwnerEpoch_.ticket();
        state.scene = sceneDocument_.ownerTicket();
        state.history = sceneHistoryEpoch_.ticket();
        state.version = stateVersion();
        state.documentEpoch = sceneDocumentEpoch_;
        state.primary = sceneDocument_.selectedObject();
        state.additional = sceneViewportAdditionalSelection_;
        state.operation = command;
        const auto* failure = captureSceneTransformBatch(state.batch, command.pivot);
        if (!failure)
            failure = state.batch.prepare(
                command.tool, command.space, command.axis, command.amount, state.entry.transforms);
        if (failure) {
            diagnostic = failure;
            return {};
        }
        state.entry.kind = SceneHistoryEntry::Kind::TransformBatch;
        state.entry.transformSceneOwner = state.scene;
        for (const auto& change : state.entry.transforms)
            state.changed = state.changed || !Frame::same(change.beforeLocal, change.afterLocal);
        if (state.changed)
            state.undo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        state.undoData = sceneUndoHistory_.data();
        state.redoData = sceneRedoHistory_.data();
        state.undoCount = sceneUndoHistory_.size();
        state.redoCount = sceneRedoHistory_.size();
        diagnostic = nullptr;
        return token;
    } catch (const std::bad_alloc&) {
        return {};
    }
}

EditorShell::TransformCommitResult EditorShell::sceneTransformReadiness(
    const PreparedSceneTransform& token) const noexcept
{
    using Result = TransformCommitResult;
    if (std::this_thread::get_id() != sceneAuthoringThread_)
        return Result::WrongThread;
    if (!token.state_)
        return Result::Stale;
    const auto& state = *token.state_;
    if (state.thread != std::this_thread::get_id())
        return Result::WrongThread;
    if (state.committed)
        return Result::Consumed;
    if (state.owner != this || !state.shell.sameCurrentEpoch(recoveryOwnerEpoch_.ticket())
        || !state.scene.sameCurrentEpoch(sceneDocument_.ownerTicket())
        || !state.history.sameCurrentEpoch(sceneHistoryEpoch_.ticket())
        || (!state.gesture && state.version != stateVersion())
        || (state.gesture && activeSceneBatchTransform_.get() != &token)
        || state.documentEpoch != sceneDocumentEpoch_
        || state.primary != sceneDocument_.selectedObject()
        || state.additional != sceneViewportAdditionalSelection_
        || state.undoData != sceneUndoHistory_.data() || state.undoCount != sceneUndoHistory_.size()
        || state.redoData != sceneRedoHistory_.data()
        || state.redoCount != sceneRedoHistory_.size())
        return Result::Stale;
    if (sceneBatchTransformBusy(state.gesture))
        return Result::Busy;
    const auto members = state.gesture
        ? core::ArrayView<EditorSceneTransformBatch::Member>(state.accepted)
        : state.batch.members();
    for (const auto& member : members) {
        const auto* object = sceneDocument_.scene().findObject(member.objectId);
        if (!object || !sceneObjectEditable(member.objectId, true)
            || object->parentId() != member.parentId
            || !Frame::same(object->local_transform(), member.local)
            || !Frame::same(object->world_transform(), member.world))
            return Result::Stale;
        const auto* parent = object->parent_object();
        if ((member.parentId.valid() && !parent)
            || !Frame::same(parent ? parent->world_transform() : scene::Transform::identity(),
                member.parentWorld))
            return Result::Stale;
    }
    return Result::Ready;
}

void EditorShell::invalidateSceneTransformInspector() noexcept
{
    // Reuse U4's nonallocating quarantine for root-level popup options as well
    // as the content tree. No callback observes this transient Failed value.
    failSceneInspector("");
    sceneInspectorState_ = InspectorState::Pending;
    synchronizeInspectorAvailability();
}

EditorShell::TransformCommitResult EditorShell::commitSceneTransform(
    PreparedSceneTransform& token) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<SceneHistoryEntry>);
    const auto ready = sceneTransformReadiness(token);
    if (ready != TransformCommitResult::Ready)
        return ready;
    auto& state = *token.state_;
    state.committed = true;
    if (!state.changed)
        return TransformCommitResult::NoChange;
    for (const auto& change : state.entry.transforms)
        sceneDocument_.scene().findObject(change.objectId)->set_local_transform(change.afterLocal);
    publishSceneTransformHistory(token);
    invalidateSceneTransformInspector();
    return TransformCommitResult::Committed;
}

void EditorShell::publishSceneTransformHistory(PreparedSceneTransform& token) noexcept
{
    auto& state = *token.state_;
    publishPreparedSceneHistory(state.undo, state.retiredRedo, std::move(state.entry));
}

EditorCommandOutcome EditorShell::transformSceneSelection(
    const TransformSceneSelectionCommand& command)
{
    if (sceneBatchTransformBusy())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    const char* diagnostic = nullptr;
    auto token = prepareSceneTransform(command, diagnostic);
    if (!token)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto result = commitSceneTransform(*token);
    if (result == TransformCommitResult::NoChange)
        return {true, false, EditorDiagnosticCode::None};
    if (result != TransformCommitResult::Committed)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    // UI is explicitly outside the Scene/history transaction (U4 semantics).
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::applySceneBatchHistory(bool undo)
{
    if (sceneBatchTransformBusy())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    auto& source = undo ? sceneUndoHistory_ : sceneRedoHistory_;
    auto& destination = undo ? sceneRedoHistory_ : sceneUndoHistory_;
    const auto& entry = source.back();
    if (!entry.transformSceneOwner.sameCurrentEpoch(sceneDocument_.ownerTicket()))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    for (const auto& change : entry.transforms)
        if (!sceneObjectEditable(change.objectId, true)
            || !matchesChange(sceneDocument_.scene(), change, undo))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    core::pmr::Vector<SceneHistoryEntry> prepared{&core::defaultCpuMemoryResource()};
    try {
        prepared.reserve(std::min(destination.size() + 1, kMaxSceneHistoryEntries));
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    for (const auto& change : entry.transforms)
        sceneDocument_.scene()
            .findObject(change.objectId)
            ->set_local_transform(undo ? change.beforeLocal : change.afterLocal);
    const std::size_t first = destination.size() == kMaxSceneHistoryEntries ? 1 : 0;
    for (std::size_t i = first; i < destination.size(); ++i)
        prepared.push_back(std::move(destination[i]));
    prepared.push_back(std::move(source.back()));
    source.pop_back();
    prepared.swap(destination);
    sceneHistoryEpoch_.advance();
    invalidateSceneTransformInspector();
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

std::uint64_t EditorShell::activeSceneSelectionTransformEditId() const noexcept
{
    return activeSceneBatchTransform_ ? activeSceneBatchTransform_->state_->gestureId : 0;
}

bool EditorShell::sceneSelectionTransformEditCurrent(std::uint64_t gestureId) const noexcept
{
    return gestureId != 0 && activeSceneSelectionTransformEditId() == gestureId
        && sceneTransformReadiness(*activeSceneBatchTransform_) == TransformCommitResult::Ready;
}

EditorCommandOutcome EditorShell::beginSceneSelectionTransformEdit(
    const TransformSceneSelectionCommand& operation)
{
    if (sceneBatchTransformBusy())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (operation.amount != 0)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const char* diagnostic = nullptr;
    auto token = prepareSceneTransform(operation, diagnostic);
    if (!token)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        auto& state = *token->state_;
        state.undo.reserve(std::min(sceneUndoHistory_.size() + 1, kMaxSceneHistoryEntries));
        const auto members = state.batch.members();
        state.accepted.assign(members.begin(), members.end());
        if (sceneTransformReadiness(*token) != TransformCommitResult::Ready
            || !sceneBatchTransformIds_.hasRemaining())
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        state.gestureId = sceneBatchTransformIds_.tryAcquire();
        state.gesture = true;
        activeSceneBatchTransform_ = std::move(token);
        return {true, false, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
}

EditorCommandOutcome EditorShell::updateSceneSelectionTransformEdit(
    std::uint64_t gestureId, double amount)
{
    if (!gestureId || activeSceneSelectionTransformEditId() != gestureId)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    auto& token = *activeSceneBatchTransform_;
    if (sceneTransformReadiness(token) != TransformCommitResult::Ready) {
        // Native out-of-band edits must never be overwritten by an old gesture.
        activeSceneBatchTransform_.reset();
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    auto& state = *token.state_;
    if (state.batch.prepare(state.operation.tool,
            state.operation.space,
            state.operation.axis,
            amount,
            state.candidate))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (sceneTransformReadiness(token) != TransformCommitResult::Ready) {
        activeSceneBatchTransform_.reset();
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    // Candidate preparation cannot execute callbacks. All members, including
    // inherited selected descendants, were checked before any Scene write.
    bool poseChanged = false, changedFromStart = false;
    for (std::size_t i = 0; i < state.candidate.size(); ++i) {
        const auto& change = state.candidate[i];
        poseChanged =
            poseChanged || !Frame::same(change.afterLocal, state.entry.transforms[i].afterLocal);
        changedFromStart = changedFromStart || !Frame::same(change.afterLocal, change.beforeLocal);
    }
    if (!poseChanged)
        return {true, false, EditorDiagnosticCode::None};
    for (const auto& change : state.candidate)
        sceneDocument_.scene().findObject(change.objectId)->set_local_transform(change.afterLocal);
    state.entry.transforms.swap(state.candidate);
    state.changed = changedFromStart;
    for (auto& member : state.accepted) {
        const auto* object = sceneDocument_.scene().findObject(member.objectId);
        member.local = object->local_transform();
        member.world = object->world_transform();
        const auto* parent = object->parent_object();
        member.parentWorld = parent ? parent->world_transform() : scene::Transform::identity();
    }
    invalidateSceneTransformInspector();
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::endSceneSelectionTransformEdit(std::uint64_t gestureId)
{
    if (!gestureId || activeSceneSelectionTransformEditId() != gestureId)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (!sceneSelectionTransformEditCurrent(gestureId)) {
        activeSceneBatchTransform_.reset();
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    }
    if (activeSceneBatchTransform_->state_->changed)
        publishSceneTransformHistory(*activeSceneBatchTransform_);
    activeSceneBatchTransform_->state_->committed = true;
    activeSceneBatchTransform_.reset();
    return {true, false, EditorDiagnosticCode::None};
}

const char* EditorShell::sceneTransformSelectionView(
    core::Vec3& origin, EditorSceneTransformFrame& frame)
{
    if (activeSceneBatchTransform_) {
        if (!sceneSelectionTransformEditCurrent(activeSceneSelectionTransformEditId()))
            return "EDITOR-TRANSFORM-GESTURE-STALE";
        const auto& state = *activeSceneBatchTransform_->state_;
        if (!state.batch.frame(state.operation.space, frame))
            return "EDITOR-TRANSFORM-INVALID";
        origin = state.batch.origin();
        const auto* primary = sceneDocument_.scene().findObject(state.primary);
        if (state.operation.pivot == EditorSceneTransformPivot::Individual)
            origin = primary->world_transform().translation;
        else if (state.operation.tool == EditorSceneTransformTool::Move) {
            for (const auto& member : state.batch.members())
                if (member.objectId == state.primary) {
                    origin =
                        origin + primary->world_transform().translation - member.world.translation;
                    break;
                }
        }
        return nullptr;
    }
    try {
        EditorSceneTransformBatch batch;
        if (const auto* diagnostic = captureSceneTransformBatch(batch, sceneTransformPivot_))
            return diagnostic;
        if (!batch.frame(sceneTransformSpace_, frame))
            return "EDITOR-TRANSFORM-INVALID";
        origin = batch.origin();
        return nullptr;
    } catch (const std::bad_alloc&) {
        return "EDITOR-TRANSFORM-ALLOCATION-FAILED";
    }
}
} // namespace arp::editor
