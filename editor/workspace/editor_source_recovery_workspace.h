#pragma once

#include "editor/workspace/editor_recovery_preparation.h"
#include "editor/workspace/editor_source_recovery_documents.h"
#include "editor/workspace/editor_source_recovery_preparation.h"
#include "editor/workspace/project_browser_model.h"

namespace arp::editor {

enum class SourceRecoveryWorkspaceState { Pending, Ready, Revalidating, Failed, Cancelled };

// Complete cold CPU handoff data. No method admits a live Shell, observes a
// complete mutable ResourceManager inventory or grants GPU retirement authority.
// The FileService/queue outlive this task and every accepted callback.
class EditorSourceRecoveryWorkspace final {
public:
    struct View {
        const EditorSourceRecoveryPreparation::Candidate& recovery;
        const EditorRecoveryPreparation::WorkspaceState& savedState;
        const ProjectBrowserModel& browser;
        const core::pmr::Vector<ScriptDocumentSession::Document>& scripts;
        const core::pmr::Vector<EditorRecoverySourceFile>& files;
    };

    EditorSourceRecoveryWorkspace(core::io::FileService&,
        const EditorRecoverySnapshot&,
        std::string_view admittedAlias,
        ProjectSourceLocation,
        ProjectInputBudget = {},
        ProjectDirectoryBudget = {},
        EditorRecoveryBudget = {});
    ~EditorSourceRecoveryWorkspace();
    EditorSourceRecoveryWorkspace(const EditorSourceRecoveryWorkspace&) = delete;
    EditorSourceRecoveryWorkspace& operator=(const EditorSourceRecoveryWorkspace&) = delete;

    void advance(std::uint64_t currentEpoch) noexcept;
    void cancel() noexcept;
    // Hides the old View while asynchronously checking dependencies, exact
    // observed formal bytes and the source directory again. No watcher or CAS.
    bool revalidate(std::uint64_t currentEpoch) noexcept;
    SourceRecoveryWorkspaceState state() const noexcept;
    std::string_view diagnostic() const noexcept;
    // Borrowed views expire with this task. Access checks epoch, owners and the
    // prepared Scene wire; resource payload inventory remains a separate gate.
    std::optional<View> inspect(std::uint64_t currentEpoch, const char*& diagnostic) const noexcept;
    core::UniquePtr<ScriptDocumentSession::PreparedRecovery> prepareForSession(
        const ScriptDocumentSession&,
        std::uint64_t currentEpoch,
        core::pmr::String& diagnostic) const;

private:
    struct State;
    core::UniquePtr<State> state_;
};
} // namespace arp::editor
