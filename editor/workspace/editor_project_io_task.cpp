#include "editor/workspace/editor_project_io_task.h"

#include "editor/workspace/editor_project_resource_planner.h"
#include "editor/workspace/editor_project_resources.h"
#include "resource/resource_source_io.h"

#include <algorithm>

namespace arp::editor {

struct EditorProjectIoTask::State {
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

    void failure(std::string_view text) noexcept
    {
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
        directory.reset();
        script.reset();
        scriptScope.reset();
        scriptReload.reset();
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

EditorProjectIoTask::~EditorProjectIoTask() = default;

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
    state_->directory.reset();
    state_->script.reset();
    state_->scriptScope.reset();
    state_->scriptReload.reset();
}

void EditorProjectIoTask::cancel() noexcept
{
    state_->failure("EDITOR-PROJECT-IO-CANCELLED");
    state_->status = ProjectIoStatus::Cancelled;
}

void EditorProjectIoTask::advance() noexcept
{
    auto& state = *state_;
    if (state.status != ProjectIoStatus::Pending || state.ready)
        return;
    try {
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
            state.resources = core::makeUnique<EditorProjectResources>();
            const auto loaded =
                state.resources->loadSources(state.snapshot, *state.files, state.location.epoch);
            if (!loaded.success) {
                state.failure(loaded.diagnostics);
                return;
            }
            state.directorySession = core::makeUnique<ProjectDirectorySession>(
                *state.files, state.snapshot, "", state.budget.directory);
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
        state.failure("EDITOR-PROJECT-IO-PREPARATION");
    }
}
} // namespace arp::editor
