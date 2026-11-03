#pragma once

#include "editor/project/project_directory_session.h"
#include "editor/project/project_file_save_task.h"
#include "editor/project/project_package_snapshot.h"
#include "editor/workspace/script_document_session.h"

namespace arp::editor {
class EditorProjectResources;
class EditorProjectSourceSaveTask;

enum class ProjectIoStatus : std::uint8_t { Unknown, Pending, Succeeded, Failed, Cancelled };

struct ProjectSourceSaveReceipt {
    std::uint64_t taskId = 0;
    std::uint64_t documentId = 0;
    bool submitted = false;
    bool terminal = false;
    bool published = false;
    bool cancellationRequested = false;
    bool baselineRefreshed = false;
    core::io::FileSaveResult outcome;
    ProjectFileReceiptObservation maintenance;
};

using ProjectScriptSaveReceipt = ProjectSourceSaveReceipt;

struct EditorProjectIoBudget {
    ProjectInputBudget inputs;
    ProjectDirectoryBudget directory;
    std::size_t maxDecodedBytes = ProjectPackageSnapshot::maxAllDecodedBytes;
    std::size_t maxDiagnosticBytes = 4096;
    std::size_t maxHistoryEntries = 256;
    // Editor source tab budget, independent of project package file counts.
    std::size_t maxScriptDocuments = 64;
    ScriptDocumentSession::ReloadBudget scriptReload;
};

// Private owner-thread preparation. IO callbacks touch only source/directory
// session candidates; the Shell dispatcher alone publishes their results.
class EditorProjectIoTask final {
public:
    struct ScriptInput {
        core::SharedPtr<const ProjectSourceSnapshot> scope;
        core::pmr::String relative{&core::defaultCpuMemoryResource()};
        core::UniquePtr<ScriptDocumentSession::PreparedReload> reload;
    };

    struct ScriptSaveInput {
        core::SharedPtr<const ProjectSourceSnapshot> scope;
        core::UniquePtr<ScriptDocumentSession::PreparedSourceSave> prepared;
        std::uint64_t documentId = 0;
    };

    struct ScriptModulesInput {
        ProjectSourceContext context;
        core::pmr::Vector<core::pmr::String> sources;
        core::pmr::String directory;
    };

    static bool validBudget(const EditorProjectIoBudget&) noexcept;
    EditorProjectIoTask(core::io::FileService&, ProjectSourceLocation, EditorProjectIoBudget);
    EditorProjectIoTask(core::io::FileService&,
        core::SharedPtr<const ProjectSourceSnapshot>,
        std::string_view directory,
        EditorProjectIoBudget);
    EditorProjectIoTask(core::io::FileService&, ScriptInput, EditorProjectIoBudget);
    EditorProjectIoTask(core::io::FileService&, ScriptSaveInput, EditorProjectIoBudget);
    EditorProjectIoTask(core::io::FileService&, ScriptModulesInput, EditorProjectIoBudget);
    EditorProjectIoTask(core::UniquePtr<EditorProjectSourceSaveTask>, EditorProjectIoBudget);
    // Revalidate an existing project scope for a cold owner-thread Scene intent.
    EditorProjectIoTask(ProjectSourceContext, EditorProjectIoBudget);
    ~EditorProjectIoTask();
    EditorProjectIoTask(const EditorProjectIoTask&) = delete;
    EditorProjectIoTask& operator=(const EditorProjectIoTask&) = delete;

    void advance() noexcept;
    void cancel() noexcept;
    void fail(const char*) noexcept;
    void published(const char* diagnostic = nullptr) noexcept;
    bool ready() const noexcept;
    bool opening() const noexcept;
    bool openingScript() const noexcept;
    bool savingScript() const noexcept;
    bool savingProject() const noexcept;
    EditorProjectSourceSaveTask* projectSave() noexcept;
    ProjectSourceSaveReceipt projectSaveReceipt(std::uint64_t taskId) const noexcept;
    void finishCancelledProjectSave(const char* diagnostic) noexcept;
    bool scriptSaveCancelled() const noexcept;
    ProjectScriptSaveReceipt scriptSaveReceipt(std::uint64_t taskId) const noexcept;
    ScriptDocumentSession::PreparedSourceSave* scriptSave() noexcept;
    const core::SharedPtr<const ProjectSourceSnapshot>& scriptSaveScope() const noexcept;
    void finishCancelledScriptSave(const char* diagnostic) noexcept;
    bool verifyingSources() const noexcept;
    bool reloadingScripts() const noexcept;
    ProjectSourceContext reloadedSources() const noexcept;
    const ProjectSourceContext& verifiedSources() const;
    ProjectIoStatus status() const noexcept;
    std::string_view diagnostic() const noexcept;
    std::uint64_t epoch() const noexcept;
    const EditorProjectIoBudget& budget() const noexcept;
    const core::SharedPtr<const ProjectDirectorySnapshot>& directory() const noexcept;
    EditorProjectResources& resources();
    ScriptDocumentSession::Document& script();
    ScriptDocumentSession::PreparedReload* scriptReload() noexcept;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
