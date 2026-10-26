#include "editor/workspace/editor_scene_transaction.h"

#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_transform_transaction.h"

#include <new>
#include <utility>

namespace arp::editor {

struct EditorShell::PreparedSceneReplacement::State {
    // Observation copies contain IDs only. History storage remains empty until
    // commit transfers the original vectors on their owner thread.
    struct View {
        core::pmr::Vector<scene::GameObjectId> selection{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<scene::GameObjectId> hierarchy{&core::defaultCpuMemoryResource()};
        core::pmr::Vector<CollapsedInspectorComponent> inspector{&core::defaultCpuMemoryResource()};
        std::uint64_t transformEdit = 0, singleGestureId = 0, batchEdit = 0, batchGestureId = 0;
        std::uint64_t viewRevision = 0;
        const PreparedSceneTransform* batch = nullptr;
        SceneViewportGesture gesture = SceneViewportGesture::None;
        bool rightButtonHeld = false;
        SceneGizmoAxis gizmoAxis = SceneGizmoAxis::None;
        scene::GameObjectId gizmoObject;
        std::size_t hierarchyScroll = 0;
        scene::GameObjectId dragObject, dragTarget;
        bool dragging = false;
        scene::GameObjectId inspectorOwner, widgetOwner;
        InspectorSelectionKey inspectorSelection, inspectorAttempt;

        bool matches(const EditorShell& shell) const noexcept
        {
            if (selection != shell.sceneViewportAdditionalSelection_
                || hierarchy != shell.sceneHierarchyCollapsedObjects_
                || inspector.size() != shell.collapsedInspectorComponents_.size())
                return false;
            for (std::size_t i = 0; i < inspector.size(); ++i)
                if (inspector[i].objectId != shell.collapsedInspectorComponents_[i].objectId
                    || inspector[i].componentId
                        != shell.collapsedInspectorComponents_[i].componentId)
                    return false;
            return viewRevision == shell.sceneViewState_.revision()
                && batch == shell.activeSceneBatchTransform_.get()
                && batchGestureId == shell.activeSceneSelectionTransformEditId()
                && batchEdit == shell.sceneGizmoBatchEditId_
                && transformEdit == shell.activeTransformEditObjectId_
                && singleGestureId
                == (shell.activeSingleSceneTransform_ ? shell.activeSingleSceneTransform_->id : 0)
                && gesture == shell.sceneViewportGesture_
                && rightButtonHeld == shell.sceneViewportRightButtonHeld_
                && gizmoAxis == shell.sceneGizmoAxis_ && gizmoObject == shell.sceneGizmoObject_
                && hierarchyScroll == shell.sceneHierarchyScrollOffset_
                && dragObject == shell.hierarchyDragObject_
                && dragTarget == shell.hierarchyDragTarget_ && dragging == shell.hierarchyDragging_
                && inspectorOwner == shell.collapsedInspectorOwner_
                && inspectorSelection.matches(shell.sceneInspectorSelectionKey_)
                && inspectorAttempt.matches(shell.sceneInspectorAttemptKey_)
                && widgetOwner == shell.sceneInspectorWidgetOwner_;
        }

        void capture(const EditorShell& shell)
        {
            selection = shell.sceneViewportAdditionalSelection_;
            hierarchy = shell.sceneHierarchyCollapsedObjects_;
            inspector = shell.collapsedInspectorComponents_;
            transformEdit = shell.activeTransformEditObjectId_;
            singleGestureId =
                shell.activeSingleSceneTransform_ ? shell.activeSingleSceneTransform_->id : 0;
            viewRevision = shell.sceneViewState_.revision();
            batch = shell.activeSceneBatchTransform_.get();
            batchGestureId = shell.activeSceneSelectionTransformEditId();
            batchEdit = shell.sceneGizmoBatchEditId_;
            gesture = shell.sceneViewportGesture_;
            rightButtonHeld = shell.sceneViewportRightButtonHeld_;
            gizmoAxis = shell.sceneGizmoAxis_;
            gizmoObject = shell.sceneGizmoObject_;
            hierarchyScroll = shell.sceneHierarchyScrollOffset_;
            dragObject = shell.hierarchyDragObject_;
            dragTarget = shell.hierarchyDragTarget_;
            dragging = shell.hierarchyDragging_;
            inspectorOwner = shell.collapsedInspectorOwner_;
            widgetOwner = shell.sceneInspectorWidgetOwner_;
            inspectorSelection = shell.sceneInspectorSelectionKey_;
            inspectorAttempt = shell.sceneInspectorAttemptKey_;
        }

        void exchange(EditorShell& shell) noexcept
        {
            selection.swap(shell.sceneViewportAdditionalSelection_);
            hierarchy.swap(shell.sceneHierarchyCollapsedObjects_);
            inspector.swap(shell.collapsedInspectorComponents_);
            std::swap(transformEdit, shell.activeTransformEditObjectId_);
            std::swap(batchEdit, shell.sceneGizmoBatchEditId_);
            std::swap(gesture, shell.sceneViewportGesture_);
            std::swap(rightButtonHeld, shell.sceneViewportRightButtonHeld_);
            std::swap(gizmoAxis, shell.sceneGizmoAxis_);
            std::swap(gizmoObject, shell.sceneGizmoObject_);
            std::swap(hierarchyScroll, shell.sceneHierarchyScrollOffset_);
            std::swap(dragObject, shell.hierarchyDragObject_);
            std::swap(dragTarget, shell.hierarchyDragTarget_);
            std::swap(dragging, shell.hierarchyDragging_);
            std::swap(inspectorOwner, shell.collapsedInspectorOwner_);
            std::swap(widgetOwner, shell.sceneInspectorWidgetOwner_);
            std::swap(inspectorSelection, shell.sceneInspectorSelectionKey_);
            std::swap(inspectorAttempt, shell.sceneInspectorAttemptKey_);
        }
    };

