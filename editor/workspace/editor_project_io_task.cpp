#include "editor/workspace/editor_project_io_task.h"

#include "editor/project/project_file_save_task.h"
#include "editor/project/project_source_verification.h"
#include "editor/workspace/editor_project_resource_planner.h"
#include "editor/workspace/editor_project_resources.h"
#include "editor/workspace/editor_project_source_save_task.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::editor {

struct EditorProjectIoTask::State {
    struct ScriptSave {
        core::UniquePtr<ScriptDocumentSession::PreparedSourceSave> prepared;
        core::UniquePtr<ProjectFileSaveTask> operation;
        std::uint64_t documentId = 0;
        bool published = false;
    };

    static bool validBudget(const EditorProjectIoBudget& budget) noexcept
    {
        return ProjectInputSnapshot::validBudget(budget.inputs)
            && ProjectDirectorySession::validBudget(budget.directory) && budget.maxDecodedBytes
            && budget.maxDecodedBytes <= ProjectPackageSnapshot::maxAllDecodedBytes
            && budget.maxDiagnosticBytes && budget.maxDiagnosticBytes <= 4096
            && budget.maxHistoryEntries && budget.maxHistoryEntries <= 4096
            && budget.scriptReload.maxHistoryEntries
            && budget.scriptReload.maxHistoryEntries
            <= ScriptDocumentSession::ReloadBudget{}.maxHistoryEntries
            && budget.scriptReload.maxHistoryBytes
            && budget.scriptReload.maxHistoryBytes
            <= ScriptDocumentSession::ReloadBudget{}.maxHistoryBytes
            && budget.maxScriptDocuments
            && budget.maxScriptDocuments <= EditorProjectIoBudget{}.maxScriptDocuments;
    }

    core::io::FileService* files = nullptr;
    ProjectSourceLocation location;
    EditorProjectIoBudget budget;
    ProjectIoStatus status = ProjectIoStatus::Pending;
    bool opening = true;
    bool openingScript = false;
    bool verifyingSources = false;
    bool reloadingScripts = false;
    core::SharedPtr<const ProjectSourceSnapshot> reloadOriginal;
    core::pmr::Vector<core::pmr::String> reloadPaths;
    core::pmr::String reloadDirectory;
    bool scriptRequested = false;
    bool ready = false;
    const char* diagnostic = nullptr;
    core::pmr::String ownedDiagnostic{&core::defaultCpuMemoryResource()};
    core::pmr::String pendingPath{&core::defaultCpuMemoryResource()};
    core::UniquePtr<ProjectSourceSession> sources;
    core::UniquePtr<resource::ResourceSourceSet> plannerSources;
    core::SharedPtr<const ProjectSourceSnapshot> snapshot;
    core::UniquePtr<EditorProjectResources> resources;
    core::UniquePtr<ProjectDirectorySession> directorySession;
    core::SharedPtr<const ProjectDirectorySnapshot> directory;
    core::SharedPtr<const ProjectSourceSnapshot> scriptScope;
    core::UniquePtr<ScriptDocumentSession::Document> script;
    core::UniquePtr<ScriptDocumentSession::PreparedReload> scriptReload;
    core::UniquePtr<ProjectSourceVerification> verification;
    core::SharedPtr<ScriptSave> save;
    core::UniquePtr<EditorProjectSourceSaveTask> projectSave;

