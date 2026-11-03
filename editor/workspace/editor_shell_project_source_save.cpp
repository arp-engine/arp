#include "editor/workspace/editor_project_source_save_task.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {
bool EditorShell::projectSettingsSavePending() const noexcept
{
    return bool(projectSaveRequest_) || (projectIoPending() && projectIo_->savingProject());
}

EditorCommandOutcome EditorShell::saveSourceProject()
{
    if (!projectManifest_ || !projectSourceFiles_ || !projectBrowser_.sourceSnapshot())
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    const auto& scope = projectBrowser_.sourceSnapshot()->project();
    if (!projectSourceFiles_->saveCapability(scope->location().mount).observedBaselineReplace) {
        projectDiagnostic_ = "PROJECT-SOURCE-SAVE-READ-ONLY";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (lastProjectSave_.terminal && !lastProjectSave_.baselineRefreshed
        && !lastProjectSave_.published && lastProjectSave_.documentId == scope->location().epoch
        && lastProjectSave_.outcome.commitState != core::io::FileCommitState::NotCommitted) {
        projectDiagnostic_ = "EDITOR-PROJECT-SAVE-REOPEN-REQUIRED";
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    }
    if (savedSceneSourceScope_ && savedSceneSourceScope_ != scope) {
        projectDiagnostic_ = "EDITOR-PROJECT-SAVE-SCENE-BASELINE";
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    }
    if (!projectManifestDirty_)
        return {true, false, EditorDiagnosticCode::None};
    try {
        const char* issue = nullptr;
        auto save = EditorProjectSourceSaveTask::prepare(
            {scope, projectSourceFiles_, scope->location().epoch},
            projectIoBudget_.inputs,
            projectResources_,
            projectBrowser_,
            sceneDocument_,
            *projectManifest_,
            issue);
        if (!save) {
            projectDiagnostic_ = issue ? issue : "EDITOR-PROJECT-SAVE-PREPARATION";
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        }
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        auto result = acceptProjectIoTask(
            core::makeUnique<EditorProjectIoTask>(std::move(save), projectIoBudget_), id);
        if (result.success) {
            projectDiagnostic_.clear();
            result.snapshot.projectDiagnostic.clear();
            result.snapshot.projectSavePending = true;
            result.snapshot.projectSaveReceipt = projectIo_->projectSaveReceipt(id);
        }
        return result;
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::advanceSourceProjectSave()
{
    auto& save = *projectIo_->projectSave();
    const auto& origin = save.context().snapshot;
    const bool currentOwner = projectSourceFiles_ && origin && projectManifest_
        && sceneDocumentEpoch_ == projectIoSceneEpoch_
        && resourceRevision_ == projectIoResourceRevision_
        && projectIoScripts_.sameCurrentEpoch(scriptDocuments_.stateTicket())
        && (!savedSceneSourceScope_ || savedSceneSourceScope_ == origin)
        && save.ownersCurrent(projectResources_, sceneDocument_);
    if (!currentOwner)
        projectIo_->cancel();
    const auto previous = lastProjectSave_;
    projectIo_->advance();
    lastProjectSave_ = projectIo_->projectSaveReceipt(projectIoTaskId_);
    if (!projectIo_->ready()) {
        const bool changed = previous.taskId != lastProjectSave_.taskId
            || previous.submitted != lastProjectSave_.submitted
            || previous.terminal != lastProjectSave_.terminal
            || !(previous.maintenance == lastProjectSave_.maintenance)
            || previous.cancellationRequested != lastProjectSave_.cancellationRequested;
        return {true, changed, EditorDiagnosticCode::None};
    }
    const auto& receipt = save.result();
    const char* issue = nullptr;
    if (receipt.commitState == core::io::FileCommitState::OutcomeUnknown) {
        issue = "EDITOR-PROJECT-SAVE-OUTCOME-UNKNOWN";
        projectIo_->fail(issue);
    } else if (!currentOwner) {
        issue = "EDITOR-PROJECT-SAVE-OWNER-CHANGED";
        projectIo_->fail(issue);
    } else if (save.cancelled()) {
        issue = receipt.commitState == core::io::FileCommitState::Committed
            ? "EDITOR-PROJECT-SAVE-CANCELLED-AFTER-COMMIT"
            : "EDITOR-PROJECT-SAVE-CANCELLED";
        projectIo_->finishCancelledProjectSave(issue);
    } else if (receipt.status != core::io::FileIoStatus::Success
        || receipt.commitState != core::io::FileCommitState::Committed) {
        issue = receipt.diagnosticId ? receipt.diagnosticId : "EDITOR-PROJECT-SAVE-NOT-COMMITTED";
        projectIo_->fail(issue);
    } else {
        const bool newer = projectManifest_->name != save.settings().name
            || projectManifest_->entryScene != save.settings().entryScene;
        auto committed = save.publish(projectResources_, projectBrowser_, sceneDocument_);
        if (!committed) {
            issue = save.diagnostic();
            projectIo_->fail(issue ? issue : "EDITOR-PROJECT-SAVE-PUBLICATION-REFUSED");
        } else {
            if (savedSceneSourceScope_)
                savedSceneSourceScope_ = std::move(committed);
            projectManifestDirty_ = newer;
            projectIo_->published();
        }
    }
    lastProjectSave_ = projectIo_->projectSaveReceipt(projectIoTaskId_);
    // Publication/receipt truth is final before fallible presentation strings.
    try {
        if (issue)
            projectDiagnostic_ = issue;
        else
            projectDiagnostic_.clear();
    } catch (...) {
        return {true, true, EditorDiagnosticCode::CommandPreparationFailed};
    }
    return {true, true, EditorDiagnosticCode::None};
}
} // namespace arp::editor
