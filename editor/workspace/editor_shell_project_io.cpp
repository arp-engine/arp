#include "core/io/filesystem_path.h"
#include "editor/workspace/editor_shell.h"

namespace arp::editor {

bool EditorShell::projectIoPending() const noexcept
{
    return projectIo_ && projectIo_->status() == ProjectIoStatus::Pending;
}

bool EditorShell::bindProjectSourceFiles(core::io::FileService& files,
    core::PoolHandle mount,
    std::string_view alias,
    EditorProjectIoBudget budget)
{
    if (projectIoPending() || sourceReceiptPending() || projectBrowser_.sourceSnapshot() || !mount
        || alias.empty() || !EditorProjectIoTask::validBudget(budget) || alias.size() > 128
        || !ProjectInputSnapshot::validLogicalPath(alias)
        || alias.find('/') != std::string_view::npos)
        return false;
    try {
        core::io::FileReadRequest probe;
        probe.contractVersion = 1;
        probe.pathScope = core::io::FilePathScope::MountedResource;
        probe.mount = mount;
        probe.path = "project.arp.yaml";
        probe.maxBytes = 1;
        if (files.validateRead(probe).status != core::io::FileIoStatus::Success)
            return false;
        core::pmr::String name(alias, &core::defaultCpuMemoryResource());
        projectSourceAlias_.swap(name);
        projectSourceFiles_ = &files;
        projectSourceMount_ = mount;
        projectIoBudget_ = budget;
        return true;
    } catch (...) {
        return false;
    }
}

EditorCommandOutcome EditorShell::acceptProjectIoTask(
    core::UniquePtr<EditorProjectIoTask> candidate, std::uint64_t id)
{
    if (!id || !candidate || candidate->status() != ProjectIoStatus::Pending)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    core::pmr::String hash, diagnostic;
    if (!observeSceneHash(hash, diagnostic))
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    EditorCommandOutcome result;
    result.hasSnapshot = true;
    appendProjectSnapshot(result.snapshot);
    appendScriptSnapshot(result.snapshot);
    result.snapshot.projectIoTaskId = id;
    result.snapshot.projectIoTaskStatus = ProjectIoStatus::Pending;
    result.snapshot.projectIoDiagnostic.clear();
    projectIoSceneHash_.swap(hash);
    projectIoSceneEpoch_ = sceneDocumentEpoch_;
    projectIoResourceRevision_ = resourceRevision_;
    projectIoScripts_ = scriptDocuments_.stateTicket();
    projectIoTaskId_ = id;
    projectIo_ = std::move(candidate);
    return result;
}

EditorCommandOutcome EditorShell::openSourceProject(const OpenProjectCommand& command)
{
    const auto gate = projectReplacementGate(command.discardChanges);
    if (!gate.success)
        return gate;
    if (!projectSourceFiles_ || command.source != projectSourceAlias_
        || (!command.root.empty() && !ProjectInputSnapshot::validLogicalPath(command.root)))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        return acceptProjectIoTask(core::makeUnique<EditorProjectIoTask>(*projectSourceFiles_,
                                       ProjectSourceLocation{projectSourceMount_, command.root, id},
                                       projectIoBudget_),
            id);
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::navigateSourceProject(const NavigateProjectSourceCommand& command)
{
    const auto& current = projectBrowser_.sourceSnapshot();
    if (!projectSourceFiles_ || !current || projectIoPending())
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    try {
        core::pmr::String target;
        if (!projectBrowser_.sourceTarget(command.intent, command.directory, target))
            return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        auto result = acceptProjectIoTask(
            core::makeUnique<EditorProjectIoTask>(
                *projectSourceFiles_, current->project(), target, projectIoBudget_),
            id);
        if (result.success)
            projectIoNavigation_ = command.intent;
        return result;
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::openScriptDocument(const OpenScriptCommand& command)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    if (!ProjectInputSnapshot::validLogicalPath(command.relativePath)
        || !ScriptDocumentSession::isScriptPath(std::filesystem::u8path(command.relativePath)))
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    try {
        const auto path = projectRoot() / std::filesystem::u8path(command.relativePath);
        const auto& directory = projectBrowser_.sourceSnapshot();
        if (!directory) {
            const bool opened = openScriptFile(path);
            return {opened,
                opened,
                opened ? EditorDiagnosticCode::None
                       : EditorDiagnosticCode::ScriptFileOperationFailed};
        }
        const auto& scope = directory->project();
        const char* issue = nullptr;
        if (!projectSourceFiles_
            || !scope->validate(*projectSourceFiles_, scope->location().epoch, issue))
            return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
        if (const auto* existing = scriptDocuments_.find(path)) {
            const auto* source = existing->sourceLocation();
            if (!source || !(source->mount == scope->location().mount)
                || source->root != scope->location().root
                || source->epoch != scope->location().epoch
                || !existing->validateSource(*projectSourceFiles_, scope->location().epoch, issue))
                return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
            const auto& docs = scriptDocuments_.documents();
            const auto index = static_cast<std::size_t>(existing - docs.data());
            const bool changed = scriptDocuments_.activeIndex() != index
                || presentationState_.workspace() != "Script" || !scriptFocused_;
            activateScriptDocument(index);
            return {true,
                changed,
                scriptPresentationFailed_ ? EditorDiagnosticCode::ScriptPresentationFailed
                                          : EditorDiagnosticCode::None};
        }
        if (scriptDocuments_.size() >= projectIoBudget_.maxScriptDocuments)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        return acceptProjectIoTask(
            core::makeUnique<EditorProjectIoTask>(*projectSourceFiles_,
                EditorProjectIoTask::ScriptInput{scope, command.relativePath},
                projectIoBudget_),
            id);
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::cancelProjectIo(std::uint64_t id)
{
    if (!id || id != projectIoTaskId_ || !projectIoPending())
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    projectIo_->cancel();
    retireSourceSceneIntent(false);
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::saveSourceScript()
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    const auto* document = scriptDocuments_.active();
    const auto& directory = projectBrowser_.sourceSnapshot();
    if (!document || !document->sourceLocation() || !directory || !projectSourceFiles_)
        return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
    const auto& scope = directory->project();
    if (!projectSourceFiles_->saveCapability(scope->location().mount).observedBaselineReplace)
        return {false, false, EditorDiagnosticCode::ProjectOperationFailed};
    if (lastScriptSave_.terminal && !lastScriptSave_.baselineRefreshed
        && lastScriptSave_.outcome.commitState == core::io::FileCommitState::OutcomeUnknown
        && scriptDocuments_.find(lastScriptSave_.documentId))
        return {false, false, EditorDiagnosticCode::WorkspaceBusy};
    try {
        const auto& source = *document->sourceLocation();
        const auto& project = scope->location();
        const char* issue = nullptr;
        if (!(source.mount == project.mount) || source.root != project.root
            || source.epoch != project.epoch
            || !scope->validate(*projectSourceFiles_, project.epoch, issue))
            return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
        auto prepared = scriptDocuments_.prepareSourceSave(
            document->id, *projectSourceFiles_, project.epoch, issue);
        if (!prepared)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        auto result = acceptProjectIoTask(
            core::makeUnique<EditorProjectIoTask>(*projectSourceFiles_,
                EditorProjectIoTask::ScriptSaveInput{scope, std::move(prepared), document->id},
                projectIoBudget_),
            id);
        if (result.success)
            result.snapshot.scriptSaveReceipt = projectIo_->scriptSaveReceipt(id);
        return result;
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::advanceSourceScriptSave()
{
    const auto& directory = projectBrowser_.sourceSnapshot();
    const bool currentOwner =
        projectSourceFiles_ && directory && directory->project() == projectIo_->scriptSaveScope();
    if (!currentOwner)
        projectIo_->cancel();
    const auto previous = lastScriptSave_;
    projectIo_->advance();
    lastScriptSave_ = projectIo_->scriptSaveReceipt(projectIoTaskId_);
    const bool changed = previous.taskId != lastScriptSave_.taskId
        || previous.submitted != lastScriptSave_.submitted
        || previous.terminal != lastScriptSave_.terminal
        || !(previous.maintenance == lastScriptSave_.maintenance)
        || previous.cancellationRequested != lastScriptSave_.cancellationRequested;
    if (projectIo_->status() != ProjectIoStatus::Pending)
        return {true, true, EditorDiagnosticCode::None};
    if (!projectIo_->ready())
        return {true, changed, EditorDiagnosticCode::None};
    const auto& receipt = lastScriptSave_.outcome;
    const char* issue = nullptr;
    if (receipt.commitState == core::io::FileCommitState::OutcomeUnknown) {
        issue = "EDITOR-SCRIPT-SAVE-OUTCOME-UNKNOWN";
        projectIo_->fail(issue);
    } else if (!currentOwner) {
        issue = "EDITOR-SCRIPT-SAVE-OWNER-CHANGED";
        projectIo_->fail(issue);
    } else if (projectIo_->scriptSaveCancelled()) {
        issue = receipt.commitState == core::io::FileCommitState::Committed
            ? "EDITOR-SCRIPT-SAVE-CANCELLED-AFTER-COMMIT"
            : "EDITOR-SCRIPT-SAVE-CANCELLED";
        projectIo_->finishCancelledScriptSave(issue);
    } else if (receipt.status != core::io::FileIoStatus::Success
        || receipt.commitState != core::io::FileCommitState::Committed) {
        issue = receipt.diagnosticId ? receipt.diagnosticId : "EDITOR-SCRIPT-SAVE-NOT-COMMITTED";
        projectIo_->fail(issue);
    } else if (!scriptDocuments_.commitSourceSave(
                   *projectIo_->scriptSave(), *projectSourceFiles_, projectIo_->epoch(), receipt)) {
        issue = projectIo_->scriptSave()->diagnostic();
        projectIo_->fail(issue ? issue : "EDITOR-SCRIPT-SAVE-PUBLICATION-REFUSED");
    } else {
        projectIo_->published();
    }
    lastScriptSave_ = projectIo_->scriptSaveReceipt(projectIoTaskId_);
    // File/domain truth is terminal before any fallible presentation work.
    try {
        const char* text = "Saved script source.";
        if (!lastScriptSave_.published) {
            if (receipt.commitState == core::io::FileCommitState::OutcomeUnknown)
                text = "Save result unknown. Reload the file before saving again.";
            else if (receipt.status == core::io::FileIoStatus::VersionMismatch)
                text = "File changed outside the editor; local edits kept. Reload to continue.";
            else if (receipt.commitState == core::io::FileCommitState::Committed)
                text =
                    "File saved before cancellation or owner change. Reload to refresh the save point.";
            else if (lastScriptSave_.cancellationRequested)
                text = "Script save cancelled; local edits kept.";
            else
                text = "Script save failed; local edits kept.";
        }
        core::pmr::String message(text, &core::defaultCpuMemoryResource());
        const bool presented = publishScriptPresentation(std::move(message));
        if (issue) {
            presentationState_.openOutput();
            if (auto* output = runtime().canvas().findWidget(scriptPresentationOutput_))
                output->setVisible(true);
        }
        if (!presented && lastScriptSave_.published)
            projectIo_->published("EDITOR-SCRIPT-PRESENTATION-FAILED");
        return {true,
            true,
            presented ? EditorDiagnosticCode::None
                      : EditorDiagnosticCode::ScriptPresentationFailed};
    } catch (...) {
        if (lastScriptSave_.published)
            projectIo_->published("EDITOR-SCRIPT-PRESENTATION-FAILED");
        return {true, true, EditorDiagnosticCode::ScriptPresentationFailed};
    }
}

EditorCommandOutcome EditorShell::reloadSourceScript(bool discardChanges)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    const auto* document = scriptDocuments_.active();
    const auto& directory = projectBrowser_.sourceSnapshot();
    if (!document || !directory || !document->sourceLocation() || !projectSourceFiles_)
        return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
    if (document->dirty() && !discardChanges)
        return {false, false, EditorDiagnosticCode::UnsavedChanges};
    try {
        const auto& scope = directory->project();
        const auto* source = document->sourceLocation();
        const char* issue = nullptr;
        if (!(source->mount == scope->location().mount) || source->root != scope->location().root
            || source->epoch != scope->location().epoch
            || !scope->validate(*projectSourceFiles_, scope->location().epoch, issue)
            || !document->validateSource(*projectSourceFiles_, scope->location().epoch, issue))
            return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
        auto relative = core::FilePath::utf8(document->path.lexically_relative(projectRoot()));
        auto reload = scriptDocuments_.prepareReload(
            document->id, discardChanges, projectIoBudget_.scriptReload, issue);
        if (!reload)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        const auto id = projectIoIds_.tryAcquire();
        if (!id)
            return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
        return acceptProjectIoTask(
            core::makeUnique<EditorProjectIoTask>(*projectSourceFiles_,
                EditorProjectIoTask::ScriptInput{scope, std::move(relative), std::move(reload)},
                projectIoBudget_),
            id);
    } catch (...) {
        return {false, false, EditorDiagnosticCode::CommandPreparationFailed};
    }
}

EditorCommandOutcome EditorShell::advanceProjectIo(std::uint64_t id)
{
    if (!projectIoPending() || id != projectIoTaskId_)
        return {false, false, EditorDiagnosticCode::InvalidCommandPayload};
    if (projectIo_->savingScript())
        return advanceSourceScriptSave();
    if (projectIo_->savingProject())
        return advanceSourceProjectSave();
    try {
        if (sceneDocumentEpoch_ != projectIoSceneEpoch_
            || resourceRevision_ != projectIoResourceRevision_
            || !projectIoScripts_.sameCurrentEpoch(scriptDocuments_.stateTicket())) {
            projectIo_->fail("EDITOR-PROJECT-IO-OWNER-CHANGED");
            retireSourceSceneIntent(false);
            return {true, true, EditorDiagnosticCode::None};
        }
        projectIo_->advance();
        if (projectIo_->status() != ProjectIoStatus::Pending) {
            retireSourceSceneIntent(false);
            return {true, true, EditorDiagnosticCode::None};
        }
        if (!projectIo_->ready())
            return {true, false, EditorDiagnosticCode::None};
        core::pmr::String hash, diagnostic;
        if (!observeSceneHash(hash, diagnostic) || hash != projectIoSceneHash_) {
            projectIo_->fail("EDITOR-PROJECT-IO-DOCUMENT-CHANGED");
            retireSourceSceneIntent(false);
            return {true, true, EditorDiagnosticCode::None};
        }
        if (projectIo_->verifyingSources())
            return completeSourceSceneAuthoring();
        if (projectIo_->reloadingScripts())
            return completeSceneScriptReload();
        if (projectIo_->openingScript()) {
            auto& document = projectIo_->script();
            auto* reload = projectIo_->scriptReload();
            const char* issue = nullptr;
            if (!document.sourceSnapshot
                || !document.sourceSnapshot->validate(
                    *projectSourceFiles_, projectIo_->epoch(), issue)
                || (!reload
                    && scriptDocuments_.size() >= projectIo_->budget().maxScriptDocuments)) {
                projectIo_->fail(issue ? issue : "EDITOR-SCRIPT-SOURCE-DOCUMENT-BUDGET");
                return {true, true, EditorDiagnosticCode::None};
            }
            // Prepare presentation before append consumes a document identity.
            core::pmr::String workspace("Script", presentationState_.workspace_.get_allocator());
            auto message =
                (reload ? "Reloaded " : "Opened ") + core::FilePath::utf8(document.path.filename());
            if (reload) {
                if (!scriptDocuments_.commitSourceReload(*reload, document)) {
                    projectIo_->fail("EDITOR-SCRIPT-RELOAD-STALE");
                    return {true, true, EditorDiagnosticCode::None};
                }
                if (lastScriptSave_.documentId == scriptDocuments_.activeId())
                    lastScriptSave_.baselineRefreshed = true;
            } else {
                const auto index = scriptDocuments_.append(std::move(document));
                if (index == ScriptDocumentSession::noActiveDocument) {
                    projectIo_->fail("EDITOR-SCRIPT-IDENTITY-EXHAUSTED");
                    return {true, true, EditorDiagnosticCode::None};
                }
                scriptDocuments_.setActiveIndex(index);
            }
            presentationState_.workspace_.swap(workspace);
            scriptFocused_ = true;
            cancelScriptPointerCapture();
            const bool presented = publishScriptPresentation(std::move(message), true);
            projectIo_->published(presented ? nullptr : "EDITOR-SCRIPT-PRESENTATION-FAILED");
            return {true,
                true,
                presented ? EditorDiagnosticCode::None
                          : EditorDiagnosticCode::ScriptPresentationFailed};
        }
        ProjectBrowserModel browser;
        const auto& origin = projectIo_->opening() ? browser : projectBrowser_;
        const auto* issue = origin.prepareSources(projectIo_->directory(),
            *projectSourceFiles_,
            projectIo_->epoch(),
            projectIo_->opening() ? ProjectBrowserModel::SourceNavigation::Navigate
                                  : projectIoNavigation_,
            browser,
            projectIo_->budget().maxHistoryEntries);
        if (issue) {
            projectIo_->fail(issue);
            return {true, true, EditorDiagnosticCode::None};
        }
        if (projectIo_->opening()) {
            if (!projectReplacementGate(true).success
                || !publishProject(std::move(projectIo_->resources()), std::move(browser))) {
                projectIo_->fail("EDITOR-PROJECT-IO-PUBLISH-PREPARATION");
                return {true, true, EditorDiagnosticCode::None};
            }
        } else {
            projectBrowser_.swap(browser);
            selectedFilePath_.clear();
            lastClickedScriptPath_.clear();
            fileScrollOffset_ = 0;
            (void)retryFileBrowserPresentation();
        }
        projectIo_->published(filesPresentationFailed_
                    || (projectIo_->opening() && sceneInspectorState_ == InspectorState::Failed)
                ? "EDITOR-PROJECT-PRESENTATION-FAILED"
                : nullptr);
        return {true, true, EditorDiagnosticCode::None};
    } catch (...) {
        projectIo_->fail("EDITOR-PROJECT-IO-PREPARATION");
        retireSourceSceneIntent(false);
        return {true, true, EditorDiagnosticCode::None};
    }
}
} // namespace arp::editor