    void failure(std::string_view text) noexcept
    {
        if (projectSave && projectSave->submitted() && !projectSave->terminal()) {
            projectSave->cancel();
            return;
        }
        if (save && save->operation->submitted() && !save->operation->terminal()) {
            save->operation->cancel();
            return;
        }
        status = ProjectIoStatus::Failed;
        ready = false;
        try {
            if (text.size() <= budget.maxDiagnosticBytes)
                ownedDiagnostic = text;
            else
                diagnostic = "EDITOR-PROJECT-IO-DIAGNOSTIC-BUDGET";
        } catch (...) {
            diagnostic = "EDITOR-PROJECT-IO-ALLOCATION";
        }
        sources.reset();
        plannerSources.reset();
        directorySession.reset();
        resources.reset();
        snapshot.reset();
        reloadOriginal.reset();
        directory.reset();
        script.reset();
        scriptScope.reset();
        scriptReload.reset();
        verification.reset();
        if (projectSave) {
            if (!projectSave->terminal())
                projectSave->reject(
                    core::io::FileIoStatus::InvalidRequest, "EDITOR-PROJECT-SAVE-NOT-SUBMITTED");
            projectSave->releasePreparation();
        }
        if (save) {
            if (!save->operation->terminal()) {
                save->operation->reject(
                    core::io::FileIoStatus::InvalidRequest, "EDITOR-SCRIPT-SAVE-NOT-SUBMITTED");
            }
            save->prepared.reset();
        }
    }
};

EditorProjectIoTask::EditorProjectIoTask(
    core::io::FileService& files, ProjectSourceLocation location, EditorProjectIoBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.files = &files;
    state.location = std::move(location);
    state.budget = budget;
    if (!State::validBudget(budget)) {
        state.failure("EDITOR-PROJECT-IO-BUDGET");
        return;
    }
    state.sources = core::makeUnique<ProjectSourceSession>(files, state.location, budget.inputs);
    state.plannerSources = core::makeUnique<resource::ResourceSourceSet>();
}

EditorProjectIoTask::EditorProjectIoTask(core::io::FileService& files,
    core::SharedPtr<const ProjectSourceSnapshot> snapshot,
    std::string_view directory,
    EditorProjectIoBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.files = &files;
    state.budget = budget;
    state.opening = false;
    state.snapshot = std::move(snapshot);
    if (!state.snapshot || !State::validBudget(budget)) {
        state.failure("EDITOR-PROJECT-IO-SOURCE-REQUIRED");
        return;
    }
    state.location = state.snapshot->location();
    state.directorySession = core::makeUnique<ProjectDirectorySession>(
        files, state.snapshot, directory, budget.directory);
}

EditorProjectIoTask::EditorProjectIoTask(
    core::io::FileService& files, ScriptModulesInput input, EditorProjectIoBudget budget)
    : EditorProjectIoTask(files,
          input.context.snapshot ? input.context.snapshot->location() : ProjectSourceLocation{},
          budget)
{
    auto& s = *state_;
    const char* issue = nullptr;
    if (s.status != ProjectIoStatus::Pending || input.context.files != &files
        || !input.context.validate(issue) || input.sources.empty()
        || input.sources.size() > budget.inputs.maxFiles
        || (!input.directory.empty() && !ProjectInputSnapshot::validLogicalPath(input.directory))) {
        s.failure(issue ? issue : "EDITOR-SCRIPT-RELOAD-INPUT");
        return;
    }
    for (std::size_t i = 0; i < input.sources.size(); ++i) {
        if (!input.context.snapshot->find(input.sources[i])
            || std::find(input.sources.begin(), input.sources.begin() + i, input.sources[i])
                != input.sources.begin() + i) {
            s.failure("EDITOR-SCRIPT-RELOAD-DECLARED-SOURCE");
            return;
        }
    }
    s.opening = false;
    s.reloadingScripts = true;
    s.reloadOriginal = std::move(input.context.snapshot);
    s.reloadPaths = std::move(input.sources);
    s.reloadDirectory = std::move(input.directory);
}

bool EditorProjectIoTask::reloadingScripts() const noexcept
{
    return state_->reloadingScripts;
}

ProjectSourceContext EditorProjectIoTask::reloadedSources() const noexcept
{
    return {state_->snapshot, state_->files, state_->location.epoch};
}

EditorProjectIoTask::~EditorProjectIoTask()
{
    if (state_->projectSave)
        state_->projectSave->cancel();
    if (state_->save && !state_->save->operation->terminal())
        state_->save->operation->cancel();
}

