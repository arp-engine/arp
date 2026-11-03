#include "core/io/filesystem_path.h"
#include "editor/workspace/editor_scene_script_policy.h"
#include "editor/workspace/editor_scene_script_reload.h"
#include "editor/workspace/editor_scene_transaction.h"
#include "editor/workspace/editor_shell.h"

#include <limits>

namespace arp::editor {

EditorCommandOutcome EditorShell::reloadSceneScripts()
try {
    if (authoringUiBlocked())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    const auto gate = sceneReplacementGate(true);
    if (!gate.success)
        return gate;
    const auto uncertain = [](const ProjectSourceSaveReceipt& value) {
        return value.terminal && !value.baselineRefreshed
            && value.outcome.commitState == core::io::FileCommitState::OutcomeUnknown;
    };
    if (projectManifestDirty_ || hasUnsavedScripts())
        return {false, false, EditorDiagnosticCode::UnsavedChanges};
    const auto& directory = projectBrowser_.sourceSnapshot();
    const auto catalog = sceneDocument_.scriptCatalog();
    if (!projectManifest_ || !directory || !projectSourceFiles_ || !catalog
        || sceneAuthoringVersion_ != EditorSceneScriptCodec::contractVersion
        || sourceSceneSaveUnknown_ || uncertain(lastScriptSave_) || uncertain(lastSceneSave_)
        || uncertain(lastProjectSave_)
        || sceneDocumentEpoch_ == std::numeric_limits<std::uint64_t>::max()
        || (!activeScenePath_.empty() && savedSceneSourceScope_ != directory->project()))
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    EditorProjectIoTask::ScriptModulesInput request;
    request.context = {
        directory->project(), projectSourceFiles_, directory->project()->location().epoch};
    for (const auto& module : catalog->modules().entries())
        request.sources.push_back(module.declaration.source);
    request.directory = core::FilePath::utf8(
        projectBrowser_.currentDirectory().lexically_relative(projectBrowser_.root()));
    if (request.directory == ".")
        request.directory.clear();
    const auto id = projectIoIds_.tryAcquire();
    if (!id)
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    return acceptProjectIoTask(core::makeUnique<EditorProjectIoTask>(
                                   *projectSourceFiles_, std::move(request), projectIoBudget_),
        id);
} catch (...) {
    return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
}

EditorCommandOutcome EditorShell::completeSceneScriptReload()
{
    core::pmr::String diagnostic;
    try {
        const auto next = projectIo_->reloadedSources();
        const auto& directory = projectBrowser_.sourceSnapshot();
        if (!projectManifest_ || !directory || !next.snapshot || !next.files
            || projectResources_.sourceSnapshot() != directory->project()
            || !sceneReplacementGate(true).success) {
            projectIo_->fail("EDITOR-SCRIPT-RELOAD-OWNER-CHANGED");
            return {true, true, EditorDiagnosticCode::None};
        }
        ProjectBrowserModel browser;
        if (const auto* issue = projectBrowser_.prepareReloadedSources(directory->project(),
                projectIo_->directory(),
                *next.files,
                next.epoch,
                browser,
                projectIoBudget_.maxHistoryEntries)) {
            projectIo_->fail(issue);
            return {true, true, EditorDiagnosticCode::None};
        }
        auto input = EditorSceneScriptPolicy::inputs(projectRoot(), *projectManifest_);
        input.sources = &next;
        const auto raw = savedSceneFileBytes_.empty()
            ? std::string_view{}
            : std::string_view(reinterpret_cast<const char*>(savedSceneFileBytes_.data()),
                  savedSceneFileBytes_.size());
        auto reload = EditorSceneScriptReload::prepare(
            sceneDocument_, input, activeScenePath_, raw, scriptReloadProof_.get(), diagnostic);
        if (!reload) {
            projectIo_->fail(diagnostic.c_str());
            return {true, true, EditorDiagnosticCode::None};
        }
        auto inputs = next.snapshot->identities();
        auto selection = sceneViewportAdditionalSelection_;
        core::pmr::String message(
            "Script modules reloaded; scene Undo history cleared. Save the scene to persist new bindings.");
        auto replacement = prepareSceneReplacement(reload->candidate(), diagnostic);
        if (!replacement
            || commitSceneReplacement(*replacement, reload->candidate())
                != SceneReplacementResult::Committed) {
            projectIo_->fail(
                diagnostic.empty() ? "EDITOR-SCRIPT-RELOAD-PUBLISH-STALE" : diagnostic.c_str());
            return {true, true, EditorDiagnosticCode::None};
        }
        // All allocations and owner checks preceded the first live mutation.
        // Non-module raw bytes were compared before preparing this candidate;
        // actual resource/physics owners remain the same, including GPU caches.
        ++sceneDocumentEpoch_;
        projectResources_.sourceSnapshot_ = next.snapshot;
        projectResources_.inputBindings_.swap(inputs);
        projectBrowser_.swap(browser);
        if (savedSceneSourceScope_)
            savedSceneSourceScope_ = next.snapshot;
        sceneViewportAdditionalSelection_.swap(selection);
        // Release old history before the retired module domain.
        replacement.reset();
        reload->published(sceneDocument_);
        scriptReloadProof_ = std::move(reload);
        projectIo_->published();
        // Fallible presentation cannot roll back the already committed domain.
        try {
            refreshFileSystem();
            refreshSceneInspector();
            feedback(std::move(message));
        } catch (...) {
            failSceneInspector("EDITOR-SCRIPT-RELOAD-PRESENTATION");
            projectIo_->published("EDITOR-SCRIPT-RELOAD-PRESENTATION");
        }
        return {true, true, EditorDiagnosticCode::None};
    } catch (...) {
        projectIo_->fail("EDITOR-SCRIPT-RELOAD-PREPARATION");
        return {true, true, EditorDiagnosticCode::None};
    }
}
} // namespace arp::editor
