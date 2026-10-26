#include "editor/workspace/editor_shell.h"

namespace arp::editor {

EditorCommandOutcome EditorShell::sceneExportTaskSnapshot(std::uint64_t taskId) const
{
    const auto& task = sceneExport_.result();
    if (!taskId || task.taskId != taskId)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    EditorCommandOutcome result;
    result.stateChanged = false;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    result.snapshot.sceneExportTask = task;
    return result;
}

EditorCommandOutcome EditorShell::exportScene(std::string_view relativeDirectory)
{
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    if (!projectManifest_) {
        projectDiagnostic_ = "EDITOR-SCENE-EXPORT-PROJECT-REQUIRED";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    const auto& id = sceneDocument_.runtimeSceneId();
    if (!resource::isResourceUuid(id)) {
        projectDiagnostic_ = "EDITOR-SCENE-EXPORT-IDENTITY-REQUIRED";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (projectResources_.manager().recordSnapshot(id)) {
        projectDiagnostic_ =
            "EDITOR-SCENE-EXPORT-IDENTITY-CONFLICT: use scene.duplicate for an independent scene; existing package updates require an explicit update transaction";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    core::pmr::String hash;
    EditorCommandOutcome result;
    result.stateChanged = true;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    result.snapshot.workspace = presentationState_.workspace();
    result.snapshot.scene = presentationState_.scene();
    result.snapshot.selection = presentationState_.selection();
    ProjectSceneAuthoring::Snapshot snapshot;
    const auto factory = ProjectSceneAuthoring::factory(
        projectResources_.manager(), projectResources_.physicsWorld2D(), projectDiagnostic_);
    if (!factory
        || !EditorSceneAuthoringCodec::contentHash(sceneDocument_, hash, projectDiagnostic_)
        || !ProjectSceneAuthoring::snapshot(
            sceneDocument_.scene(), *factory, id, snapshot, projectDiagnostic_)
        || !sceneExport_.start(projectRoot(),
            *projectManifest_,
            relativeDirectory,
            sceneDocumentEpoch_,
            hash,
            std::move(snapshot),
            projectDiagnostic_,
            &result.snapshot.sceneExportTask))
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    // Acceptance changes observable task/busy state, not the authoring save point.
    result.snapshot.sceneExportPending = true;
    return result;
}

EditorCommandOutcome EditorShell::completeSceneExport()
{
    if (!sceneExport_.finish())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    // Completion only publishes the worker result. In particular it never saves
    // a later draft or registers resources in a possibly changed project owner.
    return {true, true, EditorDiagnosticCode::None};
}

} // namespace arp::editor