EditorProjectIoTask::EditorProjectIoTask(
    core::UniquePtr<EditorProjectSourceSaveTask> task, EditorProjectIoBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.opening = false;
    state.budget = budget;
    if (!task || !State::validBudget(budget) || !task->context().snapshot) {
        state.failure("EDITOR-PROJECT-SAVE-SOURCE-REQUIRED");
        return;
    }
    state.location = task->context().snapshot->location();
    state.projectSave = std::move(task);
}

bool EditorProjectIoTask::savingProject() const noexcept
{
    return bool(state_->projectSave);
}

EditorProjectSourceSaveTask* EditorProjectIoTask::projectSave() noexcept
{
    return state_->projectSave.get();
}

ProjectSourceSaveReceipt EditorProjectIoTask::projectSaveReceipt(std::uint64_t id) const noexcept
{
    ProjectSourceSaveReceipt receipt;
    if (const auto& task = state_->projectSave) {
        receipt.taskId = id;
        receipt.documentId = state_->location.epoch;
        receipt.submitted = task->submitted();
        receipt.terminal = task->terminal();
        receipt.published = task->published();
        receipt.cancellationRequested = task->cancelled();
        receipt.outcome = task->result();
        receipt.maintenance = task->receiptObservation();
    }
    return receipt;
}

void EditorProjectIoTask::finishCancelledProjectSave(const char* diagnostic) noexcept
{
    if (!state_->projectSave || !state_->projectSave->terminal())
        return;
    state_->status = ProjectIoStatus::Cancelled;
    state_->diagnostic = diagnostic;
    state_->ready = false;
    state_->projectSave->releasePreparation();
}

EditorProjectIoTask::EditorProjectIoTask(ProjectSourceContext context, EditorProjectIoBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.budget = budget;
    state.opening = false;
    state.verifyingSources = true;
    if (!State::validBudget(budget) || !context.snapshot) {
        state.failure("EDITOR-PROJECT-IO-SOURCE-REQUIRED");
        return;
    }
    state.location = context.snapshot->location();
    state.verification =
        core::makeUnique<ProjectSourceVerification>(std::move(context), budget.inputs);
    if (state.verification->state() != ProjectSourceVerificationState::Pending)
        state.failure(state.verification->diagnostic());
}

EditorProjectIoTask::EditorProjectIoTask(
    core::io::FileService& files, ScriptInput input, EditorProjectIoBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.files = &files;
    state.budget = budget;
    state.opening = false;
    state.openingScript = true;
    state.scriptScope = std::move(input.scope);
    state.scriptReload = std::move(input.reload);
    const char* issue = nullptr;
    if (!State::validBudget(budget) || !state.scriptScope
        || !ProjectInputSnapshot::validLogicalPath(input.relative)
        || !ScriptDocumentSession::isScriptPath(std::filesystem::u8path(input.relative))
        || !state.scriptScope->validate(files, state.scriptScope->location().epoch, issue)) {
        state.failure(issue ? issue : "EDITOR-SCRIPT-SOURCE-INPUT");
        return;
    }
    state.location = state.scriptScope->location();
    state.pendingPath = std::move(input.relative);
    const auto bytes = std::min(budget.inputs.maxFileBytes, ScriptDocumentSession::maxScriptBytes);
    state.sources = core::makeUnique<ProjectSourceSession>(
        files, state.location, ProjectInputBudget{1, bytes, bytes});
}

bool EditorProjectIoTask::validBudget(const EditorProjectIoBudget& budget) noexcept
{
    return State::validBudget(budget);
}

