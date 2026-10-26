#include "editor/workspace/editor_scene_panel.h"

#include "editor/workspace/editor_shell.h"
#include "editor/workspace/editor_shell_internal.h"

#include <new>
#include <type_traits>

namespace arp::editor {
bool EditorScenePanel::handles(std::string_view source) noexcept
{
    return source.substr(0, 9) == "scene_ui_" || source == "scene_file_retry"
        || source == "new_scene" || source == "open_scene" || source == "save_scene"
        || source == "save_scene_as" || source == "duplicate_scene" || source == "export_scene";
}

EditorResponse EditorScenePanel::submit(EditorShell& shell, Operation operation)
{
    const bool exporting = std::holds_alternative<ExportSceneCommand>(operation);
    const bool loading = std::holds_alternative<LoadSceneCommand>(operation);
    const bool saving = std::holds_alternative<SaveSceneCommand>(operation);
    const bool duplicate = std::holds_alternative<DuplicateSceneCommand>(operation);
    // Own feedback before any file task or document command can commit.
    core::pmr::String status(exporting ? "Exporting scene package..."
            : loading                  ? "Opening scene..."
            : saving                   ? "Saving scene..."
            : duplicate
            ? "Independent copy created. Use Save As to keep it. The source file is unchanged."
            : "New scene created. Save it to keep your changes.");
    core::pmr::String detail(exporting ? "Closing this dialog keeps the export running." : "");
    if (!shell.runtime().cancelInput().ok()) {
        EditorResponse failed;
        failed.diagnostic = EditorDiagnosticCode::InvalidCommandPayload;
        return failed;
    }
    auto response = std::visit(
        [&](const auto& command) {
            return shell.submitCommand(command);
        },
        operation);
    try {
        if (response.status == EditorResponseStatus::Completed) {
            pending_.reset();
            status_.swap(status);
            detail_.swap(detail);
            feedbackPending_ = true;
            if (exporting && response.hasSnapshot && response.snapshot.sceneExportTask.taskId) {
                taskKind_ = TaskKind::Export;
                taskId_ = response.snapshot.sceneExportTask.taskId;
            } else if ((loading || saving) && response.hasSnapshot
                && response.snapshot.scenePersistenceTaskId) {
                taskKind_ = TaskKind::Persistence;
                taskId_ = response.snapshot.scenePersistenceTaskId;
            } else {
                closePresentation(shell);
                if (shell.presentationState_.workspace() != "3D")
                    shell.activate("workspace_3D");
            }
        } else if (response.diagnostic == EditorDiagnosticCode::UnsavedChanges
            || response.diagnostic == EditorDiagnosticCode::SceneOverwriteRequired) {
            static_assert(std::is_nothrow_move_constructible_v<Operation>);
            pending_.emplace(std::move(operation));
            modal_.armConfirmation();
            if (!render(shell) || !shell.runtime().requestFocus(ids_[Cancel]).ok()) {
                failureNotice(shell);
                return response;
            }
        } else {
            core::pmr::String message;
            core::pmr::String explanation;
            if (response.diagnostic == EditorDiagnosticCode::WorkspaceBusy)
                message =
                    "The workspace is busy. Finish pending file operations, terrain work or Play first.";
            else if (exporting) {
                const auto* record = shell.projectResources_.manager().recordSnapshot(
                    shell.sceneDocument_.runtimeSceneId());
                message = record
                    ? "This scene is already registered. Use Duplicate Scene to export an independent copy."
                    : "Export rejected. Check the project, new folder and supported scene contents.";
                explanation = record ? "Updating a registered package is not available yet."
                                     : detail::singleLine(shell.projectDiagnostic_);
            } else
                message = "Scene request rejected. Check the project-relative path and scene data.";
            status_.swap(message);
            detail_.swap(explanation);
            feedbackPending_ = true;
        }
        if (!retryDisplay(shell) && response.status == EditorResponseStatus::Completed)
            response.diagnostic = EditorDiagnosticCode::SceneModalPresentationFailed;
    } catch (const std::bad_alloc&) {
        failureNotice(shell);
        if (response.status == EditorResponseStatus::Completed)
            response.diagnostic = EditorDiagnosticCode::SceneModalPresentationFailed;
    }
    return response;
}

void EditorScenePanel::synchronize(EditorShell& shell)
{
    if (!taskId_)
        return;
    try {
        const auto result = taskKind_ == TaskKind::Export
            ? shell.sceneExportTaskSnapshot(taskId_)
            : shell.scenePersistenceTaskSnapshot(taskId_);
        const bool exporting = taskKind_ == TaskKind::Export;
        if (result.hasSnapshot
            && (exporting ? result.snapshot.sceneExportTask.status == SceneExportStatus::Pending
                          : result.snapshot.scenePersistenceTaskStatus
                        == ScenePersistenceTaskStatus::Pending))
            return;
        const bool success = result.hasSnapshot
            && (exporting ? result.snapshot.sceneExportTask.status
                        == SceneExportStatus::ExportedUnregistered
                          : result.snapshot.scenePersistenceTaskStatus
                        == ScenePersistenceTaskStatus::Succeeded);
        core::pmr::String status;
        core::pmr::String detail;
        if (!result.hasSnapshot)
            status = "The operation result is no longer retained. Another request has replaced it.";
        else if (exporting) {
            status = success ? "Exported package (not added to project):"
                             : "Scene package export failed:";
            detail = detail::singleLine(success ? result.snapshot.sceneExportTask.relativePath
                                                : result.snapshot.sceneExportTask.diagnostic);
        } else
            status = success
                ? "Scene operation completed: " + result.snapshot.scenePath
                : detail::sceneFileFailureMessage(result.snapshot.scenePersistenceTaskDiagnostic);
        if (success && !exporting && active() && !shell.runtime().cancelInput().ok()) {
            failureNotice(shell);
            return;
        }
        // Snapshot and captions are ready. Retiring the task never replays IO.
        status_.swap(status);
        detail_.swap(detail);
        taskId_ = 0;
        taskKind_ = TaskKind::None;
        if (success && !exporting && active())
            closePresentation(shell);
        feedbackPending_ = true;
        refreshFilesPending_ |= success;
        (void)retryDisplay(shell);
    } catch (const std::bad_alloc&) {
        // Retain the exact task identity until its completion can be projected.
        failureNotice(shell);
    }
}

void EditorScenePanel::commandCompleted(
    std::uint64_t previous, std::uint64_t current, bool blockedInput)
{
    modal_.commandCompleted(previous, current, blockedInput);
}

EditorCommandOutcome EditorScenePanel::edit(EditorShell& shell, const core::pmr::String& value)
{
    if (!validBindings(shell) || !active() || pending_ || taskId_ || presentationFailed_)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    bool committed = false;
    try {
        if (!ui::TextInputContract::validate(value, 512).ok())
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        const auto layout = shell.runtime().canvas().resolveLayout();
        const auto* path = layout.ok() ? layout.findWidget(ids_[Path]) : nullptr;
        if (!path || !path->visibleInHierarchy || !path->enabledInHierarchy)
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        core::pmr::String prepared(value, path_.get_allocator());
        const bool changed = prepared != path_;
        if (!setText(shell, Path, prepared))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        path_.swap(prepared);
        status_.clear();
        detail_.clear();
        committed = changed;
        if (!retryDisplay(shell))
            return {true, changed, EditorDiagnosticCode::SceneModalPresentationFailed};
        return {true, changed, EditorDiagnosticCode::None};
    } catch (const std::bad_alloc&) {
        failureNotice(shell);
        return {committed,
            committed,
            committed ? EditorDiagnosticCode::SceneModalPresentationFailed
                      : EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorScenePanel::requestSave(EditorShell& shell, bool saveAs)
{
    if (active())
        return {true, false, EditorDiagnosticCode::None};
    if (taskId_)
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    const auto gate = shell.sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    if (saveAs || shell.activeScenePath_.empty())
        return show(shell, Mode::SaveAs);
    auto response = submit(shell, SaveSceneCommand(shell.activeScenePath_));
    return std::move(response).intoNestedOutcome();
}

EditorCommandOutcome EditorScenePanel::activate(EditorShell& shell, std::string_view source)
{
    if (!validBindings(shell))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    const auto version = shell.stateVersion();
    try {
        if (source == "scene_ui_new" || source == "new_scene")
            return show(shell, Mode::New);
        if (source == "open_scene")
            return show(shell, Mode::Open);
        if (source == "save_scene" || source == "save_scene_as")
            return requestSave(shell, source == "save_scene_as");
        if (source == "duplicate_scene")
            return show(shell, Mode::Duplicate);
        if (source == "export_scene")
            return show(shell, Mode::Export);
        if (source == "scene_ui_cancel")
            return hide(shell);
        if (source == "scene_ui_retry" || source == "scene_file_retry") {
            const bool ok = !presentationFailed_ || retryDisplay(shell);
            return {ok,
                false,
                ok ? EditorDiagnosticCode::None
                   : EditorDiagnosticCode::SceneModalPresentationFailed};
        }
        if (source == "scene_ui_modal")
            return {true, false, EditorDiagnosticCode::None};
        if (!active() || taskId_)
            return {false, false, EditorDiagnosticCode::WorkspaceBusy};
        if (presentationFailed_)
            return {false, false, EditorDiagnosticCode::SceneModalPresentationFailed};
        EditorResponse response;
        if (source == "scene_ui_submit" && !pending_) {
            if (mode_ == Mode::New)
                response = submit(shell, NewSceneCommand{});
            else if (mode_ == Mode::Duplicate)
                response = submit(shell, DuplicateSceneCommand{});
            else if (path_.empty()) {
                status_ = mode_ == Mode::Export
                    ? "Enter a new project-relative package folder. Its parent must already exist."
                    : "Enter a project-relative authoring scene path.";
                return {true,
                    false,
                    retryDisplay(shell) ? EditorDiagnosticCode::None
                                        : EditorDiagnosticCode::SceneModalPresentationFailed};
            } else if (mode_ == Mode::Open)
                response = submit(shell, LoadSceneCommand(path_));
            else if (mode_ == Mode::Export)
                response = submit(shell, ExportSceneCommand{path_});
            else
                response = submit(shell, SaveSceneCommand(path_));
        } else if (source == "scene_ui_confirm" && pending_) {
            if (!modal_.confirmationValid(shell.stateVersion())) {
                pending_.reset();
                status_ = "The workspace changed. Review and submit the operation again.";
                (void)retryDisplay(shell);
                return {false, false, EditorDiagnosticCode::StaleDocumentTarget};
            }
            auto operation = *pending_;
            std::visit(
                [](auto& command) {
                    using Command = std::decay_t<decltype(command)>;
                    if constexpr (std::is_same_v<Command, SaveSceneCommand>)
                        command.overwriteExisting = true;
                    else if constexpr (std::is_same_v<Command, NewSceneCommand>
                        || std::is_same_v<Command, LoadSceneCommand>)
                        command.discardChanges = true;
                },
                operation);
            response = submit(shell, std::move(operation));
        } else
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        const bool confirming = pending_.has_value()
            && (response.diagnostic == EditorDiagnosticCode::UnsavedChanges
                || response.diagnostic == EditorDiagnosticCode::SceneOverwriteRequired);
        auto outcome = std::move(response).intoNestedOutcome();
        if (confirming) {
            // This UI request presented a confirmation; no file task was accepted.
            outcome.success = true;
            outcome.stateChanged = shell.stateVersion() == version;
        }
        return outcome;
    } catch (const std::bad_alloc&) {
        failureNotice(shell);
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}
} // namespace arp::editor
