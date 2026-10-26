#include "editor/project/project_identity.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"

#include <algorithm>

namespace arp::editor {

bool EditorShell::modalBlocksActivation(std::string_view source) const
{
    return (projectPanel_.modalActive() && source.substr(0, 11) != "project_ui_")
        || (scenePanel_.active() && source.substr(0, 9) != "scene_ui_")
        || (scriptFilePanel_.active() && source.substr(0, 10) != "script_ui_")
        || (exitPanel_.active() && source.substr(0, 8) != "exit_ui_")
        || (sceneTransformPanel_.active() && source.substr(0, 13) != "transform_ui_");
}

EditorCommandOutcome EditorShell::sceneReplacementGate(bool discardChanges) const
{
    const bool pending = std::any_of(
        scenePersistenceTasks_.begin(), scenePersistenceTasks_.end(), [](const auto& task) {
            return task.status == ScenePersistenceTaskStatus::Pending;
        });
    if (pending || sceneExport_.pending() || terrainBuild_.pending() || projectSaveRequest_
        || gamePreview_.running() || gamePreview_.starting() || gamePreview_.hasPendingRequest())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    if (!discardChanges && sceneDirty())
        return {false, false, EditorDiagnosticCode::UnsavedChanges};
    return {true, false, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::newScene(bool discardChanges)
{
    const auto gate = sceneReplacementGate(discardChanges);
    if (!gate.success)
        return gate;
    EditorSceneDocument candidate(false);
    candidate.scene().setName("Untitled");
    core::pmr::String identity, diagnostic;
    if (!ProjectIdentity::create("EditorScene/1", identity, diagnostic)
        || !candidate.setRuntimeSceneId(identity)) {
        projectDiagnostic_ = diagnostic;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    auto prepared = prepareSceneReplacement(candidate, diagnostic);
    if (!prepared
        || commitSceneReplacement(*prepared, candidate) != SceneReplacementResult::Committed)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    // An untitled document has no save point, including when it is still empty.
    savedSceneHash_.clear();
    activeScenePath_.clear();
    savedSceneFileTarget_.clear();
    savedSceneFileBytes_.clear();
    ++sceneDocumentEpoch_;
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::duplicateScene()
{
    // Preserve even unsaved edits: only identity and its authoring save point
    // change. Keep the existing scene, component and resource owners alive.
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    core::pmr::String identity, diagnostic;
    if (!ProjectIdentity::create("EditorScene/1", identity, diagnostic)) {
        projectDiagnostic_ = diagnostic;
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (identity == sceneDocument_.runtimeSceneId()
        || projectResources_.manager().recordSnapshot(identity)) {
        projectDiagnostic_ = "EDITOR-SCENE-IDENTITY-CONFLICT";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (!sceneDocument_.setRuntimeSceneId(identity))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    savedSceneHash_.clear();
    activeScenePath_.clear();
    savedSceneFileTarget_.clear();
    savedSceneFileBytes_.clear();
    ++sceneDocumentEpoch_;
    resetSceneDocumentInteraction();
    refreshSceneInspector();
    return {true, true, EditorDiagnosticCode::None};
}

} // namespace arp::editor