EditorProjectIoTask::EditorProjectIoTask(
    core::io::FileService& files, ScriptSaveInput input, EditorProjectIoBudget budget)
    : state_(core::makeUnique<State>())
{
    auto& state = *state_;
    state.files = &files;
    state.budget = budget;
    state.opening = false;
    state.scriptScope = std::move(input.scope);
    const char* issue = nullptr;
    if (!State::validBudget(budget) || !state.scriptScope || !input.prepared || !input.documentId
        || !state.scriptScope->validate(files, state.scriptScope->location().epoch, issue)
        || !files.saveCapability(state.scriptScope->location().mount).observedBaselineReplace) {
        state.failure(issue ? issue : "EDITOR-SCRIPT-SAVE-SOURCE-REQUIRED");
        return;
    }
    state.location = state.scriptScope->location();
    state.save = core::makeShared<State::ScriptSave>();
    state.save->operation =
        core::makeUnique<ProjectFileSaveTask>(files, input.prepared->takeRequest());
    state.save->prepared = std::move(input.prepared);
    state.save->documentId = input.documentId;
}

bool EditorProjectIoTask::savingScript() const noexcept
{
    return state_->save != nullptr;
}

bool EditorProjectIoTask::scriptSaveCancelled() const noexcept
{
    return state_->save && state_->save->operation->cancellationRequested();
}

ProjectScriptSaveReceipt EditorProjectIoTask::scriptSaveReceipt(std::uint64_t id) const noexcept
{
    ProjectScriptSaveReceipt receipt;
    if (const auto& save = state_->save) {
        receipt.taskId = id;
        receipt.documentId = save->documentId;
        receipt.submitted = save->operation->submitted();
        receipt.terminal = save->operation->terminal();
        receipt.published = save->published;
        receipt.cancellationRequested = save->operation->cancellationRequested();
        receipt.outcome = save->operation->result();
        receipt.maintenance = save->operation->receiptObservation();
    }
    return receipt;
}

ScriptDocumentSession::PreparedSourceSave* EditorProjectIoTask::scriptSave() noexcept
{
    return state_->save ? state_->save->prepared.get() : nullptr;
}

const core::SharedPtr<const ProjectSourceSnapshot>&
EditorProjectIoTask::scriptSaveScope() const noexcept
{
    return state_->scriptScope;
}

void EditorProjectIoTask::finishCancelledScriptSave(const char* diagnostic) noexcept
{
    if (!state_->save || !state_->save->operation->terminal())
        return;
    state_->status = ProjectIoStatus::Cancelled;
    state_->diagnostic = diagnostic;
    state_->ready = false;
    state_->save->prepared.reset();
    state_->scriptScope.reset();
}

bool EditorProjectIoTask::ready() const noexcept
{
    return state_->ready;
}

bool EditorProjectIoTask::opening() const noexcept
{
    return state_->opening;
}

bool EditorProjectIoTask::openingScript() const noexcept
{
    return state_->openingScript;
}

bool EditorProjectIoTask::verifyingSources() const noexcept
{
    return state_->verifyingSources;
}

const ProjectSourceContext& EditorProjectIoTask::verifiedSources() const
{
    return state_->verification->context();
}

ScriptDocumentSession::Document& EditorProjectIoTask::script()
{
    return *state_->script;
}

ScriptDocumentSession::PreparedReload* EditorProjectIoTask::scriptReload() noexcept
{
    return state_->scriptReload.get();
}

ProjectIoStatus EditorProjectIoTask::status() const noexcept
{
    return state_->status;
}

std::string_view EditorProjectIoTask::diagnostic() const noexcept
{
    return state_->diagnostic ? std::string_view(state_->diagnostic)
                              : std::string_view(state_->ownedDiagnostic);
}

std::uint64_t EditorProjectIoTask::epoch() const noexcept
{
    return state_->location.epoch;
}

const EditorProjectIoBudget& EditorProjectIoTask::budget() const noexcept
{
    return state_->budget;
}

const core::SharedPtr<const ProjectDirectorySnapshot>&
EditorProjectIoTask::directory() const noexcept
{
    return state_->directory;
}

EditorProjectResources& EditorProjectIoTask::resources()
{
    return *state_->resources;
}

void EditorProjectIoTask::fail(const char* diagnostic) noexcept
{
    state_->failure(diagnostic);
}