    const EditorShell* owner = nullptr;
    core::LifetimeEpoch::Ticket shellEpoch, historyEpoch;
    std::uint64_t version = 0, documentEpoch = 0;
    const SceneHistoryEntry* undoData = nullptr;
    const SceneHistoryEntry* redoData = nullptr;
    std::size_t undoCount = 0, redoCount = 0;
    View observed, retired;
    core::pmr::String expectedScene{&core::defaultCpuMemoryResource()};
    core::pmr::String expectedSelection{&core::defaultCpuMemoryResource()};
    core::pmr::String sceneLabel{&core::defaultCpuMemoryResource()};
    core::pmr::String selectionLabel{&core::defaultCpuMemoryResource()};
    core::UniquePtr<EditorSceneDocument::PreparedReplacement> scene;
    core::SharedPtr<EditorSceneServiceSession> incomingSession;
    core::pmr::Vector<SceneHistoryEntry> undo{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<SceneHistoryEntry> redo{&core::defaultCpuMemoryResource()};
    core::UniquePtr<PreparedSceneTransform> retiredBatch;
    core::UniquePtr<SingleSceneTransformEdit> retiredSingle;
    bool committed = false;

    void retireInteraction(EditorShell& shell) noexcept
    {
        if (!shell.sceneUndoHistory_.empty() || !shell.sceneRedoHistory_.empty())
            shell.sceneHistoryEpoch_.advance();
        retiredBatch.swap(shell.activeSceneBatchTransform_);
        retiredSingle.swap(shell.activeSingleSceneTransform_);
        undo.swap(shell.sceneUndoHistory_);
        redo.swap(shell.sceneRedoHistory_);
        retired.exchange(shell);
    }
};

EditorShell::PreparedSceneReplacement::PreparedSceneReplacement() = default;
EditorShell::PreparedSceneReplacement::~PreparedSceneReplacement() = default;

bool EditorShell::PreparedSceneReplacement::committed() const noexcept
{
    return state_ && state_->committed;
}

std::size_t EditorShell::PreparedSceneReplacement::retiredUndoCount() const noexcept
{
    return committed() ? state_->undo.size() : 0;
}

std::size_t EditorShell::PreparedSceneReplacement::retiredRedoCount() const noexcept
{
    return committed() ? state_->redo.size() : 0;
}

std::size_t EditorShell::PreparedSceneReplacement::retiredSelectionCount() const noexcept
{
    return committed() ? state_->retired.selection.size() : 0;
}

core::UniquePtr<EditorShell::PreparedSceneReplacement> EditorShell::prepareSceneReplacement(
    EditorSceneDocument& candidate, core::pmr::String& diagnostic) const
{
    return prepareSceneReplacement(
        candidate, diagnostic, EditorSceneDocument::ServicePolicy::KeepDestination);
}

core::UniquePtr<EditorShell::PreparedSceneReplacement> EditorShell::prepareSceneReplacement(
    EditorSceneDocument& candidate,
    core::pmr::String& diagnostic,
    EditorSceneDocument::ServicePolicy servicePolicy) const
{
    diagnostic.clear();
    if (!recoveryOwnerEpoch_.ticket().isCurrent() || !sceneHistoryEpoch_.ticket().isCurrent()
        || presentationState_.scene().size() > PreparedSceneReplacement::maxPreparedLabelBytes
        || presentationState_.selection().size() > PreparedSceneReplacement::maxPreparedLabelBytes
        || sceneViewportAdditionalSelection_.size() > EditorSceneAuthoringCodec::maxObjects
        || sceneHierarchyCollapsedObjects_.size() > EditorSceneAuthoringCodec::maxObjects
        || collapsedInspectorComponents_.size() > EditorSceneAuthoringCodec::maxComponents
        || sceneUndoHistory_.size() > kMaxSceneHistoryEntries
        || sceneRedoHistory_.size() > kMaxSceneHistoryEntries) {
        diagnostic = "EDITOR-SCENE-HANDOFF-INVALID-OR-OVER-BUDGET";
        return nullptr;
    }
    try {
        auto result = core::makeUnique<PreparedSceneReplacement>();
        result->state_ = core::makeUnique<PreparedSceneReplacement::State>();
        auto& state = *result->state_;
        if (servicePolicy == EditorSceneDocument::ServicePolicy::AdoptCandidate) {
            state.incomingSession = candidate.sceneServiceSession();
            if (!state.incomingSession) {
                diagnostic = "EDITOR-SCENE-HANDOFF-SERVICE-SESSION-REQUIRED";
                return nullptr;
            }
        }
        state.scene = sceneDocument_.prepareReplacement(candidate, diagnostic, servicePolicy);
        if (!state.scene)
            return nullptr;
        const auto* selected = candidate.scene().findObject(candidate.selectedObject());
        if (!EditorSceneDocument::validObjectName(candidate.scene().name())
            || !EditorSceneDocument::validObjectName(selected->name())) {
            diagnostic = "EDITOR-SCENE-HANDOFF-INVALID-LABEL";
            return nullptr;
        }
        state.owner = this;
        state.shellEpoch = recoveryOwnerEpoch_.ticket();
        state.historyEpoch = sceneHistoryEpoch_.ticket();
        state.version = stateVersion();
        state.documentEpoch = sceneDocumentEpoch_;
        state.undoData = sceneUndoHistory_.data();
        state.redoData = sceneRedoHistory_.data();
        state.undoCount = sceneUndoHistory_.size();
        state.redoCount = sceneRedoHistory_.size();
        state.observed.capture(*this);
        state.expectedScene = presentationState_.scene();
        state.expectedSelection = presentationState_.selection();
        state.sceneLabel = candidate.scene().name();
        state.selectionLabel = selected->name();
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-SCENE-HANDOFF-PREPARATION-FAILED";
        return nullptr;
    }
}

EditorShell::SceneReplacementResult EditorShell::sceneReplacementReadiness(
    const PreparedSceneReplacement& prepared, const EditorSceneDocument& candidate) const noexcept
{
    if (!prepared.state_)
        return SceneReplacementResult::Incomplete;
    const auto& state = *prepared.state_;
    if (state.committed)
        return SceneReplacementResult::Consumed;
    if (state.owner != this || !state.shellEpoch.sameCurrentEpoch(recoveryOwnerEpoch_.ticket())
        || !state.historyEpoch.sameCurrentEpoch(sceneHistoryEpoch_.ticket())
        || state.version != stateVersion() || state.documentEpoch != sceneDocumentEpoch_
        || state.undoData != sceneUndoHistory_.data() || state.redoData != sceneRedoHistory_.data()
        || state.undoCount != sceneUndoHistory_.size()
        || state.redoCount != sceneRedoHistory_.size() || !state.observed.matches(*this)
        || state.expectedScene != presentationState_.scene()
        || state.expectedSelection != presentationState_.selection()
        || sceneDocument_.replacementReadiness(*state.scene, candidate)
            != SceneReplacementResult::Ready)
        return SceneReplacementResult::Stale;
    const auto* selected = candidate.scene().findObject(candidate.selectedObject());
    if (!selected || candidate.scene().name() != state.sceneLabel
        || selected->name() != state.selectionLabel)
        return SceneReplacementResult::Stale;
    return SceneReplacementResult::Ready;
}

EditorShell::SceneReplacementResult EditorShell::commitSceneReplacement(
    PreparedSceneReplacement& prepared, EditorSceneDocument& candidate) noexcept
{
    const auto ready = sceneReplacementReadiness(prepared, candidate);
    if (ready != SceneReplacementResult::Ready)
        return ready;
    auto& state = *prepared.state_;
    const auto replaced = sceneDocument_.commitReplacement(*state.scene, candidate);
    if (replaced != SceneReplacementResult::Committed)
        return replaced;
    if (state.incomingSession)
        sceneServiceSession_.swap(state.incomingSession);
    state.retireInteraction(*this);
    sceneViewState_.reset();
    presentationState_.scene_.swap(state.sceneLabel);
    presentationState_.selected_.swap(state.selectionLabel);
    state.committed = true;
    return SceneReplacementResult::Committed;
}

void EditorShell::resetSceneDocumentInteraction()
{
    // Ordinary reset retires locally; a prepared transaction instead retains
    // this exact storage until the caller releases history on its owner thread.
    PreparedSceneReplacement::State retired;
    retired.retireInteraction(*this);
    sceneViewState_.reset();
}
} // namespace arp::editor
