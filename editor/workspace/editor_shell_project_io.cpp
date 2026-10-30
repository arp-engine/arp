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
    if (projectIoPending() || projectBrowser_.sourceSnapshot() || !mount || alias.empty()
        || !EditorProjectIoTask::validBudget(budget) || alias.size() > 128
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
            const auto& source = existing->sourceSnapshot;
            if (!source || !(source->location().mount == scope->location().mount)
                || source->location().root != scope->location().root
                || source->location().epoch != scope->location().epoch
                || !source->validate(*projectSourceFiles_, scope->location().epoch, issue))
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
    return {true, true, EditorDiagnosticCode::None};
}

EditorCommandOutcome EditorShell::reloadSourceScript(bool discardChanges)
{
    const auto gate = sceneNavigationGate();
    if (!gate.success)
        return gate;
    const auto* document = scriptDocuments_.active();
    const auto& directory = projectBrowser_.sourceSnapshot();
    if (!document || !directory || !document->sourceSnapshot || !projectSourceFiles_)
        return {false, false, EditorDiagnosticCode::ScriptFileOperationFailed};
    if (document->dirty() && !discardChanges)
        return {false, false, EditorDiagnosticCode::UnsavedChanges};
    try {
        const auto& scope = directory->project();
        const auto& source = document->sourceSnapshot;
        const char* issue = nullptr;
        if (!(source->location().mount == scope->location().mount)
            || source->location().root != scope->location().root
            || source->location().epoch != scope->location().epoch
            || !scope->validate(*projectSourceFiles_, scope->location().epoch, issue)
            || !source->validate(*projectSourceFiles_, scope->location().epoch, issue))
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
    try {
        if (sceneDocumentEpoch_ != projectIoSceneEpoch_
            || resourceRevision_ != projectIoResourceRevision_
            || !projectIoScripts_.sameCurrentEpoch(scriptDocuments_.stateTicket())) {
            projectIo_->fail("EDITOR-PROJECT-IO-OWNER-CHANGED");
            return {true, true, EditorDiagnosticCode::None};
        }
        projectIo_->advance();
        if (projectIo_->status() != ProjectIoStatus::Pending)
            return {true, true, EditorDiagnosticCode::None};
        if (!projectIo_->ready())
            return {true, false, EditorDiagnosticCode::None};
        core::pmr::String hash, diagnostic;
        if (!observeSceneHash(hash, diagnostic) || hash != projectIoSceneHash_) {
            projectIo_->fail("EDITOR-PROJECT-IO-DOCUMENT-CHANGED");
            return {true, true, EditorDiagnosticCode::None};
        }
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
        projectIo_->published(
            filesPresentationFailed_ || (projectIo_->opening() && inspectorFailure())
                ? "EDITOR-PROJECT-PRESENTATION-FAILED"
                : nullptr);
        return {true, true, EditorDiagnosticCode::None};
    } catch (...) {
        projectIo_->fail("EDITOR-PROJECT-IO-PREPARATION");
        return {true, true, EditorDiagnosticCode::None};
    }
}
} // namespace arp::editor