void EditorProjectIoTask::published(const char* diagnostic) noexcept
{
    state_->status = ProjectIoStatus::Succeeded;
    state_->ready = false;
    state_->diagnostic = diagnostic;
    // Published owners now belong to Shell/browser; terminal task telemetry
    // must not retain another copy of the raw source or resource candidates.
    state_->resources.reset();
    state_->snapshot.reset();
    state_->reloadOriginal.reset();
    state_->directory.reset();
    state_->script.reset();
    state_->scriptScope.reset();
    state_->scriptReload.reset();
    state_->verification.reset();
    if (state_->projectSave)
        state_->projectSave->releasePreparation();
    if (state_->save) {
        state_->save->published = true;
        state_->save->prepared.reset();
    }
}

void EditorProjectIoTask::cancel() noexcept
{
    if (state_->projectSave) {
        state_->projectSave->cancel();
        return;
    }
    if (state_->save) {
        state_->save->operation->cancel();
        return;
    }
    state_->failure("EDITOR-PROJECT-IO-CANCELLED");
    state_->status = ProjectIoStatus::Cancelled;
}

void EditorProjectIoTask::advance() noexcept
{
    auto& state = *state_;
    if (state.status != ProjectIoStatus::Pending || state.ready)
        return;
    try {
        if (state.projectSave) {
            state.projectSave->advance();
            state.ready = state.projectSave->terminal();
            return;
        }
        if (state.save) {
            auto& save = *state.save;
            if (!save.operation->submitted() && !save.operation->terminal()
                && !save.operation->cancellationRequested()) {
                const char* issue = nullptr;
                if (!state.scriptScope->validate(*state.files, state.location.epoch, issue)) {
                    state.failure(issue);
                    return;
                }
            }
            save.operation->advance();
            state.ready = save.operation->terminal();
            return;
        }
        if (state.verifyingSources) {
            state.verification->advance();
            const auto phase = state.verification->state();
            if (phase == ProjectSourceVerificationState::Verified)
                state.ready = true;
            else if (phase != ProjectSourceVerificationState::Pending)
                state.failure(state.verification->diagnostic());
            return;
        }
        if (state.openingScript) {
            const char* issue = nullptr;
            if (!state.scriptScope->validate(*state.files, state.location.epoch, issue)) {
                state.failure(issue);
                return;
            }
            if (state.sources->state() == ProjectSourceState::Pending)
                return;
            if (state.sources->state() != ProjectSourceState::Ready) {
                state.failure(state.sources->diagnostic());
                return;
            }
            if (!state.scriptRequested) {
                const auto bytes = std::min(
                    state.budget.inputs.maxFileBytes, ScriptDocumentSession::maxScriptBytes);
                if (state.sources->read(state.pendingPath, bytes)
                    != core::io::FileIoStatus::Accepted)
                    state.failure(state.sources->diagnostic());
                else
                    state.scriptRequested = true;
                return;
            }
            state.snapshot = state.sources->finish(state.location.epoch);
            if (!state.snapshot) {
                state.failure(state.sources->diagnostic());
                return;
            }
            core::UniquePtr<ScriptDocumentSession::Document> document;
            if (!ScriptDocumentSession::prepareSourceFile(state.snapshot,
                    state.pendingPath,
                    *state.files,
                    state.location.epoch,
                    document,
                    issue)) {
                state.failure(issue);
                return;
            }
            state.script = std::move(document);
            state.sources.reset();
            state.ready = true;
            return;
        }
        if (state.sources) {
            if (state.sources->state() == ProjectSourceState::Pending)
                return;
            if (state.sources->state() != ProjectSourceState::Ready) {
                state.failure(state.sources->diagnostic());
                return;
            }
            if (!state.pendingPath.empty()) {
                const auto* entry = state.sources->acquired(state.pendingPath);
                if (!entry) {
                    state.failure("EDITOR-PROJECT-IO-RECEIPT");
                    return;
                }
                resource::ResourceSourceBuffer source;
                source.path = entry->binding.path;
                source.pathScope = entry->binding.pathScope;
                source.mount = entry->binding.mount;
                source.version = entry->receipt.version;
                source.bytes = entry->receipt.bytes;
                source.role = "editor-project-task/v1";
                if (!state.plannerSources->add(std::move(source))) {
                    state.failure("EDITOR-PROJECT-IO-SOURCE-CONFLICT");
                    return;
                }
                state.pendingPath.clear();
            }
            resource::AssetPackageSourceContext context;
            context.origin = {core::io::FilePathScope::MountedResource, state.location.mount};
            context.resources.sources = state.plannerSources.get();
            context.resources.requirePreloadedSources = true;
            context.resources.maxDecodedBytes = state.budget.maxDecodedBytes;
            const auto root = std::filesystem::u8path(state.location.root);
            auto plan = EditorProjectResourcePlanner::plan(
                root, context, state.budget.inputs, state.budget.maxDecodedBytes);
            if (!plan.success) {
                state.failure(plan.diagnostics);
                return;
            }
            if (!plan.reads.empty()) {
                const auto& read = plan.reads.front();
                state.pendingPath = core::FilePath::utf8(
                    std::filesystem::u8path(read.path).lexically_relative(root));
                const auto status = state.sources->read(
                    state.pendingPath, read.maxBytes, read.expectedVersion, read.maxVersionBytes);
                if (status != core::io::FileIoStatus::Accepted)
                    state.failure(state.sources->diagnostic());
                return;
            }
            state.snapshot = state.sources->finish(state.location.epoch);
            if (!state.snapshot) {
                state.failure(state.sources->diagnostic());
                return;
            }
            state.sources.reset();
            state.plannerSources.reset();
            if (state.reloadingScripts) {
                if (state.snapshot->size() != state.reloadOriginal->size()) {
                    state.failure("EDITOR-SCRIPT-RELOAD-DEPENDENCY-SCOPE");
                    return;
                }
                for (std::size_t i = 0; i < state.reloadOriginal->size(); ++i) {
                    const auto& old = *state.reloadOriginal->entryAt(i);
                    const auto* current = state.snapshot->find(old.relative);
                    if (!current
                        || (std::find(
                                state.reloadPaths.begin(), state.reloadPaths.end(), old.relative)
                                == state.reloadPaths.end()
                            && current->receipt.bytes != old.receipt.bytes)) {
                        state.failure("EDITOR-SCRIPT-RELOAD-NONMODULE-CHANGE");
                        return;
                    }
                }
            } else {
                state.resources = core::makeUnique<EditorProjectResources>();
                const auto loaded = state.resources->loadSources(
                    state.snapshot, *state.files, state.location.epoch);
                if (!loaded.success) {
                    state.failure(loaded.diagnostics);
                    return;
                }
            }
            state.directorySession = core::makeUnique<ProjectDirectorySession>(
                *state.files, state.snapshot, state.reloadDirectory, state.budget.directory);
        }
        if (state.directorySession) {
            const auto phase = state.directorySession->state();
            if (phase == ProjectDirectoryState::Pending)
                return;
            if (phase == ProjectDirectoryState::Ready) {
                if (state.directorySession->next(state.location.epoch)
                    != core::io::FileIoStatus::Accepted)
                    state.failure(state.directorySession->diagnostic());
                return;
            }
            if (phase != ProjectDirectoryState::Complete) {
                state.failure(state.directorySession->diagnostic());
                return;
            }
            state.directory = state.directorySession->finish(state.location.epoch);
            if (!state.directory) {
                state.failure(state.directorySession->diagnostic());
                return;
            }
            state.directorySession.reset();
            state.ready = true;
        }
    } catch (...) {
        if (state.save && !state.save->operation->submitted()
            && !state.save->operation->terminal()) {
            state.save->operation->reject(
                core::io::FileIoStatus::BudgetExceeded, "EDITOR-SCRIPT-SAVE-PREPARATION");
        }
        state.failure("EDITOR-PROJECT-IO-PREPARATION");
    }
}
} // namespace arp::editor
